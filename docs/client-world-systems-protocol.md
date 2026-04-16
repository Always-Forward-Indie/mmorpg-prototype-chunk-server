# Протокол клиент–сервер: Мировые системы (Phase 2)

**Версия документа:** v1.4  
**Дата:** 2026-03-20  
**Актуально для:** chunk-server v0.0.4, game-server v0.0.4  
**Зависимости:** migration 037, `slug`-based localisation

**Изменения v1.4:**
- Секция 7.5: исправлено название `ATTACK_COOLDOWN` (было `COOLDOWN`), уточнён порог `FLEEING` (configurable per-mob), уточнена иммунность RETURNING/EVADING
- Добавлен раздел 7.6 `mobTargetLost` — broadcast когда моб теряет цель
- Добавлен раздел 7.7 `mobHealthUpdate` — broadcast обновления HP во время leash-регенерации
- Добавлен раздел 7.5 с кросс-ссылкой на `mob-ai-movement-protocol.md`

**Изменения v1.3:**
- `bestiary_tier_unlocked`: добавлено поле `killCount` в `data` — клиент больше не
  обнуляет счётчик при разблокировке тира
- Добавлен тип уведомления `bestiary_kill_update` (`priority: low`, `channel: silent`) —
  сервер отправляет его **на каждое убийство** моба; клиент обновляет счётчик в overview-списке
  без перезапроса сервера
- Исправлена sequence-диаграмма 6.2 с учётом двух новых уведомлений

**Изменения v1.2:**
- Бестиарий: `getBestiaryEntry` — клиент отправляет `mobSlug` вместо `mobTemplateId`
- Бестиарий: ответ `getBestiaryEntry` больше не содержит `entry.mobTemplateId`
- Бестиарий: добавлен пакет `getBestiaryOverview` — список открытых мобов по `mobSlug`
- `bestiary_tier_unlocked`: поле `mobTemplateId` удалено из `data`, остался только `mobSlug`

**Изменения v1.1:**
- Бестиарий: 6 тиров пересмотрены (новые слаги и пороги убийств)
- Бестиарий: тиры `lore`, `combat_info` (+ abilities), `loot_table`, `drop_rates`, `hunter_mastery`
- `bestiary_tier_unlocked`: добавлено поле `mobSlug`
- `world_notification`: добавлены поля `priority`, `channel`, `notificationId`; поле `text` всегда `""`
- Все `text` в `sendWorldNotification` удалены — локализация только на клиенте

---

## Содержание

1. [Локализация предметов через slug](#1-локализация-предметов-через-slug)
2. [Система Pity (гарантированный дроп)](#2-система-pity-гарантированный-дроп)
3. [Бестиарий (Bestiary)](#3-бестиарий-bestiary)
   - 3.1 Обзор открытых мобов (`getBestiaryOverview`)
   - 3.2 Запись о конкретном мобе (`getBestiaryEntry`)
4. [world_notification — уведомления от мира](#4-world_notification--уведомления-от-мира)
5. [Формат предмета в пакетах (унифицированный)](#5-формат-предмета-в-пакетах-унифицированный)
6. [Sequence-диаграммы](#6-sequence-диаграммы)
7. [Мобы — спавн, движение, смерть](#7-мобы--спавн-движение-смерть)
   - 7.1 Спавн мобов в зоне (`spawnMobsInZone`)
   - 7.2 Полное обновление мобов зоны (`zoneMoveMobs`)
   - 7.3 Лёгкое обновление позиций мобов (`mobMoveUpdate`)
   - 7.4 Смерть моба (`mobDeath`)
   - 7.5 Состояние ИИ моба (`combatState`)
   - 7.6 Моб потерял цель (`mobTargetLost`)
   - 7.7 Обновление HP моба (`mobHealthUpdate`)
8. [NPC — спавн и данные](#8-npc--спавн-и-данные)
9. [Чемпионы — мобы](#9-чемпионы--мобы)
10. [Справочник world_notification](#10-справочник-world_notification)

> **AI-поведение мобов** (state machine, тайминги, архетипы, threat, social aggro, melee slots):  
> см. [mob-ai-movement-protocol.md](mob-ai-movement-protocol.md)

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
| 2 | 5 | `lore` — ключ лора (`loreKey`); клиент берёт текст из `mobs.{slug}.lore` |
| 3 | 15 | `combat_info` — слабости, сопротивления, слаги активных способностей (`abilities`) |
| 4 | 30 | `loot_table` — все дропаемые предметы (только слаги, без шансов) |
| 5 | 75 | `drop_rates` — все предметы с точными шансами дропа |
| 6 | 150 | `hunter_mastery` — milestone: `titleSlug`, `achievementSlug` |

> Все числовые пороги тиров хранятся в конфигурации сервера (`bestiary.tierN_kills`).
> Клиент **никогда** не хардкодит пороги — они всегда приходят в ответе сервера
> в поле `requiredKills`.

> **Нет миграции БД.** Данные тиров 2–6 уже присутствуют в схеме (mob_skills,
> mob_loot_info). Лор и milestone-слаги генерируются из `mobSlug` на сервере;
> текст хранится в клиентских локализационных файлах.

### Разделение данных между сервером и клиентом

Клиентский файл локализации хранит имя, описание и лор моба по `slug`, а также
локализованные названия категорий:

```json
{
  "mobs": {
    "forest_wolf": {
      "name": "Лесной волк",
      "description": "Серый волк из древних лесов.",
      "lore": "Серые волки Древнего леса держатся стаями по 3–5 особей. В полнолуние стаи объединяются и становятся агрессивны даже к вооружённым путникам."
    }
  },
  "bestiary_categories": {
    "basic_info":      "Основные сведения",
    "lore":            "История",
    "combat_info":     "Боевые особенности",
    "loot_table":      "Добыча",
    "drop_rates":      "Шансы дропа",
    "hunter_mastery":  "Мастерство охотника"
  }
}
```

Все игровые данные (HP range, слабости, лут, шансы дропа) **хранятся только на сервере**
и передаются клиенту исключительно при открытии соответствующего тира.
Это исключает data leakage: игрок не может узнать точные шансы дропа, не убив моба нужное количество раз.

### 3.1 Обзор открытых мобов — `getBestiaryOverview`

При подключении сервер **автоматически** отправляет `getBestiaryOverview` сразу после загрузки данных бестиария из БД.
Клиент также может запросить его повторно в любой момент, например для рефреша UI.

**Автоматический пуш при логине** (направление: Сервер → Клиент) и **по запросу** клиента:

**Запрос (необязателен, только для рефреша):**
```json
{
  "header": { "eventType": "getBestiaryOverview" },
  "body": { "characterId": 42 }
}
```

**Ответ:**
```json
{
  "header": { "eventType": "getBestiaryOverview", "status": "success", "clientId": 12 },
  "body": {
    "characterId": 42,
    "entries": [
      { "mobSlug": "forest_wolf", "killCount": 23 },
      { "mobSlug": "cave_bat",    "killCount": 5  }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `entries` | array | Моб с `killCount > 0`. Пустой массив = персонаж ещё никого не убивал |
| `entries[].mobSlug` | string | Slug моба — ключ для имени/иконки в локализации |
| `entries[].killCount` | int | Общее количество убийств |

> **Обновление счётчиков в overview-списке:**
> - `bestiary_kill_update` (`channel: silent`) — приходит **на каждое убийство**; обновить `killCount`
>   для `mobSlug` в локальном кэше overview.
> - `bestiary_tier_unlocked` с `unlockedTier: 1` — первое убийство нового моба; добавить новую
>   запись `{ mobSlug, killCount: 1 }` в список (поле `killCount` теперь есть в пакете).
> Перезапрос `getBestiaryOverview` нужен только при явном ручном рефреше UI.

### 3.2 Запись о конкретном мобе — `getBestiaryEntry`

Клиент запрашивает полные данные о конкретном мобе (при открытии карточки):

```json
{
  "header": {
    "eventType": "getBestiaryEntry",
    "clientId": 12,
    "hash": "abc123"
  },
  "body": {
    "characterId": 42,
    "mobSlug": "forest_wolf"
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа |
| `mobSlug` | string | Slug моба — ключ локализации |

### 3.3 Ответ бестиария — сервер → клиент

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
          "categorySlug": "lore",
          "requiredKills": 5,
          "unlocked": true,
          "data": {
            "loreKey": "forest_wolf"
          }
        },
        {
          "tier": 3,
          "categorySlug": "combat_info",
          "requiredKills": 15,
          "unlocked": false,
          "requiredKillsLeft": 2
        },
        {
          "tier": 4,
          "categorySlug": "loot_table",
          "requiredKills": 30,
          "unlocked": false
        },
        {
          "tier": 5,
          "categorySlug": "drop_rates",
          "requiredKills": 75,
          "unlocked": false
        },
        {
          "tier": 6,
          "categorySlug": "hunter_mastery",
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
| `lore` | `loreKey` (string) — slug для lookup в `locale.mobs.{loreKey}.lore` |
| `combat_info` | `weaknesses` (string[]), `resistances` (string[]), `abilities` (string[]) — слаги активных скиллов |
| `loot_table` | `items` (string[]) — все slug'и дропаемых предметов, без шансов |
| `drop_rates` | `loot`: `[{ itemSlug, chance }]` — полный лут с точными шансами |
| `hunter_mastery` | `titleSlug` (string), `achievementSlug` (string) — `"{mobSlug}_hunter"` / `"{mobSlug}_master"` |

#### Ранги мобов (`rank`)

| Значение | Описание |
|---|---|
| `normal` | Рядовой моб |
| `elite` | Элитный — повышенные статы |
| `rare` | Редкий именной моб |
| `boss` | Босс |

### 3.4 Отображение закрытых тиров на клиенте

```
Открыл UI бестиария
    → (getData из накопленного на логине overview)
    → по клику на моба: отправить getBestiaryEntry { characterId, mobSlug }
    → для каждого tier в entry.tiers:
        если unlocked == true  → показать tier.data
        если unlocked == false → показать заглушку "???" + categorySlug (локализованное)
                                  + "ещё N убийств" (tier.requiredKillsLeft, если есть)
```

Пример UI для моба с `killCount = 23`:

```
[✓] Основные сведения       (1 убийство)   → level 12, HP 80–120, тип: зверь, биом: лес
[✓] История                 (5 убийств)    → «Серые волки Древнего леса держатся стаями...»
[✓] Боевые особенности      (15 убийств)   → слабости, сопротивления и т.д.
[?] Добыча                  (ещё 7 убийств)→ ???
[?] Шансы дропа             (75 убийств)   → ???
[?] Мастерство охотника     (150 убийств)  → ???
```

### 3.5 Рекомендации по реализации

**Кэширование:** Данные бестиария можно кэшировать на сессию. Перезапрос не нужен —
сервер присылает все обновления через `world_notification`.

**`bestiary_kill_update` — обновление счётчика на каждое убийство:**
После каждого убийства моба сервер отправляет тихое (`channel: silent`) уведомление
с актуальным `killCount`. Клиент обновляет счётчик в overview-списке без перезапроса.

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationId": "101",
    "notificationType": "bestiary_kill_update",
    "priority": "low",
    "channel": "silent",
    "text": "",
    "data": {
      "mobSlug": "forest_wolf",
      "killCount": 24
    }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `data.mobSlug` | string | Slug моба — ключ для обновления в кэше overview |
| `data.killCount` | int | Актуальное количество убийств после текущего инкремента |

**`bestiary_tier_unlocked` — разблокировка нового тира:**
Когда убийство открывает новый тир, сервер отправляет `world_notification` с типом
`bestiary_tier_unlocked`. Клиент инвалидирует кэш для данного `mobSlug`
и, если UI открыт, перезапрашивает `getBestiaryEntry`.

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationId": "42",
    "notificationType": "bestiary_tier_unlocked",
    "priority": "medium",
    "channel": "toast",
    "text": "",
    "data": {
      "mobSlug": "forest_wolf",
      "unlockedTier": 3,
      "categorySlug": "combat_info",
      "killCount": 15
    }
  }
}
```

**Первое убийство нового моба** (тир 1 открыт) тоже приходит как `bestiary_tier_unlocked`
с `unlockedTier: 1` и `killCount: 1`. Клиент добавляет новую запись в overview-список
и показывает тост «Новая запись бестиария: Лесной волк».

> **Порядок уведомлений при первом убийстве:** сервер отправляет сначала `bestiary_kill_update`,
> затем `bestiary_tier_unlocked` (tier 1). Клиент может игнорировать `bestiary_kill_update`
> если моб ещё не в overview-списке — его добавит следующее `bestiary_tier_unlocked`.

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
    "notificationId": "17",
    "notificationType": "...",
    "priority": "medium",
    "channel": "toast",
    "text": "",
    "data": { }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа-получателя (личное уведомление) |
| `notificationId` | string | Монотонно возрастающий счётчик (per server lifetime) — для дедупликации |
| `notificationType` | string | Тип уведомления — см. таблицу ниже |
| `priority` | string | `critical` \| `high` \| `medium` \| `low` \| `ambient` |
| `channel` | string | `screen_center` \| `toast` \| `float_text` \| `zone_banner` \| `atmosphere` \| `chat_log` |
| `text` | string | Всегда `""` — клиент строит отображаемый текст из `data` + locale |
| `data` | object | Дополнительные данные, зависят от типа |

> **Правило текста:** поле `text` всегда пусто. Весь отображаемый текст клиент
> получает через `notificationType` + `data` + локализационные файлы. Это гарантирует
> корректную i18n без изменений серверного кода.

### Типы уведомлений

| `notificationType` | Приоритет | Канал | `data` | Рекомендуемое отображение |
|--------------------|-----------|-------|--------|--------------------------|
| `pity_hint` | `ambient` | `atmosphere` | `{}` | Полупрозрачный текст по центру 3 с, без звука |
| `zone_entered` | `high` | `zone_banner` | `{ zoneSlug, minLevel, maxLevel, isPvp, isSafeZone }` | Баннер с именем зоны (slug→locale), PvP/safe-иконка |
| `zone_explored` | `medium` | `toast` | `{ zoneSlug, xpGained }` | Тост «Зона открыта» + +XP |
| `level_up` | `critical` | `screen_center` | `{ newLevel }` | Большой flash-эффект, звук |
| `fellowship_bonus` | `low` | `float_text` | `{ xpBonus }` | Плавающий +XP над персонажем |
| `bestiary_tier_unlocked` | `medium` | `toast` | `{ mobSlug, unlockedTier, categorySlug, killCount }` | Тост «Открыт новый тир бестиария: {mobSlug}»; инвалидировать кэш `getBestiaryEntry` |
| `bestiary_kill_update` | `low` | `silent` | `{ mobSlug, killCount }` | Обновить `killCount` в кэше overview-списка; не показывать UI |


> Список будет расширяться в последующих этапах. Клиент должен обрабатывать
> неизвестные `notificationType` gracefully (не краш, просто лог или игнор).

### Пример: zone_entered

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationId": "5",
    "notificationType": "zone_entered",
    "priority": "high",
    "channel": "zone_banner",
    "text": "",
    "data": {
      "zoneSlug":   "dead_forest",
      "minLevel":   10,
      "maxLevel":   20,
      "isPvp":      false,
      "isSafeZone": false
    }
  }
}
```

> `text` всегда пуст — клиент строит название зоны через `locale.zones.dead_forest.name`.

### Пример: zone_explored

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationId": "6",
    "notificationType": "zone_explored",
    "priority": "medium",
    "channel": "toast",
    "text": "",
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
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationId": "7",
    "notificationType": "pity_hint",
    "priority": "ambient",
    "channel": "atmosphere",
    "text": "",
    "data": {}
  }
}
```

> Атмосферный текст (например «Ты давно охотишься здесь...») хранится в locale клиента
> под ключом `notifications.pity_hint.text`.

### Пример: fellowship_bonus

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 42,
    "notificationId": "8",
    "notificationType": "fellowship_bonus",
    "priority": "low",
    "channel": "float_text",
    "text": "",
    "data": {
      "xpBonus": 120
    }
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
  |◄──getBestiaryOverview────|  (автопуш)
  |◄──joinGameCharacter────|
  |◄──stats_update─────────|
  |◄──getPlayerInventory───|
```

Pity загружается внутренне. `getBestiaryOverview` приходит клиенту автоматически.

### 6.2 Убийство моба → дроп с pity

```
Client                 Chunk Server
  |                        |
  |───combatAction────────►|  (атака)
  |                        |  CombatSystem: урон, моб умер
  |                        |  BestiaryManager.recordKill(charId, mobTemplateId)
  |◄──world_notification───|  bestiary_kill_update { mobSlug, killCount: N }  (всегда)
  |◄──world_notification───|  bestiary_tier_unlocked { mobSlug, unlockedTier, categorySlug, killCount: N }  (только при кроссинге порога)
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
  |    (использует кэш overview с логина)
  | [открыл карточку моба]  |
  |───getBestiaryEntry─────►|  { characterId: 42, mobSlug: "forest_wolf" }
  |◄──getBestiaryEntry──────|  { entry: { mobSlug: "forest_wolf", killCount: 23, tiers: [...] } }
```

Если нужен рефреш списка:

```
  |───getBestiaryOverview──►|  { characterId: 42 }
  |◄──getBestiaryOverview───|  { entries: [{ mobSlug: "forest_wolf", killCount: 23 }, ...] }
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
| 8 | `getBestiaryOverview` | Список уже открытых мобов |

Pity загружается внутренне. `getBestiaryOverview` приходит клиенту автоматически.

---

## Appendix B: Коды ошибок / edge cases

| Ситуация | Поведение сервера |
|----------|------------------|
| `getBestiaryEntry` с `mobSlug`, которого нет у персонажа | Возвращает `entry` с `killCount: 0`, все тиры закрыты |
| `itemPickup` — предмет зарезервирован за другим игроком | `success: false` только для инициатора, без broadcast |
| `itemPickup` — предмет уже подобрали | `success: false`, itemRemove уже был отправлен ранее |
| Pity counter достиг hard pity | Дроп происходит, counter сбрасывается, клиент видит обычный `itemDrop` |
| Неизвестный `notificationType` в `world_notification` | Клиент должен игнорировать и продолжать работу |

---

## 7. Мобы — спавн, движение, смерть

### 7.1 Спавн мобов в зоне — `spawnMobsInZone`

Отправляется клиенту автоматически после `joinGameCharacter` (индивидуально, по одному пакету на зону) — клиент **не запрашивает** эти данные. Также рассылается при спавне новых мобов (broadcast).

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
- Возможен `experienceUpdate` (только убийце)
- Тело моба остаётся для системы `harvestCorpse` до respawn таймера

### 7.5 Состояние ИИ моба — combatState

| Значение | Название | Описание |
|---------|----------|----------|
| `0` | `PATROLLING` | Патрулирует зону спавна (случайные waypoint-ы внутри AABB зоны) |
| `1` | `CHASING` | Преследует цель; шаг каждые 0.3 с (config `chaseMovementInterval`) |
| `2` | `PREPARING_ATTACK` | Подготовка удара (castTime, мob заморожен, не двигается) |
| `3` | `ATTACKING` | Анимация удара (swingMs, мob заморожен, не двигается) |
| `4` | `ATTACK_COOLDOWN` | Кулдаун после удара (мob заморожен, не двигается) |
| `5` | `RETURNING` | Leash: возврат к точке спавна, **неуязвим**, регенерирует HP 10%/сек |
| `6` | `EVADING` | 2-секундное окно неуязвимости после возврата (затем → PATROLLING) |
| `7` | `FLEEING` | Бежит от атакующего (порог HP настраивается per-mob, по умолчанию отключено) |

**Рекомендации клиенту:**
- Состояния 2–4: мob стоит на месте → не применять dead reckoning, заморозить позицию
- Состояния 5–6: мob неуязвим → показывать HP-бар серым или скрывать кнопку атаки
- Состояние 7: воспроизводить анимацию бегства

> Подробная документация AI: тайминги, переходы между состояниями, threat table, social aggro, melee slots, архетипы (melee/caster) — в [mob-ai-movement-protocol.md](mob-ai-movement-protocol.md)

### 7.6 Моб потерял цель — `mobTargetLost`

Broadcast. Отправляется когда моб теряет цель: цель вышла за пределы `aggroRange × chaseMultiplier`, цель умерла, истёк `chaseDuration`, или моб покинул зону (`maxChaseFromZoneEdge`).

**Направление:** Сервер → Broadcast  
**eventType:** `mobTargetLost`

```json
{
  "header": {
    "eventType": "mobTargetLost",
    "message": "Mob lost target"
  },
  "body": {
    "mobUID": 1001,
    "mobId": 5,
    "lostTargetPlayerId": 7,
    "positionX": 163.4,
    "positionY": 95.2,
    "positionZ": 0.0,
    "rotationZ": 1.2
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `mobUID` | int | Уникальный ID экземпляра моба |
| `mobId` | int | ID шаблона моба |
| `lostTargetPlayerId` | int | ID игрока, которого моб перестал преследовать |
| `positionX/Y/Z` | float | Позиция моба в момент потери цели |
| `rotationZ` | float | Направление взгляда в момент потери цели |

**Действие клиента:** обновить позицию моба, запустить анимацию перехода в PATROLLING / RETURNING. Следующий `mobMoveUpdate` или `zoneMoveMobs` придёт с `combatState: 5` (RETURNING) или `0` (PATROLLING).

### 7.7 Обновление HP моба — `mobHealthUpdate`

Broadcast. Отправляется каждую секунду пока моб находится в состоянии `RETURNING` и восстанавливает HP (leash regen). Темп: **10% maxHP / секунду**.

**Направление:** Сервер → Broadcast  
**eventType:** `mobHealthUpdate`

```json
{
  "header": {
    "eventType": "mobHealthUpdate",
    "message": "Mob health updated"
  },
  "body": {
    "mobUID": 1001,
    "mobId": 5,
    "currentHealth": 160,
    "maxHealth": 200
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `mobUID` | int | Уникальный ID экземпляра |
| `mobId` | int | ID шаблона |
| `currentHealth` | int | Текущий HP после регенерации |
| `maxHealth` | int | Максимальный HP |

**Действие клиента:** обновить HP-бар моба. Пакет приходит только в состоянии `RETURNING` (combatState 5). Когда HP достигает max или моб умирает — пакеты прекращаются.

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
        "quests": [],
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
        "npcType": "blacksmith",
        "isInteractable": true,
        "dialogueId": 8,
        "quests": [],
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
| `dialogueId` | string | ID диалогового дерева ("" = нет диалога) |
| `quests` | array | Квесты NPC с per-player статусом (см. ниже) |
| `stats` | object | HP/Mana для отображения |
| `position` | object | `{x, y, z, rotationZ}` |
| `attributes` | array | Боевые атрибуты |

**Структура элемента `quests[]`:**

| Поле | Тип | Описание |
|------|-----|----------|
| `slug` | string | Slug квеста |
| `status` | string | Статус для **этого персонажа**: `available`, `in_progress`, `completable`, `turned_in`, `failed` |

> `available` — квест можно взять; `completable` — все шаги выполнены, необходимо сдать.

**Типы NPC (`npcType`):**

| Значение | Описание |
|----------|----------|
| `general` | Обычный NPC, только диалог |
| `vendor` | Торговец предметами |
| `blacksmith` | Кузнец / ремонтник экипировки |
| `quest_giver` | Квестовый NPC (выдаёт и принимает квесты) |
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
   experienceUpdate (x2 XP)
```

---

## 10. Справочник world_notification

Актуальная таблица всех типов уведомлений (поле `body.notificationType`):

| `notificationType` | `data` поля | Кому | Описание |
|--------------------|-------------|------|----------|
| `pity_hint` | `{}` | Только владельцу | Счётчик пити близок к дропу |
| `zone_entered` | `zoneSlug`, `minLevel`, `maxLevel`, `isPvp`, `isSafeZone` | Только владельцу | Смена зоны (при каждом входе, вкл. первый логин) |
| `zone_explored` | `zoneSlug`, `xpGained` | Только владельцу | Первое посещение зоны |
| `mastery_tier_up` | `masterySlug`, `tier` (effectSlug) | Только владельцу | Повышение тира мастерства |
| `champion_spawned_soon` | `slug` | Всем в игровой зоне | Чемпион вот-вот появится |
| `champion_spawned` | `mobSlug`, `uid` | Всем в игровой зоне | Чемпион заспавнился |
| `champion_killed` | `killerCharId` | Всем в игровой зоне | Чемпион убит игроком |
| `champion_despawned` | `{}` | Всем в игровой зоне | Чемпион исчез по таймауту |
| `zone_event_start` | `eventSlug`, `durationSec`, `gameZoneId` | Всем в игровой зоне | Началось зональное событие |
| `zone_event_end` | `eventSlug` | Всем в игровой зоне | Зональное событие завершилось |
