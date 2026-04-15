# NPC Ambient Speech — Protocol Reference

Документ описывает систему фоновых реплик NPC: DB-схему, поток событий между серверами,
пакеты Chunk Server ↔ Client и примеры JSON на основе фактического кода.

**Версия реализации:** v1.0.0  
**Дата:** 2026-04-15  
**Миграции:** `054_npc_ambient_speech.sql`

---

## Содержание

1. [Обзор системы](#1-обзор-системы)
2. [DB-схема](#2-db-схема)
3. [Поток событий](#3-поток-событий)
4. [AmbientSpeechManager — серверная логика](#4-ambientspeechmanager--серверная-логика)
5. [Пакеты](#5-пакеты)
6. [Условия на реплики](#6-условия-на-реплики)
7. [Примеры JSON](#7-примеры-json)

---

## 1. Обзор системы

NPC может произносить реплики над головой **вне диалога**. Сервер фильтрует реплики
индивидуально для каждого игрока через `DialogueConditionEvaluator` и отдаёт готовый пул.
Клиент сам тактирует таймер, принимает решение о показе и рендерит bubble-виджет.

**Два режима активации реплики (поле `triggerType`):**

| `triggerType` | Описание |
|---------------|----------|
| `periodic`    | Клиент показывает реплику по таймеру пока игрок в зоне (`triggerRadius` не используется) |
| `proximity`   | Клиент показывает реплику **один раз** при первом приближении игрока на расстояние ≤ `triggerRadius` |

**Система приоритетов внутри пула:**

Реплики сгруппированы по `priority`. Клиент сначала выбирает реплику из пула с наивысшим
`priority` (weighted random по полю `weight`). Пул с меньшим приоритетом используется
только если перешли к следующей группе (клиентская логика). Таким образом, временные
квест-специфичные реплики (высокий priority) вытесняют базовые idle-реплики (priority 0).

---

## 2. DB-схема

```sql
-- migration 054_npc_ambient_speech.sql

CREATE TABLE public.npc_ambient_speech_configs (
    id               SERIAL PRIMARY KEY,
    npc_id           INT    NOT NULL REFERENCES public.npc(id) ON DELETE CASCADE,
    min_interval_sec INT    NOT NULL DEFAULT 20,
    max_interval_sec INT    NOT NULL DEFAULT 60,
    CONSTRAINT uq_npc_ambient_config UNIQUE (npc_id)
);

CREATE TABLE public.npc_ambient_speech_lines (
    id              SERIAL       PRIMARY KEY,
    npc_id          INT          NOT NULL REFERENCES public.npc(id) ON DELETE CASCADE,
    line_key        VARCHAR(128) NOT NULL,  -- ключ локализации, e.g. "npc.blacksmith.idle_1"
    trigger_type    VARCHAR(16)  NOT NULL DEFAULT 'periodic',  -- 'periodic' | 'proximity'
    trigger_radius  INT          NOT NULL DEFAULT 400,         -- единицы мира, только для proximity
    priority        INT          NOT NULL DEFAULT 0,
    weight          INT          NOT NULL DEFAULT 10,          -- weighted random внутри priority-пула
    cooldown_sec    INT          NOT NULL DEFAULT 60,          -- клиентский cooldown между показами реплики
    condition_group JSONB                 DEFAULT NULL         -- null = показывать всегда
);

CREATE INDEX idx_ambient_lines_npc_id ON public.npc_ambient_speech_lines(npc_id);
```

**Пример данных:**

```sql
-- Кузнец: idle-реплики + квест-специфичная реплика
INSERT INTO npc_ambient_speech_configs (npc_id, min_interval_sec, max_interval_sec)
VALUES (5, 20, 60);

-- base idle (priority 0, всегда показывается)
INSERT INTO npc_ambient_speech_lines (npc_id, line_key, trigger_type, priority, weight, cooldown_sec)
VALUES (5, 'npc.blacksmith.idle_1', 'periodic', 0, 10, 60);

-- proximity greeting (priority 0, однократно при подходе)
INSERT INTO npc_ambient_speech_lines (npc_id, line_key, trigger_type, trigger_radius, priority, weight, cooldown_sec)
VALUES (5, 'npc.blacksmith.proximity_greet', 'proximity', 600, 0, 5, 300);

-- квест-благодарность (priority 100, только если квест wolf_hunt сдан)
INSERT INTO npc_ambient_speech_lines (
    npc_id, line_key, trigger_type, priority, weight, cooldown_sec, condition_group
) VALUES (
    5, 'npc.blacksmith.thanks_quest', 'periodic', 100, 10, 300,
    '{"type": "quest", "slug": "wolf_hunt", "state": "turned_in"}'
);
```

---

## 3. Поток событий

### Загрузка при подключении Chunk Server

```
Game Server (PostgreSQL)           Chunk Server
        │                               │
        │◄─── [TCP connect] ────────────│  (joinChunkServer)
        │                               │
        ├── GET_NPC_AMBIENT_SPEECH ─────►handleGetNPCAmbientSpeechEvent
        │   (запускается вместе с         │  ↓ SELECT ... FROM npc_ambient_speech_configs
        │    GET_EMOTE_DEFINITIONS и      │    JOIN npc_ambient_speech_lines
        │    остальными startup-событиями)│
        │                               │
        │── setNPCAmbientSpeech ─────────►GameServerWorker
        │   eventType в header           │  ↓ parseNPCAmbientSpeech()
        │                               │  ↓ Event::SET_NPC_AMBIENT_SPEECH
        │                               │  ↓ NPCEventHandler::handleSetNPCAmbientSpeechEvent()
        │                               │  ↓ AmbientSpeechManager::setAmbientSpeechData()
```

### Отправка клиенту при входе персонажа

```
Client                           Chunk Server
  │                                   │
  │── joinGameCharacter ──────────────►
  │   (или playerReady)               │
  │                                   ├── (NPC spawn)
  │                                   │   sendNPCSpawnDataToClient()
  │                                   │
  │                                   ├── NPCEventHandler::sendAmbientPoolsToClient()
  │                                   │   1. Получить список NPC в радиусе 50000 ед.
  │                                   │   2. Для каждого NPC с ambient config:
  │                                   │      buildFilteredPoolForPlayer(npcId, PlayerContextStruct)
  │                                   │      └─ DialogueConditionEvaluator::evaluate() per line
  │                                   │   3. Собрать массив → NPC_AMBIENT_POOLS
  │◄── NPC_AMBIENT_POOLS ─────────────│
```

### Пакет от Game Server к Chunk Server при старте

Отправляется как JSON внутри TCP-соединения сразу после регистрации chunk-сервера.
`clientId` = ID chunk-сервера.

```json
{
  "header": {
    "message": "NPC ambient speech configs",
    "hash": "",
    "clientId": 1,
    "eventType": "setNPCAmbientSpeech"
  },
  "body": {
    "ambientSpeech": [
      {
        "npcId": 5,
        "minIntervalSec": 20,
        "maxIntervalSec": 60,
        "lines": [
          {
            "id": 1,
            "lineKey": "npc.blacksmith.idle_1",
            "triggerType": "periodic",
            "triggerRadius": 400,
            "priority": 0,
            "weight": 10,
            "cooldownSec": 60,
            "conditionGroup": null
          },
          {
            "id": 2,
            "lineKey": "npc.blacksmith.thanks_quest",
            "triggerType": "periodic",
            "triggerRadius": 400,
            "priority": 100,
            "weight": 10,
            "cooldownSec": 300,
            "conditionGroup": { "type": "quest", "slug": "wolf_hunt", "state": "turned_in" }
          }
        ]
      }
    ]
  }
}
```

---

## 4. AmbientSpeechManager — серверная логика

**Класс:** `AmbientSpeechManager` (`include/services/AmbientSpeechManager.hpp`)  
**Thread-safety:** `std::shared_mutex` (read-heavy)  
**Хранение:** `std::unordered_map<int, NPCAmbientSpeechConfigStruct> configs_` — индекс по `npcId`

### `buildFilteredPoolForPlayer(npcId, ctx)`

1. Получает конфиг NPC из `configs_`
2. Для каждой реплики: если `conditionGroup` не null → вызывает `DialogueConditionEvaluator::evaluate()`
3. Прошедшие условие реплики группируются по `priority` в убывающем порядке (`std::map<int, …, std::greater<int>>`)
4. Возвращает JSON-объект NPC с массивом `pools`; если ни одна реплика не прошла условие — возвращает пустой `json{}`

### `buildFilteredPoolsForPlayer(npcIds, ctx)`

Вызывает `buildFilteredPoolForPlayer` для каждого NPC из списка. NPC у которых пустой конфиг или все реплики заблокированы условием — пропускаются. Возвращает JSON-массив.

### Заполнение `PlayerContextStruct`

`sendAmbientPoolsToClient` заполняет контекст аналогично диалоговому условий:

| Поле | Источник |
|------|---------|
| `characterId` | `CharacterManager::getCharacterData()` |
| `characterLevel` | `CharacterManager::getCharacterData()` |
| `freeSkillPoints` | `CharacterManager::getCharacterData()` |
| `flagsBool` / `flagsInt` | `charData.flags` (bool/int флаги персонажа) |
| `questStates` / `questCurrentStep` / `questProgress` | `QuestManager::fillQuestContext()` |
| `flagsInt["item_<id>"]` | `InventoryManager::getPlayerInventory()` |
| Репутация | `ReputationManager::fillReputationContext()` |
| Мастерство | `MasteryManager::fillMasteryContext()` |
| `learnedSkillSlugs` | `charData.skills` |

---

## 5. Пакеты

### `NPC_AMBIENT_POOLS` — пул реплик для клиента

**Направление:** Chunk Server → Client  
**Когда:** Сразу после `spawnNPCs` при `playerReady`

```json
{
  "header": {
    "message": "success",
    "eventType": "NPC_AMBIENT_POOLS",
    "clientId": 7
  },
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
                "weight": 10,
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
                "triggerType": "periodic",
                "triggerRadius": 400,
                "weight": 10,
                "cooldownSec": 60
              },
              {
                "id": 3,
                "lineKey": "npc.blacksmith.proximity_greet",
                "triggerType": "proximity",
                "triggerRadius": 600,
                "weight": 5,
                "cooldownSec": 300
              }
            ]
          }
        ]
      }
    ]
  }
}
```

**Поля NPC-объекта:**

| Поле | Тип | Описание |
|------|-----|----------|
| `npcId` | int | ID NPC |
| `minIntervalSec` | int | Минимальный интервал между репликами (клиентский таймер) |
| `maxIntervalSec` | int | Максимальный интервал между репликами |
| `pools` | array | Массив пулов, отсортированных по `priority` по убыванию |

**Поля pool-объекта:**

| Поле | Тип | Описание |
|------|-----|----------|
| `priority` | int | Приоритет пула. Клиент сначала выбирает из пула с наивысшим priority |
| `lines` | array | Реплики внутри пула |

**Поля line-объекта:**

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | ID реплики в БД |
| `lineKey` | string | Ключ локализации, e.g. `npc.blacksmith.idle_1` |
| `triggerType` | string | `periodic` или `proximity` |
| `triggerRadius` | int | Радиус (единицы мира) — релевантен только для `proximity` |
| `weight` | int | Вес в weighted random (используется клиентом при выборе реплики из пула) |
| `cooldownSec` | int | Клиентский cooldown: не показывать эту реплику снова раньше чем через N секунд |

> **Важно:** `NPC_AMBIENT_POOLS` содержит только реплики, условия которых прошли проверку
> для этого конкретного игрока. Клиент не делает никакой фильтрации.

---

## 6. Условия на реплики

Поле `condition_group` в строке таблицы `npc_ambient_speech_lines` поддерживает
**те же условия** что и диалоговый граф (`DialogueConditionEvaluator`):

| Пример | Описание |
|--------|----------|
| `null` | Реплика всегда видна |
| `{"type": "quest", "slug": "wolf_hunt", "state": "turned_in"}` | Только если квест сдан |
| `{"type": "quest", "slug": "wolf_hunt", "state": "active"}` | Только если квест активен |
| `{"type": "flag", "key": "met_king", "eq": true}` | Только если флаг установлен |
| `{"type": "level", "gte": 10}` | Только если уровень ≥ 10 |
| `{"all": [{"type": "quest", "slug": "Q", "state": "active"}, {"type": "level", "gte": 5}]}` | Оба условия сразу |

Полный список поддерживаемых условий — см. [npc-dialogue-quest-trade.md](./npc-dialogue-quest-trade.md), раздел *3. Диалоги → Условия*.

---

## 7. Примеры JSON

### Пример: NPC без ambient config или все реплики заблокированы

Если у NPC нет конфига или ни одна реплика не прошла условие — NPC просто отсутствует
в массиве `body.npcs`. Клиент не получает записи для такого NPC.

### Пример: только proximity-реплика для новичка

NPC приветствует игрока при первом приближении, никаких периодических реплик:

```json
{
  "npcId": 12,
  "minIntervalSec": 30,
  "maxIntervalSec": 90,
  "pools": [
    {
      "priority": 0,
      "lines": [
        {
          "id": 7,
          "lineKey": "npc.guard.proximity_warning",
          "triggerType": "proximity",
          "triggerRadius": 500,
          "weight": 10,
          "cooldownSec": 600
        }
      ]
    }
  ]
}
```

### Пример: логика клиента (UE5 псевдокод)

```
// При получении NPC_AMBIENT_POOLS:
for each npcData in packet.npcs:
    actor = FindNPCActor(npcData.npcId)
    actor.AmbientSpeechComponent.SetPool(npcData)

// В AmbientSpeechComponent.Tick():
if (timeSinceLastLine >= Random(minIntervalSec, maxIntervalSec)):
    highestPriorityPool = pools[0]  // уже отсортированы сервером
    line = WeightedRandom(highestPriorityPool.lines)
    if (Now() - line.lastShownAt >= line.cooldownSec):
        ShowBubble(line.lineKey)
        line.lastShownAt = Now()
        timeSinceLastLine = 0

// Для proximity в BeginOverlap или OnTick:
for each line in all pools where triggerType == "proximity":
    dist = Distance(PlayerPawn, NPC)
    if (dist <= line.triggerRadius && !line.wasTriggeredThisSession):
        ShowBubble(line.lineKey)
        line.wasTriggeredThisSession = true
```

---

## Ограничения и планы

| Функциональность | Статус | Примечание |
|-----------------|:------:|------------|
| Загрузка данных из БД при старте chunk-сервера | ✅ | Полностью |
| Фильтрация по условиям при `playerReady` | ✅ | Полностью |
| Отправка `NPC_AMBIENT_POOLS` при входе персонажа | ✅ | Полностью |
| Обновление пула при изменении квеста/флага (`UPDATE_NPC_AMBIENT_POOLS`) | ⏳ | Запланировано. Сейчас пул пересчитывается при следующем подключении игрока |
| Proximity-реплики — серверная проверка расстояния | ⏳ | Клиентская ответственность. Сервер только фильтрует условия, не координаты |
