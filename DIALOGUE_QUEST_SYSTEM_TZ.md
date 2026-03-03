# ТЗ: Система диалогов и квестов

**Версия:** 1.0  
**Дата:** 2026-03-01  
**Серверы затронуты:** chunk-server, game-server, login-server (БД)

---

## 1. Обзор архитектуры

### Принцип распределения ответственности

```
Login Server (PostgreSQL)
  └── хранит статику (quest, quest_step, quest_reward, dialogue, dialogue_node, dialogue_edge, npc_dialogue)
  └── хранит динамику игрока (player_quest, player_flag)

Game Server
  └── при старте чанк-сервера отдаёт ему всю предварительно загруженную из БД статику через события (аналогично SET_ALL_NPCS_LIST)
  └── принимает запросы на персистирование прогресса (флаги, квесты) и пишет в БД

Chunk Server  ← основная логика
  └── кеширует статику диалогов/квестов в памяти
  └── ведёт in-memory сессии диалогов
  └── ведёт in-memory прогресс квестов
  └── вычисляет condition_group / action_group
  └── слушает триггеры (убийство моба, получение предмета, движение)
  └── флашит прогресс в game-server асинхронно

Client
  └── хранит ТОЛЬКО тексты (локализация по ключам) и формирует UI на их основе
  └── не знает о condition_group / action_group / граф-структуре диалога
```

---

## 2. Структура базы данных (существующая, менять не нужно)

### 2.1 Статика диалогов

```sql
-- Диалог как граф. У NPC может быть несколько диалогов.
dialogue (id, slug, version, start_node_id)

-- Узел графа. Типы: line / choice_hub / action / jump / end
dialogue_node (
    id, dialogue_id,
    type node_type,       -- line|choice_hub|action|jump|end
    speaker_npc_id,       -- кто говорит (для клиента)
    client_node_key,      -- ключ текста на клиенте
    condition_group jsonb, -- когда узел актуален (пропускаем иначе)
    action_group jsonb,    -- действия при входе в action-узел
    jump_target_node_id    -- куда прыгаем (только type=jump)
)

-- Рёбра графа — варианты выбора
dialogue_edge (
    id, from_node_id, to_node_id,
    order_index,           -- порядок кнопок на клиенте
    client_choice_key,     -- ключ текста варианта
    condition_group jsonb, -- когда вариант доступен
    action_group jsonb,    -- что делаем при выборе
    hide_if_locked         -- скрыть если условие не выполнено (иначе показать серым)
)

-- Связка NPC → Диалог с приоритетом
npc_dialogue (
    npc_id, dialogue_id,
    priority,              -- чем выше — тем приоритетнее
    condition_group jsonb  -- когда этот диалог активен для данного NPC
)
```

### 2.2 Статика квестов

```sql
-- Карточка квеста
quest (
    id, slug, min_level,
    repeatable, cooldown_sec,
    giver_npc_id,   -- кто выдаёт
    turnin_npc_id,  -- кому сдавать
    client_quest_key
)

-- Шаги квеста (последовательные, step_index 0→1→2→...)
quest_step (
    id, quest_id, step_index,
    step_type,   -- collect|kill|talk|reach|custom
    params jsonb, -- параметры шага (см. раздел 4.2)
    client_step_key
)

-- Награды за сдачу
quest_reward (
    id, quest_id,
    reward_type,  -- 'item' | 'exp' | 'gold'
    item_id, quantity, amount
)
```

### 2.3 Динамика игрока

```sql
-- Прогресс квеста конкретного игрока
player_quest (
    player_id, quest_id,
    state,         -- offered|active|completed|turned_in|failed
    current_step,  -- индекс текущего шага (quest_step.step_index)
    progress jsonb, -- счётчик текущего шага: {"killed":3} / {"have":2} / {"done":false}
    updated_at
)

-- Произвольные флаги состояния игрока (НЕ квестовые)
player_flag (
    player_id, flag_key,
    int_value,   -- числовые счётчики
    bool_value,  -- булевы переключатели
    updated_at
)
```

---

## 3. Новые компоненты chunk-server

### 3.1 Структуры данных (`include/data/DataStructs.hpp` — добавить)

```cpp
// Узел диалогового графа
struct DialogueNodeStruct {
    int id = 0;
    int dialogueId = 0;
    std::string type = "";          // "line" | "choice_hub" | "action" | "jump" | "end"
    int speakerNpcId = 0;
    std::string clientNodeKey = "";
    nlohmann::json conditionGroup;  // null если нет условий
    nlohmann::json actionGroup;     // null если нет действий
    int jumpTargetNodeId = 0;
};

// Ребро (вариант выбора)
struct DialogueEdgeStruct {
    int id = 0;
    int fromNodeId = 0;
    int toNodeId = 0;
    int orderIndex = 0;
    std::string clientChoiceKey = "";
    nlohmann::json conditionGroup;
    nlohmann::json actionGroup;
    bool hideIfLocked = false;
};

// Диалог целиком (граф в памяти)
struct DialogueGraphStruct {
    int id = 0;
    std::string slug = "";
    int version = 0;
    int startNodeId = 0;
    std::unordered_map<int, DialogueNodeStruct> nodes;  // nodeId → node
    std::unordered_map<int, std::vector<DialogueEdgeStruct>> edges; // fromNodeId → edges
};

// Связка NPC → Диалог
struct NPCDialogueMappingStruct {
    int npcId = 0;
    int dialogueId = 0;
    int priority = 0;
    nlohmann::json conditionGroup;
};

// Активная сессия диалога
struct DialogueSessionStruct {
    std::string sessionId = "";     // уникальный ID: "dlg_{clientId}_{timestamp}"
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    int dialogueId = 0;
    int currentNodeId = 0;
    std::chrono::steady_clock::time_point lastActivity;
    static constexpr int TTL_SECONDS = 300; // 5 минут
};

// Контекст игрока для вычисления условий
struct PlayerContextStruct {
    int characterId = 0;
    int characterLevel = 0;
    std::unordered_map<std::string, bool> flagsBool;  // flag_key → bool_value
    std::unordered_map<std::string, int> flagsInt;    // flag_key → int_value
    // Краткое состояние квестов: quest_slug → state
    std::unordered_map<std::string, std::string> questStates;
    // Прогресс активных квестов: quest_id → progress json
    std::unordered_map<int, nlohmann::json> questProgress;
};

// Шаг квеста
struct QuestStepStruct {
    int id = 0;
    int questId = 0;
    int stepIndex = 0;
    std::string stepType = ""; // "kill" | "collect" | "talk" | "reach" | "custom"
    nlohmann::json params;
    std::string clientStepKey = "";
};

// Карточка квеста (статика)
struct QuestStruct {
    int id = 0;
    std::string slug = "";
    int minLevel = 0;
    bool repeatable = false;
    int cooldownSec = 0;
    int giverNpcId = 0;
    int turninNpcId = 0;
    std::string clientQuestKey = "";
    std::vector<QuestStepStruct> steps;
    std::vector<QuestRewardStruct> rewards;
};

// Награда за квест
struct QuestRewardStruct {
    std::string rewardType = ""; // "item" | "exp" | "gold"
    int itemId = 0;
    int quantity = 1;
    int64_t amount = 0;
};

// Прогресс квеста игрока (in-memory)
struct PlayerQuestProgressStruct {
    int characterId = 0;
    int questId = 0;
    std::string state = "";   // offered|active|completed|turned_in|failed
    int currentStep = 0;
    nlohmann::json progress;
    bool isDirty = false;     // нужно ли флашить в БД
    std::chrono::steady_clock::time_point updatedAt;
};
```

### 3.2 `DialogueManager` (`include/services/DialogueManager.hpp`)

**Аналог:** `NPCManager`

```cpp
class DialogueManager {
public:
    explicit DialogueManager(Logger& logger);

    // Вызываются при загрузке данных от game-server
    void setDialogues(const std::vector<DialogueGraphStruct>& dialogues);
    void setNPCDialogueMappings(const std::vector<NPCDialogueMappingStruct>& mappings);

    // Выбор диалога для NPC с учётом контекста игрока
    // Возвращает dialogueId или -1 если нет подходящего
    int selectDialogueForNPC(int npcId, const PlayerContextStruct& ctx) const;

    // Получить граф диалога
    const DialogueGraphStruct* getDialogueById(int id) const;
    const DialogueGraphStruct* getDialogueBySlug(const std::string& slug) const;

    bool isLoaded() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<int, DialogueGraphStruct> dialoguesById_;
    std::unordered_map<std::string, int> dialoguesBySlug_; // slug → id
    // npcId → mappings отсортированные по priority DESC
    std::unordered_map<int, std::vector<NPCDialogueMappingStruct>> npcMappings_;
    bool loaded_ = false;
    Logger& logger_;
};
```

### 3.3 `DialogueSessionManager` (`include/services/DialogueSessionManager.hpp`)

**Аналог:** логика `HarvestManager`

```cpp
class DialogueSessionManager {
public:
    explicit DialogueSessionManager(Logger& logger);

    // Создать сессию. Если у игрока уже есть — удаляет старую.
    DialogueSessionStruct& createSession(int clientId, int characterId,
                                          int npcId, int dialogueId, int startNodeId);

    // Получить сессию по sessionId
    DialogueSessionStruct* getSession(const std::string& sessionId);

    // Получить активную сессию игрока
    DialogueSessionStruct* getSessionByCharacter(int characterId);

    // Закрыть сессию
    void closeSession(const std::string& sessionId);
    void closeSessionByCharacter(int characterId);

    // Вызывать из Scheduler каждые 60 секунд
    void cleanupExpiredSessions();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, DialogueSessionStruct> sessions_;
    std::unordered_map<int, std::string> characterToSession_; // characterId → sessionId
    Logger& logger_;
};
```

### 3.4 `DialogueConditionEvaluator` (`include/services/DialogueConditionEvaluator.hpp`)

Stateless utility-класс. Принимает `condition_group` JSON и `PlayerContextStruct`, возвращает bool.

```cpp
class DialogueConditionEvaluator {
public:
    // Вычислить condition_group. Возвращает true если условие выполнено.
    // Если conditionGroup is null → всегда true.
    static bool evaluate(const nlohmann::json& conditionGroup,
                         const PlayerContextStruct& ctx);

private:
    static bool evaluateRule(const nlohmann::json& rule, const PlayerContextStruct& ctx);
    static bool evaluateFlag(const nlohmann::json& rule, const PlayerContextStruct& ctx);
    static bool evaluateQuest(const nlohmann::json& rule, const PlayerContextStruct& ctx);
    static bool evaluateLevel(const nlohmann::json& rule, const PlayerContextStruct& ctx);
    static bool evaluateInventory(const nlohmann::json& rule, const PlayerContextStruct& ctx);
};
```

**Формат `condition_group` JSON (соглашение):**

```json
// Одно условие:
{"type": "flag",  "key": "milaya_greeted", "eq": true}
{"type": "flag",  "key": "visit_count",    "gte": 3}
{"type": "quest", "slug": "wolf_hunt",     "state": "active"}
{"type": "quest", "slug": "wolf_hunt",     "state": "not_started"}
{"type": "level", "gte": 5}
{"type": "level", "lte": 20}
{"type": "item",  "item_id": 7,            "gte": 5}

// Логические группы:
{"all": [ ...условия... ]}   // AND
{"any": [ ...условия... ]}   // OR
{"not": { ...условие...  }}  // NOT
```

### 3.5 `DialogueActionExecutor` (`include/services/DialogueActionExecutor.hpp`)

Выполняет `action_group` — обновляет PlayerContext в памяти и ставит в очередь на персистирование.

```cpp
class DialogueActionExecutor {
public:
    DialogueActionExecutor(GameServices& services, Logger& logger);

    // Результат выполнения action_group для отправки клиенту
    struct ActionResult {
        std::vector<nlohmann::json> clientNotifications; // что сообщить клиенту
    };

    ActionResult execute(const nlohmann::json& actionGroup,
                         int characterId, int clientId,
                         PlayerContextStruct& ctx /* изменяется на месте */);

private:
    void executeSetFlag(const nlohmann::json& action, int characterId, PlayerContextStruct& ctx);
    void executeOfferQuest(const nlohmann::json& action, int characterId);
    void executeTurnInQuest(const nlohmann::json& action, int characterId, int clientId);
    void executeAdvanceQuestStep(const nlohmann::json& action, int characterId);

    GameServices& services_;
    Logger& logger_;
};
```

**Формат `action_group` JSON (соглашение):**

```json
{"actions": [
    {"type": "set_flag",            "key": "milaya_greeted",   "bool_value": true},
    {"type": "set_flag",            "key": "visit_count",      "inc": 1},
    {"type": "offer_quest",         "slug": "wolf_hunt"},
    {"type": "turn_in_quest",       "slug": "wolf_hunt"},
    {"type": "advance_quest_step",  "slug": "wolf_hunt"},
    {"type": "give_item",           "item_id": 7,  "quantity": 3},
    {"type": "give_exp",            "amount": 500}
]}
```

### 3.6 `QuestManager` (`include/services/QuestManager.hpp`)

**Аналог:** `ExperienceManager`

```cpp
class QuestManager {
public:
    QuestManager(GameServices* services, Logger& logger);

    // Загрузка статики от game-server
    void setQuests(const std::vector<QuestStruct>& quests);

    // Прогресс в памяти (загружается при входе игрока в чанк)
    void loadPlayerQuests(int characterId,
                          const std::vector<PlayerQuestProgressStruct>& quests);

    // Получить контекст для condition evaluator
    void fillQuestContext(int characterId, PlayerContextStruct& ctx) const;

    // Выдать квест игроку
    bool offerQuest(int characterId, const std::string& questSlug);

    // Сдать квест
    bool turnInQuest(int characterId, const std::string& questSlug, int clientId);

    // Триггеры — вызываются из других систем
    void onMobKilled(int characterId, int mobId);
    void onItemObtained(int characterId, int itemId, int quantity);
    void onNPCTalked(int characterId, int npcId);
    void onPositionReached(int characterId, float x, float y);

    // Флаш грязных записей в game-server (вызывается Scheduler каждые 5 сек)
    void flushDirtyProgress();

    // При дисконнекте — немедленный флаш
    void flushAllProgress(int characterId);

private:
    void checkStepCompletion(int characterId, PlayerQuestProgressStruct& pq);
    void advanceStep(int characterId, PlayerQuestProgressStruct& pq);
    void completeQuest(int characterId, PlayerQuestProgressStruct& pq);
    void grantRewards(int characterId, const QuestStruct& quest, int clientId);
    bool isQuestCompletable(int characterId, const std::string& questSlug) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, QuestStruct> questsBySlug_;
    // characterId → (questId → progress)
    std::unordered_map<int, std::unordered_map<int, PlayerQuestProgressStruct>> playerProgress_;
    GameServices* services_;
    Logger& logger_;
};
```

### 3.7 `DialogueEventHandler` (`include/events/handlers/DialogueEventHandler.hpp`)

**Аналог:** `NPCEventHandler`, `HarvestEventHandler`

```cpp
class DialogueEventHandler : public BaseEventHandler {
public:
    DialogueEventHandler(NetworkManager& networkManager,
                         GameServerWorker& gameServerWorker,
                         GameServices& gameServices);

    // От game-server при старте
    void handleSetAllDialoguesEvent(const Event& event);

    // От клиента
    void handleNPCInteractEvent(const Event& event);
    void handleDialogueChoiceEvent(const Event& event);
    void handleDialogueCloseEvent(const Event& event);

private:
    // Обход графа: пропускает jump/action узлы автоматически до line/choice_hub/end
    // Возвращает nodeId первого узла требующего ответа клиента (или -1 если end)
    int traverseToInteractiveNode(int startNodeId,
                                   const DialogueGraphStruct& graph,
                                   const PlayerContextStruct& ctx,
                                   int characterId, int clientId,
                                   std::vector<nlohmann::json>& outActionResults);

    // Собрать список рёбер из узла с учётом условий
    nlohmann::json buildChoicesJson(int nodeId,
                                    const DialogueGraphStruct& graph,
                                    const PlayerContextStruct& ctx);

    // Отправить узел клиенту
    void sendNodeToClient(int clientId,
                          const std::string& sessionId,
                          const DialogueNodeStruct& node,
                          const nlohmann::json& choices,
                          const std::string& packetType); // "DIALOGUE_OPEN" | "DIALOGUE_NODE"

    // Валидация: игрок в радиусе NPC?
    bool isPlayerInRange(int characterId, int npcId) const;
};
```

---

## 4. Протокол пакетов

### 4.1 Client → Server

#### `NPC_INTERACT` — нажатие на NPC

```json
{
    "header": {
        "action": "NPC_INTERACT",
        "clientId": 1,
        "hash": "abc123",
        "clientSendMs": 1234567890
    },
    "body": {
        "npcId": 42,
        "playerId": 7
    }
}
```

**Условия обработки:**
- NPC существует и `is_interactable = true`
- Игрок в радиусе `npc.radius` (+ 20% люфт на lag)
- Игрок не мёртв
- Игрок не в бою
- У игрока нет другой активной диалоговой сессии

---

#### `DIALOGUE_CHOICE` — выбор варианта ответа

```json
{
    "header": {
        "action": "DIALOGUE_CHOICE",
        "clientId": 1,
        "hash": "abc123",
        "clientSendMs": 1234567890
    },
    "body": {
        "sessionId": "dlg_1_1740825600000",
        "edgeId": 17,
        "playerId": 7
    }
}
```

**Условия обработки:**
- `sessionId` существует и принадлежит данному `characterId`
- `edgeId` выходит из `session.currentNodeId`
- `edge.condition_group` до сих пор выполняется (повторная проверка)
- Игрок всё ещё в радиусе NPC

---

#### `DIALOGUE_CLOSE` — игрок закрыл диалог

```json
{
    "header": {
        "action": "DIALOGUE_CLOSE",
        "clientId": 1,
        "hash": "abc123"
    },
    "body": {
        "sessionId": "dlg_1_1740825600000",
        "playerId": 7
    }
}
```

---

### 4.2 Server → Client

#### `DIALOGUE_OPEN` — успешный старт диалога

Отправляется в ответ на `NPC_INTERACT`. Содержит первый интерактивный узел.

```json
{
    "header": {
        "action": "DIALOGUE_OPEN",
        "serverRecvMs": 1234567890,
        "serverSendMs": 1234567891
    },
    "body": {
        "sessionId": "dlg_1_1740825600000",
        "npcId": 42,
        "node": {
            "nodeId": 5,
            "type": "line",
            "speakerNpcId": 42,
            "clientNodeKey": "milaya.dialogue.greeting"
        },
        "choices": [
            {
                "edgeId": 17,
                "orderIndex": 0,
                "clientChoiceKey": "milaya.choice.about_village",
                "isLocked": false,
                "isHidden": false
            },
            {
                "edgeId": 18,
                "orderIndex": 1,
                "clientChoiceKey": "milaya.choice.quest",
                "isLocked": true,
                "isHidden": false
            }
        ]
    }
}
```

**Примечания:**
- `choices` может быть пустым если `type = "line"` с единственным авто-переходом
- `isLocked = true` + `isHidden = false` → клиент показывает вариант серым
- `isHidden = true` → клиент не показывает вариант вообще (edge имел `hide_if_locked=true`)
- Узлы `action` / `jump` проходятся сервером **автоматически** — клиент их не видит

---

#### `DIALOGUE_NODE` — следующий узел после выбора

Отправляется в ответ на `DIALOGUE_CHOICE`. Структура тела идентична `DIALOGUE_OPEN`.

```json
{
    "header": { "action": "DIALOGUE_NODE", ... },
    "body": {
        "sessionId": "dlg_1_1740825600000",
        "node": {
            "nodeId": 8,
            "type": "line",
            "speakerNpcId": 42,
            "clientNodeKey": "milaya.dialogue.village_story"
        },
        "choices": [ ... ]
    }
}
```

---

#### `DIALOGUE_CLOSE` (server → client) — завершение диалога

```json
{
    "header": { "action": "DIALOGUE_CLOSE", ... },
    "body": {
        "sessionId": "dlg_1_1740825600000",
        "reason": "end_node"
    }
}
```

**Значения `reason`:**
- `end_node` — нормальное завершение (достигли type=end)
- `player_closed` — игрок сам закрыл
- `npc_out_of_range` — игрок вышел из радиуса
- `combat_started` — начался бой
- `session_expired` — TTL истёк

---

#### `DIALOGUE_ACTION_RESULT` — результат side-effects

Отправляется **до** `DIALOGUE_OPEN` / `DIALOGUE_NODE` если при автообходе были `action`-узлы.

```json
{
    "header": { "action": "DIALOGUE_ACTION_RESULT", ... },
    "body": {
        "sessionId": "dlg_1_1740825600000",
        "results": [
            {"type": "quest_offered",  "questId": 12, "clientQuestKey": "wolf_hunt_intro"},
            {"type": "item_received",  "itemId": 5,   "quantity": 3},
            {"type": "exp_received",   "amount": 500},
            {"type": "quest_turned_in","questId": 12}
        ]
    }
}
```

---

#### `QUEST_UPDATE` — обновление прогресса квеста у игрока

Отправляется клиенту всякий раз когда меняется прогресс (`onMobKilled`, `onItemObtained`, и т.д.)

```json
{
    "header": { "action": "QUEST_UPDATE", ... },
    "body": {
        "questId": 1,
        "clientQuestKey": "wolf_hunt_intro",
        "state": "active",
        "currentStep": 0,
        "clientStepKey": "quest_step_collect_pelts",
        "stepType": "kill",
        "progress": {"killed": 4},
        "required": {"count": 10}
    }
}
```

---

#### `DIALOGUE_ERROR` — отказ открыть диалог

```json
{
    "header": { "action": "DIALOGUE_ERROR", ... },
    "body": {
        "code": "OUT_OF_RANGE",
        "npcId": 42
    }
}
```

**Коды ошибок:** `OUT_OF_RANGE`, `NPC_NOT_INTERACTABLE`, `IN_COMBAT`, `NO_DIALOGUE_AVAILABLE`, `SESSION_CONFLICT`

---

## 5. Алгоритм обработки на сервере

### 5.1 `NPC_INTERACT`

```
1. Найти NPC по npcId → ошибка NPC_NOT_INTERACTABLE если !is_interactable
2. Проверить дистанцию игрок→NPC ≤ npc.radius * 1.2 → ошибка OUT_OF_RANGE
3. Проверить что игрок не в бою → ошибка IN_COMBAT
4. Закрыть старую сессию если есть
5. Построить PlayerContextStruct для characterId
   (уровень, флаги из CharacterManager, прогресс квестов из QuestManager)
6. DialogueManager::selectDialogueForNPC(npcId, ctx)
   → перебрать npc_dialogue записи по priority DESC
   → для каждой вычислить condition_group
   → взять первую подходящую → ошибка NO_DIALOGUE_AVAILABLE если нет
7. DialogueSessionManager::createSession(...)  с startNodeId = dialogue.start_node_id
8. traverseToInteractiveNode(startNodeId, ...) → автообход action/jump узлов
9. Если actionResults не пусты → send DIALOGUE_ACTION_RESULT
10. Если достигли end → send DIALOGUE_CLOSE reason=end_node
11. Иначе → send DIALOGUE_OPEN с текущим узлом и choices
```

### 5.2 `DIALOGUE_CHOICE`

```
1. Найти сессию по sessionId → игнорировать если нет (сессия истекла)
2. Проверить characterId сессии == characterId из ClientDataStruct
3. Найти edge по edgeId
4. Проверить edge.fromNodeId == session.currentNodeId
5. Повторно вычислить edge.condition_group → если false → игнорировать
6. Проверить дистанцию игрок→NPC (актуально — мог отойти)
7. Если edge.action_group → DialogueActionExecutor::execute(...)
8. traverseToInteractiveNode(edge.toNodeId, ...)
9. Если actionResults → send DIALOGUE_ACTION_RESULT
10. Если end → send DIALOGUE_CLOSE reason=end_node + closeSession
11. Иначе → send DIALOGUE_NODE
```

### 5.3 `traverseToInteractiveNode` — автообход

```
Вход: nodeId

loop:
    node = graph.nodes[nodeId]
    
    if node.type == "end":
        return -1 (признак конца)
    
    if node.type == "jump":
        nodeId = node.jumpTargetNodeId
        continue
    
    if node.type == "action":
        if condition_group == null OR evaluate(condition_group, ctx):
            results += execute(node.action_group)
        nextEdge = первое ребро из nodeId (их должно быть ровно 1)
        nodeId = nextEdge.toNodeId
        continue
    
    if node.type == "line" OR "choice_hub":
        session.currentNodeId = nodeId
        return nodeId  // стоп, нужен ответ клиента
```

### 5.4 `buildChoicesJson`

```
edges = graph.edges[nodeId]  // все рёбра из узла
result = []

for each edge (отсортированных по order_index):
    condMet = evaluate(edge.condition_group, ctx)
    
    if !condMet AND edge.hideIfLocked:
        continue  // полностью скрыть
    
    result.push({
        edgeId:          edge.id,
        orderIndex:      edge.orderIndex,
        clientChoiceKey: edge.clientChoiceKey,
        isLocked:        !condMet,
        isHidden:        false
    })

return result
```

---

## 6. Интеграция квестовых триггеров

### 6.1 В `CombatEventHandler` (убийство моба)

```cpp
// После death-обработки дропа лута добавить:
services_.getQuestManager().onMobKilled(characterId, mobId);
```

### 6.2 В `InventoryManager` (получение предмета)

```cpp
// После добавления предмета в инвентарь:
services_.getQuestManager().onItemObtained(characterId, itemId, quantity);
```

### 6.3 В `DialogueActionExecutor` (разговор с NPC)

```cpp
// При обходе action-узла или action_group в edge:
if (action["type"] == "advance_quest_step") {
    services_.getQuestManager().onNPCTalked(characterId, session.npcId);
}
```

### 6.4 В `ChunkEventHandler` (движение игрока)

```cpp
// При обработке позиции игрока (уже существует):
services_.getQuestManager().onPositionReached(characterId, pos.x, pos.y);
```

---

## 7. Загрузка данных от game-server

### 7.1 Новые события game-server → chunk-server

| Событие | Что содержит | Когда |
|---|---|---|
| `SET_ALL_DIALOGUES` | все `dialogue` + `dialogue_node` + `dialogue_edge` | при старте чанк-сервера |
| `SET_NPC_DIALOGUE_MAPPINGS` | все `npc_dialogue` | при старте чанк-сервера |
| `SET_ALL_QUESTS` | все `quest` + `quest_step` + `quest_reward` | при старте чанк-сервера |
| `SET_PLAYER_QUESTS` | `player_quest[]` для конкретного персонажа | при входе игрока в чанк |
| `SET_PLAYER_FLAGS` | `player_flag[]` для конкретного персонажа | при входе игрока в чанк |

### 7.2 Новые события chunk-server → game-server (персистирование)

| Событие | Что содержит | Частота |
|---|---|---|
| `UPDATE_PLAYER_QUEST_PROGRESS` | characterId, questId, state, currentStep, progress | каждые 5 сек если isDirty |
| `UPDATE_PLAYER_FLAG` | characterId, flagKey, value | сразу при изменении |
| `FLUSH_PLAYER_QUESTS` | все активные квесты игрока | при дисконнекте |

---

## 8. Изменения в существующих структурах

### 8.1 `CharacterDataStruct` — добавить поля

```cpp
struct CharacterDataStruct {
    // ... существующие поля без изменений ...
    
    // НОВЫЕ ПОЛЯ:
    std::vector<PlayerFlagStruct> flags;         // player_flag записи
    std::vector<PlayerQuestProgressStruct> quests; // активные квесты (state != turned_in/failed)
};

struct PlayerFlagStruct {
    std::string flagKey = "";
    std::optional<bool> boolValue;
    std::optional<int> intValue;
};
```

### 8.2 `GameServices` — добавить менеджеры

```cpp
// В GameServices добавить:
private:
    DialogueManager dialogueManager_;
    DialogueSessionManager dialogueSessionManager_;
    QuestManager questManager_;

public:
    DialogueManager& getDialogueManager() { return dialogueManager_; }
    DialogueSessionManager& getDialogueSessionManager() { return dialogueSessionManager_; }
    QuestManager& getQuestManager() { return questManager_; }
```

### 8.3 `Scheduler` — добавить задачи

```cpp
// В ChunkServer при инициализации Scheduler добавить:

// Очистка истёкших диалоговых сессий
scheduler_.every(60s, [&]() {
    gameServices_.getDialogueSessionManager().cleanupExpiredSessions();
});

// Флаш прогресса квестов
scheduler_.every(5s, [&]() {
    gameServices_.getQuestManager().flushDirtyProgress();
});
```

---

## 9. Пример полного сценария: квест "Wolf Hunt"

### Данные в БД (статика, вставить один раз)

```sql
-- Квест
INSERT INTO quest (slug, min_level, repeatable, cooldown_sec, giver_npc_id, turnin_npc_id, client_quest_key)
VALUES ('wolf_hunt_intro', 1, true, 600, 2, 2, 'wolf_hunt_intro');

-- Шаг 0: убить 10 волков
INSERT INTO quest_step (quest_id, step_index, step_type, params, client_step_key)
VALUES (1, 0, 'kill', '{"mob_id": 5, "count": 10}', 'quest_step_kill_wolves');

-- Шаг 1: вернуться к NPC
INSERT INTO quest_step (quest_id, step_index, step_type, params, client_step_key)
VALUES (1, 1, 'talk', '{"npc_id": 2}', 'quest_step_talk_milaya');

-- Награды
INSERT INTO quest_reward (quest_id, reward_type, amount) VALUES (1, 'exp', 500);
INSERT INTO quest_reward (quest_id, reward_type, item_id, quantity) VALUES (1, 'item', 7, 3);

-- Диалог выдачи квеста (уже есть диалог id=1)
-- Добавляем edge с action_group выдачи квеста
INSERT INTO dialogue_edge (from_node_id, to_node_id, order_index, client_choice_key,
    condition_group, action_group, hide_if_locked)
VALUES (
    1, -- from: узел приветствия
    3, -- to:   узел "Иди убей волков"
    1,
    'milaya.choice.accept_quest',
    '{"type": "quest", "slug": "wolf_hunt_intro", "state": "not_started"}',
    '{"actions": [{"type": "offer_quest", "slug": "wolf_hunt_intro"}]}',
    true -- скрыть если квест уже взят
);

-- Edge для сдачи квеста
INSERT INTO dialogue_edge (from_node_id, to_node_id, order_index, client_choice_key,
    condition_group, action_group, hide_if_locked)
VALUES (
    1, -- from: узел приветствия  
    4, -- to:   узел "Молодец, держи награду"
    0,
    'milaya.choice.turnin_quest',
    '{"type": "quest", "slug": "wolf_hunt_intro", "state": "completed"}',
    '{"actions": [{"type": "turn_in_quest", "slug": "wolf_hunt_intro"}]}',
    true -- скрыть если квест не активен/не выполнен
);
```

### Последовательность событий

```
1. Игрок кликает на Милаю
   → NPC_INTERACT {npcId: 2}
   → Сервер: state="not_started" → edge "accept_quest" доступен, "turnin" скрыт
   → DIALOGUE_OPEN с choices: ["о деревне", "принять задание", "пока"]

2. Игрок выбирает "принять задание"
   → DIALOGUE_CHOICE {edgeId: 10}
   → action_group: offer_quest "wolf_hunt_intro"
   → INSERT player_quest (state='active', step=0, progress='{"killed":0}')
   → DIALOGUE_ACTION_RESULT {quest_offered, questId:1}
   → DIALOGUE_NODE — Милая говорит "Убей 10 волков"

3. Игрок убивает волков
   → onMobKilled(characterId, mobId=5) из CombatEventHandler
   → progress.killed++ для каждого
   → QUEST_UPDATE {killed:1, required:10} отправляется клиенту после каждого
   → При killed=10: state='completed', currentStep=1
   → QUEST_UPDATE {state:'completed'}

4. Игрок снова кликает на Милаю
   → Сервер: state="completed" → edge "turnin" доступен, "accept" скрыт
   → DIALOGUE_OPEN с choices: ["сдать задание", "пока"]

5. Игрок выбирает "сдать задание"
   → action_group: turn_in_quest "wolf_hunt_intro"
   → grantRewards: +500 exp, +3 предмета
   → state='turned_in'
   → DIALOGUE_ACTION_RESULT {quest_turned_in, exp_received:500, item_received:...}
```

---

## 10. Порядок реализации

### Этап 1 — Структуры и статика (без сети)
1. Добавить структуры в `DataStructs.hpp` (раздел 3.1)
2. Реализовать `DialogueManager` — загрузка и хранение графов
3. Реализовать `QuestManager` — загрузка статики квестов
4. Реализовать `DialogueConditionEvaluator` — вычисление условий
5. Добавить в `GameServices`

### Этап 2 — Сессии и выдача диалога
6. Реализовать `DialogueSessionManager`
7. Реализовать `DialogueEventHandler::handleNPCInteractEvent`
8. Реализовать `traverseToInteractiveNode` + `buildChoicesJson`
9. Добавить регистрацию событий в ChunkServer
10. Добавить в Scheduler очистку сессий

### Этап 3 — Выбор варианта и actions
11. Реализовать `DialogueActionExecutor`
12. Реализовать `DialogueEventHandler::handleDialogueChoiceEvent`
13. Реализовать `handleDialogueCloseEvent`

### Этап 4 — Квестовые триггеры
14. Реализовать `QuestManager::onMobKilled` + интеграция в `CombatEventHandler`
15. Реализовать `QuestManager::onItemObtained` + интеграция в `InventoryManager`
16. Реализовать `QuestManager::onNPCTalked` + `onPositionReached`
17. Реализовать `flushDirtyProgress` + Scheduler задача

### Этап 5 — Game-server и персистирование
18. Добавить новые события загрузки статики в game-server
19. Добавить приём `SET_PLAYER_QUESTS` / `SET_PLAYER_FLAGS` при входе игрока  
20. Добавить обработчики `UPDATE_PLAYER_QUEST_PROGRESS` / `UPDATE_PLAYER_FLAG`
21. Запросы к БД в login-server для новых событий

### Этап 6 — Тестирование
22. Прогнать сценарий Wolf Hunt end-to-end
23. Проверить TTL сессий (отошёл пока диалог открыт)
24. Проверить кулдаун повторного квеста
25. Проверить флаш прогресса при дисконнекте

---

## 11. Что НЕ входит в данное ТЗ

- Редактор диалогов (GUI-инструмент для заполнения БД)
- Тексты/локализация (на клиенте)
- Система торговли с NPC (отдельное ТЗ)
- Групповые квесты
- Квесты с временным ограничением (`failed` по таймеру)
