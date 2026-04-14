# Skill Learning System

## Overview

Players learn skills by talking to NPC trainers. Each skill may require:
- **Skill Points (SP)** — earned automatically on level-up (1 SP per level gained)
- **Gold** — coins removed from inventory on purchase
- **Skill Book** — a specific item consumed on purchase

All three costs are optional and configured per skill in `class_skill_tree`.

---

## Trainers

| NPC | ID | Class | World Position |
|-----|----|-------|----------------|
| Theron | 4 | Warrior | 1200, -2800, 200 |
| Sylara | 5 | Mage | -400, 1600, 200 |

---

## Available Skills

### Warrior Skills (learn from Theron)

| Skill | Slug | SP Cost | Gold | Requires Book |
|-------|------|---------|------|---------------|
| Shield Bash | `shield_bash` | 1 | 50 | No |
| Whirlwind | `whirlwind` | 2 | 100 | Yes — Tome of Whirlwind |
| Iron Skin | `iron_skin` | 1 | 75 | No |
| Constitution Mastery | `constitution_mastery` | 2 | 0 | Yes — Tome of Constitution Mastery |

### Mage Skills (learn from Sylara)

| Skill | Slug | SP Cost | Gold | Requires Book |
|-------|------|---------|------|---------------|
| Frost Bolt | `frost_bolt` | 1 | 50 | No |
| Arcane Blast | `arcane_blast` | 2 | 100 | Yes — Tome of Arcane Blast |
| Chain Lightning | `chain_lightning` | 2 | 150 | Yes — Tome of Chain Lightning |
| Mana Shield | `mana_shield` | 1 | 75 | No |
| Elemental Mastery | `elemental_mastery` | 2 | 0 | Yes — Tome of Elemental Mastery |

---

## Skill Books (Items)

| Item Name | Item ID | Drop Only | Unlocks |
|-----------|---------|-----------|---------|
| Tome of Whirlwind | 18 | No | `whirlwind` |
| Tome of Iron Skin | 19 | Yes | `iron_skin` |
| Tome of Constitution Mastery | 20 | No | `constitution_mastery` |
| Tome of Frost Bolt | 21 | No | `frost_bolt` |
| Tome of Arcane Blast | 22 | No | `arcane_blast` |
| Tome of Chain Lightning | 23 | No | `chain_lightning` |
| Tome of Mana Shield | 24 | Yes | `mana_shield` |
| Tome of Elemental Mastery | 25 | No | `elemental_mastery` |
| Tome of Shield Bash | 26 | No | `shield_bash` |

Drop-only books (`vendor_price_buy = 0`) cannot be purchased from vendors — they must be found as loot.

---

## Packet Flow

Существует два пути изучения скила — через диалог и через UI магазина тренера.
Оба пути сходятся с шага `saveLearnedSkill` и дальше идентичны.

### Путь 1: через диалог (DialogueActionExecutor)

```
Client                Chunk Server              Game Server           Database
  |                        |                        |                     |
  |--- dialogue_choice --->|                        |                     |
  |   (node with           |                        |                     |
  |    learn_skill action) |                        |                     |
  |                        |                        |                     |
  |                   [DialogueActionExecutor]       |                     |
  |                   - already learned?            |                     |
  |                   - enough SP?                  |                     |
  |                   - enough gold?                |                     |
  |                   - has skill book?             |                     |
  |                        |                        |                     |
  |                   [on failure]                  |                     |
  |<-- learn_skill_failed -|                        |                     |
  |   {reason, skillSlug}  |                        |                     |
  |                        |                        |                     |
  |                   [on success]                  |                     |
  |                   - removeItemFromInventory(book)|                    |
  |                   - removeItemFromInventory(gold)|                    |
  |                   - modifyFreeSkillPoints(-n)   |                     |
  |                        |                        |                     |
  |                        |-- saveLearnedSkill --->|  ← общий путь ↓    |
```

### Путь 2: через UI тренера (requestLearnSkill)

```
Client                Chunk Server              Game Server           Database
  |                        |                        |                     |
  |--- requestLearnSkill ->|                        |                     |
  |   {npcId, skillSlug}   |                        |                     |
  |                        |                        |                     |
  |                   [SkillEventHandler]           |                     |
  |                   - npc_not_found?              |                     |
  |                   - out_of_range?               |                     |
  |                   - skill_not_available?        |                     |
  |                   - already_learned?            |                     |
  |                   - insufficient_level?         |                     |
  |                   - missing_prerequisite?       |                     |
  |                   - insufficient_sp?            |                     |
  |                   - insufficient_gold?          |                     |
  |                   - missing_skill_book?         |                     |
  |                        |                        |                     |
  |                   [on failure]                  |                     |
  |<-- learn_skill_failed -|                        |                     |
  |   {reason, skillSlug}  |                        |                     |
  |                        |                        |                     |
  |                   [on success]                  |                     |
  |                   - removeItemFromInventory(book)|                    |
  |                   - removeItemFromInventory(gold)|                    |
  |                   - modifyFreeSkillPoints(-n)   |                     |
  |                        |                        |                     |
  |                        |-- saveLearnedSkill --->|  ← общий путь ↓    |
```

### Общий путь (оба варианта)

```
Client                Chunk Server              Game Server           Database
  |                        |                        |                     |
  |                        |-- saveLearnedSkill --->|                     |
  |                        |  {characterId,         |                     |
  |                        |   skillSlug}           |                     |
  |                        |                        |-- INSERT ---------->|
  |                        |                        |   character_skills  |
  |                        |                        |<-- skill row -------|
  |                        |                        |                     |
  |                        |<-- setLearnedSkill ----|                     |
  |                        |   {characterId,        |                     |
  |                        |    skillData:{...      |                     |
  |                        |     effects:[...]}}    |                     |
  |                        |                        |                     |
  |                   [addCharacterSkill()]         |                     |
  |                   if isPassive && effects:      |                     |
  |                   - addActiveEffect() per eff   |                     |
  |<-- skill_learned ------|                        |                     |
  |   {skillSlug,          |                        |                     |
  |    skillName,          |                        |                     |
  |    isPassive,          |                        |                     |
  |    newFreeSkillPoints, |                        |                     |
  |    skillData:{...}}    |                        |                     |
  |                        |                        |                     |
  | (только если isPassive |                        |                     |
  |  && effects непустой)  |                        |                     |
  |<-- stats_update -------|                        |                     |
  |   {attributes,         |                        |                     |
  |    activeEffects, ...} |                        |                     |
```

---

## Dialogue Conditions

Three new condition types support skill-related branching:

### `has_skill_points`
Shows a choice only if the player has enough free skill points.

```json
{
  "type": "has_skill_points",
  "amount": 2
}
```

### `skill_learned`
Shows a choice only if the player already knows a specific skill.

```json
{
  "type": "skill_learned",
  "skill_slug": "shield_bash"
}
```

### `skill_not_learned`
Shows a choice only if the player has NOT yet learned a skill.

```json
{
  "type": "skill_not_learned",
  "skill_slug": "shield_bash"
}
```

Conditions are stored as a JSON array in `dialogue_node_edges.conditions`. Multiple conditions are AND-joined.

---

## Dialogue Actions

### `learn_skill`

Placed inside an `action_group` on a dialogue node. Triggers the full learn-skill validation and purchase flow.

```json
{
  "type": "action_group",
  "actions": [
    {
      "type": "learn_skill",
      "skill_slug": "shield_bash"
    }
  ]
}
```

Only one `learn_skill` action should exist per action group.

---

## Client Packets

### `skill_learned` (chunk server → client)

Sent when the skill is successfully learned and saved to the database.

```json
{
  "type": "skill_learned",
  "skillSlug": "shield_bash",
  "skillName": "Shield Bash",
  "isPassive": false,
  "newFreeSkillPoints": 1,
  "skillData": {
    "skillSlug": "shield_bash",
    "skillName": "Shield Bash",
    "isPassive": false,
    "scaleStat": "strength",
    "school": "physical",
    "skillEffectType": "damage",
    "skillLevel": 1,
    "coeff": 0.8,
    "flatAdd": 0.0,
    "cooldownMs": 8000,
    "gcdMs": 1500,
    "castMs": 0,
    "costMp": 0,
    "maxRange": 2.0,
    "areaRadius": 0.0,
    "swingMs": 300,
    "animationName": "ShieldBash",
    "effects": []
  }
}
```

**Для пассивных скилов** (например `constitution_mastery`) поле `effects` содержит массив модификаторов:

```json
{
  "type": "skill_learned",
  "skillSlug": "constitution_mastery",
  "skillName": "Constitution Mastery",
  "isPassive": true,
  "newFreeSkillPoints": 0,
  "skillData": {
    "skillSlug": "constitution_mastery",
    "skillName": "Constitution Mastery",
    "isPassive": true,
    "effects": [
      {
        "effectSlug": "constitution_mastery_con_bonus",
        "effectTypeSlug": "buff",
        "attributeSlug": "constitution",
        "value": 5.0,
        "durationSeconds": 0,
        "tickMs": 0
      }
    ]
  }
}
```

**Важно:** для пассивных скилов с непустым `effects` сервер немедленно отправляет
дополнительный `stats_update` пакет — клиент получает обновлённые `effective`-атрибуты
и актуальный список `activeEffects` (для buff-bar) без необходимости делать что-либо
дополнительно. Порядок пакетов гарантирован: сначала `skill_learned`, затем `stats_update`.
```

### `learn_skill_failed` (chunk server → client)

Sent when a skill purchase attempt fails validation.

```json
{
  "type": "learn_skill_failed",
  "reason": "insufficient_sp",
  "skillSlug": "whirlwind"
}
```

**Reason codes:**

| Code | Meaning |
|------|---------|
| `already_learned` | Player already knows this skill |
| `insufficient_level` | Character level below `requiredLevel` |
| `missing_prerequisite` | Prerequisite skill not yet learned |
| `insufficient_sp` | Not enough free skill points |
| `insufficient_gold` | Not enough gold coins in inventory |
| `missing_skill_book` | Required skill book not in inventory |
| `npc_not_found` | Referenced NPC does not exist |
| `out_of_range` | Player is too far from the trainer |
| `skill_not_available` | NPC does not teach this skill |

---

## Skill Trainer Shop UI (direct packets)

In addition to the dialogue-based `learn_skill` action, clients can open a dedicated
**skill trainer shop window** — a UI showing all skills the trainer teaches, along
with per-skill affordability flags.  This is the preferred path for new UI work;
the dialogue path is kept for backward compatibility.

### Packet: `openSkillShop` (client → chunk)

Opens the trainer shop window without going through a dialogue.

```json
{
  "header": { "eventType": "openSkillShop", "clientId": 42 },
  "body": {
    "npcId": 4,
    "posX": 1200, "posY": -2800, "posZ": 200
  }
}
```

### Response: `skillShop` (chunk → client)

```json
{
  "header": { "eventType": "skillShop", "status": "success", "clientId": 42 },
  "body": {
    "npcId": 4,
    "npcSlug": "theron",
    "freeSkillPoints": 2,
    "goldBalance": 350,
    "skills": [
      {
        "skillId": 4,
        "skillSlug": "shield_bash",
        "skillName": "Shield Bash",
        "description": "Stuns the target briefly with your shield.",
        "isPassive": false,
        "requiredLevel": 1,
        "spCost": 1,
        "goldCost": 50,
        "requiresBook": false,
        "bookItemId": 0,
        "prerequisiteSkillSlug": "",
        "isLearned": false,
        "canLearn": true,
        "prereqMet": true,
        "levelMet": true,
        "spMet": true,
        "goldMet": true,
        "bookMet": true
      }
    ]
  }
}
```

**Per-skill flags:**

| Flag | Meaning |
|------|---------|
| `isLearned` | Player already knows this skill |
| `canLearn` | All other flags are true (shortcut for UI enable/disable) |
| `prereqMet` | Prerequisite skill is learned (or none required) |
| `levelMet` | Character level ≥ `requiredLevel` |
| `spMet` | `freeSkillPoints` ≥ `spCost` |
| `goldMet` | Gold balance ≥ `goldCost` |
| `bookMet` | Skill book is in inventory (or not required) |

### Packet: `requestLearnSkill` (client → chunk)

Sent when the player clicks "Learn" in the skill shop UI.

```json
{
  "header": { "eventType": "requestLearnSkill", "clientId": 42 },
  "body": {
    "npcId": 4,
    "skillSlug": "shield_bash",
    "posX": 1200, "posY": -2800, "posZ": 200
  }
}
```

On success the server deducts SP, gold, and the skill book, sends `saveLearnedSkill`
to the game server, and the usual `skill_learned` / `learn_skill_failed` response is
returned (see above).

Error codes from `requestLearnSkill` are identical to the `learn_skill_failed` reason
table above, plus `npc_not_found`, `out_of_range`, and `skill_not_available`.

---

## Server-Side Data Flow

### Chunk Server — `DialogueActionExecutor::executeLearnSkill()`

Located: `src/services/DialogueActionExecutor.cpp`

1. Look up `class_skill_tree` entry via `GameConfigService` for `skillSlug`
2. **Guard: already learned** — check `ctx.learnedSkillSlugs`; send `learn_skill_failed{already_learned}` and return
3. **Guard: SP** — check `ctx.freeSkillPoints >= skillPointCost`; send `learn_skill_failed{insufficient_sp}` and return
4. **Guard: gold** — count `gold_coin` items in inventory; send `learn_skill_failed{insufficient_gold}` and return
5. **Guard: skill book** — if `requiresBook`, find `skillBookItemId` in inventory; send `learn_skill_failed{missing_skill_book}` and return
6. Remove skill book from inventory (if required)
7. Remove gold coins (loop-deduct by stack sizes)
8. Call `characterManager_.modifyFreeSkillPoints(characterId, -spCost)`
9. Update `ctx.freeSkillPoints` and `ctx.learnedSkillSlugs`
10. Build `saveLearnedSkill` JSON packet, push to `result.pendingGameServerPackets`

The `pendingGameServerPackets` vector in `ActionResult` is forwarded by `DialogueEventHandler` to `GameServerWorker` after the dialogue action completes.

### Game Server — `EventHandler::handleSaveLearnedSkillEvent()`

Located: `src/events/EventHandler.cpp`

1. Parse `characterId` + `skillSlug` from event body
2. Execute `save_learned_skill` prepared statement (INSERT … ON CONFLICT DO NOTHING)
3. Execute `get_character_skills` to load full skill list
4. Find the row matching `skillSlug`
5. Build `SkillStruct` with properties from the result set
6. Send `setLearnedSkill` response back to chunk server

---

## Database Schema

### Relevant Tables

- **`skills`** — master skill list (id, slug, name, description, is_passive, skill_type)
- **`class_skill_tree`** — per-class skill config (prerequisite, sp_cost, gold_cost, requires_book, skill_book_item_id)
- **`character_skills`** — player's learned skills (character_id, skill_id, current_level)
- **`characters`** — includes `free_skill_points` column

### SP Grant

Stored procedure `set_character_exp_level` grants SP automatically:

```sql
SET free_skill_points = free_skill_points + GREATEST(0, new_level - current_level)
```

This means leveling from 1 → 3 grants 2 SP, but SP is never reduced by the level-up procedure.

---

## Adding New Trainable Skills

1. **Add the skill** in a migration — insert into `skills`, `skill_properties_mapping`
2. **Add to `class_skill_tree`** — set `skill_point_cost`, `gold_cost`, `requires_book`, `skill_book_item_id`
3. **Add a skill book item** (optional) — insert into `item_types` and `items` with `item_type = 'Skill Book'`
4. **Add trainer dialogue nodes** — create `dialogue_node` entries with `action_type = 'action_group'` containing a `learn_skill` action; add edge conditions like `skill_not_learned` / `has_skill_points`
5. No C++ changes required — the system is entirely data-driven

---

## Trainer Dialogue IDs

| Trainer | Dialogue ID | Start Node | Node Range |
|---------|-------------|------------|------------|
| Theron | 4 | 400 | 400–499 |
| Sylara | 5 | 500 | 500–599 |
