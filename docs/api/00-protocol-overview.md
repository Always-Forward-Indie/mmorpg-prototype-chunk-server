# Протокол клиент-сервер: Обзор

## Содержание документации

| # | Документ | Описание |
|---|----------|----------|
| 00 | [Обзор протокола](00-protocol-overview.md) | Транспорт, формат пакетов, заголовки, авторизация |
| 01 | [Подключение и авторизация](01-connection-auth-protocol.md) | Подключение, вход персонажа, пинг, дисконнект |
| 02 | [Боевая система и скиллы](02-combat-skills-protocol.md) | Атаки, скиллы, формулы урона, эффекты, DoT/HoT |
| 03 | [Мобы и AI](03-mobs-ai-protocol.md) | Спавн, AI-состояния, движение, смерть мобов |
| 04 | [Инвентарь, предметы и экипировка](04-inventory-items-equipment-protocol.md) | Инвентарь, предметы, экипировка, прочность, вес |
| 05 | [NPC, диалоги и квесты](05-npc-dialogue-quests-protocol.md) | NPC-взаимодействие, диалоговые графы, квесты, флаги |
| 06 | [Вендоры, трейд и ремонт](06-vendor-trade-repair-protocol.md) | Магазин NPC, P2P-торговля, ремонт |
| 07 | [Лут и сбор ресурсов](07-loot-harvest-protocol.md) | Таблицы лута, сбор с трупов, дроп предметов |
| 08 | [Прогрессия и характеристики](08-progression-stats-protocol.md) | Опыт, левелинг, статы, OOC-регенерация, мастерство, репутация, изучение скиллов |
| 09 | [Смерть и респавн](09-death-respawn-protocol.md) | Смерть, штрафы, респавн, воскрешение |
| 10 | [Бестиарий](10-bestiary-protocol.md) | Тиры бестиария, отслеживание убийств |
| 11 | [Чат и уведомления](11-chat-notifications-protocol.md) | Система чата, мировые уведомления |
| 12 | [Admin-RPC (DEV ONLY)](12-admin-rpc-protocol.md) | Тестовый крюк: teleport/spawn/grant/read, защита, brief-join |
| 13 | [Надёжная доставка фактов](13-reliability-delivery.md) | factKey, factAck, claim 1/0/-1, ретраи, снапшоты |

---

## Транспортный уровень

| Параметр | Значение |
|----------|----------|
| Протокол | TCP/IP (Boost.Asio) |
| Формат данных | JSON (UTF-8) |
| Разделитель сообщений | `\n` (LF, 0x0A) |
| Макс. размер строки | 8 КБ |
| Макс. размер логического сообщения | 1 МБ (только через фрагментацию, см. ниже) |
| Макс. буфер соединения | 64 КБ |
| Макс. сообщений за цикл чтения | 10 |
| Persistent connection | Да |

### Фрейминг

Каждое сообщение — одна JSON-строка, завершающаяся символом `\n`. Клиент ДОЛЖЕН:
1. Накапливать данные в буфере
2. Разделять по `\n`
3. Парсить каждую строку как JSON
4. Игнорировать пустые строки

```
{"header":{...},"body":{...}}\n
{"header":{...},"body":{...}}\n
```

### Ошибки фрейминга (явные, соединение живёт)

Оверсайз и мусор не роняются молча (раньше клиент висел до таймаута) —
сервер отвечает ошибкой и ждёт дальше:

| Условие | `header.message` / `body.errorCode` |
|---------|--------------------------------------|
| Сообщение > 8 КБ | `ERR_OVERSIZE` |
| Не-JSON / оборванный JSON | `ERR_MALFORMED` |
| Буфер без `\n` > 64 КБ (DoS-гард) | `ERR_OVERSIZE`, буфер сброшен |

За цикл чтения разбирается не более 10 сообщений, остаток ждёт следующего
цикла (защита от starvation io-потока).

### Фрагментация wireFrag (логические сообщения > 8 КБ)

Строка по-прежнему обязана влезать в 8 КБ, но логическое сообщение —
нет. Транспорт отправителя режет oversize-сообщение (> 6 КБ) на кадры,
транспорт получателя собирает их обратно и отдаёт наверх исходный JSON.
Игровая логика обеих сторон фрагментов не видит.

Кадр — обычная `\n`-строка:

```json
{"header":{"eventType":"wireFrag","status":"pending","clientId":0,"hash":""},
 "body":{"streamId":7,"index":0,"total":3,"payload":"<кусок исходной строки>"}}
```

- Сборка по `streamId`, склейка `payload` по `index` (порядок не важен, дубли игнорятся); `total==count` → исходное сообщение готово.
- Лимиты: кадр ≤ ~6 КБ; посылка ≤ 1 МБ (больше — не фрагментируется, идёт целиком по легаси-пути + warn); ≤ 16 активных стримов на соединение; недособранный стрим чистится по таймауту 30 с; несогласованный `total` сбрасывает стрим.
- Capability: клиент заявляет поддержку аддитивным флагом `body.frag=true` начиная с `joinGameClient` (дублируется в `joinGameCharacter`/`playerReady`; сервер учит с ПЕРВОГО сообщения с флагом, S13). Без него — легаси-поведение: oversize идёт целиком и может дропаться 8КБ-капом. Старые пиры ничего не меняют.
- Эталон: C++ `wire::packWireMessage` / `wire::FragReassembler` (`JsonLineChunking.hpp`, юниты в `test_json_chunking.cpp`), Python — `FragReassembler` в `mmo_proto.py`, UE — сборщик в `FNetFraming`. Трое обязаны совпадать.
- Метрики отправителя: ADMIN-оп `wireStats` (`oversizeSeen`, `fragStreams`, `fragFrames`, `oversizeByEvent`).

### Версионирование

- `header.version` ответа всегда `"1.0"` (wire v1, аддитивный: новые поля
  только добавляются в `body`, старые клиенты их игнорируют).
- `body.clientVersion` (`"major.minor.patch"`) гейтит **только login-сервер**
  на `authentificationClient`/`registerAccount`: вне `[min_client_version,
  max_client_version]` (dev: `0.1.2–0.1.4`, конфиг + `MIN/MAX_CLIENT_VERSION`)
  отвечает `status:error`:
  - ниже минимума / мусор → `ERR_VERSION_OUTDATED`;
  - выше максимума → `ERR_VERSION_TOO_NEW`;
  - тело reject'а несёт подсказки: `body.errorCode` (= код),
    `body.serverVersion` (`"1.0"`), `body.minClientVersion`,
    `body.maxClientVersion`.
- **Game/chunk версию игнорируют**: `joinGameClient`/`moveCharacter` с любым
  (или без) `clientVersion` работают как обычно — старые и будущие клиенты
  не ломаются (пины: `test_tolerant.py`, `test_protocol_errors.py`).

---

## Универсальная структура пакета

### Запрос (клиент → сервер)

```json
{
  "header": {
    "eventType": "string",
    "clientId": 0,
    "hash": "string",
    "timestamps": {
      "clientSendMsEcho": 1711709400000,
      "requestId": "sync_1711709400000_42_001_abc"
    }
  },
  "body": {
    // payload запроса
  }
}
```

### Ответ (сервер → клиент)

```json
{
  "header": {
    "eventType": "string",
    "clientId": 42,
    "status": "success",
    "message": "string",
    "timestamp": "2026-03-29T14:30:00Z",
    "version": "1.0",
    "serverRecvMs": 1711709400010,
    "clientSendMs": 1711709400000,
    "serverSendMs": 1711709400012
  },
  "body": {
    // payload ответа
  }
}
```

---

## Поля заголовка

| Поле | Тип | Направление | Описание |
|------|-----|-------------|----------|
| `eventType` | string | Оба | Тип сообщения (обязательно) |
| `clientId` | int | Оба | ID сессии клиента. `0` при первом подключении |
| `hash` | string | C→S | Токен авторизации (из login-сервера) |
| `status` | string | S→C | `"success"` или `"error"` |
| `message` | string | S→C | Человекочитаемое описание / код ошибки |
| `timestamp` | string | S→C | ISO 8601 время сервера |
| `version` | string | S→C | Версия протокола: `"1.0"` |

### Timestamps (лаг-компенсация)

| Поле | Тип | Описание |
|------|-----|----------|
| `clientSendMsEcho` / `clientSendMs` | int64 | Unix ms клиента при отправке |
| `serverRecvMs` | int64 | Unix ms сервера при получении |
| `serverSendMs` | int64 | Unix ms сервера при отправке ответа |
| `requestId` | string | Формат: `sync_{ms}_{session}_{seq}_{hash}` |

---

## Таблица всех клиентских eventType

| eventType | Описание | Документ |
|-----------|----------|----------|
| `joinGameClient` | Регистрация сессии | [01](01-connection-auth-protocol.md) |
| `joinGameCharacter` | Вход персонажа в мир | [01](01-connection-auth-protocol.md) |
| `playerReady` | Сцена загружена, запрос world-state (**обязательно**) | [01](01-connection-auth-protocol.md) |
| `pingClient` | Keep-alive | [01](01-connection-auth-protocol.md) |
| `moveCharacter` | Обновление позиции | [01](01-connection-auth-protocol.md) |
| `playerAttack` | Инициация атаки / скилла | [02](02-combat-skills-protocol.md) |
| `getPlayerInventory` | Запрос инвентаря | [04](04-inventory-items-equipment-protocol.md) |
| `itemPickup` | Подбор предмета с земли | [04](04-inventory-items-equipment-protocol.md) |
| `dropItem` | Выбросить предмет | [04](04-inventory-items-equipment-protocol.md) |
| `useItem` | Использовать предмет | [04](04-inventory-items-equipment-protocol.md) |
| `equipItem` | Экипировать предмет | [04](04-inventory-items-equipment-protocol.md) |
| `unequipItem` | Снять экипировку | [04](04-inventory-items-equipment-protocol.md) |
| `getEquipment` | Запрос состояния экипировки | [04](04-inventory-items-equipment-protocol.md) |
| `npcInteract` | Начало диалога с NPC | [05](05-npc-dialogue-quests-protocol.md) |
| `dialogueChoice` | Выбор в диалоге | [05](05-npc-dialogue-quests-protocol.md) |
| `dialogueClose` | Закрытие диалога | [05](05-npc-dialogue-quests-protocol.md) |
| `openVendorShop` | Открыть магазин NPC | [06](06-vendor-trade-repair-protocol.md) |
| `buyItem` | Купить предмет | [06](06-vendor-trade-repair-protocol.md) |
| `sellItem` | Продать предмет | [06](06-vendor-trade-repair-protocol.md) |
| `buyItemBatch` | Пакетная покупка | [06](06-vendor-trade-repair-protocol.md) |
| `sellItemBatch` | Пакетная продажа | [06](06-vendor-trade-repair-protocol.md) |
| `openRepairShop` | Открыть кузницу | [06](06-vendor-trade-repair-protocol.md) |
| `repairItem` | Починить предмет | [06](06-vendor-trade-repair-protocol.md) |
| `repairAll` | Починить всё | [06](06-vendor-trade-repair-protocol.md) |
| `tradeRequest` | Инициация торговли | [06](06-vendor-trade-repair-protocol.md) |
| `tradeAccept` | Принять торговлю | [06](06-vendor-trade-repair-protocol.md) |
| `tradeDecline` | Отклонить торговлю | [06](06-vendor-trade-repair-protocol.md) |
| `tradeOfferUpdate` | Обновить предложение | [06](06-vendor-trade-repair-protocol.md) |
| `tradeConfirm` | Подтвердить торговлю | [06](06-vendor-trade-repair-protocol.md) |
| `tradeCancel` | Отменить торговлю | [06](06-vendor-trade-repair-protocol.md) |
| `harvestStart` | Начать сбор | [07](07-loot-harvest-protocol.md) |
| `harvestCancel` | Отменить сбор | [07](07-loot-harvest-protocol.md) |
| `getNearbyCorpses` | Запрос трупов рядом | [07](07-loot-harvest-protocol.md) |
| `corpseLootInspect` | Просмотр лута трупа | [07](07-loot-harvest-protocol.md) |
| `corpseLootPickup` | Подобрать лут с трупа | [07](07-loot-harvest-protocol.md) |
| `getCharacterExperience` | Запрос XP | [08](08-progression-stats-protocol.md) |
| `respawnRequest` | Запрос респавна | [09](09-death-respawn-protocol.md) |
| `getBestiaryOverview` | Обзор бестиария | [10](10-bestiary-protocol.md) |
| `getBestiaryEntry` | Детали записи бестиария | [10](10-bestiary-protocol.md) |
| `chatMessage` | Отправка сообщения в чат | [11](11-chat-notifications-protocol.md) |
| `getConnectedCharacters` | Список игроков онлайн | [01](01-connection-auth-protocol.md) |
| `skillUsage` | Использование скилла (формат — как `playerAttack`: `skillSlug`/`targetType`) | [02](02-combat-skills-protocol.md) |
| `openSkillShop` / `requestLearnSkill` | UI тренера: список → изучение скилла | [05](05-npc-dialogue-quests-protocol.md) |
| `getTitles` / `equipTitle` | Титулы: список → экипировать | [08](08-progression-stats-protocol.md) |
| `setSkillBarSlot` | Слот панели скиллов | [08](08-progression-stats-protocol.md) |
| `useEmote` | Эмоция | [11](11-chat-notifications-protocol.md) |
| `worldObjectInteract` / `worldObjectChannelCancel` | Взаимодействие с объектами мира | [05](05-npc-dialogue-quests-protocol.md) |
| `getSpawnZones` | Legacy-вызов: сервер игнорирует, сессия живёт | [01](01-connection-auth-protocol.md) |
| `adminCommand` | Админ-RPC (только DEV-сборка) | [12](12-admin-rpc-protocol.md) |

---

## Таблица серверных eventType (S→C)

| eventType | Тип доставки | Описание |
|-----------|-------------|----------|
| `joinGameClient` | Broadcast | Новый клиент подключился |
| `joinGameCharacter` | Broadcast | Персонаж вошёл в мир |
| `playerReady` | Unicast | ACK: сцена загружена, world-state отправлен |
| `pingClient` | Unicast | Ответ на пинг: `header.message == "Pong!"` (имя то же, что запрос) |
| `disconnectClient` | Broadcast | Клиент отключился (тело пустое; ушедшего в списке уже нет) |
| `moveCharacter` | Broadcast | Обновление позиции игрока |
| `positionCorrection` | Unicast | Серверная коррекция позиции |
| `spawnMobsInZone` | Unicast | Данные мобов зоны |
| `mobMoveUpdate` | Unicast | Пакет движения мобов |
| `mobCellLeft` | Unicast | Моб ушёл из подписанной ячейки (interest v2, evict) |
| `mobDeath` | Broadcast | Смерть моба |
| `mobHealthUpdate` | Broadcast | Обновление HP моба |
| `mobTargetLost` | Broadcast | Моб потерял цель |
| `combatInitiation` | Broadcast | Начало каста damage-скилла |
| `healingInitiation` | Broadcast | Начало каста heal-скилла |
| `buffInitiation` | Broadcast | Начало каста баффа |
| `debuffInitiation` | Broadcast | Начало каста дебаффа |
| `combatResult` | Broadcast | Результат damage-скилла |
| `healingResult` | Broadcast | Результат heal-скилла |
| `buffResult` | Broadcast | Результат баффа |
| `debuffResult` | Broadcast | Результат дебаффа |
| `effectTick` | Broadcast | Тик DoT/HoT |
| `combatAoeResult` | Broadcast | Батч-результат AoE-скилла |
| `itemDrop` | Broadcast | Предмет появился на земле |
| `itemPickup` | Broadcast | Предмет подобран |
| `itemRemove` | Broadcast | Предметы исчезли с земли |
| `getPlayerInventory` | Unicast | Состояние инвентаря |
| `WEIGHT_STATUS` | Unicast | Статус нагрузки |
| `EQUIPMENT_STATE` | Unicast | Состояние экипировки |
| `EQUIP_RESULT` | Unicast | Результат экипировки |
| `PLAYER_EQUIPMENT_UPDATE` | Unicast | Обновление экипировки |
| `DIALOGUE_NODE` | Unicast | Нода диалога |
| `DIALOGUE_CLOSE` | Unicast | Закрытие диалога |
| `dialogueError` | Unicast | Ошибка диалога |
| `vendorShop` | Unicast | Данные магазина |
| `buyItemResult` | Unicast | Результат покупки |
| `sellItemResult` | Unicast | Результат продажи |
| `buyItemBatchResult` | Unicast | Результат пакетной покупки |
| `sellItemBatchResult` | Unicast | Результат пакетной продажи |
| `repairShop` | Unicast | Данные кузницы |
| `repairItemResult` | Unicast | Результат ремонта |
| `repairAllResult` | Unicast | Результат ремонта всего |
| `tradeInvite` | Unicast | Приглашение к торговле |
| `tradeState` | Unicast | Состояние торговли |
| `tradeComplete` | Unicast | Торговля завершена |
| `tradeDeclined` | Unicast | Торговля отклонена |
| `tradeCancelled` | Unicast | Торговля отменена |
| `harvestStarted` | Unicast | Сбор начат |
| `harvestStartBroadcast` | Broadcast | Сбор начат (наблюдателям) |
| `harvestComplete` | Unicast | Сбор завершён |
| `harvestCompleteBroadcast` | Broadcast | Сбор завершён (наблюдателям) |
| `harvestCancelled` | Unicast | Сбор отменён |
| `harvestCancelBroadcast` | Broadcast | Сбор отменён (наблюдателям) |
| `harvestError` | Unicast | Ошибка сбора |
| `factAck` | Unicast | Квитанция факта: `body{key, status: applied\|duplicate}` |
| `nearbyCorpsesResponse` | Unicast | Список трупов рядом |
| `corpseLootInspect` | Unicast | Превью лута трупа |
| `corpseLootPickup` | Unicast | Результат подбора лута |
| `experience_update` | Broadcast | Обновление опыта — шлёт `ExperienceManager` через broadcast-callback (содержит `levelUp: true` при повышении уровня) |
| `experienceUpdate` | Broadcast | Дублирующий пакет от `ExperienceEventHandler` тем же получателям |
| `levelUp` | Broadcast | Отдельный пакет левел-апа сразу после (всем подключённым) |
| `characterExperience` | Unicast | Данные опыта (ответ на `getCharacterExperience`) |
| `stats_update` | Unicast | Полное обновление статов (включая тик регенерации) |
| `initializePlayerSkills` | Unicast | Инициализация скиллов |
| `respawnResult` | Unicast | Результат респавна |
| `getBestiaryOverview` | Unicast | Обзор бестиария |
| `getBestiaryEntry` | Unicast | Детали записи бестиария |
| `world_notification` | Unicast | Мировое уведомление |
| `chatMessage` | Varies | Сообщение чата |
| `nearbyItems` | Unicast | Предметы на земле рядом |

---

## Типы доставки

| Тип | Описание |
|-----|----------|
| **Broadcast** | Отправляется всем подключённым клиентам в зоне |
| **Unicast** | Отправляется только запросившему клиенту |
| **Multicast** | Отправляется группе клиентов (по расстоянию / каналу) |

---

## Обработка ошибок

Любой запрос может вернуть ошибку. Машиночитаемый код — всегда
`body.errorCode`, дублирующий `header.message` (серверы инжектят его
центрально в `generateResponseMessage`; явный код хендлера — например
диалоги/harvest/WIO — побеждает). Клиент матчится по `body.errorCode`:

```json
{
  "header": {
    "eventType": "pingClient",
    "status": "error",
    "message": "ERR_MALFORMED",
    "clientId": 42,
    "version": "1.0"
  },
  "body": {
    "errorCode": "ERR_MALFORMED"
  }
}
```

Коды фрейминга/версий — в разделах выше. Коды механик — в документах
02–11 (все ответы `status:error` несут `errorCode`). Коды сессий game:
`ERR_SESSION_INVALID` (login-токен неизвестен/протух/чужой account — game сверяет
`search_user(hash)` + привязку к account, обрыв БД — `ERR_DB`, никогда не висит),
`Authentication failed for user!` (пустой `clientId`/`hash`). Особые случаи:
`worldObjectInteractResult` приходит с `status:success` даже при
`success:false` — ошибку читать из `body.errorCode` (`OBJECT_NOT_FOUND`,
`TOO_FAR`, …).

---

## Безопасность

1. **Аутентификация**: `hash` проверяется при `joinGameClient` через login-сервер
2. **Верификация владельца**: `playerId == characterId` (сервер) проверяется на каждом запросе
3. **Валидация позиции**: дельта перемещения проверяется на максимальную скорость
4. **Проверка дистанции**: NPC/мобы/трупы проверяются на дистанцию взаимодействия
5. **Инвентарь**: предмет должен принадлежать персонажу перед любой операцией
6. **Защита буфера**: 64КБ макс буфер, 8КБ макс сообщение

## Вне скоупа (by design, не баги)

Следующего в протоколе **нет и не планируется** в текущем эпике:

- **PvP**: любой урон/дебафф по игроку отклоняется (`"PvP is not available"`).
- **Пати/гильдии/друзья, аукцион/почта/гильдбанк**: ни пакетов, ни таблиц, ни хендлеров.
- **День/ночь и погода**: нет цикла, нет пакетов (`rare`-мобы ждут цикла).
- **Proc-пассивы** (срабатывание от событий): только always-on модификаторы.
- **Прерывание кастов**: `interruptCombatAction` не зарегистрирован.
