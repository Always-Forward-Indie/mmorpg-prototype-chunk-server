# 10. Бестиарий

## Обзор

Бестиарий — система прогрессивного раскрытия информации о мобах. По мере убийств открываются тиры с данными. 6 тиров, от базовой информации до полной drop-таблицы.

---

## 10.1. Тиры бестиария

| Тир | categorySlug | Требуется убийств | Раскрываемые данные |
|:---:|:-------------|-------------------:|---------------------|
| 1 | `basic_info` | 1 | Уровень, ранг, диапазон HP, тип, биом |
| 2 | `lore` | 5 | Ключ локализации лора |
| 3 | `combat_info` | 15 | Слабости, сопротивления, способности |
| 4 | `loot_table` | 30 | Список предметов (без шансов) |
| 5 | `drop_rates` | 75 | Полная таблица лута с процентами |
| 6 | `hunter_mastery` | 150 | Титул и достижение |

Пороги настраиваются: `bestiary.tier1_kills` ... `bestiary.tier6_kills`

---

## 10.2. getBestiaryOverview — Список всех записей

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "getBestiaryOverview",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": { "clientSendMsEcho": 1711709400000, "requestId": "..." }
  },
  "body": {
    "characterId": 7
  }
}
```

### Сервер → Unicast

```json
{
  "header": {
    "eventType": "getBestiaryOverview",
    "status": "success",
    "requestId": "..."
  },
  "body": {
    "characterId": 7,
    "entries": [
      { "mobSlug": "forest_wolf", "killCount": 47 },
      { "mobSlug": "cave_spider", "killCount": 12 },
      { "mobSlug": "bandit_scout", "killCount": 105 }
    ]
  },
  "timestamps": { "serverRecvMs": 1711709400010, "serverSendMs": 1711709400020 }
}
```

> Отправляется автоматически при `joinGameCharacter`.

---

## 10.3. getBestiaryEntry — Детальная запись

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "getBestiaryEntry",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": { "clientSendMsEcho": 1711709400100, "requestId": "..." }
  },
  "body": {
    "characterId": 7,
    "mobSlug": "forest_wolf"
  }
}
```

### Сервер → Unicast

```json
{
  "header": {
    "eventType": "getBestiaryEntry",
    "status": "success",
    "requestId": "..."
  },
  "body": {
    "characterId": 7,
    "mobSlug": "forest_wolf",
    "killCount": 47,
    "tiers": [
      {
        "tier": 1,
        "categorySlug": "basic_info",
        "requiredKills": 1,
        "unlocked": true,
        "data": {
          "level": 5,
          "rank": "normal",
          "hpMin": 80,
          "hpMax": 120,
          "type": "beast",
          "biomeSlug": "enchanted_forest"
        }
      },
      {
        "tier": 2,
        "categorySlug": "lore",
        "requiredKills": 10,
        "unlocked": true,
        "data": {
          "loreKey": "bestiary_lore_forest_wolf"
        }
      },
      {
        "tier": 3,
        "categorySlug": "combat_info",
        "requiredKills": 25,
        "unlocked": true,
        "data": {
          "weaknesses": ["fire"],
          "resistances": ["cold"],
          "abilities": ["bite", "howl"]
        }
      },
      {
        "tier": 4,
        "categorySlug": "loot_table",
        "requiredKills": 50,
        "unlocked": false,
        "requiredKillsLeft": 3,
        "data": null
      },
      {
        "tier": 5,
        "categorySlug": "drop_rates",
        "requiredKills": 100,
        "unlocked": false,
        "data": null
      },
      {
        "tier": 6,
        "categorySlug": "hunter_mastery",
        "requiredKills": 300,
        "unlocked": false,
        "data": null
      }
    ]
  },
  "timestamps": { ... }
}
```

### Данные тиров при `unlocked == true`

#### T1: basic_info

```json
{
  "level": 5,
  "rank": "normal",
  "hpMin": 80,
  "hpMax": 120,
  "type": "beast",
  "biomeSlug": "enchanted_forest"
}
```

#### T2: lore

```json
{
  "loreKey": "bestiary_lore_forest_wolf"
}
```

> `loreKey` — ключ для клиентской локализации.

#### T3: combat_info

```json
{
  "weaknesses": ["fire", "holy"],
  "resistances": ["cold", "nature"],
  "abilities": ["bite", "howl", "pack_tactics"]
}
```

#### T4: loot_table (без шансов)

```json
{
  "items": ["wolf_pelt", "wolf_fang", "raw_meat"]
}
```

#### T5: drop_rates (полная таблица)

```json
{
  "loot": [
    { "itemSlug": "wolf_pelt", "chance": 0.75 },
    { "itemSlug": "wolf_fang", "chance": 0.35 },
    { "itemSlug": "raw_meat", "chance": 0.90 },
    { "itemSlug": "rare_wolf_eye", "chance": 0.02 }
  ]
}
```

#### T6: hunter_mastery

```json
{
  "titleSlug": "wolf_slayer",
  "achievementSlug": "achievement_wolf_hunter_master"
}
```

### Поле `requiredKillsLeft`

Присутствует **только** у **первого заблокированного** тира. Показывает сколько убийств осталось до открытия.

---

## 10.4. Нотификации при убийстве

### bestiary_kill_update (каждое убийство)

**Сервер → Unicast** (world_notification, канал: silent)

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "notificationId": "bestiary_kill_7_15_47",
    "notificationType": "bestiary_kill_update",
    "priority": "low",
    "channel": "silent",
    "text": "",
    "data": {
      "mobSlug": "forest_wolf",
      "killCount": 47
    }
  }
}
```

> Канал `silent` — клиент обновляет счётчик без отображения тоста.

### bestiary_tier_unlocked (при пересечении порога)

**Сервер → Unicast** (world_notification, канал: toast)

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "notificationId": "bestiary_tier_7_15_3",
    "notificationType": "bestiary_tier_unlocked",
    "priority": "medium",
    "channel": "toast",
    "text": "",
    "data": {
      "mobSlug": "forest_wolf",
      "unlockedTier": 3,
      "killCount": 15,
      "categorySlug": "combat_info"
    }
  }
}
```

> Канал `toast` — клиент должен показать всплывающее уведомление: «Бестиарий: Forest Wolf — Combat Info разблокировано!»

---

## 10.5. Конфигурация

| Параметр | Default | Описание |
|----------|---------|----------|
| `bestiary.tier1_kills` | 1 | Порог T1 (basic_info) |
| `bestiary.tier2_kills` | 5 | Порог T2 (lore) |
| `bestiary.tier3_kills` | 15 | Порог T3 (combat_info) |
| `bestiary.tier4_kills` | 30 | Порог T4 (loot_table) |
| `bestiary.tier5_kills` | 75 | Порог T5 (drop_rates) |
| `bestiary.tier6_kills` | 150 | Порог T6 (hunter_mastery) |
