# Dialogue & Quest System — Developer / Designer Guide

> Chunk-server: `mmorpg-prototype-chunk-server-new`  
> Дата: 2026-03-01

---

## Содержание

1. [Архитектурный обзор](#1-архитектурный-обзор)
2. [Структуры данных](#2-структуры-данных)
3. [Диалоговая система](#3-диалоговая-система)
   - 3.1 [Как хранится граф диалога](#31-как-хранится-граф-диалога)
   - 3.2 [Типы узлов (nodes)](#32-типы-узлов-nodes)
   - 3.3 [Рёбра (edges) — выборы игрока](#33-рёбра-edges--выборы-игрока)
   - 3.4 [Условия (condition_group)](#34-условия-condition_group)
   - 3.5 [Действия (action_group)](#35-действия-action_group)
   - 3.6 [NPC→Dialogue маппинги и приоритеты](#36-npcdialogue-маппинги-и-приоритеты)
   - 3.7 [Сессия диалога](#37-сессия-диалога)
   - 3.8 [Полный flow: от клика по NPC до закрытия окна](#38-полный-flow-от-клика-по-npc-до-закрытия-окна)
4. [Квест-система](#4-квест-система)
   - 4.1 [Статические определения квестов](#41-статические-определения-квестов)
   - 4.2 [Жизненный цикл квеста у персонажа](#42-жизненный-цикл-квеста-у-персонажа)
   - 4.3 [Шаги квеста (steps) и типы](#43-шаги-квеста-steps-и-типы)
   - 4.4 [Триггер-хуки](#44-триггер-хуки)
   - 4.5 [Выдача наград](#45-выдача-наград)
   - 4.6 [Персистентность — сброс на game-server](#46-персистентность--сброс-на-game-server)
5. [Флаги игрока (player flags)](#5-флаги-игрока-player-flags)
6. [PlayerContextStruct — снимок состояния](#6-playercontextstruct--снимок-состояния)
7. [Взаимодействие модулей](#7-взаимодействие-модулей)
8. [Пакеты клиент ↔ сервер](#8-пакеты-клиент--сервер)
9. [Примеры](#9-примеры)
   - 9.1 [Пример 1: Простой приветственный диалог](#91-пример-1-простой-приветственный-диалог)
   - 9.2 [Пример 2: Диалог с выдачей квеста](#92-пример-2-диалог-с-выдачей-квеста)
   - 9.3 [Пример 3: Диалог с приёмом квеста (turn-in)](#93-пример-3-диалог-с-приёмом-квеста-turn-in)
   - 9.4 [Пример 4: Квест «Убей 5 волков»](#94-пример-4-квест-убей-5-волков)
10. [Важные ограничения и подводные камни](#10-важные-ограничения-и-подводные-камни)

---

## 1. Архитектурный обзор

```
┌─────────────────────────────────────────────────────────────────┐
│                        GAME-SERVER                               │
│  БД: dialogues, npc_dialogue, quests, character_flags,          │
│       character_quest_progress                                    │
│                                                                  │
│  При старте → SET_ALL_DIALOGUES, SET_NPC_DIALOGUE_MAPPINGS,      │
│               SET_ALL_QUESTS                                     │
│  При логине → SET_PLAYER_QUESTS, SET_PLAYER_FLAGS               │
│  Для приёма → updatePlayerQuestProgress, updatePlayerFlag        │
└────────────────────────┬────────────────────────────────────────┘
                         │ TCP / internal event bus
┌────────────────────────▼────────────────────────────────────────┐
│                      CHUNK-SERVER                                │
│                                                                  │
│  DialogueManager          — граф диалогов в памяти              │
│  DialogueSessionManager   — сессия (одна на игрока)             │
│  DialogueConditionEvaluator — оценка условий (stateless)        │
│  DialogueActionExecutor   — выполнение действий                 │
│  QuestManager             — статика квестов + прогресс игроков  │
│  DialogueEventHandler     — точка входа для всех событий        │
└────────────────────────┬────────────────────────────────────────┘
                         │ WebSocket / TCP
┌────────────────────────▼────────────────────────────────────────┐
│                         КЛИЕНТ (Unity)                           │
│  NPC_INTERACT → DIALOGUE_NODE → DIALOGUE_CHOICE → DIALOGUE_CLOSE│
│                               ← QUEST_UPDATE                    │
└─────────────────────────────────────────────────────────────────┘
```

**Ключевое правило**: весь статический контент (графы диалогов, определения квестов) живёт в RAM чанк-сервера и никогда не запрашивается у game-server в рантайме. Это делает ответ на взаимодействие с NPC практически мгновенным.

---

## 2. Структуры данных

### Граф диалога

```cpp
struct DialogueNodeStruct {
    int    id;
    int    dialogueId;
    string type;              // "line" | "choice_hub" | "action" | "jump" | "end"
    int    speakerNpcId;      // 0 = нарратор
    string clientNodeKey;     // локализационный ключ для Unity
    json   conditionGroup;    // null → всегда true
    json   actionGroup;       // null → нет действий
    int    jumpTargetNodeId;  // используется только для type="jump"
};

struct DialogueEdgeStruct {
    int    id;
    int    fromNodeId;
    int    toNodeId;
    int    orderIndex;        // порядок отображения вариантов ответа
    string clientChoiceKey;   // локализационный ключ кнопки
    json   conditionGroup;
    json   actionGroup;
    bool   hideIfLocked;      // скрыть вариант если условие не выполнено
};

struct DialogueGraphStruct {
    int    id;
    string slug;
    int    version;
    int    startNodeId;
    unordered_map<int, DialogueNodeStruct>               nodes; // nodeId → node
    unordered_map<int, vector<DialogueEdgeStruct>>       edges; // fromNodeId → edges
};
```

### Квест

```cpp
struct QuestStepStruct {
    int    id;
    int    questId;
    int    stepIndex;
    string stepType;       // "kill" | "collect" | "talk" | "reach" | "custom"
    json   params;         // { "mob_id": 3, "count": 5 } и т.п.
    string clientStepKey;  // локализационный ключ шага
};

struct QuestRewardStruct {
    string rewardType;  // "item" | "exp" | "gold"
    int    itemId;
    int    quantity;
    int64_t amount;
};

struct QuestStruct {
    int    id;
    string slug;
    int    minLevel;
    bool   repeatable;
    int    cooldownSec;
    int    giverNpcId;
    int    turninNpcId;
    string clientQuestKey;
    vector<QuestStepStruct>   steps;
    vector<QuestRewardStruct> rewards;
};
```

### Прогресс квеста игрока

```cpp
struct PlayerQuestProgressStruct {
    int    characterId;
    int    questId;
    string questSlug;
    string state;       // "active" | "completed" | "turned_in" | "failed"
    int    currentStep;
    json   progress;    // { "killed": 3 } — данные текущего шага
    bool   isDirty;     // true → нужно сбросить на game-server
};
```

---

## 3. Диалоговая система

### 3.1 Как хранится граф диалога

При старте чанк-сервера game-server присылает два события:

| Событие | Что делает |
|---|---|
| `SET_ALL_DIALOGUES` | Заполняет `DialogueManager::dialoguesById_` и `dialoguesBySlug_` |
| `SET_NPC_DIALOGUE_MAPPINGS` | Заполняет `DialogueManager::npcMappings_` (npcId → вектор маппингов, сортированный по `priority DESC`) |

После этого `DialogueManager::isLoaded()` возвращает `true` и взаимодействие с NPC становится возможным.

### 3.2 Типы узлов (nodes)

| Тип | Поведение на сервере | Что получает клиент |
|---|---|---|
| `line` | **Интерактивный.** Сервер останавливает обход и отправляет `DIALOGUE_NODE` | Текст реплики NPC |
| `choice_hub` | **Интерактивный.** Сервер строит список рёбер (с учётом условий) и отправляет `DIALOGUE_NODE` с `choices[]` | Список кнопок-ответов |
| `action` | **Авто.** Выполняет `actionGroup`, затем переходит по первому ребру (`edges[nodeId][0]`) | Не виден; уведомления приходят отдельно |
| `jump` | **Авто.** Мгновенно переходит к `jumpTargetNodeId` | Не виден |
| `end` | **Авто.** Завершает диалог, сессия закрывается | Сервер шлёт `DIALOGUE_CLOSE` |

> **Важно**: обход авто-узлов (`action`, `jump`) происходит в цикле `traverseToInteractiveNode` с лимитом 50 итераций — защита от бесконечных петель в графе.

### 3.3 Рёбра (edges) — выборы игрока

Ребро описывает один вариант ответа из `choice_hub`-узла и имеет:

- **`conditionGroup`** — условие показа / доступности кнопки  
- **`hideIfLocked`** — если `true` и условие не выполнено, кнопка скрыта; если `false` — показана как неактивная (серая)  
- **`actionGroup`** — выполняется в момент выбора до перехода к следующему узлу  
- **`orderIndex`** — порядок кнопок (сортируется по возрастанию перед отправкой клиенту)

### 3.4 Условия (condition_group)

`DialogueConditionEvaluator::evaluate()` — полностью **stateless**-функция, принимающая JSON и `PlayerContextStruct`.

#### Атомарные правила

```json
{ "type": "flag",  "key": "mila_greeted",  "eq": true }
{ "type": "flag",  "key": "visit_count",   "gte": 3 }
{ "type": "quest", "slug": "wolf_hunt",    "state": "active" }
{ "type": "quest", "slug": "wolf_hunt",    "state": "not_started" }
{ "type": "level", "gte": 5 }
{ "type": "level", "lte": 20 }
{ "type": "item",  "item_id": 7,           "gte": 5 }
```

#### Логические группы

```json
{ "all": [ ...условия... ] }   // AND — все должны быть true
{ "any": [ ...условия... ] }   // OR  — хотя бы одно true
{ "not": { ...условие... } }   // NOT — инверсия
```

#### Пример составного условия

```json
{
  "all": [
    { "type": "level", "gte": 10 },
    { "any": [
      { "type": "quest", "slug": "intro_quest", "state": "completed" },
      { "type": "flag",  "key": "skipped_intro", "eq": true }
    ]}
  ]
}
```

> Правило `"not_started"` для квеста выполняется, если слаг вообще отсутствует в `ctx.questStates`.

### 3.5 Действия (action_group)

`DialogueActionExecutor::execute()` принимает JSON и мутирует `PlayerContextStruct` на месте, не делая ни одного синхронного запроса к БД.

Поддерживаемые типы:

| Тип | Параметры | Что делает |
|---|---|---|
| `set_flag` | `key`, `bool_value` / `int_value` / `inc` | Устанавливает или инкрементирует флаг; ставит в очередь на сброс к game-server |
| `offer_quest` | `slug` | Вызывает `QuestManager::offerQuest()`, возвращает уведомление `quest_offered` |
| `turn_in_quest` | `slug` | Вызывает `QuestManager::turnInQuest()`, возвращает уведомления `quest_turned_in`, `exp_received` и т.п. |
| `advance_quest_step` | `slug` | Принудительно двигает квест на следующий шаг |
| `give_item` | `item_id`, `quantity` | Кладёт предмет в инвентарь, возвращает уведомление `item_received` |
| `give_exp` | `amount` | Начисляет опыт через `ExperienceManager`, возвращает уведомление `exp_received` |

Формат `action_group` может быть одним из трёх вариантов:

```json
// 1. Одиночный объект
{ "type": "set_flag", "key": "mila_greeted", "bool_value": true }

// 2. Массив
[
  { "type": "set_flag", "key": "mila_greeted", "bool_value": true },
  { "type": "offer_quest", "slug": "wolf_hunt" }
]

// 3. Объект-обёртка с вложенным массивом
{ "actions": [
  { "type": "give_exp", "amount": 100 },
  { "type": "give_item", "item_id": 42, "quantity": 1 }
]}
```

### 3.6 NPC→Dialogue маппинги и приоритеты

Один NPC может иметь несколько диалогов с разными условиями появления. `DialogueManager::selectDialogueForNPC()` перебирает маппинги в порядке убывания `priority` и возвращает первый, чьё `conditionGroup` выполнено для текущего игрока.

```
npc_id=7, priority=10, condition={"type":"quest","slug":"main_quest","state":"completed"}
npc_id=7, priority=5,  condition={"type":"quest","slug":"main_quest","state":"active"}
npc_id=7, priority=1,  condition=null  ← фолбэк, всегда выполняется
```

Если не найдено ни одного подходящего маппинга → клиент получает `dialogueError { "errorCode": "NO_DIALOGUE" }`.

### 3.7 Сессия диалога

`DialogueSessionStruct` — лёгкий кешированный объект в `DialogueSessionManager`:

```
sessionId    = "dlg_{clientId}_{steadyClockMs}"   → например "dlg_3_1709298000000"
characterId  = 42    ← ID персонажа в БД
clientId     = 3     ← ID TCP-соединения (≠ characterId)
npcId        = 7
dialogueId   = 3
currentNodeId = 15   ← обновляется с каждым шагом
lastActivity = now() ← обновляется при каждом DIALOGUE_CHOICE
TTL          = 300 с
```

- Один персонаж = одна сессия. При новом `NPC_INTERACT` старая сессия закрывается автоматически.
- `DialogueSessionManager::cleanupExpiredSessions()` вызывается Scheduler'ом раз в 60 секунд.

### 3.8 Полный flow: от клика по NPC до закрытия окна

```
[КЛИЕНТ]                          [CHUNK-SERVER]
   │                                    │
   │── NPC_INTERACT ──────────────────►│
   │   { npcId, characterId, clientId } │
   │                                    │ 1. Проверка: NPC существует?
   │                                    │ 2. Проверка: isInteractable?
   │                                    │ 3. Проверка: дистанция ≤ npc.radius?
   │                                    │ 4. buildPlayerContext(charData)
   │                                    │ 5. selectDialogueForNPC(npcId, ctx)
   │                                    │ 6. QuestManager::onNPCTalked(char, npc)
   │                                    │ 7. DialogueSessionManager::createSession(...)
   │                                    │ 8. traverseToInteractiveNode(startNodeId)
   │                                    │    └─ action/jump узлы выполняются сразу
   │◄─ DIALOGUE_NODE ──────────────────│
   │   { sessionId, nodeId,             │
   │     clientNodeKey, type,           │
   │     choices[] }                    │
   │                                    │
   │   (если type="choice_hub")         │
   │── DIALOGUE_CHOICE ───────────────►│
   │   { sessionId, edgeId }            │
   │                                    │ 1. Получить сессию по sessionId
   │                                    │ 2. Найти edge по edgeId
   │                                    │ 3. Проверить edge.conditionGroup
   │                                    │ 4. Выполнить edge.actionGroup
   │◄─ (уведомления: quest_offered…) ──│
   │                                    │ 5. traverseToInteractiveNode(toNodeId)
   │◄─ DIALOGUE_NODE или DIALOGUE_CLOSE │
   │                                    │
   │── DIALOGUE_CLOSE ────────────────►│  (игрок закрыл окно вручную)
   │◄─ DIALOGUE_CLOSE ─────────────────│
```

---

## 4. Квест-система

### 4.1 Статические определения квестов

Загружаются один раз при старте через событие `SET_ALL_QUESTS`. `QuestManager` хранит их в двух индексах:

- `questsBySlug_` — `unordered_map<string, QuestStruct>` (владелец данных)
- `questsById_`  — `unordered_map<int, QuestStruct*>` (указатели в предыдущую карту)

### 4.2 Жизненный цикл квеста у персонажа

```
                   ┌──────────────────────────┐
                   │        not_started        │ ← квеста нет в playerProgress_
                   └─────────────┬────────────┘
                                 │ offerQuest()
                   ┌─────────────▼────────────┐
                   │           active          │ ← прогресс накапливается
                   └─────────────┬────────────┘
                                 │ все шаги выполнены (checkStepCompletion)
                   ┌─────────────▼────────────┐
                   │         completed         │ ← можно сдать NPC
                   └─────────────┬────────────┘
                                 │ turnInQuest()
                   ┌─────────────▼────────────┐
                   │          turned_in        │ ← награды выданы
                   └──────────────────────────┘
```

Состояние `failed` предусмотрено структурой, но логика его выставления на момент написания этого документа не реализована.

### 4.3 Шаги квеста (steps) и типы

| stepType | params | Что инкрементируется | Условие завершения |
|---|---|---|---|
| `kill` | `{ "mob_id": N, "count": K }` | `progress["killed"]` | `killed >= count` |
| `collect` | `{ "item_id": N, "count": K }` | `progress["have"]` | `have >= count` |
| `talk` | `{ "npc_id": N }` | `progress["done"]` | `done == true` |
| `reach` | `{ "x": F, "y": F, "radius": R }` | `progress["done"]` | расстояние до точки ≤ radius |
| `custom` | произвольный | вручную через `advanceQuestStepBySlug` | — |

При переходе на следующий шаг `progress` сбрасывается в пустой `{}` и инициализируется заново для нового типа.

### 4.4 Триггер-хуки

Другие системы вызывают хуки `QuestManager` напрямую, без каких-либо событий:

| Хук | Вызывается из | Условие продвижения |
|---|---|---|
| `onMobKilled(charId, mobId)` | `CombatSystem` | Текущий шаг — `kill` с нужным `mob_id` |
| `onItemObtained(charId, itemId, qty)` | `InventoryManager` | Текущий шаг — `collect` с нужным `item_id` |
| `onNPCTalked(charId, npcId)` | `DialogueEventHandler::handleNPCInteractEvent` | Текущий шаг — `talk` с нужным `npc_id` |
| `onPositionReached(charId, x, y)` | `PositionHandler` (предположительно) | Текущий шаг — `reach` в заданном радиусе |

> **Хук вызывается для КАЖДОГО активного квеста персонажа.** Если у игрока 10 активных квестов — итерируются все 10. При большом числе квестов это может стать горячей точкой, но в прототипе это приемлемо.

### 4.5 Выдача наград

`QuestManager::turnInQuest()` выполняет три действия:

1. Переводит квест в `turned_in`
2. Вызывает `ExperienceManager::grantExperience()` для наград типа `exp`
3. Вызывает `InventoryManager::addItemToInventory()` для наград типа `item`

Награды типа `gold` — нотификация строится, но фактическая выдача не реализована (заглушка).

### 4.6 Персистентность — сброс на game-server

Данные никогда не пишутся в БД напрямую из чанк-сервера. Цепочка:

```
QuestManager (in-memory, isDirty=true)
  │
  │  flushDirtyProgress() — вызывается Scheduler каждые 5 секунд
  │  flushAllProgress(charId) — вызывается при дисконнекте
  │
  ▼
GameServerWorker::sendDataToGameServer(json)
  │
  │  TCP
  ▼
Game-server → пишет в PostgreSQL
```

Пакет сброса:
```json
{
  "header": { "eventType": "updatePlayerQuestProgress" },
  "body": {
    "characterId": 42,
    "questId": 5,
    "questSlug": "wolf_hunt",
    "state": "active",
    "currentStep": 1,
    "progress": { "killed": 3 }
  }
}
```

Пакет сброса флага:
```json
{
  "header": { "eventType": "updatePlayerFlag" },
  "body": {
    "characterId": 42,
    "flagKey": "mila_greeted",
    "boolValue": true
  }
}
```

> Флаги сбрасываются в той же `flushDirtyProgress()` через `pendingFlagUpdates_` вектор. Это значит, что флаг, выставленный во время диалога, попадёт в БД не позже чем через 5 секунд (или немедленно при выходе).

---

## 5. Флаги игрока (player flags)

Флаги — это произвольные key-value пары на персонаже:

| Тип | Пример ключа | Пример значения | Используется для |
|---|---|---|---|
| bool | `mila_greeted` | `true / false` | Одноразовые события |
| int | `visit_count` | `7` | Счётчики посещений, порядковые состояния |

Флаги загружаются через `SET_PLAYER_FLAGS` при логине и кешируются в `CharacterDataStruct::flags`. При построении `PlayerContextStruct` они копируются в `ctx.flagsBool` / `ctx.flagsInt`.

**Флаги читаются** `DialogueConditionEvaluator`.  
**Флаги пишутся** `DialogueActionExecutor::executeSetFlag()` → `QuestManager::queueFlagUpdate()` → `flushDirtyProgress()`.

---

## 6. PlayerContextStruct — снимок состояния

```cpp
struct PlayerContextStruct {
    int    characterId;
    int    characterLevel;
    unordered_map<string, bool>   flagsBool;    // flag_key → bool
    unordered_map<string, int>    flagsInt;     // flag_key → int
    unordered_map<string, string> questStates; // quest_slug → "active"|"completed"|…
    unordered_map<int, json>      questProgress; // quest_id → progress json
};
```

`PlayerContextStruct` создаётся в `DialogueEventHandler::buildPlayerContext()` на каждый запрос взаимодействия и при каждом выборе. Это **снимок состояния в момент запроса** — мутируется в процессе обхода графа (через `executeSetFlag`, `executeOfferQuest`), но не является эталоном — эталон всегда в `CharacterDataStruct::flags` и `QuestManager::playerProgress_`.

---

## 7. Взаимодействие модулей

```
DialogueEventHandler
    │
    ├── DialogueManager          (read-only: граф, маппинги)
    ├── DialogueSessionManager   (create/get/close сессии)
    ├── DialogueConditionEvaluator (stateless evaluate)
    ├── DialogueActionExecutor
    │       ├── QuestManager     (offerQuest, turnInQuest, advanceStep)
    │       ├── InventoryManager (addItemToInventory)
    │       └── ExperienceManager (grantExperience)
    ├── QuestManager::onNPCTalked (триггер)
    ├── CharacterManager         (getCharacterData для контекста)
    └── ClientManager            (getClientSocket для ответа)

QuestManager
    ├── CharacterManager         (getCharacterData в offerQuest)
    ├── ExperienceManager        (grantExperience для наград)
    ├── InventoryManager         (addItemToInventory для наград)
    ├── ClientManager            (getClientSocket для QUEST_UPDATE)
    └── GameServerWorker         (flushDirtyProgress → TCP к game-server)
```

---

## 8. Пакеты клиент ↔ сервер

### Клиент → Сервер

> **Как работает роутинг:** сервер читает `header.eventType` для определения типа события.  
> `header.clientId` — единственный идентификатор, который клиент обязан передавать.  
> `characterId` сервер **никогда не читает из тела пакета** — он достаёт его сам из `ClientManager` по `clientId`.  
> Поля `characterId` и `playerId` в body для диалоговых пакетов игнорируются сервером (рудимент).

#### `npcInteract`
```json
{
  "header": { "eventType": "npcInteract", "clientId": 3 },
  "body": {
    "npcId": 7
  }
}
```

#### `dialogueChoice`
```json
{
  "header": { "eventType": "dialogueChoice", "clientId": 3 },
  "body": {
    "sessionId": "dlg_3_1709298000000",
    "edgeId": 15
  }
}
```

#### `dialogueClose`
```json
{
  "header": { "eventType": "dialogueClose", "clientId": 3 },
  "body": {
    "sessionId": "dlg_3_1709298000000"
  }
}
```

---

### Сервер → Клиент

> Все пакеты от сервера формируются через `NetworkManager::generateResponseMessage("success", packet)` + `sendResponse`.
> Итоговая структура единообразна со всем остальным протоколом: `header` содержит `eventType`, `status`, `timestamp`, `version`; данные — в `body`.

#### `DIALOGUE_NODE`

```json
{
  "header": {
    "eventType": "DIALOGUE_NODE",
    "status":    "success",
    "timestamp": "2026-03-01T12:00:00",
    "version":   "1.0"
  },
  "body": {
    "sessionId":    "dlg_3_1709298000000",
    "npcId":        7,
    "nodeId":       15,
    "clientNodeKey": "mila.greeting",
    "type":         "line",
    "speakerNpcId": 7,
    "choices":      []
  }
}
```

Для `type="choice_hub"` поле `choices` заполнено:
```json
"choices": [
  { "edgeId": 20, "clientChoiceKey": "choice.ask_quest",    "conditionMet": true  },
  { "edgeId": 21, "clientChoiceKey": "choice.ask_merchant",  "conditionMet": false }
]
```
> Рёбра с `hideIfLocked=true` и `conditionMet=false` в список не попадают вообще.  
> Рёбра с `hideIfLocked=false` и `conditionMet=false` попадают с флагом `conditionMet: false` — Unity отображает их серыми / неактивными.  
> Рёбра сортируются по `orderIndex` по возрастанию перед отправкой.

#### `DIALOGUE_CLOSE`

```json
{
  "header": { "eventType": "DIALOGUE_CLOSE", "status": "success", "timestamp": "...", "version": "1.0" },
  "body":   { "sessionId": "dlg_3_1709298000000" }
}
```

#### `QUEST_UPDATE`

Отправляется из `QuestManager::sendQuestUpdate()` при любом изменении прогресса.

```json
{
  "header": { "eventType": "QUEST_UPDATE", "status": "success", "timestamp": "...", "version": "1.0" },
  "body": {
    "questId":      5,
    "clientQuestKey": "quest.wolf_hunt",
    "state":        "active",
    "currentStep":  0,
    "progress":     { "killed": 2 },
    "clientStepKey": "step.kill_wolves",
    "stepType":     "kill",
    "required":     { "mob_id": 3, "count": 5 }
  }
}
```

> Поля `clientStepKey`, `stepType`, `required` присутствуют только если `currentStep < steps.size()`.  
> При `state="completed"` или `state="turned_in"` все шаги пройдены — этих полей не будет:

```json
{
  "header": { "eventType": "QUEST_UPDATE", "status": "success", "timestamp": "...", "version": "1.0" },
  "body":   { "questId": 5, "clientQuestKey": "quest.wolf_hunt", "state": "turned_in", "currentStep": 1, "progress": {} }
}
```

#### Уведомления от действий (action notifications)

Отправляются **поштучно**, каждое как отдельный стандартный пакет через `generateResponseMessage("success", ...)`.  
`header.eventType` = значение поля `type` из уведомления. Тело содержит все поля уведомления.

```json
{ "header": { "eventType": "quest_offered",   "status": "success", "timestamp": "...", "version": "1.0" },
  "body":   { "type": "quest_offered", "questId": 5, "clientQuestKey": "quest.wolf_hunt" } }

{ "header": { "eventType": "quest_turned_in", "status": "success", ... },
  "body":   { "type": "quest_turned_in", "questId": 5, "clientQuestKey": "quest.wolf_hunt" } }

{ "header": { "eventType": "exp_received",    "status": "success", ... },
  "body":   { "type": "exp_received", "amount": 500 } }

{ "header": { "eventType": "item_received",   "status": "success", ... },
  "body":   { "type": "item_received", "itemId": 42, "quantity": 1 } }

{ "header": { "eventType": "gold_received",   "status": "success", ... },
  "body":   { "type": "gold_received", "amount": 100 } }
```

> Уведомления приходят **до** следующего `DIALOGUE_NODE`.

#### Ошибки диалога

Отправляются через `NetworkManager::generateResponseMessage("error", ...)`.  
Структура стандартная, `status = "error"`:

```json
{
  "header": {
    "eventType": "dialogueError",
    "status":    "error",
    "message":   "NPC not found",
    "clientId":  3,
    "hash":      "abc123",
    "timestamp": "2026-03-01T12:00:00",
    "version":   "1.0"
  },
  "body": {
    "errorCode": "NPC_NOT_FOUND"
  }
}
```

Возможные `errorCode`:

| errorCode | Причина | Когда возникает |
|---|---|---|
| `NPC_NOT_FOUND` | NPC с таким ID не существует | `NPC_INTERACT` |
| `OUT_OF_RANGE` | Персонаж слишком далеко от NPC (дистанция > `npc.radius`) | `NPC_INTERACT` |
| `NO_DIALOGUE` | Нет подходящего диалога для NPC при текущем состоянии игрока | `NPC_INTERACT` |
| `SESSION_EXPIRED` | Сессия диалога не найдена или истекла | `DIALOGUE_CHOICE` |
| `CHOICE_LOCKED` | Условие выбранного варианта ответа не выполнено | `DIALOGUE_CHOICE` |

---

## 9. Примеры

### 9.1 Пример 1: Простой приветственный диалог

NPC Мила здоровается по-разному в зависимости от того, говорили с ней раньше или нет.

**Граф:**
```
[start=node_1]

node_1: type="action"
  conditionGroup: null
  actionGroup: { "type": "set_flag", "key": "mila_greeted", "bool_value": true }
  → edge → node_2 (hideIfLocked=false)

node_2: type="line"     clientNodeKey="mila.first_greeting"
  conditionGroup: { "type": "flag", "key": "mila_greeted", "eq": false }

node_3: type="line"     clientNodeKey="mila.return_greeting"
  conditionGroup: { "type": "flag", "key": "mila_greeted", "eq": true }

node_end: type="end"
```

> ⚠️ Проблема этого примера: `action` узел выставляет флаг ДО того, как condition_group у `line`-узлов оценит его. Порядок важен — действия меняют `ctx` в памяти немедленно. Поэтому флаг `mila_greeted` нужно выставлять **после** выбора реплики, например на ребре.

**Правильный вариант:**
```
node_1: type="choice_hub"
  → edge_A (conditionGroup: {"type":"flag","key":"mila_greeted","eq":false})
         actionGroup: { "type": "set_flag", "key": "mila_greeted", "bool_value": true }
         → node_first_greeting
  → edge_B (conditionGroup: {"type":"flag","key":"mila_greeted","eq":true})
         hideIfLocked=true
         → node_return_greeting

node_first_greeting: type="line"  clientNodeKey="mila.first_greeting"
  → edge → node_end

node_return_greeting: type="line"  clientNodeKey="mila.return_greeting"
  → edge → node_end

node_end: type="end"
```

**Что происходит пошагово:**
1. Игрок нажимает на Мила → `NPC_INTERACT`
2. Сервер строит `PlayerContextStruct`, `flagsBool["mila_greeted"]` = false (первый раз)
3. `selectDialogueForNPC` возвращает диалог ID=1 (единственный маппинг, condition=null)
4. `traverseToInteractiveNode` → node_1 (`choice_hub`) — интерактивный, останавливаемся
5. `buildChoicesJson` отдаёт: edge_A conditionMet=true, edge_B conditionMet=false (скрыт)
6. Клиент получает `DIALOGUE_NODE { type: "choice_hub", choices: [{edgeId:A}] }`
7. Клиент шлёт `DIALOGUE_CHOICE { edgeId: A }`
8. Сервер выполняет edge_A.actionGroup → `set_flag mila_greeted=true`
9. Переходит к node_first_greeting → интерактивный `line`
10. Клиент получает `DIALOGUE_NODE { type: "line", clientNodeKey: "mila.first_greeting" }`
11. Клиент закрывает диалог → `DIALOGUE_CLOSE`

---

### 9.2 Пример 2: Диалог с выдачей квеста

NPC Гравий выдаёт квест «Убей 5 волков» если игрок ≥5 уровня и квест ещё не начат.

**Определение квеста:**
```json
{
  "id": 5,
  "slug": "wolf_hunt",
  "minLevel": 5,
  "repeatable": false,
  "giverNpcId": 7,
  "turninNpcId": 7,
  "clientQuestKey": "quest.wolf_hunt",
  "steps": [
    {
      "stepIndex": 0,
      "stepType": "kill",
      "params": { "mob_id": 3, "count": 5 },
      "clientStepKey": "step.kill_wolves"
    }
  ],
  "rewards": [
    { "rewardType": "exp", "amount": 500 },
    { "rewardType": "item", "itemId": 42, "quantity": 1 }
  ]
}
```

**Граф диалога:**
```
[start=node_1]

node_1: type="line"   clientNodeKey="graviy.greeting"
  → edges → node_2

node_2: type="choice_hub"
  edge_quest (conditionGroup: {
    "all": [
      {"type":"level","gte":5},
      {"type":"quest","slug":"wolf_hunt","state":"not_started"}
    ]
  }, hideIfLocked=true)
    → node_quest_offer

  edge_busy (conditionGroup: {
    "type":"quest","slug":"wolf_hunt","state":"active"
  }, hideIfLocked=true)
    → node_quest_active

  edge_done (conditionGroup: {
    "type":"quest","slug":"wolf_hunt","state":"completed"
  }, hideIfLocked=true)
    → node_quest_turnin

  edge_bye
    → node_bye

node_quest_offer: type="action"
  actionGroup: { "type": "offer_quest", "slug": "wolf_hunt" }
  → edge → node_quest_offered_line

node_quest_offered_line: type="line"  clientNodeKey="graviy.quest_offered"
  → edge → node_end

node_quest_active: type="line"  clientNodeKey="graviy.quest_in_progress"
  → edge → node_end

node_quest_turnin: type="action"
  actionGroup: { "type": "turn_in_quest", "slug": "wolf_hunt" }
  → edge → node_quest_rewarded_line

node_quest_rewarded_line: type="line"  clientNodeKey="graviy.quest_complete"
  → edge → node_end

node_bye: type="line"  clientNodeKey="graviy.bye"
  → edge → node_end

node_end: type="end"
```

**Что получит клиент при сдаче квеста:**
1. `DIALOGUE_NODE { type: "line", clientNodeKey: "graviy.quest_complete" }`
2. Уведомления из `ActionResult.clientNotifications` (каждое в стандартной обёртке):
   - `{ "header": { "eventType": "quest_turned_in", "status": "success", ... }, "body": { "type": "quest_turned_in", "questId": 5, ... } }`
   - `{ "header": { "eventType": "exp_received",    "status": "success", ... }, "body": { "type": "exp_received", "amount": 500 } }`
   - `{ "header": { "eventType": "item_received",   "status": "success", ... }, "body": { "type": "item_received", "itemId": 42, "quantity": 1 } }`
3. `QUEST_UPDATE { state: "turned_in", ... }`

---

### 9.3 Пример 3: Диалог с приёмом квеста (turn-in)

Использование `turn_in_quest` как действия на ребре (вместо action-узла):

```
node_choice: type="choice_hub"
  edge_turnin (conditionGroup: {"type":"quest","slug":"wolf_hunt","state":"completed"})
    actionGroup: { "type": "turn_in_quest", "slug": "wolf_hunt" }
    → node_rewarded
```

Действие выполняется до перехода к `node_rewarded`. Клиент сначала получит уведомления о награде, затем следующий `DIALOGUE_NODE`.

---

### 9.4 Пример 4: Квест «Убей 5 волков»

**Полный flow прохождения:**

```
1. Игрок берёт квест через диалог (offer_quest action)
   → QuestManager::offerQuest("wolf_hunt") вызывается
   → progress = { "killed": 0 }, state = "active"
   → Клиент: QUEST_UPDATE { state:"active", currentStep:0, progress:{"killed":0} }

2. Игрок убивает первого волка (mob_id=3)
   → CombatSystem::onMobKill() → QuestManager::onMobKilled(charId, 3)
   → progress["killed"] = 1
   → Клиент: QUEST_UPDATE { progress:{"killed":1} }

3. Убивает ещё трёх
   → progress["killed"] = 4

4. Убивает пятого
   → progress["killed"] = 5
   → checkStepCompletion() → 5 >= 5 → advanceStep()
   → nextStep(1) >= steps.size(1) → completeQuest()
   → state = "completed"
   → Клиент: QUEST_UPDATE { state:"completed" }

5. Игрок идёт к Гравию, нажимает "Сдать квест" в диалоге
   → turn_in_quest action → QuestManager::turnInQuest()
   → state = "turned_in"
   → ExperienceManager::grantExperience(charId, 500, "quest_reward", 5)
   → InventoryManager::addItemToInventory(charId, 42, 1)
   → Клиент: QUEST_UPDATE { state:"turned_in" }
   → Клиент: уведомления о наградах

6. В течение 5 секунд Scheduler вызывает flushDirtyProgress()
   → updatePlayerQuestProgress { state:"turned_in" } → game-server → БД
```

---

## 10. Важные ограничения и подводные камни

### PlayerContextStruct — снимок, не эталон
После `buildPlayerContext()` контекст меняется действиями в процессе обхода графа. Изменения в `ctx` видны **в рамках одного запроса**. При следующем запросе контекст строится заново из `CharacterDataStruct::flags` и `QuestManager::playerProgress_`. Если флаг выставлен но ещё не сброшен на game-server — он всё равно корректно читается из памяти чанк-сервера.

### Порядок evaluateStyle в traverseToInteractiveNode
Action-узлы и jump-узлы обходятся автоматически. Если в графе есть action-узел, выставляющий флаг, и следующий за ним узел с условием на этот же флаг — всё работает правильно, потому что `ctx` мутируется in-place до проверки следующего узла.

### Лимит авто-обхода = 50 итераций
При более длинных цепочках action/jump-узлов (маловероятно в реальных диалогах, но теоретически возможно) обход обрывается с ошибкой и диалог не открывается. Следите за глубиной графа.

### Один персонаж — одна сессия
Нельзя одновременно разговаривать с двумя NPC. Повторный `NPC_INTERACT` убивает предыдущую сессию без предупреждения клиента.

### Флаги — flat namespace
Все флаги персонажа живут в одном пространстве имён. Используйте префиксы: `npc7_greeted`, `q42_flag_xyz` и т.п.

### Сброс прогресса при дисконнекте
`flushAllProgress(charId)` вызывается при отключении игрока. Это синхронный вызов, который держит `mutex_` — убедитесь, что `GameServerWorker::sendDataToGameServer()` не блокируется надолго.

### Gold-награды не реализованы
Тип `"gold"` в `QuestRewardStruct` строит уведомление, но не вызывает никакого менеджера. Это заглушка для будущей реализации системы валюты.

### Квест-шаг `custom`
`stepType = "custom"` не имеет автоматических хуков. Продвижение — только через `advanceQuestStepBySlug()` из `action_group` в диалоге.

---

*Документация основана на исходном коде чанк-сервера. При изменении логики — обновите соответствующие разделы.*
