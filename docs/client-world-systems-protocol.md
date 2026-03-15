# Протокол клиент–сервер: Мировые системы (Phase 2)

**Версия документа:** v1.0  
**Дата:** 2026-03-15  
**Актуально для:** chunk-server v0.0.4, game-server v0.0.4  
**Зависимости:** migration 037, `slug`-based localisation

---

## Содержание

1. [Локализация предметов через slug](#1-локализация-предметов-через-slug)
2. [Система Pity (гарантированный дроп)](#2-система-pity-гарантированный-дроп)
3. [Бестиарий (Bestiary)](#3-бестиарий-bestiary)
4. [world_notification — уведомления от мира](#4-world_notification--уведомления-от-мира)
5. [Формат предмета в пакетах (унифицированный)](#5-формат-предмета-в-пакетах-унифицированный)
6. [Sequence-диаграммы](#6-sequence-диаграммы)

---

## 1. Локализация предметов через slug

### Принцип

Поля `name` и `description` **удалены** из всех пакетов предметов. Клиент должен
использовать поле `slug` как ключ локализации.

```
items.slug  →  locale["items"]["iron_sword"]["name"]
            →  locale["items"]["iron_sword"]["description"]
```

Пример файла локализации (формат на усмотрение клиента):

```json
{
  "items": {
    "iron_sword": {
      "name": "Iron Sword",
      "description": "A sturdy iron sword forged from cold iron."
    },
    "potion_hp_small": {
      "name": "Minor Health Potion",
      "description": "Restores 150 HP instantly."
    }
  }
}
```

### Откуда брать slug

`slug` присутствует во всех пакетах, содержащих данные предмета:
- `getPlayerInventory` — поле `slug` в каждом объекте инвентаря
- `itemDrop` — поле `item.slug` в каждом наземном предмете
- `equipmentState` — поле `slug` в каждом слоте экипировки

> **Slug гарантированно уникален** и стабилен между версиями (первичный ключ поведения
> предмета, не только его имени).

### Slug'и типов, редкостей и слотов

Помимо `slug` самого предмета, пакеты инвентаря/экипировки содержат вспомогательные
slug'и для категорий — используйте их для иконок, цветов рамок и т.д.:

| Поле | Пример значений |
|------|----------------|
| `itemTypeSlug` | `weapon`, `armor`, `consumable`, `material`, `quest` |
| `raritySlug` | `common`, `uncommon`, `rare`, `epic`, `legendary` |
| `equipSlotSlug` | `main_hand`, `off_hand`, `head`, `chest`, `legs`, `feet`, `hands`, `waist`, `necklace`, `ring_1`, `ring_2`, `cloak` |

---

## 2. Система Pity (гарантированный дроп)

### Что это

Pity — механика защиты от неудачи при дропе редких предметов.
Сервер автоматически управляет счётчиком — клиент получает только:

1. **Hint-уведомление** (`world_notification` с типом `pity_hint`) — когда счётчик
   достиг порога `pity.hint_threshold_kills` (по умолчанию 500 убийств).
2. **Сам предмет** в пакете `itemDrop` — когда сработал гарантированный дроп.

Клиент **не управляет** счётчиком pity и **не запрашивает** данные pity явно.

### Как работает на сервере (справочно)

| Параметр | Конфиг-ключ | Значение по умолчанию |
|----------|-------------|----------------------|
| Старт soft pity | `pity.soft_pity_kills` | 300 убийств |
| Hard pity (гарантия) | `pity.hard_pity_kills` | 800 убийств |
| Бонус шанса за убийство после soft | `pity.soft_bonus_per_kill` | +0.005% |
| Порог hint-уведомления | `pity.hint_threshold_kills` | 500 убийств |

Счётчик per-character, per-item: если игрок убил 300+ мобов, которые могут дропнуть
`glacial_sword`, и предмет так и не выпал — начинает нарастать soft pity. На 800-м
убийстве предмет выпадает гарантированно. Счётчик сбрасывается при получении предмета.

### Hint-уведомление — сервер → клиент

Когда счётчик达到 `pity.hint_threshold_kills`, сервер отправляет:

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 42,
    "notificationType": "pity_hint",
    "text": "Ты давно охотишься здесь...",
    "data": {}
  }
}
```

**Рекомендуемое отображение:**
- Показать небольшое туманное сообщение в углу экрана (не интрузивный UI).
- НЕ показывать прогресс-бар — цель создать атмосферное ощущение, а не ещё один
  индикатор задания.

### Подтверждение Hard Pity

Когда hard pity срабатывает, предмет просто появляется в `itemDrop` как обычный дроп
моба. Никакого специального пакета нет — клиент не знает, что это было gарантированным
дропом.

---

## 3. Бестиарий (Bestiary)

### Концепция

Бестиарий хранит количество убийств каждого типа моба игроком. По мере накопления
убийств открываются тиры знаний о мобе:

| Тир | Убийств (по умолчанию) | Категория (`categorySlug`) |
|-----|----------------------|---------------------------|
| 1 | 1 | `basic_info` — уровень, ранг, диапазон HP, тип, биом |
| 2 | 5 | `weaknesses` — слабости и сопротивления |
| 3 | 15 | `common_loot` — обычный лут-тейбл |
| 4 | 30 | `uncommon_loot` — необычный лут-тейбл |
| 5 | 75 | `rare_loot` — редкий лут-тейбл |
| 6 | 150 | `very_rare_loot` — очень редкий лут (с примерным шансом) |

> Все числовые пороги тиров хранятся в конфигурации сервера (`bestiary.tierN_kills`).
> Клиент **никогда** не хардкодит пороги — они всегда приходят в ответе сервера
> в поле `requiredKills`.

### Разделение данных между сервером и клиентом

Клиентский файл локализации хранит **только** имя и описание моба по `slug`:

```json
{
  "mobs": {
    "forest_wolf": {
      "name": "Лесной волк",
      "description": "Серый волк из древних лесов."
    }
  },
  "bestiary_categories": {
    "basic_info":    "Основные сведения",
    "weaknesses":    "Слабости и сопротивления",
    "common_loot":   "Обычный лут",
    "uncommon_loot": "Необычный лут",
    "rare_loot":     "Редкий лут",
    "very_rare_loot":"Очень редкий лут"
  }
}
```

Все игровые данные (HP range, слабости, лут, шансы дропа) **хранятся только на сервере**
и передаются клиенту исключительно при открытии соответствующего тира.
Это исключает data leakage: игрок не может узнать редкий лут моба, не убив его нужное количество раз.

### 3.1 Запрос записи бестиария — клиент → сервер

Клиент запрашивает данные о конкретном шаблоне моба:

```json
{
  "header": {
    "eventType": "getBestiaryEntry",
    "clientId": 12,
    "hash": "abc123"
  },
  "body": {
    "characterId": 42,
    "mobTemplateId": 7
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа |
| `mobTemplateId` | int | ID шаблона моба (`mobs.id` в БД) |

### 3.2 Ответ бестиария — сервер → клиент

Ответ содержит единый массив `tiers` — **все** тиры моба, и открытые и закрытые.
Для открытых тиров присутствует поле `data` с реальными данными.
Для закрытых тиров `data` **отсутствует** — только метаинформация (`categorySlug`, `requiredKills`).

```json
{
  "header": {
    "eventType": "getBestiaryEntry",
    "clientId": 12,
    "status": "success"
  },
  "body": {
    "characterId": 42,
    "entry": {
      "mobTemplateId": 7,
      "mobSlug": "forest_wolf",
      "killCount": 23,
      "tiers": [
        {
          "tier": 1,
          "categorySlug": "basic_info",
          "requiredKills": 1,
          "unlocked": true,
          "data": {
            "level": 12,
            "rank": "normal",
            "hpMin": 80,
            "hpMax": 120,
            "type": "beast",
            "biomeSlug": "forest"
          }
        },
        {
          "tier": 2,
          "categorySlug": "weaknesses",
          "requiredKills": 5,
          "unlocked": true,
          "data": {
            "weaknesses": ["fire"],
            "resistances": ["nature", "water"]
          }
        },
        {
          "tier": 3,
          "categorySlug": "common_loot",
          "requiredKills": 15,
          "unlocked": false,
          "requiredKillsLeft": 7
        },
        {
          "tier": 4,
          "categorySlug": "uncommon_loot",
          "requiredKills": 30,
          "unlocked": false
        },
        {
          "tier": 5,
          "categorySlug": "rare_loot",
          "requiredKills": 75,
          "unlocked": false
        },
        {
          "tier": 6,
          "categorySlug": "very_rare_loot",
          "requiredKills": 150,
          "unlocked": false
        }
      ]
    }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `entry.mobTemplateId` | int | ID шаблона моба |
| `entry.mobSlug` | string | Slug моба — ключ для локализации имени/описания |
| `entry.killCount` | int | Общее количество убийств этого моба |
| `entry.tiers` | array | Все тиры моба — и открытые, и закрытые |
| `tier.tier` | int | Номер тира (1-индексированный) |
| `tier.categorySlug` | string | Тип раскрываемых данных — клиент локализует (`bestiary_categories`) |
| `tier.requiredKills` | int | Порог убийств для открытия тира (из конфига сервера) |
| `tier.unlocked` | bool | `true` — тир открыт, присутствует `data`. `false` — тир закрыт, `data` отсутствует |
| `tier.requiredKillsLeft` | int? | Убийств до открытия — только для ближайшего закрытого тира |
| `tier.data` | object? | Только для `unlocked: true`. Содержимое зависит от `categorySlug` (см. ниже) |

#### Структура `tier.data` по категориям

| `categorySlug` | Поля в `data` |
|---|---|
| `basic_info` | `level` (int), `rank` (string), `hpMin` (int), `hpMax` (int), `type` (string), `biomeSlug` (string) |
| `weaknesses` | `weaknesses` (string[]), `resistances` (string[]) |
| `common_loot` | `loot`: `[{ itemSlug, chance }]` |
| `uncommon_loot` | `loot`: `[{ itemSlug, chance }]` |
| `rare_loot` | `loot`: `[{ itemSlug, chance }]` |
| `very_rare_loot` | `loot`: `[{ itemSlug, chance }]` |

#### Ранги мобов (`rank`)

| Значение | Описание |
|---|---|
| `normal` | Рядовой моб |
| `elite` | Элитный — повышенные статы |
| `rare` | Редкий именной моб |
| `boss` | Босс |

### 3.3 Отображение закрытых тиров на клиенте

```
Открыл UI бестиария / навёл на моба
    → отправить getBestiaryEntry { characterId, mobTemplateId }
    → для каждого tier в entry.tiers:
        если unlocked == true  → показать tier.data
        если unlocked == false → показать заглушку "???" + categorySlug (локализованное)
                                  + "ещё N убийств" (tier.requiredKillsLeft, если есть)
```

Пример UI для моба с `killCount = 23`:

```
[✓] Основные сведения       (1 убийство)   → level 12, HP 80–120, тип: зверь, биом: лес
[✓] Слабости                (5 убийств)    → Слабость: огонь | Сопротивление: природа, вода
[?] Обычный лут             (ещё 7 убийств)→ ???
[?] Необычный лут           (30 убийств)   → ???
[?] Редкий лут              (75 убийств)   → ???
[?] Очень редкий лут        (150 убийств)  → ???
```

### 3.4 Рекомендации по реализации

**Кэширование:** Данные бестиария можно кэшировать на сессию.
После убийства нового типа моба не нужно делать запрос — только при открытии UI.
Инвалидировать кэш стоит при получении `bestiary_tier_unlocked` уведомления (см. ниже).

**Уведомление при разблокировке тира:**
Когда убийство открывает новый тир, сервер отправляет `world_notification` с типом
`bestiary_tier_unlocked`. Клиент должен инвалидировать кэш для данного `mobTemplateId`
и, если UI открыт, перезапросить `getBestiaryEntry`.

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationType": "bestiary_tier_unlocked",
    "text": "",
    "data": {
      "mobTemplateId": 7,
      "mobSlug": "forest_wolf",
      "unlockedTier": 3,
      "categorySlug": "common_loot"
    }
  }
}
```

**Первое убийство нового моба** (тир 1 открыт) тоже приходит как `bestiary_tier_unlocked`
с `unlockedTier: 1`. Клиент может показать тост "Новая запись бестиария: Лесной волк".

---

## 4. world_notification — уведомления от мира

### Общий формат

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 42,
    "notificationType": "...",
    "text": "...",
    "data": { }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа-получателя (личное уведомление) |
| `notificationType` | string | Тип уведомления — см. таблицу ниже |
| `text` | string | Текст уведомления (может быть пустым, если клиент строит его сам) |
| `data` | object | Дополнительные данные, зависят от типа |

### Типы уведомлений

| `notificationType` | Источник | `text` | `data` | Рекомендуемое отображение |
|--------------------|----------|--------|--------|--------------------------|
| `pity_hint` | `PityManager` при достижении `pity.hint_threshold_kills` | `"Ты давно охотишься здесь..."` | `{}` | Атмосферное сообщение, полупрозрачный текст |
| `zone_explored` | `GameZoneManager` при первом посещении зоны | `"Ты исследовал: {zone_name}"` | `{ "zoneSlug": "dead_forest", "xpGained": 250 }` | Флоат-текст + звук открытия |
| `level_up` | `ExperienceManager` | `"Новый уровень: {N}"` | `{ "newLevel": N }` | Большой центральный flash-эффект |
| `fellowship_bonus` | `FellowshipBonusSystem` при совместном убийстве | `"Fellowship bonus!"` | `{ "xpBonus": 50 }` | Небольшой +XP флоат рядом с другим игроком |
| `bestiary_tier_unlocked` | `BestiaryManager` при открытии нового тира (в т.ч. тира 1) | `""` | `{ "mobTemplateId": 7, "mobSlug": "forest_wolf", "unlockedTier": 3, "categorySlug": "common_loot" }` | Тост "Новая запись бестиария" / "Открыт новый тир" |

> Список будет расширяться в последующих этапах. Клиент должен обрабатывать
> неизвестные `notificationType` gracefully (не краш, просто лог или игнор).

### Пример: zone_explored

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 42,
    "notificationType": "zone_explored",
    "text": "Ты исследовал: Мёртвый Лес",
    "data": {
      "zoneSlug": "dead_forest",
      "xpGained": 250
    }
  }
}
```

### Пример: pity_hint

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 42,
    "notificationType": "pity_hint",
    "text": "Ты давно охотишься здесь...",
    "data": {}
  }
}
```

---

## 5. Формат предмета в пакетах (унифицированный)

Актуальная структура объекта предмета (item object) после удаления `name`/`description`.
Используется во всех пакетах: `getPlayerInventory`, `itemDrop`, `equipmentState`.

```json
{
  "id": 5,
  "slug": "iron_sword",
  "isQuestItem": false,
  "itemType": 2,
  "itemTypeName": "Weapon",
  "itemTypeSlug": "weapon",
  "isContainer": false,
  "isDurable": true,
  "isTradable": true,
  "isEquippable": true,
  "isHarvest": false,
  "isUsable": false,
  "weight": 3.5,
  "rarityId": 1,
  "rarityName": "Common",
  "raritySlug": "common",
  "stackMax": 1,
  "durabilityMax": 100,
  "vendorPriceBuy": 150,
  "vendorPriceSell": 75,
  "equipSlot": 3,
  "equipSlotName": "Main Hand",
  "equipSlotSlug": "main_hand",
  "levelRequirement": 5,
  "isTwoHanded": false,
  "allowedClassIds": [],
  "setId": 0,
  "setSlug": "",
  "attributes": [
    {
      "id": 1,
      "item_id": 5,
      "name": "Attack Power",
      "slug": "attack_power",
      "value": 12
    }
  ],
  "useEffects": []
}
```

> `rarityName`, `itemTypeName`, `equipSlotName` — человекочитаемые названия
> (на английском, для внутренних нужд). Для отображения игроку используйте
> slug-ключи + локализацию клиента.

### Поля, специфичные для инвентаря

При получении предмета через `getPlayerInventory` к объекту предмета добавляются:

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа |
| `itemId` | int | ID предмета в таблице `items` |
| `quantity` | int | Количество в стаке |
| `slotIndex` | int | Индекс слота инвентаря |
| `isEquipped` | bool | Надет ли прямо сейчас |
| `durabilityCurrent` | int | Текущая прочность |

### Поля, специфичные для наземного предмета

При получении предмета через `itemDrop`:

| Поле | Тип | Описание |
|------|-----|----------|
| `uid` | int | Уникальный ID наземного предмета (для подбора) |
| `itemId` | int | ID предмета |
| `quantity` | int | Количество |
| `canBePickedUp` | bool | Можно ли подобрать |
| `droppedByMobUID` | int | UID моба-источника (0 = не моб) |
| `droppedByCharacterId` | int | ID персонажа-дроппера (0 = не персонаж) |
| `reservedForCharacterId` | int | Кому зарезервирован (0 = всем доступен) |
| `reservationSecondsLeft` | int | Секунд до снятия резерва |
| `position` | object | `{ x, y, z, rotationZ }` |
| `item` | object | Полный объект предмета (структура выше) |

---

## 6. Sequence-диаграммы

### 6.1 Загрузка персонажа (pity + bestiary)

```
Client                 Chunk Server              Game Server
  |                        |                         |
  |───joinGame────────────►|                         |
  |                        |───getPlayerPityData────►|
  |                        |───getPlayerBestiary─────►|
  |                        |◄──setPlayerPityData─────|  (загружается в PityManager)
  |                        |◄──setPlayerBestiary──────|  (загружается в BestiaryManager)
  |◄──joinGameCharacter────|
  |◄──stats_update─────────|
  |◄──getPlayerInventory───|
```

Клиент не участвует в обмене pity/bestiary при логине — это внутренний обмен между серверами.

### 6.2 Убийство моба → дроп с pity

```
Client                 Chunk Server
  |                        |
  |───combatAction────────►|  (атака)
  |                        |  CombatSystem: урон, моб умер
  |                        |  BestiaryManager.recordKill(charId, mobTemplateId)
  |                        |  LootManager.generateLoot:
  |                        |    - roll шанс дропа
  |                        |    - isHardPity? → гарантированный дроп
  |                        |    - успех → resetCounter(pity)
  |                        |    - провал → incrementCounter(pity)
  |                        |      (если hit threshold → отправить pity_hint)
  |◄──itemDrop─────────────|  (broadcast; зарезервировано для убийцы 30 сек)
  |◄──world_notification───|  (только если pity_hint threshold достигнут)
  |◄──stats_update─────────|  (XP за убийство)
```

### 6.3 Запрос бестиария

```
Client                 Chunk Server
  |                        |
  | [открыл UI бестиария]  |
  |───getBestiaryEntry────►|  { characterId: 42, mobTemplateId: 7 }
  |◄──getBestiaryEntry─────|  { entry: { mobTemplateId: 7, killCount: 23, revealedTiers: [1,2,3] } }
```

### 6.4 Подбор предмета с земли

```
Client                 Chunk Server
  |                        |
  |───itemPickup──────────►|  { characterId, itemUID, posX, posY, posZ, rotZ }
  |                        |  проверка резервации и позиции
  |◄──itemPickup───────────|  { success: true, characterId, droppedItemUID } (broadcast)
  |◄──getPlayerInventory───|  обновлённый инвентарь (только для поднявшего)
  |◄──stats_update─────────|  обновлённый вес (только для поднявшего)
```

---

## Appendix A: Какие пакеты приходят автоматически при логине

| Порядок | Пакет | Источник |
|---------|-------|----------|
| 1 | `joinGameClient` | Сессия установлена, clientId выдан |
| 2 | `joinGameCharacter` | Broadcast всем в чанке: появился новый персонаж |
| 3 | `stats_update` | Базовые статы + вес после загрузки инвентаря |
| 4 | `getPlayerInventory` | Полный инвентарь |
| 5 | `itemDrop` | Снапшот наземных предметов в чанке |
| 6 | `stats_update` | (второй раз) Статы после загрузки активных эффектов |
| 7 | `equipmentState` | Надетая экипировка |

Pity и bestiary загружаются **серверами** внутренне — клиент не получает отдельных пакетов.

---

## Appendix B: Коды ошибок / edge cases

| Ситуация | Поведение сервера |
|----------|------------------|
| `getBestiaryEntry` с `mobTemplateId`, которого нет у персонажа | Возвращает `entry` с `killCount: 0, revealedTiers: []` |
| `itemPickup` — предмет зарезервирован за другим игроком | `success: false` только для инициатора, без broadcast |
| `itemPickup` — предмет уже подобрали | `success: false`, itemRemove уже был отправлен ранее |
| Pity counter достиг hard pity | Дроп происходит, counter сбрасывается, клиент видит обычный `itemDrop` |
| Неизвестный `notificationType` в `world_notification` | Клиент должен игнорировать и продолжать работу |

---

## 7. Мобы — спавн, движение, смерть

### 7.1 Спавн мобов в зоне — `spawnMobsInZone`

Отправляется клиенту при входе в зону (индивидуально) и при спавне новых мобов (broadcast).

**Направление:** Сервер → Клиент (индивидуально/broadcast)  
**eventType:** `spawnMobsInZone`

```json
{
  "header": {
    "eventType": "spawnMobsInZone",
    "clientId": 42,
    "message": "Spawning mobs success!"
  },
  "body": {
    "spawnZone": {
      "id": 3,
      "name": "Dark Forest Spawn",
      "bounds": {
        "minX": 100.0, "maxX": 300.0,
        "minY": 50.0,  "maxY": 250.0,
        "minZ": 0.0,   "maxZ": 0.0
      },
      "spawnMobId": 5,
      "maxSpawnCount": 10,
      "spawnedMobsCount": 7,
      "respawnTime": 60000,
      "spawnEnabled": true
    },
    "mobs": [
      {
        "id": 5,
        "uid": 1001,
        "zoneId": 3,
        "name": "Волк",
        "slug": "wolf",
        "race": "Beast",
        "level": 6,
        "isAggressive": true,
        "isDead": false,
        "stats": {
          "health": { "current": 200, "max": 200 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 143.5, "y": 88.2, "z": 0.0, "rotationZ": 1.57 },
        "velocity": { "dirX": 0.7, "dirY": 0.0, "speed": 150.0 },
        "combatState": 0,
        "attributes": [
          { "id": 1, "name": "Physical Attack", "slug": "physical_attack", "value": 30 }
        ]
      }
    ]
  }
}
```

**Поля объекта моба:**

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | ID шаблона моба (из `mobs` таблицы) |
| `uid` | int | Уникальный ID экземпляра в текущей сессии |
| `zoneId` | int | ID спавн-зоны |
| `slug` | string | Slug моба для локализации имени |
| `race` | string | Раса для визуальной классификации |
| `level` | int | Уровень моба |
| `isAggressive` | bool | Атакует ли игроков первым |
| `isDead` | bool | Мёртв ли (не должен появляться до respawn) |
| `stats.health` / `stats.mana` | object | Текущие и максимальные HP/Mana |
| `position` | object | `{x, y, z, rotationZ}` |
| `velocity` | object | `{dirX, dirY, speed}` — нормализованный вектор + скорость в юнитах/сек |
| `combatState` | int | Состояние ИИ (0=патруль, 1=преследование, ...) |
| `attributes` | array | Атрибуты моба (одни и те же для всех экземпляров шаблона) |

**Dead reckoning:** Используйте `velocity.dirX/dirY * speed * deltaTime` для интерполяции позиции между пакетами. При следующем `zoneMoveMobs` — плавно lerp к autoritатичной позиции (~100-150ms).

### 7.2 Полное обновление мобов зоны — `zoneMoveMobs`

Рассылается при перемещении мобов и содержит полный снапшот данных каждого моба. Используется для первичной синхронизации и при необходимости пересинхронизации.

**Направление:** Сервер → Broadcast  
**eventType:** `zoneMoveMobs`

```json
{
  "header": {
    "eventType": "zoneMoveMobs",
    "message": "Moving mobs success!"
  },
  "body": {
    "mobs": [
      {
        "id": 5,
        "uid": 1001,
        "zoneId": 3,
        "name": "Волк",
        "slug": "wolf",
        "race": "Beast",
        "level": 6,
        "isAggressive": true,
        "isDead": false,
        "stats": {
          "health": { "current": 155, "max": 200 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 150.2, "y": 92.1, "z": 0.0, "rotationZ": 0.8 },
        "velocity": { "dirX": 0.6, "dirY": 0.8, "speed": 150.0 },
        "combatState": 1,
        "attributes": [
          { "id": 1, "name": "Physical Attack", "slug": "physical_attack", "value": 30 }
        ]
      }
    ]
  }
}
```

> Структура каждого моба в массиве идентична `spawnMobsInZone`. Пакет содержит только тех мобов, которые фактически переместились с прошлого тика.

### 7.3 Лёгкое обновление позиций мобов — `mobMoveUpdate`

Периодический высокочастотный пакет, содержащий только данные для интерполяции движения. Отправляется **конкретному клиенту** (не broadcast) и содержит минимально необходимые поля.

**Направление:** Сервер → Клиент (конкретному)  
**eventType:** `mobMoveUpdate`

```json
{
  "header": {
    "eventType": "mobMoveUpdate",
    "clientId": 42,
    "serverSendMs": 1741789000123
  },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 3,
        "position": { "x": 150.2, "y": 92.1, "z": 0.0, "rotationZ": 0.8 },
        "velocity": { "dirX": 0.6, "dirY": 0.8, "speed": 150.0 },
        "combatState": 1
      },
      {
        "uid": 1002,
        "zoneId": 3,
        "position": { "x": 210.0, "y": 130.5, "z": 0.0, "rotationZ": 2.3 },
        "velocity": { "dirX": 0.0, "dirY": 0.0, "speed": 0.0 },
        "combatState": 0
      }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `uid` | int | Уникальный ID экземпляра моба |
| `zoneId` | int | ID спавн-зоны |
| `position` | object | Авторитативная позиция на момент отправки |
| `velocity` | object | `{dirX, dirY, speed}` — нормализованный вектор + скорость в юнитах/сек |
| `combatState` | int | Текущее состояние ИИ (0-7, см. раздел 7.4) |

**Логика dead reckoning на клиенте:**
1. При получении `mobMoveUpdate`: lerp текущую позицию моба к `position` за ~100-150ms
2. Между пакетами: экстраполировать `position += dirX/dirY * speed * deltaTime`
3. `serverSendMs` в заголовке — метка серверного времени для компенсации лага

### 7.4 Смерть моба — `mobDeath`

**Направление:** Сервер → Broadcast  
**eventType:** `mobDeath`

```json
{
  "header": {
    "eventType": "mobDeath",
    "message": "Mob died"
  },
  "body": {
    "mobUID": 1001,
    "zoneId": 3
  }
}
```

После `mobDeath`:
- Удалить моба из отображаемого мира
- Возможен `itemDrop` broadcast (дроп предметов с трупа)
- Возможен `experience_update` (только убийце)
- Тело моба остаётся для системы `harvestCorpse` до respawn таймера

### 7.5 Состояние ИИ моба — combatState

| Значение | Название | Описание |
|---------|----------|----------|
| `0` | PATROLLING | Патрулирует зону спавна |
| `1` | CHASING | Преследует цель |
| `2` | PREPARING_ATTACK | Подготовка атаки |
| `3` | ATTACKING | Атакует |
| `4` | COOLDOWN | Восстановление после атаки |
| `5` | RETURNING | Возвращается на точку спавна (неуязвим) |
| `6` | EVADING | Период неуязвимости после возвращения |
| `7` | FLEEING | Бежит при < 20% HP |

---

## 8. NPC — спавн и данные

### 8.1 Спавн NPC — `spawnNPCs`

Отправляется клиенту сразу после `joinGameCharacter` — список всех NPC вокруг позиции персонажа (радиус 50,000 юнитов).

**Направление:** Сервер → Клиент (только вошедшему)  
**eventType:** `spawnNPCs`

```json
{
  "header": {
    "eventType": "spawnNPCs",
    "clientId": 42,
    "message": "NPCs spawn data for area"
  },
  "body": {
    "spawnRadius": 50000.0,
    "npcCount": 3,
    "npcsSpawn": [
      {
        "id": 10,
        "name": "Мечник Торвальд",
        "slug": "merchant_torvald",
        "race": "Human",
        "level": 1,
        "npcType": "vendor",
        "isInteractable": true,
        "dialogueId": 5,
        "questId": 0,
        "stats": {
          "health": { "current": 500, "max": 500 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 50.0, "y": 30.0, "z": 0.0, "rotationZ": 0.0 },
        "attributes": []
      },
      {
        "id": 11,
        "name": "Кузнец Брагор",
        "slug": "blacksmith_bragor",
        "race": "Dwarf",
        "level": 1,
        "npcType": "repair",
        "isInteractable": true,
        "dialogueId": 8,
        "questId": 0,
        "stats": {
          "health": { "current": 800, "max": 800 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 80.0, "y": 30.0, "z": 0.0, "rotationZ": 3.14 },
        "attributes": []
      }
    ]
  }
}
```

**Поля NPC объекта:**

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | ID NPC в базе данных |
| `slug` | string | Уникальный slug для локализации имени |
| `race` | string | Раса NPC |
| `level` | int | Уровень (влияет на отображение в UI) |
| `npcType` | string | Тип NPC (см. таблицу ниже) |
| `isInteractable` | bool | Можно ли взаимодействовать |
| `dialogueId` | int | ID диалогового дерева (0 = нет диалога) |
| `questId` | int | ID квеста NPC (0 = нет квеста) |
| `stats` | object | HP/Mana для отображения |
| `position` | object | `{x, y, z, rotationZ}` |
| `attributes` | array | Боевые атрибуты |

**Типы NPC (`npcType`):**

| Значение | Описание |
|----------|----------|
| `vendor` | Торговец предметами |
| `repair` | Ремонтник экипировки |
| `quest` | Квестовый NPC |
| `dialogue` | Только диалог (без квеста/магазина) |
| `guard` | Охранник |
| `trainer` | Тренер скиллов |

---

## 9. Чемпионы — мобы

Чемпион отображается как обычный моб в `spawnMobsInZone`, идентифицируется по:
1. **Уведомлению `champion_spawned`** с `data.uid` — сохраните этот UID как "чемпион"
2. **Префиксу имени** `[!] ` в поле `name`

> `isChampion` и `rankCode` хранятся в серверной памяти, но **не сериализуются** в пакет `spawnMobsInZone`. Клиент использует `data.uid` из `world_notification`.

### Последовательность появления чемпиона

```
1. world_notification (notificationType: "champion_spawned_soon") — предупреждение
   data: { "slug": "wolf" }

2. world_notification (notificationType: "champion_spawned") — чемпион заспавнился
   data: { "mobSlug": "wolf", "uid": 10023 }

3. spawnMobsInZone — broadcast со стандартным mob JSON
   name: "[!] Матёрый Волк"  ← идентификатор для UI

4. (через 30 минут без убийства)
   world_notification (notificationType: "champion_despawned")
   mobDeath (broadcast)

   или при убийстве:
   combatResult (targetDied: true)
   mobDeath
   world_notification (notificationType: "champion_killed")
   itemDrop (увеличенный лут)
   experience_update (x2 XP)
```

---

## 10. Справочник world_notification

Актуальная таблица всех типов уведомлений (поле `body.notificationType`):

| `notificationType` | `data` поля | Кому | Описание |
|--------------------|-------------|------|----------|
| `pity_hint` | `{}` | Только владельцу | Счётчик пити близок к дропу |
| `zone_explored` | `zoneSlug`, `xpGained` | Только владельцу | Первое посещение зоны |
| `mastery_tier_up` | `masterySlug`, `masteryValue`, `tier` | Только владельцу | Повышение тира мастерства |
| `champion_spawned_soon` | `slug` | Всем в игровой зоне | Чемпион вот-вот появится |
| `champion_spawned` | `mobSlug`, `uid` | Всем в игровой зоне | Чемпион заспавнился |
| `champion_killed` | `killerCharId` | Всем в игровой зоне | Чемпион убит игроком |
| `champion_despawned` | `{}` | Всем в игровой зоне | Чемпион исчез по таймауту |
| `zone_event_start` | `eventSlug`, `durationSec`, `gameZoneId` | Всем в игровой зоне | Началось зональное событие |
| `zone_event_end` | `eventSlug` | Всем в игровой зоне | Зональное событие завершилось |
