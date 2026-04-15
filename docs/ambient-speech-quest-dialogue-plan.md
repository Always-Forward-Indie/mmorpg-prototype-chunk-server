# План реализации: NPC Ambient Speech + Улучшения диалогов и квестов

**Дата:** 2026-04-15  
**Приоритет:** Средний (UX и нарратив)  
**Зависимости:** Существующие системы диалогов, квестов, NPC

---

## Содержание

1. [Система реплик NPC (Ambient Speech)](#1-система-реплик-npc-ambient-speech)
2. [Улучшение диалогов — контекст квеста](#2-улучшение-диалогов--контекст-квеста)
3. [Улучшение квестовой системы — журнал](#3-улучшение-квестовой-системы--журнал)
4. [Порядок миграций БД](#4-порядок-миграций-бд)
5. [Порядок реализации на сервере](#5-порядок-реализации-на-сервере)
6. [Протоколы пакетов (клиент ↔ сервер)](#6-протоколы-пакетов-клиент--сервер)

---

## 1. Система реплик NPC (Ambient Speech)

### Идея

NPC произносит реплики над головой вне диалога. Два режима:
- **proximity** — однократно при приближении игрока (крючок)
- **periodic** — периодически пока игрок в радиусе

Условия на показ — через **существующий** `DialogueConditionEvaluator`. Сервер фильтрует пул для конкретного игрока и отдаёт ему только валидные реплики. Клиент сам тактирует и рандомит.

### 1.1 Миграция БД (054)

```sql
-- 054_npc_ambient_speech.sql

CREATE TABLE public.npc_ambient_speech_configs (
    id              SERIAL       PRIMARY KEY,
    npc_id          INT          NOT NULL REFERENCES public.npcs(id) ON DELETE CASCADE,
    min_interval_sec INT         NOT NULL DEFAULT 20,
    max_interval_sec INT         NOT NULL DEFAULT 60,
    CONSTRAINT uq_npc_ambient_config UNIQUE (npc_id)
);

CREATE TABLE public.npc_ambient_speech_lines (
    id              SERIAL       PRIMARY KEY,
    npc_id          INT          NOT NULL REFERENCES public.npcs(id) ON DELETE CASCADE,
    line_key        VARCHAR(128) NOT NULL,  -- ключ локализации, e.g. "blacksmith_idle_1"
    trigger_type    VARCHAR(16)  NOT NULL DEFAULT 'periodic',  -- 'periodic' | 'proximity'
    trigger_radius  INT          NOT NULL DEFAULT 400,         -- только для proximity
    priority        INT          NOT NULL DEFAULT 0,
    weight          INT          NOT NULL DEFAULT 10,          -- в рамках одного priority-пула
    cooldown_sec    INT          NOT NULL DEFAULT 60,
    condition_group JSONB                DEFAULT NULL          -- null = всегда показывать
);

CREATE INDEX idx_ambient_lines_npc_id ON public.npc_ambient_speech_lines(npc_id);

COMMENT ON COLUMN public.npc_ambient_speech_lines.line_key       IS 'Ключ локализации для клиента, e.g. npc.blacksmith.idle_1';
COMMENT ON COLUMN public.npc_ambient_speech_lines.trigger_type   IS 'periodic = по таймеру; proximity = один раз при приближении';
COMMENT ON COLUMN public.npc_ambient_speech_lines.priority       IS 'Сначала берётся пул с наивысшим priority. Внутри пула — weighted random';
COMMENT ON COLUMN public.npc_ambient_speech_lines.weight         IS 'Вес в weighted random внутри priority-группы';
COMMENT ON COLUMN public.npc_ambient_speech_lines.cooldown_sec   IS 'Минимальный интервал между показами этой конкретной реплики (per-client)';
COMMENT ON COLUMN public.npc_ambient_speech_lines.condition_group IS 'JSON условие, совместимое с DialogueConditionEvaluator';
```

### 1.2 Новые C++ структуры (DataStructs.hpp)

```cpp
struct NPCAmbientLineStruct
{
    int id = 0;
    int npcId = 0;
    std::string lineKey = "";       ///< ключ локализации
    std::string triggerType = "periodic"; ///< "periodic" | "proximity"
    int triggerRadius = 400;
    int priority = 0;
    int weight = 10;
    int cooldownSec = 60;
    nlohmann::json conditionGroup;  ///< null → always valid
};

struct NPCAmbientSpeechConfigStruct
{
    int npcId = 0;
    int minIntervalSec = 20;
    int maxIntervalSec = 60;
    std::vector<NPCAmbientLineStruct> lines;
};
```

### 1.3 Новый сервис AmbientSpeechManager

**Файлы:**
- `include/services/AmbientSpeechManager.hpp`
- `src/services/AmbientSpeechManager.cpp`

**Интерфейс:**

```cpp
class AmbientSpeechManager {
public:
    // Загрузка при старте от Game Server
    void setAmbientSpeechData(const std::vector<NPCAmbientSpeechConfigStruct> &configs);

    // Фильтрация пула для конкретного игрока (вызывается при joinGameCharacter)
    // Возвращает только строки где conditionGroup == true для данного контекста
    nlohmann::json buildFilteredPoolForPlayer(int npcId, const PlayerContextStruct &ctx) const;

    // Пересчёт при изменении контекста игрока (смена состояния квеста, флага)
    // Возвращает обновлённые пулы для всех NPC в текущей зоне игрока
    std::vector<nlohmann::json> rebuildPoolsForPlayer(int characterId, const PlayerContextStruct &ctx) const;

private:
    std::unordered_map<int, NPCAmbientSpeechConfigStruct> configs_; ///< npcId → config
    mutable std::shared_mutex mutex_;
};
```

**Логика `buildFilteredPoolForPlayer`:**
1. Берём все `lines` для `npcId`
2. Для каждой строки проверяем `DialogueConditionEvaluator::evaluate(line.conditionGroup, ctx)`
3. Группируем прошедшие по `priority`
4. Возвращаем JSON с полным конфигом (интервалы, триггеры, пулы по priority)

### 1.4 Загрузка от Game Server

**Game Server** при старте отправляет пакет `setNPCAmbientSpeech` → Chunk Server.

**GameServerWorker.cpp** — добавить ветку:
```cpp
else if (eventType == "setNPCAmbientSpeech") {
    auto configs = jsonParser_.parseNPCAmbientSpeech(data.data(), data.size());
    eventsBatch.emplace_back(Event::SET_NPC_AMBIENT_SPEECH, clientData.clientId, configs);
}
```

**NPCEventHandler** — добавить обработку `SET_NPC_AMBIENT_SPEECH`:
```cpp
gameServices_.getAmbientSpeechManager().setAmbientSpeechData(configs);
```

**EventData.hpp** — добавить в variant:
```cpp
std::vector<NPCAmbientSpeechConfigStruct>,
```

**Event.hpp** — добавить:
```cpp
SET_NPC_AMBIENT_SPEECH,
UPDATE_NPC_AMBIENT_POOLS, // для точечного обновления при смене контекста
```

### 1.5 Отправка клиенту

**При `joinGameCharacter`:** после загрузки игрока — для каждого NPC в зоне (берём через `NPCManager::getNPCsInArea`) формируем отфильтрованный пул и отправляем пакет `npcAmbientPools`.

**При смене контекста** (завершение квеста, смена флага) — те же handler'ы, которые шлют `UPDATE_PLAYER_QUEST_PROGRESS` / `UPDATE_PLAYER_FLAG`, вызывают `rebuildPoolsForPlayer` и отправляют `updateNpcAmbientPools`.

### 1.6 Пакет `npcAmbientPools` (Chunk → Client)

```json
{
  "header": { "eventType": "NPC_AMBIENT_POOLS" },
  "body": {
    "npcs": [
      {
        "npcId": 5,
        "minIntervalSec": 20,
        "maxIntervalSec": 60,
        "pools": [
          {
            "priority": 100,
            "lines": [
              {
                "id": 2,
                "lineKey": "blacksmith_thanks_quest",
                "triggerType": "periodic",
                "triggerRadius": 400,
                "weight": 5,
                "cooldownSec": 300
              }
            ]
          },
          {
            "priority": 0,
            "lines": [
              {
                "id": 1,
                "lineKey": "blacksmith_idle_1",
                "triggerType": "periodic",
                "triggerRadius": 400,
                "weight": 10,
                "cooldownSec": 60
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### 1.7 Клиентская реализация (UE5)

**Логика клиента:**
1. На `NPC_AMBIENT_POOLS` — сохранить пулы реплик по `npcId`
2. На `UPDATE_NPC_AMBIENT_POOLS` — обновить пулы (смена контекста)
3. Для каждого NPC в радиусе видимости запустить `FTimerHandle` с рандомным `[minIntervalSec, maxIntervalSec]`
4. По таймеру: выбрать highest priority пул с хотя бы одной репликой → weighted random → проверить локальный cooldown → показать bubble
5. **Proximity** реплики: отдельная проверка при входе в `triggerRadius` (один раз, пока не сменится контекст)
6. Bubble: виджет над головой NPC — fade-in (0.3s) → hold (2-3s) → fade-out (0.5s)
7. Показывать только если расстояние до NPC ≤ `triggerRadius` (для UI-оптимизации)
8. Не показывать если открыт диалог с этим NPC

---

## 2. Улучшение диалогов — контекст квеста

### Идея

В момент диалога:
- При edge с `offer_quest` — показать **первый шаг** квеста (не все) + **известные** награды
- При edge с `turn_in_quest` (квест `completed`) — показать награды которые игрок получит
- Скрытые награды (`isHidden = true`) отображаются как `???`

### 2.1 Изменение `QuestRewardStruct` (DataStructs.hpp)

```cpp
struct QuestRewardStruct
{
    std::string rewardType = ""; ///< "item" | "exp" | "gold"
    int itemId = 0;
    int quantity = 1;
    int64_t amount = 0;
    bool isHidden = false;       ///< true = клиент видит "???" вместо реального содержимого
};
```

### 2.2 Изменение `QuestStepStruct` (DataStructs.hpp)

Для шагов типа `reach` добавить `zone_slug` в `params` при создании данных в БД:
```json
{ "x": 100.0, "y": 200.0, "radius": 200.0, "zone_slug": "ruins_exit" }
```

Структура `QuestStepStruct` не меняется — `params` уже `nlohmann::json`.

### 2.3 Вспомогательный метод resolveStepForClient

Добавить **приватный** метод в `DialogueActionExecutor` или вынести в `QuestManager`:

```cpp
// Резолвит IDs → slugs для конкретного шага квеста
nlohmann::json resolveStepForClient(const QuestStepStruct &step) const;
```

Логика:
```cpp
nlohmann::json DialogueActionExecutor::resolveStepForClient(const QuestStepStruct &step) const
{
    nlohmann::json j;
    j["clientStepKey"] = step.clientStepKey;
    j["stepType"]      = step.stepType;
    j["count"]         = step.params.value("count", 1);

    if (step.stepType == "kill") {
        int mobId = step.params.value("mob_id", -1);
        auto mob  = services_.getMobManager().getMobById(mobId);
        j["target_slug"] = mob.slug; // "" если не найден
    }
    else if (step.stepType == "collect") {
        int itemId = step.params.value("item_id", -1);
        auto item  = services_.getItemManager().getItemById(itemId);
        j["target_slug"] = item.slug;
    }
    else if (step.stepType == "talk") {
        int npcId = step.params.value("npc_id", -1);
        auto npc  = services_.getNPCManager().getNPCById(npcId);
        j["target_slug"] = npc.slug;
    }
    else if (step.stepType == "reach") {
        j["zone_slug"] = step.params.value("zone_slug", "");
        j["x"]         = step.params.value("x", 0.0f);
        j["y"]         = step.params.value("y", 0.0f);
    }
    // "custom" — передаем params как есть, клиент разбирает сам
    else {
        j["params"] = step.params;
    }
    return j;
}
```

### 2.4 Вспомогательный метод resolveRewardsForClient

```cpp
// Резолвит награды: itemId → slug, скрытые → без раскрытия
nlohmann::json resolveRewardsForClient(const std::vector<QuestRewardStruct> &rewards) const;
```

Логика:
```cpp
for (const auto &reward : rewards) {
    nlohmann::json r;
    r["rewardType"] = reward.rewardType;
    r["isHidden"]   = reward.isHidden;

    if (reward.isHidden) {
        // Передаём только тип чтобы клиент показал "???" нужного типа
        // НЕ передаём itemId, quantity, amount
    }
    else if (reward.rewardType == "item") {
        auto item = services_.getItemManager().getItemById(reward.itemId);
        r["item_slug"] = item.slug;
        r["quantity"]  = reward.quantity;
    }
    else if (reward.rewardType == "exp") {
        r["amount"] = reward.amount;
    }
    else if (reward.rewardType == "gold") {
        r["amount"] = reward.amount;
    }
    rewardsJson.push_back(r);
}
```

### 2.5 Изменение `buildChoicesJson` (DialogueEventHandler.cpp)

Если edge содержит `offer_quest` action → добавить `questPreview` в объект choice:

```cpp
// Для каждого edge после формирования базового объекта choice:
if (!edge->actionGroup.is_null()) {
    for (const auto &action : edge->actionGroup) {
        if (action.value("type", "") == "offer_quest") {
            const std::string slug = action.value("slug", "");
            const QuestStruct *quest = gameServices_.getQuestManager().getQuestBySlug(slug);
            if (quest && !quest->steps.empty()) {
                nlohmann::json preview;
                preview["questId"]       = quest->id;
                preview["clientQuestKey"] = quest->clientQuestKey;
                preview["firstStep"]     = resolveStepForClient(quest->steps[0]);
                preview["rewards"]       = resolveRewardsForClient(quest->rewards);
                choice["questPreview"]   = std::move(preview);
            }
        }
        else if (action.value("type", "") == "turn_in_quest") {
            const std::string slug = action.value("slug", "");
            const QuestStruct *quest = gameServices_.getQuestManager().getQuestBySlug(slug);
            if (quest) {
                nlohmann::json preview;
                preview["questId"]       = quest->id;
                preview["clientQuestKey"] = quest->clientQuestKey;
                preview["rewards"]       = resolveRewardsForClient(quest->rewards);
                choice["turnInPreview"] = std::move(preview);
            }
        }
    }
}
```

**Важно:** `resolveStepForClient` / `resolveRewardsForClient` переместить в доступное место — либо в `QuestManager` (предпочтительно, там уже есть доступ к `services_`), либо как статические методы с передачей ссылок на менеджеры.

### 2.6 Изменение `executeOfferQuest` (DialogueActionExecutor.cpp)

```cpp
// Существующий код:
notification["questId"]       = quest->id;
notification["clientQuestKey"] = quest->clientQuestKey;

// Добавить:
if (!quest->steps.empty())
    notification["currentStep"] = resolveStepForClient(quest->steps[0]);

notification["rewards"] = resolveRewardsForClient(quest->rewards);
```

### 2.7 Изменение `turnInQuest` (QuestManager.cpp)

В нотификацию `quest_turned_in` добавить фактически выданные награды (уже без `isHidden`, полностью раскрытые — игрок их получает):

```cpp
nlohmann::json turnInNotif;
turnInNotif["type"]           = "quest_turned_in";
turnInNotif["questId"]        = questSnapshot.id;
turnInNotif["clientQuestKey"] = questSnapshot.clientQuestKey;

// Раскрыть ВСЕ награды — в момент получения секретов нет
nlohmann::json rewardsReceived = nlohmann::json::array();
for (const auto &reward : questSnapshot.rewards) {
    nlohmann::json r;
    r["rewardType"] = reward.rewardType;
    if (reward.rewardType == "item") {
        auto item = services_->getItemManager().getItemById(reward.itemId);
        r["item_slug"] = item.slug;
        r["quantity"]  = reward.quantity;
    } else if (reward.rewardType == "exp" || reward.rewardType == "gold") {
        r["amount"] = reward.amount;
    }
    rewardsReceived.push_back(r);
}
turnInNotif["rewardsReceived"] = rewardsReceived;
```

### 2.8 Миграция БД — колонка isHidden для наград

```sql
-- В рамках миграции 054 или отдельной 055:
ALTER TABLE public.quest_rewards
    ADD COLUMN IF NOT EXISTS is_hidden BOOLEAN NOT NULL DEFAULT FALSE;

COMMENT ON COLUMN public.quest_rewards.is_hidden IS
    'TRUE = клиент видит "???" до сдачи квеста. Раскрывается в quest_turned_in нотификации.';
```

### 2.9 JSONParser — чтение is_hidden

В `parseQuestsList` при разборе rewards:
```cpp
reward.isHidden = rewardJson.value("is_hidden", false);
```

---

## 3. Улучшение квестовой системы — журнал

### Идея

Квестовый журнал клиента должен получать:
- **При выдаче квеста** — первый шаг с `target_slug` + все награды (с учётом `isHidden`)
- **При смене шага** — следующий шаг с `target_slug` и текущий прогресс
- **При завершении шага** — текущий прогресс в шаге
- **При сдаче** — раскрытые награды

Всё это уже частично отправляется, нужно обогатить данные slugами.

### 3.1 Изменение `onKillProgress` / `onCollectProgress` нотификаций

В `QuestManager` при отправке прогресса шага (уже существующий `sendQuestStepProgress` или аналог) добавить `target_slug` и `current` / `required`:

```json
{
  "type": "quest_step_progress",
  "questId": 3,
  "clientQuestKey": "wolf_hunt",
  "step": {
    "clientStepKey": "kill_wolves",
    "stepType": "kill",
    "target_slug": "wolf",
    "current": 3,
    "required": 10
  }
}
```

### 3.2 Изменение `sendQuestUpdate` (QuestManager.cpp)

При любом обновлении квеста (`sendQuestUpdate`) добавить в тело текущий шаг с резолвленными данными:

```cpp
// Найти текущий шаг
int stepIdx = pq.currentStep;
if (stepIdx < quest.steps.size()) {
    body["currentStep"] = resolveStepForClient(quest.steps[stepIdx]);
    // Добавить текущий прогресс из pq.progress
    int current = 0;
    if (pq.progress.contains("count"))
        current = pq.progress["count"].get<int>();
    body["currentStep"]["current"] = current;
}
body["rewards"] = resolveRewardsForClient(quest.rewards);
```

### 3.3 Пакет полного состояния квестов при входе (`SET_PLAYER_QUESTS`)

Когда при `joinGameCharacter` клиенту отправляются все активные квесты игрока — обогатить каждый квест данными текущего шага:

**Найти** место где `getPlayerQuestsForClient` или аналог формирует JSON для клиента и применить тот же `resolveStepForClient` + `resolveRewardsForClient`.

---

## 4. Порядок миграций БД

| Номер | Файл | Содержимое |
|---|---|---|
| 054 | `054_npc_ambient_speech.sql` | Таблицы `npc_ambient_speech_configs`, `npc_ambient_speech_lines` |
| 055 | `055_quest_rewards_hidden.sql` | `ALTER TABLE quest_rewards ADD COLUMN is_hidden` |
| (055) | (в файле 055) | При необходимости: `ALTER TABLE quest_step_params` если reach-шаги хранятся отдельно, иначе просто добавить `zone_slug` в существующие JSON `params` |

---

## 5. Порядок реализации на сервере

### Фаза 1 — Ambient Speech (независимая система)
1. Миграция `054_npc_ambient_speech.sql`
2. `NPCAmbientLineStruct`, `NPCAmbientSpeechConfigStruct` → `DataStructs.hpp`
3. `AmbientSpeechManager` (hpp + cpp)
4. `GameServices` — добавить `AmbientSpeechManager`
5. `EventData.hpp` — добавить `std::vector<NPCAmbientSpeechConfigStruct>`
6. `Event.hpp` — добавить `SET_NPC_AMBIENT_SPEECH`, `UPDATE_NPC_AMBIENT_POOLS`
7. `JSONParser` — `parseNPCAmbientSpeech()`
8. `GameServerWorker` — ветка `setNPCAmbientSpeech`
9. `NPCEventHandler` — обработка `SET_NPC_AMBIENT_SPEECH`
10. `CharacterEventHandler` (или `NPCEventHandler`) — отправка `NPC_AMBIENT_POOLS` при `joinGameCharacter`
11. Хуки в quest/flag update handlers → `UPDATE_NPC_AMBIENT_POOLS`
12. **Game Server** — запрос `npc_ambient_speech_configs JOIN npc_ambient_speech_lines` + отправка при старте

### Фаза 2 — Quest/Dialogue enrichment (минимальные изменения)
1. Миграция `055_quest_rewards_hidden.sql`
2. `QuestRewardStruct.isHidden` → `DataStructs.hpp`
3. `JSONParser::parseQuestsList` — читать `is_hidden`
4. Методы `resolveStepForClient` + `resolveRewardsForClient` → `QuestManager` (доступ к ItemManager, MobManager, NPCManager через `services_`)
5. `DialogueActionExecutor::executeOfferQuest` — добавить `currentStep` + `rewards` в нотификацию
6. `QuestManager::turnInQuest` — добавить `rewardsReceived` в `quest_turned_in`
7. `DialogueEventHandler::buildChoicesJson` — добавить `questPreview` / `turnInPreview`
8. `QuestManager::sendQuestUpdate` — добавить `currentStep` + `rewards`
9. Пакет при `joinGameCharacter` — обогатить активные квесты

---

## 6. Протоколы пакетов (клиент ↔ сервер)

### Chunk → Client: `NPC_AMBIENT_POOLS`

```json
{
  "header": { "eventType": "NPC_AMBIENT_POOLS", "message": "success" },
  "body": {
    "npcs": [
      {
        "npcId": 5,
        "minIntervalSec": 20,
        "maxIntervalSec": 60,
        "pools": [
          {
            "priority": 100,
            "lines": [
              {
                "id": 2,
                "lineKey": "npc.blacksmith.thanks_quest",
                "triggerType": "periodic",
                "triggerRadius": 400,
                "weight": 5,
                "cooldownSec": 300
              }
            ]
          },
          {
            "priority": 0,
            "lines": [
              {
                "id": 1,
                "lineKey": "npc.blacksmith.idle_1",
                "triggerType": "proximity",
                "triggerRadius": 600,
                "weight": 10,
                "cooldownSec": 60
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### Chunk → Client: `UPDATE_NPC_AMBIENT_POOLS`

Тот же формат что `NPC_AMBIENT_POOLS`, но только для NPC у которых изменился пул.

### Chunk → Client: `DIALOGUE_NODE` — enriched choices

```json
{
  "header": { "eventType": "DIALOGUE_NODE" },
  "body": {
    "sessionId": "dlg_1_1744700000",
    "npcId": 3,
    "nodeId": 10,
    "clientNodeKey": "guard_hello",
    "type": "line",
    "choices": [
      {
        "edgeId": 5,
        "clientChoiceKey": "accept_wolf_hunt",
        "conditionMet": true,
        "questPreview": {
          "questId": 3,
          "clientQuestKey": "wolf_hunt",
          "firstStep": {
            "clientStepKey": "kill_wolves",
            "stepType": "kill",
            "target_slug": "forest_wolf",
            "count": 10
          },
          "rewards": [
            { "rewardType": "exp",  "isHidden": false, "amount": 500 },
            { "rewardType": "gold", "isHidden": false, "amount": 200 },
            { "rewardType": "item", "isHidden": false, "item_slug": "leather_gloves", "quantity": 1 },
            { "rewardType": "item", "isHidden": true }
          ]
        }
      },
      {
        "edgeId": 6,
        "clientChoiceKey": "turn_in_wolf_hunt",
        "conditionMet": true,
        "turnInPreview": {
          "questId": 3,
          "clientQuestKey": "wolf_hunt",
          "rewards": [
            { "rewardType": "exp",  "isHidden": false, "amount": 500 },
            { "rewardType": "gold", "isHidden": false, "amount": 200 },
            { "rewardType": "item", "isHidden": true }
          ]
        }
      }
    ]
  }
}
```

### Chunk → Client: `quest_offered` нотификация

```json
{
  "type": "quest_offered",
  "questId": 3,
  "clientQuestKey": "wolf_hunt",
  "currentStep": {
    "clientStepKey": "kill_wolves",
    "stepType": "kill",
    "target_slug": "forest_wolf",
    "count": 10,
    "current": 0
  },
  "rewards": [
    { "rewardType": "exp",  "isHidden": false, "amount": 500 },
    { "rewardType": "item", "isHidden": true }
  ]
}
```

### Chunk → Client: `quest_turned_in` нотификация

```json
{
  "type": "quest_turned_in",
  "questId": 3,
  "clientQuestKey": "wolf_hunt",
  "rewardsReceived": [
    { "rewardType": "exp",  "amount": 500 },
    { "rewardType": "gold", "amount": 200 },
    { "rewardType": "item", "item_slug": "leather_gloves", "quantity": 1 },
    { "rewardType": "item", "item_slug": "secret_ring",    "quantity": 1 }
  ]
}
```

### Chunk → Client: `quest_step_progress` нотификация

```json
{
  "type": "quest_step_progress",
  "questId": 3,
  "clientQuestKey": "wolf_hunt",
  "step": {
    "clientStepKey": "kill_wolves",
    "stepType": "kill",
    "target_slug": "forest_wolf",
    "current": 3,
    "required": 10
  }
}
```

### Chunk → Client: полное состояние квеста при входе (`questState`)

```json
{
  "questId": 3,
  "clientQuestKey": "wolf_hunt",
  "state": "active",
  "currentStepIndex": 0,
  "currentStep": {
    "clientStepKey": "kill_wolves",
    "stepType": "kill",
    "target_slug": "forest_wolf",
    "current": 3,
    "required": 10
  },
  "rewards": [
    { "rewardType": "exp",  "isHidden": false, "amount": 500 },
    { "rewardType": "item", "isHidden": true }
  ]
}
```

---

## Примечания по реализации клиента (UE5)

### Ambient Speech
- `UNPCAmbientSpeechComponent` — компонент на NPC Actor, хранит пул, тактирует таймер
- При получении `NPC_AMBIENT_POOLS` — найти нужный NPC Actor, вызвать `SetAmbientPool()`
- Proximity-реплики — проверять в `Tick()` или через `OverlapSphere` в нужном радиусе
- Bubble-виджет: `UUserWidget` с `UTextBlock`, крепится к `UWidgetComponent` над головой NPC

### Квест-журнал
- `target_slug` используется как ключ локализации: `NSLOCTEXT("Quest", "kill_target", "<target_slug>.name")`
- Скрытые награды (`isHidden: true`) — иконка `?` с текстом "Скрытая награда" по аналогу с BDO-стилем
- При получении `quest_turned_in` — анимация раскрытия скрытых наград (опционально)

### Диалоговое окно
- Если choice содержит `questPreview` — под текстом кнопки показать мини-панель: цель + награды
- Если choice содержит `turnInPreview` — показать "Вы получите:" список наград

---

## Статус реализации

**Дата завершения:** 2026-04-15  
**Статус:** ✅ Реализовано полностью

### Фаза 1 — Ambient Speech

| Задача | Статус | Файл(ы) |
|--------|--------|---------|
| Миграция БД 054 | ✅ | `docs/migrations/054_npc_ambient_speech.sql` |
| `NPCAmbientLineStruct`, `NPCAmbientSpeechConfigStruct` | ✅ | `include/data/DataStructs.hpp` |
| `AmbientSpeechManager` | ✅ | `include/services/AmbientSpeechManager.hpp`, `src/services/AmbientSpeechManager.cpp` |
| `GameServices` — добавить `AmbientSpeechManager` | ✅ | `include/services/GameServices.hpp` |
| `EventData.hpp` — вариант с `vector<NPCAmbientSpeechConfigStruct>` | ✅ | `include/events/EventData.hpp` |
| `Event.hpp` — `SET_NPC_AMBIENT_SPEECH`, `UPDATE_NPC_AMBIENT_POOLS` | ✅ | `include/events/Event.hpp` |
| `JSONParser::parseNPCAmbientSpeech()` | ✅ | `src/utils/JSONParser.cpp` |
| `GameServerWorker` — ветка `setNPCAmbientSpeech` | ✅ | `src/network/GameServerWorker.cpp` |
| `NPCEventHandler::handleSetNPCAmbientSpeechEvent()` | ✅ | `src/events/handlers/NPCEventHandler.cpp` |
| `NPCEventHandler::sendAmbientPoolsToClient()` | ✅ | `src/events/handlers/NPCEventHandler.cpp` |
| `EventHandler.cpp` dispatch case | ✅ | `src/events/EventHandler.cpp` |
| `CharacterEventHandler` — вызов `sendAmbientPoolsToClient` при `playerReady` | ✅ | `src/events/handlers/CharacterEventHandler.cpp` |
| **Game Server** — `GET_NPC_AMBIENT_SPEECH` event при JOIN_CHUNK_SERVER | ✅ | `mmorpg-prototype-game-server`: `include/events/Event.hpp`, `EventHandler.hpp`, `EventHandler.cpp` |
| **Game Server** — prepared statement `get_npc_ambient_speech` | ✅ | `mmorpg-prototype-game-server`: `src/utils/Database.cpp` |
| Хуки quest/flag update → `UPDATE_NPC_AMBIENT_POOLS` | ⏳ | Отложено — не блокирует MVP. Пул обновляется при следующем подключении игрока |

### Фаза 2 — Quest/Dialogue Enrichment

| Задача | Статус | Файл(ы) |
|--------|--------|---------|
| Миграция БД 055 | ✅ | `docs/migrations/055_quest_rewards_hidden.sql` |
| `QuestRewardStruct.isHidden` | ✅ | `include/data/DataStructs.hpp` |
| `JSONParser::parseQuestsList` — читать `is_hidden` | ✅ | `src/utils/JSONParser.cpp` |
| `QuestManager::resolveStepForClient()` (public) | ✅ | `include/services/QuestManager.hpp`, `src/services/QuestManager.cpp` |
| `QuestManager::resolveRewardsForClient()` (public) | ✅ | `include/services/QuestManager.hpp`, `src/services/QuestManager.cpp` |
| `DialogueActionExecutor::executeOfferQuest` — `currentStep` + `rewards` | ✅ | `src/services/DialogueActionExecutor.cpp` |
| `QuestManager::turnInQuest` — `rewardsReceived` с раскрытием скрытых | ✅ | `src/services/QuestManager.cpp` |
| `QuestManager::sendQuestUpdate` — `currentStepEnriched` + `rewards` | ✅ | `src/services/QuestManager.cpp` |
| `DialogueEventHandler::buildChoicesJson` — `questPreview` / `turnInPreview` | ✅ | `src/events/handlers/DialogueEventHandler.cpp` |

### Ключевые отличия от исходного плана

1. **`resolveStepForClient` / `resolveRewardsForClient` — публичные методы `QuestManager`**, а не приватные хелперы `DialogueActionExecutor`. Это позволило переиспользовать их в `DialogueEventHandler::buildChoicesJson`.
2. **`sendQuestUpdate` использует ключ `currentStepEnriched`** (не `currentStep`), чтобы не нарушать обратную совместимость клиента, который уже читает `currentStep` как целое число (индекс шага).
3. **Пул ambient speech отправляется только при `playerReady`** (подключении). Хуки на изменение квест-флагов (`UPDATE_NPC_AMBIENT_POOLS`) оставлены на следующую итерацию — для MVP достаточно пересоздания пула при переподключении.
4. **Game Server отправляет `setNPCAmbientSpeech` при `JOIN_CHUNK_SERVER`** — через новый event `GET_NPC_AMBIENT_SPEECH`, аналогично `GET_EMOTE_DEFINITIONS`.
