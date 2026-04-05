# 05. NPC, система диалогов и квесты

## Обзор

Система диалогов — графовая. Каждый NPC может иметь несколько графов, выбираемых по условиям.
Квесты интегрированы в диалоги через actions (`offer_quest`, `turn_in_quest`, `advance_quest_step`).

---

## 5.1. NPCDataStruct

```cpp
struct NPCDataStruct {
    int id;
    string name;
    string slug;
    string raceName;
    int level;
    int currentHealth, currentMana, maxHealth, maxMana;
    vector<NPCAttributeStruct> attributes;
    vector<SkillStruct> skills;
    PositionStruct position;
    string npcType;       // "vendor", "quest_giver", "blacksmith", "guard", "trainer", "general"
    bool isInteractable;
    string dialogueId;
    vector<string> questSlugs;
    int radius;           // Радиус взаимодействия (проверка дистанции)
    string factionSlug;   // Фракция NPC (репутация, скидки, гейт диалогов)
};
```

### Спавн NPC (при входе в зону)

**Сервер → Unicast**

```json
{
  "header": {
    "eventType": "spawnNPCs",
    "message": "success"
  },
  "body": {
    "npcs": [
      {
        "id": 42,
        "name": "Merchant John",
        "slug": "merchant_john",
        "npcType": "vendor",
        "level": 10,
        "position": { "x": 200.0, "y": 150.0, "z": 0.0, "rotationZ": 90.0 },
        "isInteractable": true,
        "factionSlug": "traders_guild",
        "radius": 200
      }
    ]
  }
}
```

---

## 5.2. npcInteract — Начать диалог

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "npcInteract",
    "clientId": 7,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1709834400000,
      "requestId": "sync_1709834400000_7_100_abc"
    }
  },
  "body": {
    "characterId": 101,
    "npcId": 42,
    "playerId": 1
  }
}
```

### Серверная обработка

1. Проверяет что персонаж жив + ID валидны
2. Находит NPC, проверяет `isInteractable`
3. Проверяет дистанцию: `distance(playerPos, npcPos) <= npc.radius`
4. Проверяет репутацию: `rep < -500` с фракцией NPC → блокировка
5. Вызывает `questManager.onNPCTalked(characterId, npcId)` → обновляет квесты со степом `talk`
6. Собирает `PlayerContextStruct` (флаги, квесты, уровень, репутация, мастерство, скилл-поинты)
7. Выбирает граф диалога по `NPCDialogueMappingStruct` (приоритет + условия)
8. Создаёт сессию: `"dlg_{clientId}_{timestamp}"` (TTL = 300 секунд)
9. Проходит авто-ноды до первой интерактивной ноды (`line`/`choice_hub`)
10. Отправляет `DIALOGUE_NODE`

### Сервер → Unicast (успех: DIALOGUE_NODE)

```json
{
  "header": {
    "eventType": "DIALOGUE_NODE",
    "status": "success"
  },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "npcId": 42,
    "nodeId": 5,
    "clientNodeKey": "npc_greeting",
    "type": "line",
    "speakerNpcId": 42,
    "choices": [
      {
        "edgeId": 1,
        "clientChoiceKey": "ask_about_shop",
        "conditionMet": true
      },
      {
        "edgeId": 2,
        "clientChoiceKey": "ask_about_quest",
        "conditionMet": false
      }
    ]
  }
}
```

### Поля DIALOGUE_NODE

| Поле | Тип | Описание |
|------|-----|----------|
| `sessionId` | string | ID сессии диалога |
| `npcId` | int | ID NPC |
| `nodeId` | int | ID текущей ноды |
| `clientNodeKey` | string | Ключ для клиентской локализации текста |
| `type` | string | `"line"` или `"choice_hub"` |
| `speakerNpcId` | int | ID говорящего NPC |
| `choices` | array | Доступные варианты ответа |
| `choices[].edgeId` | int | ID ребра (отправляется в `dialogueChoice`) |
| `choices[].clientChoiceKey` | string | Ключ локализации варианта |
| `choices[].conditionMet` | bool | `true` = доступно, `false` = заблокировано (показать серым) |

> **Важно**: Если `conditionMet == false`, клиент должен показать вариант серым/неактивным. Если ребро имеет `hideIfLocked: true`, оно **не включается** в массив `choices`.

### Сервер → Unicast (ошибка: dialogueError)

```json
{
  "header": {
    "eventType": "dialogueError",
    "clientId": 7
  },
  "body": {
    "errorCode": "OUT_OF_RANGE"
  }
}
```

### Коды ошибок npcInteract

| Код | Причина |
|-----|---------|
| `NPC_NOT_FOUND` | NPC не существует |
| `OUT_OF_RANGE` | Слишком далеко от NPC |
| `BLOCKED_BY_REPUTATION` | Репутация с фракцией NPC < -500 |
| `NO_DIALOGUE` | Нет подходящего диалога для текущего контекста |

---

## 5.3. dialogueChoice — Выбор варианта

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "dialogueChoice",
    "clientId": 7,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1709834401000,
      "requestId": "sync_1709834401000_7_101_abc"
    }
  },
  "body": {
    "characterId": 101,
    "sessionId": "dlg_7_1709834400000",
    "edgeId": 1,
    "playerId": 1
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `sessionId` | string | ID активной сессии |
| `edgeId` | int | ID выбранного ребра (из `choices[].edgeId`) |

### Серверная обработка

1. Находит сессию, проверяет TTL (300с) и владельца
2. Находит ребро по `edgeId`
3. Проверяет условие ребра (`conditionGroup`)
4. Если условие не выполнено → ошибка `CHOICE_LOCKED`
5. Выполняет `actionGroup` ребра → собирает нотификации
6. **Отправляет все action-нотификации клиенту** (в порядке выполнения)
7. Переходит к `toNodeId`
8. Обходит авто-ноды (`action`, `jump`) до следующей интерактивной ноды
9. Если достигнут `end` → отправляет `DIALOGUE_CLOSE`
10. Иначе → отправляет новый `DIALOGUE_NODE`

### Сервер → Unicast (поток ответов)

**Сначала**: Нотификации от actions (0+):
```json
{ "header": { "eventType": "quest_offered" }, "body": { "type": "quest_offered", "questId": 10, "clientQuestKey": "quest_wolf_hunt" } }
```
```json
{ "header": { "eventType": "item_received" }, "body": { "type": "item_received", "itemId": 7, "quantity": 3 } }
```

**Затем**: Следующая нода (или закрытие):
```json
{
  "header": { "eventType": "DIALOGUE_NODE" },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "nodeId": 6,
    "clientNodeKey": "quest_accepted_response",
    "type": "line",
    "speakerNpcId": 42,
    "choices": [
      { "edgeId": 5, "clientChoiceKey": "goodbye", "conditionMet": true }
    ]
  }
}
```

### Ошибки dialogueChoice

| Код | Причина |
|-----|---------|
| `SESSION_EXPIRED` | Сессия не найдена или TTL (300с) истёк |
| `CHOICE_LOCKED` | Условие ребра не выполнено |

---

## 5.4. dialogueClose — Закрыть диалог

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "dialogueClose",
    "clientId": 7,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1709834402000,
      "requestId": "sync_1709834402000_7_102_abc"
    }
  },
  "body": {
    "characterId": 101,
    "sessionId": "dlg_7_1709834400000",
    "playerId": 1
  }
}
```

### Сервер → Unicast

```json
{
  "header": {
    "eventType": "DIALOGUE_CLOSE",
    "status": "success"
  },
  "body": {
    "sessionId": "dlg_7_1709834400000"
  }
}
```

---

## Архитектура графа диалогов

### Типы нод

| Тип | Интерактивная | Описание |
|-----|:---:|----------|
| `line` | ✅ | Реплика NPC. Отправляется клиенту с выборами |
| `choice_hub` | ✅ | Узел множественного выбора |
| `action` | ❌ | Выполняет `actionGroup`, автоматически переходит по первому ребру |
| `jump` | ❌ | Безусловный переход к `jumpTargetNodeId` |
| `end` | ❌ | Конец диалога → закрытие сессии |

### Обход графа

```
npcInteract → [startNodeId] → traverse auto-nodes → STOP at line/choice_hub → DIALOGUE_NODE
dialogueChoice(edgeId) → execute edge.actions → [toNodeId] → traverse auto-nodes → STOP → DIALOGUE_NODE
...
→ [end node] → DIALOGUE_CLOSE
```

**Ограничения:**
- Максимум 50 автоматических переходов (защита от циклов)
- TTL сессии: 300 секунд неактивности

### Структуры графа

```cpp
struct DialogueNodeStruct {
    int id;
    int dialogueId;
    string type;              // "line"|"choice_hub"|"action"|"jump"|"end"
    int speakerNpcId;
    string clientNodeKey;     // Ключ локализации
    json conditionGroup;      // null = всегда true
    json actionGroup;         // null = нет действий
    int jumpTargetNodeId;     // Только для type=="jump"
};

struct DialogueEdgeStruct {
    int id;
    int fromNodeId;
    int toNodeId;
    int orderIndex;           // Порядок в списке choices
    string clientChoiceKey;   // Ключ локализации варианта
    json conditionGroup;      // Условие доступности
    json actionGroup;         // Действия при выборе
    bool hideIfLocked;        // true = скрыть если условие не выполнено
};

struct DialogueGraphStruct {
    int id;
    string slug;
    int version;
    int startNodeId;
    map<int, DialogueNodeStruct> nodes;           // nodeId → node
    map<int, vector<DialogueEdgeStruct>> edges;    // fromNodeId → edges
};
```

### Выбор диалога для NPC

```cpp
struct NPCDialogueMappingStruct {
    int npcId;
    int dialogueId;
    int priority;             // Чем выше — тем приоритетнее
    json conditionGroup;      // Условие выбора этого графа
};
```

Сервер перебирает все маппинги для `npcId`, отсортированные по `priority` (DESC), и выбирает первый, чьё `conditionGroup` выполнено.

---

## Система условий (conditionGroup)

### Формат

| JSON | Описание |
|------|----------|
| `null` / `{}` | Всегда `true` |
| `{"all": [...]}` | AND — все должны быть `true` |
| `{"any": [...]}` | OR — хотя бы одно `true` |
| `{"not": {...}}` | NOT — инвертирует результат |
| `{"type": "...", ...}` | Атомарное правило |

### Операторы сравнения

Все числовые условия поддерживают: `eq`, `gt`, `gte`, `lt`, `lte`, `ne`

### Типы условий

#### flag — Проверка флага

```json
{ "type": "flag", "key": "mila_greeted", "eq": true }
```
```json
{ "type": "flag", "key": "visit_count", "gte": 3 }
```

#### quest — Состояние квеста

```json
{ "type": "quest", "slug": "wolf_hunt", "state": "active" }
```

Допустимые значения `state`: `"not_started"`, `"offered"`, `"active"`, `"completed"`, `"turned_in"`, `"failed"`

> `"not_started"` означает отсутствие записи прогресса (квест никогда не предлагался).

#### quest_step — Текущий шаг квеста

```json
{ "type": "quest_step", "slug": "wolf_hunt", "step": 1 }
```
```json
{ "type": "quest_step", "slug": "wolf_hunt", "gte": 2 }
```

#### level — Уровень персонажа

```json
{ "type": "level", "gte": 10 }
```

#### item — Предмет в инвентаре

```json
{ "type": "item", "item_id": 7, "gte": 5 }
```

#### reputation — Репутация с фракцией

```json
{ "type": "reputation", "faction": "bandits", "gte": 200 }
```

Или по тиру:
```json
{ "type": "reputation", "faction": "bandits", "tier": "friendly" }
```

Тиры репутации:

| Тир | Значение | Ранг |
|-----|----------|------|
| `enemy` | < -500 | 0 |
| `stranger` | -500 ... -1 | 1 |
| `neutral` | 0 ... 199 | 2 |
| `friendly` | 200 ... 499 | 3 |
| `ally` | >= 500 | 4 |

#### mastery — Уровень мастерства

```json
{ "type": "mastery", "slug": "sword", "gte": 50.0 }
```

#### has_skill_points — Свободные очки навыков

```json
{ "type": "has_skill_points", "gte": 1 }
```

#### skill_learned — Навык изучен

```json
{ "type": "skill_learned", "slug": "shield_bash" }
```

#### skill_not_learned — Навык НЕ изучен

```json
{ "type": "skill_not_learned", "slug": "whirlwind" }
```

### Комбинированные условия

```json
{
  "all": [
    { "type": "level", "gte": 10 },
    { "type": "quest", "slug": "intro_quest", "state": "turned_in" },
    {
      "any": [
        { "type": "flag", "key": "path_warrior", "eq": true },
        { "type": "flag", "key": "path_mage", "eq": true }
      ]
    }
  ]
}
```

---

## Система действий (actionGroup)

### Форматы

```json
{"type": "set_flag", "key": "...", "bool_value": true}
```
```json
{"actions": [{"type": "give_item", ...}, {"type": "give_exp", ...}]}
```
```json
[{"type": "set_flag", ...}, {"type": "offer_quest", ...}]
```

Действия выполняются последовательно в порядке массива.

### Типы действий

#### set_flag

```json
{ "type": "set_flag", "key": "mila_greeted", "bool_value": true }
{ "type": "set_flag", "key": "visit_count", "int_value": 5 }
{ "type": "set_flag", "key": "visit_count", "inc": 1 }
```

**Нотификация клиенту**: Нет

#### offer_quest

```json
{ "type": "offer_quest", "slug": "wolf_hunt" }
```

**Нотификация**:
```json
{ "type": "quest_offered", "questId": 10, "clientQuestKey": "quest_wolf_hunt" }
```

#### turn_in_quest

```json
{ "type": "turn_in_quest", "slug": "wolf_hunt" }
```

**Нотификации** (множественные):
```json
{ "type": "quest_turned_in", "questId": 10, "clientQuestKey": "quest_wolf_hunt" }
```
Плюс для каждой награды:
```json
{ "type": "exp_received", "amount": 5000 }
{ "type": "item_received", "itemId": 7, "quantity": 3 }
{ "type": "gold_received", "amount": 500 }
```

#### fail_quest

```json
{ "type": "fail_quest", "slug": "wolf_hunt" }
```

**Нотификация**:
```json
{ "type": "quest_failed", "questId": 10, "clientQuestKey": "quest_wolf_hunt" }
```

#### advance_quest_step

```json
{ "type": "advance_quest_step", "slug": "wolf_hunt" }
```

**Нотификация**: Нет (отправляется `QUEST_UPDATE` отдельно — см. ниже)

#### give_item

```json
{ "type": "give_item", "item_id": 7, "quantity": 3 }
```

**Нотификация**:
```json
{ "type": "item_received", "itemId": 7, "quantity": 3 }
```

#### give_exp

```json
{ "type": "give_exp", "amount": 5000 }
```

**Нотификация**:
```json
{ "type": "exp_received", "amount": 5000 }
```

#### give_gold

```json
{ "type": "give_gold", "amount": 500 }
```

**Нотификация**:
```json
{ "type": "gold_received", "amount": 500 }
```

#### open_vendor_shop

```json
{ "type": "open_vendor_shop", "mode": "shop" }
```

**Нотификация**: Полный пакет `openVendorShop` (см. документ 06)

#### open_repair_shop

```json
{ "type": "open_repair_shop" }
```

**Нотификация**: Полный пакет `openRepairShop` (см. документ 06)

#### change_reputation

```json
{ "type": "change_reputation", "faction": "bandits", "delta": 50 }
```

**Нотификация**:
```json
{ "type": "reputationChanged", "faction": "bandits", "delta": 50 }
```

#### learn_skill

```json
{
  "type": "learn_skill",
  "skill_slug": "shield_bash",
  "sp_cost": 1,
  "gold_cost": 500,
  "requires_book": true,
  "book_item_id": 18
}
```

**Нотификация (успех)**:
```json
{ "type": "skill_learned", "skillSlug": "shield_bash" }
```

**Нотификация (неудача)**:
```json
{
  "type": "learn_skill_failed",
  "reason": "insufficient_sp",
  "skillSlug": "shield_bash"
}
```

Возможные причины: `insufficient_sp`, `insufficient_gold`, `missing_skill_book`, `already_learned`

---

## Система квестов

### QuestStruct

```cpp
struct QuestStruct {
    int id;
    string slug;
    int minLevel;
    bool repeatable;
    int cooldownSec;
    int giverNpcId;
    int turninNpcId;
    string clientQuestKey;      // Ключ локализации
    vector<QuestStepStruct> steps;
    vector<QuestRewardStruct> rewards;
};
```

### Состояния квеста (state machine)

```
[не существует] → offered → active → completed → turned_in
                                   ↘              ↗
                                    → failed ────→ (repeatable → offered)
```

| Состояние | Описание |
|-----------|----------|
| *(нет записи)* | Квест никогда не предлагался (`"not_started"`) |
| `offered` | Квест предложен, но ещё не принят (→ auto-переход в `active`) |
| `active` | Квест в процессе выполнения |
| `completed` | Все шаги выполнены, ждёт сдачи |
| `turned_in` | Квест сдан, награды получены (терминальное) |
| `failed` | Квест провален (терминальное; если `repeatable` — можно предложить снова) |

> `offer_quest` сразу ставит состояние `active` (не `offered`).

### QuestStepStruct

```cpp
struct QuestStepStruct {
    int id;
    int questId;
    int stepIndex;               // 0-based порядок шага
    string stepType;             // "kill"|"collect"|"talk"|"reach"|"custom"
    string completionMode;       // "auto"|"manual"
    json params;                 // Параметры шага (зависят от типа)
    string clientStepKey;        // Ключ локализации
};
```

### Типы шагов

#### kill — Убить мобов

```json
{ "stepType": "kill", "completionMode": "auto", "params": { "mob_id": 15, "count": 10 } }
```
Прогресс: `{ "killed": 5 }`
Триггер: `questManager.onMobKilled(characterId, mobId)`

#### collect — Собрать предметы

```json
{ "stepType": "collect", "completionMode": "auto", "params": { "item_id": 7, "count": 3 } }
```
Прогресс: `{ "have": 2 }`
Триггер: `questManager.onItemObtained(characterId, itemId, quantity)`

#### talk — Поговорить с NPC

```json
{ "stepType": "talk", "completionMode": "auto", "params": { "npc_id": 42 } }
```
Прогресс: `{ "done": true }`
Триггер: `questManager.onNPCTalked(characterId, npcId)` (вызывается при `npcInteract`)

#### reach — Достичь точки

```json
{ "stepType": "reach", "completionMode": "auto", "params": { "x": 100.0, "y": 200.0, "radius": 150.0 } }
```
Прогресс: `{ "done": true }`
Триггер: `questManager.onPositionReached(characterId, x, y)`

#### custom — Произвольный (ручной)

```json
{ "stepType": "custom", "completionMode": "manual", "params": { /* произвольные данные */ } }
```
Продвижение: Только через `advance_quest_step` действие в диалоге.

### Режимы завершения шагов

| Режим | Описание |
|-------|----------|
| `auto` | Автоматическое продвижение при выполнении условия |
| `manual` | Только через `advance_quest_step` в действии диалога |

### QuestRewardStruct

```json
{
  "rewards": [
    { "rewardType": "exp", "amount": 5000 },
    { "rewardType": "item", "itemId": 7, "quantity": 3 },
    { "rewardType": "gold", "amount": 500 }
  ]
}
```

---

## 5.5. QUEST_UPDATE — Обновление прогресса

**Сервер → Unicast** (при каждом изменении прогресса квеста)

```json
{
  "header": {
    "eventType": "QUEST_UPDATE",
    "status": "success"
  },
  "body": {
    "questId": 10,
    "questSlug": "wolf_hunt",
    "clientQuestKey": "quest_wolf_hunt",
    "state": "active",
    "currentStep": 1,
    "progress": { "killed": 5 },
    "totalSteps": 3,
    "clientStepKey": "step_kill_wolves",
    "stepType": "kill",
    "completionMode": "auto",
    "required": { "mob_id": 15, "count": 10 }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `questId` | int | ID квеста |
| `questSlug` | string | Slug квеста |
| `clientQuestKey` | string | Ключ для локализации |
| `state` | string | Текущее состояние |
| `currentStep` | int | Индекс текущего шага (0-based) |
| `progress` | object | Прогресс текущего шага |
| `totalSteps` | int | Общее количество шагов |
| `clientStepKey` | string | Ключ локализации шага |
| `stepType` | string | Тип текущего шага |
| `completionMode` | string | Режим завершения |
| `required` | object | Параметры шага (что нужно сделать) |

---

## Персистентность

- Квесты и флаги сохраняются через game-server
- Dirty flush каждые 5 секунд
- Полный flush при дисконнекте
- Сессии диалогов: только в памяти, TTL 300 секунд

### PlayerContextStruct (контекст для условий)

```cpp
struct PlayerContextStruct {
    int characterId;
    int characterLevel;
    map<string, bool> flagsBool;       // Булевы флаги
    map<string, int> flagsInt;         // Числовые флаги
    map<string, string> questStates;   // quest_slug → state
    map<string, int> questCurrentStep; // quest_slug → step index (0-based)
    map<int, json> questProgress;      // quest_id → progress json
    map<string, int> reputations;      // faction_slug → value
    map<string, float> masteries;      // mastery_slug → [0..100]
    int freeSkillPoints;
    set<string> learnedSkillSlugs;
};
```
