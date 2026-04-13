# stats_update — Player Stats Packet Protocol

## Overview

`stats_update` is a **server-push** packet sent by the Chunk Server to a client whenever
the character's visible statistics change. It provides a **full snapshot** — the client
always replaces its local state with the received values instead of applying a diff.

### When the server sends `stats_update`

| Trigger | Call site |
|---|---|
| Player joins chunk — inventory loaded | `EventHandler::handleSetPlayerInventoryEvent` |
| Player joins chunk — active effects loaded | `EventHandler::handleSetPlayerActiveEffectsEvent` |
| Skill use (mana spent, damage taken) | `CombatSystem::executeCombatAction` |
| DoT / HoT tick (HP change) | `CombatSystem::tickEffects` |
| XP gained / level-up | `ExperienceManager::grantExperience` |
| Item equipped | `EquipmentEventHandler::handleEquipItemEvent` |
| Item unequipped | `EquipmentEventHandler::handleUnequipItemEvent` |
| Attribute reload from game server | `CharacterEventHandler::handleSetCharacterAttributesEvent` |
| Item used (potion, scroll, food) | `ItemEventHandler::handleUseItemEvent` |
| OOC HP/MP regeneration tick (каждые ~4 с) | `RegenManager::tickRegen` |
| Item Soul tier перейдён (50 / 200 / 500 убийств) | `CombatSystem::handleMobDeath` |

> On player join the server sends **two** `stats_update` packets: first after inventory
> is loaded (equipment state and weight are correct), then after active effects arrive
> (stat buffs/debuffs are applied). The client simply replaces its state with the latest.
>
> Equipment and level-up both trigger a `getCharacterAttributes` round-trip to the
> Game Server. The `sendStatsUpdate` in `handleSetCharacterAttributesEvent` fires
> **after** the fresh base attributes are loaded, so the packet always reflects
> the latest DB values.
>
> `joinGameCharacter` no longer carries `attributes[]`. It is a broadcast to all
> clients in the chunk for nameplate/spawn purposes only. The owning client must
> use `stats_update` as the sole source of truth for its HUD.

---

## Packet format

### Full example — normal combat state

```json
{
  "header": {
    "eventType": "stats_update",
    "status": "success",
    "requestId": "stats_update_42",
    "timestamps": {
      "sendTimestamp": 0,
      "receiveTimestamp": 1741787400123,
      "processingTimestamp": 0
    }
  },
  "body": {
    "characterId": 42,
    "level": 7,
    "freeSkillPoints": 2,
    "experience": {
      "current": 3400,
      "levelStart": 2500,
      "nextLevel": 5000,
      "debt": 0
    },
    "health": {
      "current": 85,
      "max": 150
    },
    "mana": {
      "current": 40,
      "max": 80
    },
    "weight": {
      "current": 18.5,
      "max": 74.0
    },
    "attributes": [
      { "slug": "physical_attack",  "name": "Physical Attack",  "base": 40, "effective": 58 },
      { "slug": "magical_attack",   "name": "Magical Attack",   "base": 10, "effective": 10 },
      { "slug": "physical_defense", "name": "Physical Defense", "base": 22, "effective": 35 },
      { "slug": "magical_defense",  "name": "Magical Defense",  "base": 8,  "effective": 8  },
      { "slug": "crit_chance",      "name": "Critical Chance",  "base": 5,  "effective": 8  },
      { "slug": "crit_multiplier",  "name": "Crit Multiplier",  "base": 150,"effective": 150},
      { "slug": "accuracy",         "name": "Accuracy",         "base": 80, "effective": 87 },
      { "slug": "evasion",          "name": "Evasion",          "base": 12, "effective": 12 },
      { "slug": "block_chance",     "name": "Block Chance",     "base": 0,  "effective": 15 },
      { "slug": "block_value",      "name": "Block Value",      "base": 0,  "effective": 20 },
      { "slug": "strength",         "name": "Strength",         "base": 18, "effective": 23 }
    ],
    "activeEffects": [
      {
        "slug": "battle_fury",
        "effectTypeSlug": "buff",
        "attributeSlug": "physical_attack",
        "value": 18.0,
        "expiresAt": 1741787700
      },
      {
        "slug": "poison",
        "effectTypeSlug": "dot",
        "attributeSlug": "",
        "value": 5.0,
        "expiresAt": 1741787430
      }
    ]
  }
}
```

---

## Field reference

### `body`

| Field | Type | Description |
|---|---|---|
| `characterId` | int | Identifies whose stats these are |
| `level` | int | Current character level |
| `freeSkillPoints` | int | Unspent skill points available for learning skills |
| `experience.current` | int | Total accumulated experience points |
| `experience.levelStart` | int | XP threshold at the **start** of the current level |
| `experience.nextLevel` | int | XP threshold to reach the **next** level |
| `experience.debt` | int | XP debt accumulated on death; 50 % of each XP reward is deducted from debt first before adding to `current` |
| `health.current` | int | Current HP |
| `health.max` | int | Maximum HP |
| `mana.current` | int | Current mana |
| `mana.max` | int | Maximum mana |
| `weight.current` | float | Current carried weight |
| `weight.max` | float | Carry weight limit (base + `strength * per_strength_config`) |
| `attributes` | array | All character attributes — see below |
| `activeEffects` | array | All non-expired buffs/debuffs — see below |

### `attributes[]` entry

| Field | Type | Description |
|---|---|---|
| `slug` | string | Attribute identifier, matches `entity_attributes` table slugs |
| `name` | string | Display name |
| `base` | int | Base value from `character_attributes` in DB |
| `effective` | int | `base` + equipment bonuses (`apply_on = 'equip'`) + stat-modifier active effects |

**`effective` composition:**
```
effective = base
          + Σ item.attributes[apply_on='equip'].value  (for all equipped items)
          + Σ activeEffect.value                        (non-expired, non-dot, non-hot)
```

> Attributes that exist **only** on equipment or active effects (not present as a base
> character attribute) are also included with `"base": 0`.

### Known attribute slugs

| Slug | Used in |
|---|---|
| `physical_attack` | Physical skill/attack damage scaling |
| `magical_attack` | Magical skill damage scaling |
| `physical_defense` | Incoming physical damage reduction |
| `magical_defense` | Incoming magical damage reduction |
| `crit_chance` | Probability of a critical hit (integer, treated as %) |
| `crit_multiplier` | Critical hit damage multiplier (integer, treated as %) |
| `accuracy` | Hit chance vs evasion |
| `evasion` | Dodge chance vs accuracy |
| `block_chance` | Probability to block (integer, treated as %) |
| `block_value` | Flat damage absorbed on block |
| `strength` | Also drives carry weight limit |
| `constitution` | Scales HP regeneration per tick (`hpGain += constitution * 0.3`) |
| `wisdom` | Scales MP regeneration per tick (`mpGain += wisdom * 0.5`) |
| `move_speed` | Server-side anti-cheat speed cap: `worldUnits/s = move_speed * 40`; default value: `5` |

Additional slugs may appear dynamically from DB — the client should treat the array as
extensible.

### `activeEffects[]` entry

| Field | Type | Description |
|---|---|---|
| `slug` | string | Effect identifier (e.g. `"battle_fury"`, `"poison"`) |
| `effectTypeSlug` | string | `"buff"` · `"debuff"` · `"dot"` · `"hot"` |
| `attributeSlug` | string | Which stat is modified; empty for pure DoT/HoT |
| `value` | float | Stat modifier amount (buff/debuff), or per-tick amount (dot/hot) |
| `expiresAt` | int64 | Unix timestamp (seconds) when effect expires; `0` = permanent |

> Effects with `expiresAt != 0 && expiresAt <= now` are **excluded** — the client only
> receives currently active effects.
>
> **Passive skill effects** appear with `expiresAt: 0` and `"buff"` type. They are
> injected automatically on every character join (after active effects load from the
> game server) and survive reloads. The client should treat `expiresAt == 0` as
> "show icon but no countdown timer" — identical to how permanent quest-reward buffs work.

---

## Minimal example — fresh level-1 character, no gear, no effects

```json
{
  "header": {
    "eventType": "stats_update",
    "status": "success",
    "requestId": "stats_update_1",
    "timestamps": { "sendTimestamp": 0, "receiveTimestamp": 1741787400000, "processingTimestamp": 0 }
  },
  "body": {
    "characterId": 1,
    "level": 1,
    "experience": { "current": 0, "levelStart": 0, "nextLevel": 300 },
    "health":  { "current": 50, "max": 50 },
    "mana":    { "current": 20, "max": 20 },
    "weight":  { "current": 0.0, "max": 50.0 },
    "attributes": [
      { "slug": "physical_attack",  "name": "Physical Attack",  "base": 10, "effective": 10 },
      { "slug": "physical_defense", "name": "Physical Defense", "base": 5,  "effective": 5  },
      { "slug": "accuracy",         "name": "Accuracy",         "base": 70, "effective": 70 },
      { "slug": "evasion",          "name": "Evasion",          "base": 8,  "effective": 8  },
      { "slug": "crit_chance",      "name": "Critical Chance",  "base": 3,  "effective": 3  },
      { "slug": "crit_multiplier",  "name": "Crit Multiplier",  "base": 150,"effective": 150},
      { "slug": "strength",         "name": "Strength",         "base": 5,  "effective": 5  }
    ],
    "activeEffects": []
  }
}
```

---

## Example — after equipping a sword and chestplate

The sword adds `physical_attack +15`, the chestplate adds `physical_defense +12` and
`evasion +3`. Both have `apply_on = 'equip'` on their item attributes.

```json
{
  "header": { "eventType": "stats_update", "status": "success", "requestId": "stats_update_1" },
  "body": {
    "characterId": 1,
    "level": 5,
    "experience": { "current": 1200, "levelStart": 800, "nextLevel": 2000 },
    "health":  { "current": 100, "max": 100 },
    "mana":    { "current": 40,  "max": 40  },
    "weight":  { "current": 11.5, "max": 62.0 },
    "attributes": [
      { "slug": "physical_attack",  "name": "Physical Attack",  "base": 22, "effective": 37 },
      { "slug": "physical_defense", "name": "Physical Defense", "base": 10, "effective": 22 },
      { "slug": "accuracy",         "name": "Accuracy",         "base": 75, "effective": 75 },
      { "slug": "evasion",          "name": "Evasion",          "base": 10, "effective": 13 },
      { "slug": "crit_chance",      "name": "Critical Chance",  "base": 4,  "effective": 4  },
      { "slug": "crit_multiplier",  "name": "Crit Multiplier",  "base": 150,"effective": 150},
      { "slug": "strength",         "name": "Strength",         "base": 12, "effective": 12 }
    ],
    "activeEffects": []
  }
}
```

---

## Example — passive skill + timed buff + DoT

The character has:
- `warrior_toughness` passive skill: +12 `physical_defense`, permanent
- `battle_fury` buff (from a skill cast): +18 `physical_attack`, expires in 5 minutes
- `poison` DoT: 5 HP per tick, expires in 30 seconds

```json
{
  "header": { "eventType": "stats_update", "status": "success", "requestId": "stats_update_7" },
  "body": {
    "characterId": 7,
    "level": 10,
    "experience": { "current": 9800, "levelStart": 8000, "nextLevel": 12000 },
    "health":  { "current": 130, "max": 200 },
    "mana":    { "current": 60,  "max": 100 },
    "weight":  { "current": 22.0, "max": 86.0 },
    "attributes": [
      { "slug": "physical_attack",  "name": "Physical Attack",  "base": 40, "effective": 73 },
      { "slug": "magical_attack",   "name": "Magical Attack",   "base": 15, "effective": 15 },
      { "slug": "physical_defense", "name": "Physical Defense", "base": 30, "effective": 42 },
      { "slug": "magical_defense",  "name": "Magical Defense",  "base": 12, "effective": 12 },
      { "slug": "crit_chance",      "name": "Critical Chance",  "base": 7,  "effective": 7  },
      { "slug": "crit_multiplier",  "name": "Crit Multiplier",  "base": 150,"effective": 150},
      { "slug": "accuracy",         "name": "Accuracy",         "base": 85, "effective": 85 },
      { "slug": "evasion",          "name": "Evasion",          "base": 14, "effective": 14 },
      { "slug": "strength",         "name": "Strength",         "base": 20, "effective": 20 }
    ],
    "activeEffects": [
      {
        "slug": "warrior_toughness",
        "effectTypeSlug": "buff",
        "attributeSlug": "physical_defense",
        "value": 12.0,
        "expiresAt": 0
      },
      {
        "slug": "battle_fury",
        "effectTypeSlug": "buff",
        "attributeSlug": "physical_attack",
        "value": 18.0,
        "expiresAt": 1741787700
      },
      {
        "slug": "poison",
        "effectTypeSlug": "dot",
        "attributeSlug": "",
        "value": 5.0,
        "expiresAt": 1741787430
      }
    ]
  }
}
```

> Note: both `warrior_toughness` (passive, permanent) and `battle_fury` (timed buff)
> contribute to the `effective` value of their respective attributes.
> `physical_defense` effective = base (30) + chestplate bonus (12 from equip) + passive (12) = 54.
> `physical_attack` effective = base (22) + sword bonus (33 from equip) + buff (18) = 73.
> The `poison` DoT does **not** affect any `effective` value since `effectTypeSlug == "dot"`
> — it is listed in `activeEffects` for UI display only (icon + timer).

---

## Client-side rendering guidance

```
XP bar progress = (experience.current - experience.levelStart)
                / (experience.nextLevel  - experience.levelStart)

XP debt bar     = experience.debt (show separately, e.g. greyed-out XP portion)

Weight bar      = weight.current / weight.max
isOverweight    = weight.current > weight.max

For each attribute entry:
  display value = effective
  bonus tooltip = effective - base  (show in green if > 0, red if < 0)

For each activeEffect entry:
  if expiresAt == 0  → permanent icon (no timer) — typical for passive skill effects
  else               → countdown timer = expiresAt - Date.now()/1000
```

---

## Item Soul — `weapon_kill_count_update`

Отдельный lightweight пакет, отправляемый **только владельцу** каждый раз при убийстве моба с экипированным оружием. Клиент использует его для обновления тултипа оружия (счётчик убийств) без полного пересчёта всего HUD.

**Direction:** Server → Client (owner only, `worldNotification` envelope)  
**eventType:** `worldNotification` с `type: "weapon_kill_count_update"`

```json
{
  "header": {
    "eventType": "worldNotification"
  },
  "body": {
    "type": "weapon_kill_count_update",
    "scope": "low",
    "displayType": "silent",
    "data": {
      "inventoryItemId": 137,
      "killCount": 51
    }
  }
}
```

| Поле | Тип | Описание |
|---|---|---|
| `inventoryItemId` | int | ID экземпляра оружия в инвентаре (`PlayerInventoryItemStruct.id`) |
| `killCount` | int | Новое значение счётчика убийств |

### Item Soul тиры и `stats_update`

При каждом убийстве сервер посылает `weapon_kill_count_update`. Атрибутный бонус из тира пересчитывается в `stats_update.attributes[].effective`. Пакет `stats_update` отправляется **только** при пересечении порога тира:

| Тир | Порог (`killCount`) | Конфиг-ключ | Бонус |
|-----|---------------------|-------------|-------|
| 1 | 50 | `item_soul.tier1_kills` / `item_soul.tier1_bonus_flat` | +1 к primary атрибуту оружия |
| 2 | 200 | `item_soul.tier2_kills` / `item_soul.tier2_bonus_flat` | +2 к primary атрибуту |
| 3 | 500 | `item_soul.tier3_kills` / `item_soul.tier3_bonus_flat` | +3 к primary атрибуту |

> При пересечении тира клиент получает **оба** пакета: `weapon_kill_count_update` (новый killCount) и `stats_update` (обновлённые `effective` атрибуты). Ожидать их в любом порядке — оба приходят в одном цикле обработки события.

### Flush в БД

Запись kill count в БД (через пакет `saveItemKillCount` на game-server) происходит не на каждом убийстве, а дебаунс-стратегией:
- каждые `item_soul.db_flush_every_kills` убийств (default: 5), **или**  
- при пересечении тира.

При дисконнекте персонажа несохранённые убийства **не теряются** — `ClientEventHandler` и `evictStaleSession` принудительно сбрасывают `saveItemKillCount` во время logout.
