# Документация: NPC, Диалоги, Квесты, Торговля

**Актуальная версия:** v0.0.5  
**Дата обновления:** 2026-04-15  
**Серверная архитектура:** Login Server → Game Server ↔ Chunk Server ↔ Client

---

## Содержание

1. [Общая архитектура](#1-общая-архитектура)
2. [NPC](#2-npc)
3. [Диалоги](#3-диалоги)
4. [Квесты](#4-квесты)
5. [Валюта — Gold Coin](#5-валюта--gold-coin)
6. [Торговля](#6-торговля)
7. [Сводная таблица пакетов](#7-сводная-таблица-пакетов)
8. [Примеры полных JSON-пакетов](#8-примеры-полных-json-пакетов)
9. [Ограничения и статус реализации](#9-ограничения-и-статус-реализации)

> **v0.0.5 изменения:** Обогащение choices (`questPreview`/`turnInPreview`), обогащение `quest_offered` и `quest_turned_in`, поле `currentStepEnriched` и `rewards` в `QUEST_UPDATE`, поддержка скрытых наград (`isHidden`). NPC Ambient Speech — отдельный документ: [npc-ambient-speech-protocol.md](./npc-ambient-speech-protocol.md).

---

## 1. Общая архитектура

Система работает на двух серверах:

**Game Server** — источник истины. Хранит все данные в PostgreSQL: шаблоны NPC, диалоги, квесты, прогресс игроков, флаги. При запуске отдаёт Chunk Server'у весь массив данных. Принимает изменения от Chunk Server'а и сохраняет в БД.

**Chunk Server** — движок реального времени. Держит всё в памяти. Обрабатывает запросы клиентов, управляет сессиями диалогов, считает прогресс квестов, хранит флаги. Каждые **5 секунд** сбрасывает изменения обратно в Game Server.

Клиент общается **только** с Chunk Server'ом.

```
Game Server (PostgreSQL)
       ↕  (startup bulk-load)
Chunk Server (in-memory)
       ↕  (websocket / TCP)
     Client
```

---

## 2. NPC

### Данные NPC

Каждый NPC имеет:

| Поле             | Тип    | Описание |
|------------------|--------|----------|
| `id`             | int    | Уникальный ID |
| `name`           | string | Отображаемое имя |
| `slug`           | string | Машиночитаемый ключ |
| `raceName`       | string | Раса |
| `level`          | int    | Уровень |
| `npcType`        | string | `general`, `quest_giver`, `vendor`, `blacksmith`, `guard`, `trainer` |
| `isInteractable` | bool   | Можно ли взаимодействовать |
| `radius`         | int    | Радиус взаимодействия (по умолчанию 200 единиц мира) |
| `dialogueId`     | string | ID диалогового дерева ("" = нет) |
| `quests[]`       | array  | Квесты NPC, каждый с per-player `status` (`available` / `in_progress` / `completable` / `turned_in` / `failed`) |
| `position`       | object | Координаты и поворот в мире |
| `currentHealth`  | int    | Текущее HP |
| `currentMana`    | int    | Текущая мана |
| `attributes[]`   | array  | Атрибуты (сила, ловкость...) |

### NPC спавн при входе в зону

Когда персонаж входит в зону, Chunk Server автоматически отправляет все NPC в радиусе 1000 единиц.

**Направление:** Chunk Server → Client  
**Тип события:** `spawnNPCs`

```json
{
  "header": {
    "message": "NPCs spawn data for area",
    "eventType": "spawnNPCs",
    "clientId": 42
  },
  "body": {
    "npcCount": 2,
    "spawnRadius": 50000.0,
    "npcsSpawn": [
      {
        "id": 42,
        "name": "Милая",
        "slug": "milaya",
        "race": "Human",
        "level": 1,
        "npcType": "quest_giver",
        "isInteractable": true,
        "dialogueId": "1",
        "quests": [
          { "slug": "wolf_hunt_intro", "status": "available" }
        ],
        "stats": {
          "health": { "current": 100, "max": 100 },
          "mana":   { "current": 50,  "max": 50  }
        },
        "position": { "x": 2200.0, "y": 1120.0, "z": 200.0, "rotationZ": 145.0 },
        "attributes": [
          { "id": 1, "name": "Strength", "slug": "strength", "value": 10 }
        ]
      },
      {
        "id": 1,
        "name": "Варан",
        "slug": "varan",
        "race": "Human",
        "level": 1,
        "npcType": "general",
        "isInteractable": true,
        "dialogueId": "2",
        "quests": [],
        "stats": {
          "health": { "current": 100, "max": 100 },
          "mana":   { "current": 10, "max": 10 }
        },
        "position": { "x": 585.0, "y": -3300.0, "z": 200.0, "rotationZ": -40.0 },
        "attributes": []
      }
    ]
  }
}
```

---

## 3. Диалоги

### Концепция диалогового графа

Диалог — это **граф узлов и рёбер**.

**Типы узлов:**

| Тип           | Видим игроку | Описание |
|---------------|:---:|----------|
| `line`        | ✓  | Реплика NPC. Игрок нажимает "Далее" |
| `choice_hub`  | ✓  | Выбор игрока. Несколько кнопок-ответов |
| `action`      | ✗  | Выполняет действие (выдать квест, поставить флаг), переходит дальше автоматически |
| `jump`        | ✗  | Телепортирует диалог на другой узел |
| `end`         | ✗  | Завершение диалога |

Сервер **автоматически** проходит `action` и `jump` узлы без участия игрока.

### Условия (`conditionGroup`)

Каждый узел и каждое ребро-выбор могут иметь условие. Примеры:

```json
{ "all": [
    { "type": "level", "gte": 5 },
    { "type": "quest", "slug": "wolf_hunt", "state": "not_started" }
]}
```

```json
{ "type": "flag", "key": "met_king", "eq": true }
```

```json
{ "type": "item", "item_id": 16, "gte": 50 }
```

```json
{ "type": "quest_step", "slug": "wolf_hunt_intro", "step": 1 }
```

```json
{ "all": [
    { "type": "quest", "slug": "wolf_hunt_intro", "state": "active" },
    { "type": "quest_step", "slug": "wolf_hunt_intro", "step": 0 }
]}
```

**Поддерживаемые типы условий:**

| `type`       | Параметры | Описание |
|--------------|-----------|----------|
| `flag`       | `key`, `eq`/`gte`/`lte`/`gt`/`lt` | Проверка булева или числового флага игрока |
| `quest`      | `slug`, `state` | Проверка состояния квеста (`active`, `completed`, `turned_in`, `not_started`, `failed`) |
| `quest_step` | `slug`, `step`/`eq`/`gte`/`lte`/`gt`/`lt` | Проверка текущего шага квеста (0-based). Если квест не начат — шаг равен −1 |
| `level`      | `gte`/`lte`/`eq`/`gt`/`lt` | Проверка уровня персонажа |
| `item`       | `item_id`, `gte`/`lte`/`eq` | Проверка количества предмета в инвентаре |

Логические операторы: `{ "all": [...] }`, `{ "any": [...] }`, `{ "not": {...} }`.

### Действия (`actionGroup`)

Действия могут быть на узле или на ребре-выборе. Поддерживаемые типы:

| `type`               | Параметры | Описание |
|----------------------|-----------|----------|
| `set_flag`           | `key`, `bool_value`/`int_value`/`inc` | Установить или изменить флаг игрока |
| `offer_quest`        | `slug` | Выдать квест |
| `turn_in_quest`      | `slug` | Сдать квест и получить награды |
| `fail_quest`         | `slug` | Провалить квест (переводит в состояние `failed`) |
| `advance_quest_step` | `slug` | Принудительно перейти к следующему шагу квеста |
| `give_item`          | `item_id`, `quantity` | Выдать предмет |
| `give_exp`           | `amount` | Выдать опыт |
| `give_gold`          | `amount` | Выдать золото (Gold Coin, item ID 16) |

### Выбор диалога для NPC

У одного NPC может быть несколько диалогов с разными приоритетами и условиями. Сервер перебирает от наибольшего приоритета к наименьшему и запускает первый подходящий.

**Пример:** У Гримора три диалога:
- Приоритет 10: "Сдача квеста на волков" — условие: `wolf_hunt` = `completed`
- Приоритет 5: "Выдача квеста на волков" — условие: `wolf_hunt` = `not_started`, уровень ≥ 5
- Приоритет 1: "Общий разговор" — без условий

### Сессия диалога

- Создаётся при `npcInteract`
- ID: `"dlg_{clientId}_{timestamp_ms}"`
- TTL: **300 секунд** без активности
- Одновременно у игрока только **одна** активная сессия

---

### Пакеты диалога

#### `npcInteract` — взаимодействие с NPC

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "npcInteract", "clientId": 7 },
  "body": { "npcId": 42, "characterId": 101 }
}
```

**Возможные ошибки** (`dialogueError`):

| `errorCode`              | Причина |
|--------------------------|---------|
| `NPC_NOT_FOUND`          | NPC не существует |
| `OUT_OF_RANGE`           | Персонаж слишком далеко (> `npc.radius`) |
| `NO_DIALOGUE`            | Нет подходящего диалога (условия не соблюдены) |
| `BLOCKED_BY_REPUTATION`  | Репутация персонажа с фракцией NPC < −500 (`enemy`). Тело ошибки дополнительно содержит поле `factionSlug`. |
| `SESSION_EXPIRED`        | Сессия диалога не найдена или истёк TTL (при `dialogueChoice`) |
| `CHOICE_LOCKED`          | Выбранная ветка заблокирована условием (при `dialogueChoice`) |

---

#### `DIALOGUE_NODE` — узел диалога

**Направление:** Chunk Server → Client

```json
{
  "header": {
    "message": "success",
    "eventType": "DIALOGUE_NODE",
    "clientId": 7
  },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "npcId": 42,
    "nodeId": 15,
    "clientNodeKey": "npc.grimor.quest_intro",
    "type": "choice_hub",
    "speakerNpcId": 42,
    "choices": [
      {
        "edgeId": 8,
        "clientChoiceKey": "npc.grimor.accept_quest",
        "conditionMet": true,
        "questPreview": {
          "questId": 5,
          "clientQuestKey": "quest.wolf_hunt",
          "firstStep": {
            "clientStepKey": "quest.wolf_hunt.step_0",
            "stepType": "kill",
            "target_slug": "forest_wolf",
            "count": 5,
            "current": 0
          },
          "rewards": [
            { "rewardType": "exp",  "isHidden": false, "amount": 500 },
            { "rewardType": "gold", "isHidden": false, "amount": 100 },
            { "rewardType": "item", "isHidden": false, "item_slug": "wooden_staff", "quantity": 1 },
            { "rewardType": "item", "isHidden": true }
          ]
        }
      },
      {
        "edgeId": 9,
        "clientChoiceKey": "npc.grimor.decline_quest",
        "conditionMet": true
      }
    ]
  }
}
```

Поле `questPreview` присутствует в choice если ребро содержит действие `offer_quest`.
Поле `turnInPreview` присутствует если ребро содержит `turn_in_quest` (структура аналогична, без `firstStep`).

Поле `choices` присутствует для **всех** типов узлов. Для `line`-узлов содержит одно ребро — «Далее». Клиент должен передать его `edgeId` в `dialogueChoice` для продвижения диалога.

---

#### `dialogueChoice` — выбор варианта

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "dialogueChoice", "clientId": 7 },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "edgeId": 8,
    "characterId": 101
  }
}
```

**Ошибка при заблокированном выборе:** `CHOICE_LOCKED`

---

#### `dialogueClose` — закрытие диалога

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "dialogueClose", "clientId": 7 },
  "body": { "sessionId": "dlg_7_1709834400000", "characterId": 101 }
}
```

---

#### `DIALOGUE_CLOSE` — диалог завершён

**Направление:** Chunk Server → Client

```json
{
  "header": { "eventType": "DIALOGUE_CLOSE", "clientId": 7 },
  "body": { "sessionId": "dlg_7_1709834400000" }
}
```

Отправляется при: достижении `end`-узла, закрытии игроком, истечении TTL.

---

#### `dialogueError` — ошибка взаимодействия

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "Too far from NPC", "eventType": "dialogueError", "clientId": 7 },
  "body": { "errorCode": "OUT_OF_RANGE" }
}
```

---

### Полный пример: диалог с выдачей квеста

> Игрок Алексей (уровень 7) подходит к страже Гримору и нажимает взаимодействие.

**1. Запрос:**
```json
{ "header": { "eventType": "npcInteract" }, "body": { "npcId": 42, "characterId": 101 } }
```

**2. Сервер проверяет:** NPC существует, `isInteractable=true`, дистанция 150 < 200 — ОК.  
Диалог "Выдача квеста на волков" (приоритет 5) подходит: `wolf_hunt not_started` ✓, уровень 7 ≥ 5 ✓.  
Сессия создана, граф проходится до первого `line`-узла.

**3. Ответ — первая реплика:**
```json
{
  "header": { "eventType": "DIALOGUE_NODE" },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "npcId": 42, "nodeId": 10,
    "clientNodeKey": "npc.grimor.greeting",
    "type": "line",
    "speakerNpcId": 42,
    "choices": [
      { "edgeId": 5, "clientChoiceKey": "npc.grimor.continue", "conditionMet": true }
    ]
  }
}
```

**4. Игрок нажимает «Далее» (отправляет edgeId из choices):**
```json
{ "header": { "eventType": "dialogueChoice" }, "body": { "sessionId": "dlg_7_1709834400000", "edgeId": 5, "characterId": 101 } }
```

**5. Ответ — выбор:**
```json
{
  "header": { "eventType": "DIALOGUE_NODE" },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "npcId": 42, "nodeId": 15,
    "clientNodeKey": "npc.grimor.quest_offer",
    "type": "choice_hub",
    "choices": [
      { "edgeId": 8, "clientChoiceKey": "npc.grimor.accept", "conditionMet": true },
      { "edgeId": 9, "clientChoiceKey": "npc.grimor.decline", "conditionMet": true }
    ]
  }
}
```

**6. Игрок нажимает "Принять задание":**
```json
{ "header": { "eventType": "dialogueChoice" }, "body": { "sessionId": "dlg_7_1709834400000", "edgeId": 8, "characterId": 101 } }
```

**7. Сервер выполняет `actionGroup` ребра:** `offer_quest("wolf_hunt")` → квест принят.

**8. Ответы (уведомления + закрытие):**
```json
{
  "header": { "eventType": "quest_offered" },
  "body": {
    "questId": 5,
    "clientQuestKey": "quest.wolf_hunt",
    "currentStep": {
      "clientStepKey": "quest.wolf_hunt.step_0",
      "stepType": "kill",
      "target_slug": "forest_wolf",
      "count": 5,
      "current": 0
    },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": true }
    ]
  }
}
```
```json
{ "header": { "eventType": "DIALOGUE_CLOSE" }, "body": { "sessionId": "dlg_7_1709834400000" } }
```

---

## 4. Квесты

### Структура квеста

Квест состоит из **шагов** (последовательных задач) и **наград** (выдаются при сдаче).

**Типы шагов:**

| `stepType` | `progress` JSON  | `required` JSON                          | Когда закрывается               |
|------------|------------------|------------------------------------------|---------------------------------|
| `kill`     | `{"killed": N}`  | `{"mob_id": N, "count": N}`              | При убийстве нужных мобов       |
| `collect`  | `{"have": N}`    | `{"item_id": N, "count": N}`             | При накоплении предметов        |
| `talk`     | `{"done": bool}` | `{"npc_id": N}`                          | В момент открытия диалога с NPC |
| `reach`    | `{"done": bool}` | `{"x": f, "y": f, "radius": f}`          | При входе в радиус точки        |
| `custom`   | произвольный     | произвольный                             | Принудительно через `advance_quest_step` |

Каждый шаг имеет поле `completionMode`: `"auto"` — шаг закрывается автоматически при выполнении условия; `"manual"` — только через явное действие диалога `advance_quest_step`.

**Типы наград:**

| `rewardType` | Поля        | Описание |
|--------------|-------------|----------|
| `exp`        | `amount`    | Опыт персонажа |
| `item`       | `item_id`, `quantity` | Предмет в инвентарь |
| `gold`       | `amount`    | Монеты Gold Coin в инвентарь |

**Скрытые награды (`isHidden`):**

Любая награда может иметь `is_hidden = TRUE` в БД. Сервер различает два режима:
- До сдачи квеста (`quest_offered`, `questPreview`, `QUEST_UPDATE`): скрытая награда сериализуется как `{ "rewardType": "item", "isHidden": true }` — только тип, без slug и количества. Клиент показывает иконку `???`.
- При сдаче квеста (`quest_turned_in.rewardsReceived`): все награды раскрываются полностью независимо от флага `isHidden`.

**Состояния квеста:**

| Состояние    | Описание |
|--------------|----------|
| `active`     | Принят, выполняется |
| `completed`  | Все шаги выполнены, готов к сдаче |
| `turned_in`  | Сдан, награды получены |
| `failed`     | Провален |

### Прогресс и сохранение

- Загружается из БД при входе персонажа
- В памяти Chunk Server обновляется мгновенно
- Сбрасывается в Game Server каждые **5 секунд** (только изменённые записи)
- При отключении — немедленный flush

### Автоматические триггеры

Сервер отслеживает игровые события и обновляет прогресс автоматически:

| Событие               | Шаг `stepType` | Что происходит |
|-----------------------|---------------|----------------|
| Убийство моба         | `kill`        | `progress.killed++` |
| Получение предмета    | `collect`     | `progress.have += quantity` (до нужного максимума) |
| Открытие диалога с NPC | `talk`       | `progress.done = true` |
| Подход к точке        | `reach`       | `progress.done = true` при входе в радиус |

---

### Пакеты квестовой системы

#### `QUEST_UPDATE` — обновление квеста

**Направление:** Chunk Server → Client  
Отправляется при любом изменении: принятие, прогресс, завершение шага, сдача.

> **Обновление иконок NPC на клиенте:** при получении `QUEST_UPDATE` клиент находит все NPC у которых в `quests[]` есть элемент с совпадающим `slug == questSlug`, и пересчитывает их статус по таблице (`active` → `in_progress`, `completed` → `completable`, `turned_in` → `turned_in`). Повторный запрос `spawnNPCs` не нужен.

**При принятии квеста:**
```json
{
  "header": { "eventType": "QUEST_UPDATE", "clientId": 7 },
  "body": {
    "questId": 5,
    "questSlug": "wolf_hunt_intro",
    "clientQuestKey": "quest.wolf_hunt",
    "state": "active",
    "currentStep": 0,
    "totalSteps": 2,
    "clientStepKey": "quest.wolf_hunt.step_0",
    "stepType": "kill",
    "completionMode": "auto",
    "progress": { "killed": 0 },
    "required": { "mob_id": 2, "count": 5 },
    "currentStepEnriched": {
      "clientStepKey": "quest.wolf_hunt.step_0",
      "stepType": "kill",
      "target_slug": "forest_wolf",
      "count": 5,
      "current": 0
    },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": true }
    ]
  }
}
```

**При прогрессе (убито 3 из 5):**
```json
{
  "header": { "eventType": "QUEST_UPDATE" },
  "body": {
    "questId": 5,
    "questSlug": "wolf_hunt_intro",
    "clientQuestKey": "quest.wolf_hunt",
    "state": "active",
    "currentStep": 0,
    "totalSteps": 2,
    "clientStepKey": "quest.wolf_hunt.step_0",
    "stepType": "kill",
    "completionMode": "auto",
    "progress": { "killed": 3 },
    "required": { "mob_id": 2, "count": 5 },
    "currentStepEnriched": {
      "clientStepKey": "quest.wolf_hunt.step_0",
      "stepType": "kill",
      "target_slug": "forest_wolf",
      "count": 5,
      "current": 3
    },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": true }
    ]
  }
}
```

**При переходе к следующему шагу (сбор шкур):**
```json
{
  "header": { "eventType": "QUEST_UPDATE" },
  "body": {
    "questId": 5,
    "questSlug": "wolf_hunt_intro",
    "clientQuestKey": "quest.wolf_hunt",
    "state": "active",
    "currentStep": 1,
    "totalSteps": 2,
    "clientStepKey": "quest.wolf_hunt.step_1",
    "stepType": "collect",
    "completionMode": "auto",
    "progress": { "have": 0 },
    "required": { "item_id": 9, "count": 3 },
    "currentStepEnriched": {
      "clientStepKey": "quest.wolf_hunt.step_1",
      "stepType": "collect",
      "target_slug": "wolf_pelt",
      "count": 3,
      "current": 0
    },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": true }
    ]
  }
}
```

**При завершении всех шагов:**

Код отправляет полный пакет — `state`, `progress`, `clientStepKey`, `stepType`, `completionMode`, `required`, `currentStepEnriched` и `rewards` присутствуют (шаг не сбрасывается в коде, `currentStep` остаётся индексом последнего шага):
```json
{
  "header": { "eventType": "QUEST_UPDATE" },
  "body": {
    "questId": 5,
    "questSlug": "wolf_hunt_intro",
    "clientQuestKey": "quest.wolf_hunt",
    "state": "completed",
    "currentStep": 1,
    "totalSteps": 2,
    "clientStepKey": "quest.wolf_hunt.step_1",
    "stepType": "kill",
    "completionMode": "auto",
    "progress": { "killed": 1 },
    "required": { "mob_id": 3, "count": 1 },
    "currentStepEnriched": { "clientStepKey": "quest.wolf_hunt.step_1", "stepType": "kill", "target_slug": "wolf_alpha", "count": 1, "current": 1 },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": true }
    ]
  }
}
```

**После сдачи:**

Аналогично — полный пакет с последним шагом и `rewards`:
```json
{
  "header": { "eventType": "QUEST_UPDATE" },
  "body": {
    "questId": 5,
    "questSlug": "wolf_hunt_intro",
    "clientQuestKey": "quest.wolf_hunt",
    "state": "turned_in",
    "currentStep": 1,
    "totalSteps": 2,
    "progress": { "killed": 1 },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": false, "item_slug": "secret_amulet", "quantity": 1 }
    ]
  }
}
```

**Поля тела `QUEST_UPDATE`:**

| Поле | Тип | Описание |
|------|-----|---------|
| `questId` | int | ID квеста || `questSlug` | string | Slug квеста — используется клиентом для обновления иконок NPC || `clientQuestKey` | string | Ключ квеста для локализации (`quest.slug`) |
| `state` | string | `active`, `completed`, `turned_in`, `failed` |
| `currentStep` | int | Индекс текущего шага (0-based) |
| `totalSteps` | int | Общее число шагов в квесте |
| `clientStepKey` | string | Ключ шага для локализации (`quest.slug.step_N`). Отсутствует если `state = completed/turned_in` |
| `stepType` | string | Тип шага: `kill`, `collect`, `talk`, `reach`, `custom`. Отсутствует если `state = completed/turned_in` |
| `completionMode` | string | `"auto"` — шаг завершается автоматически; `"manual"` — только через диалоговое действие. Отсутствует если `state = completed/turned_in` |
| `progress` | object | Текущий прогресс шага (зависит от `stepType`). Отсутствует если `state = completed/turned_in` |
| `required` | object | Требования шага (= `params` из БД). Отсутствует если `state = completed/turned_in` |
| `currentStepEnriched` | object | Обогащённый шаг с resolved slugs (см. ниже). Присутствует когда есть текущий шаг |
| `rewards` | array | Награды квеста (с учётом `isHidden`). Присутствует всегда |

**Поля `currentStepEnriched`:**

| `stepType` | Поля | Описание |
|------------|------|----------|
| `kill` | `clientStepKey`, `stepType`, `target_slug`, `count`, `current` | `target_slug` — slug моба из `mob_definitions` |
| `collect` | `clientStepKey`, `stepType`, `target_slug`, `count`, `current` | `target_slug` — slug предмета |
| `talk` | `clientStepKey`, `stepType`, `target_slug`, `count`, `current` | `target_slug` — slug NPC |
| `reach` | `clientStepKey`, `stepType`, `zone_slug`, `x`, `y`, `count`, `current` | Координаты цели |
| `custom` | `clientStepKey`, `stepType`, `params`, `count`, `current` | `params` — raw JSON из БД |

---

#### `quest_offered` — квест принят

**Направление:** Chunk Server → Client (уведомление из диалогового действия `offer_quest`)

```json
{
  "header": { "eventType": "quest_offered" },
  "body": {
    "questId": 5,
    "clientQuestKey": "quest.wolf_hunt",
    "currentStep": {
      "clientStepKey": "quest.wolf_hunt.step_0",
      "stepType": "kill",
      "target_slug": "forest_wolf",
      "count": 5,
      "current": 0
    },
    "rewards": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "gold", "isHidden": false, "amount": 100 },
      { "rewardType": "item", "isHidden": false, "item_slug": "wooden_staff", "quantity": 1 },
      { "rewardType": "item", "isHidden": true }
    ]
  }
}
```

`currentStep` отсутствует если у квеста нет шагов. `rewards` — массив с учётом `isHidden`.

---

#### `quest_turned_in` — квест сдан

**Направление:** Chunk Server → Client

```json
{
  "header": { "eventType": "quest_turned_in" },
  "body": {
    "questId": 5,
    "clientQuestKey": "quest.wolf_hunt",
    "rewardsReceived": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "gold", "isHidden": false, "amount": 100 },
      { "rewardType": "item", "isHidden": false, "item_slug": "wooden_staff",  "quantity": 1 },
      { "rewardType": "item", "isHidden": false, "item_slug": "secret_amulet", "quantity": 1 }
    ]
  }
}
```

`rewardsReceived` — все награды полностью раскрыты (скрытые показываются с реальными `item_slug`).
Сразу за `quest_turned_in` идут отдельные уведомления `exp_received`, `item_received`, `gold_received`.

---

#### `exp_received` — получен опыт

**Направление:** Chunk Server → Client

```json
{ "header": { "eventType": "exp_received" }, "body": { "amount": 500 } }
```

---

#### `item_received` — получен предмет

**Направление:** Chunk Server → Client

```json
{ "header": { "eventType": "item_received" }, "body": { "itemId": 15, "quantity": 1 } }
```

---

#### `gold_received` — получено золото

**Направление:** Chunk Server → Client

```json
{ "header": { "eventType": "gold_received" }, "body": { "amount": 100 } }
```

---

### Полный пример: цикл квеста "Охота на волков"

**Принятие:** → `quest_offered { currentStep, rewards }` + `QUEST_UPDATE { state: "active", step: 0, currentStepEnriched, rewards }`  
**Убийство 1 волка:** → `QUEST_UPDATE { progress: {"killed": 1} }`  
**Убийство 5 волков:** → `QUEST_UPDATE { progress: {"killed": 5} }` → шаг 0 завершён, переход к шагу 1  
→ `QUEST_UPDATE { step: 1, stepType: "kill", required: {"mob_id": 2, "count": 1} }`  

*Диалог с Миладой на шаге 1: ребро с условием `quest_step = 1` становится видимым вместо шага 0.*  

**Убийство вожака:** → `QUEST_UPDATE { progress: {"killed": 1} }` → все шаги выполнены  
→ `QUEST_UPDATE { state: "completed" }`  
**Диалог сдачи у Миладки (действие `turn_in_quest`):**  
→ `QUEST_UPDATE { state: "turned_in" }`  
→ `quest_turned_in { rewardsReceived: [...все раскрыто...] }`  
→ `exp_received { amount: 500 }`  
→ `item_received { itemId: 15, quantity: 1 }` (Wooden Staff)  
→ `gold_received { amount: 100 }` (добавляет 100 Gold Coin в инвентарь)

*Альтернативный исход — провал через диалог (действие `fail_quest`):*  
→ `QUEST_UPDATE { state: "failed" }`

---

## 5. Валюта — Gold Coin

### Реализация (v0.0.3+, добавлено 2026-03-09)

Золото в игре реализовано как обычный предмет в инвентаре:

| Поле          | Значение |
|---------------|----------|
| `id`          | 16 |
| `name`        | Gold Coin |
| `slug`        | `gold_coin` |
| `item_type`   | 8 (`currency`) |
| `stack_max`   | 9_999_999 |
| `isTradable`  | false (нельзя передать другому игроку напрямую) |
| `isUsable`    | false |
| `weight`      | 0.01 |

### Способы получения золота

**1. Награда за квест** (`rewardType: "gold"` в таблице `quest_reward`):  
При сдаче квеста автоматически добавляет указанное количество Gold Coin в инвентарь.

**2. Действие диалога** (`give_gold` в `actionGroup`):
```json
{ "type": "give_gold", "amount": 50 }
```
NPC может напрямую выдать золото без квеста.

### Пример конфигурации квеста с золотой наградой (SQL)

```sql
INSERT INTO quest_reward (quest_id, reward_type, item_id, quantity, amount)
VALUES (5, 'gold', NULL, 0, 100);
```

---

## 6. Торговля

Торговля с NPC (магазин, ремонт), а также P2P-торговля между игроками **полностью реализованы**.

Документация покетов:
- **Магазин NPC** (`openVendorShop`, `buyItem`, `sellItem`, `buyItemBatch`, `sellItemBatch`) — см. `docs/client-server-protocol.md`, раздел *Торговля с NPC*.
- **Ремонт NPC** (`openRepairShop`, `repairItem`, `repairAll`) — см. `docs/client-server-protocol.md`, раздел *Ремонт*.
- **P2P-торговля** (`tradeRequest`, `tradeAccept`, `tradeOfferUpdate`, `tradeConfirm`, `tradeCancel`) — см. `docs/client-server-protocol.md`, раздел *P2P-торговля*.

#### Особенность: репутация и цены

NPC типа `vendor` применяет скидку 5% если репутация игрока с фракцией NPC ≥ 200 (`friendly`). Цены в пакете `vendorShopData` уже учитывают скидку — пересчитывать не нужно.

NPC типа `vendor` полностью заблокирован при репутации < -500 (`enemy`) — сервер отклоняет запрос `openVendorShop` с `dialogueError { errorCode: "BLOCKED_BY_REPUTATION" }`.

Подробнее о репутации: см. `docs/client-progression-protocol.md`, раздел *Репутация с фракциями*.

---

## 7. Сводная таблица пакетов

### Client → Chunk Server

| Пакет (`eventType`) | Когда | Ключевые поля |
|---------------------|-------|---------------|
| `npcInteract`       | Клик по NPC | `npcId`, `characterId` |
| `dialogueChoice`    | Выбор ответа | `sessionId`, `edgeId`, `characterId` |
| `dialogueClose`     | Закрытие диалога | `sessionId`, `characterId` |

### Chunk Server → Client

| Пакет (`eventType`) | Когда | Ключевые поля |
|---------------------|-------|---------------|
| `spawnNPCs`         | Вход в зону | `npcsSpawn[]`, `npcCount` |
| `NPC_AMBIENT_POOLS` | После spawnNPCs при playerReady | `npcs[]` (filtered pools, см. [npc-ambient-speech-protocol.md](./npc-ambient-speech-protocol.md)) |
| `DIALOGUE_NODE`     | Очередной узел | `sessionId`, `type`, `clientNodeKey`, `choices[]` (+ `questPreview`/`turnInPreview`) |
| `DIALOGUE_CLOSE`    | Диалог завершён | `sessionId` |
| `dialogueError`     | Ошибка взаимодействия | `errorCode` |
| `QUEST_UPDATE`      | Любое изменение квеста | `questId`, `questSlug`, `state`, `progress`, `required`, `currentStepEnriched`, `rewards` |
| `quest_offered`     | Квест принят | `questId`, `clientQuestKey`, `currentStep`, `rewards` |
| `quest_turned_in`   | Квест сдан | `questId`, `clientQuestKey`, `rewardsReceived` |
| `exp_received`      | Выдан опыт | `amount` |
| `item_received`     | Выдан предмет | `itemId`, `quantity` |
| `gold_received`     | Выдано золото | `amount` |

### Chunk Server → Game Server (персистентность)

| Пакет (`eventType`)          | Когда | Ключевые поля |
|------------------------------|-------|---------------|
| `updatePlayerQuestProgress`  | Каждые 5с / отключение | `characterId`, `questId`, `state`, `progress` |
| `updatePlayerFlag`           | Сразу после изменения флага | `characterId`, `flagKey`, `boolValue`/`intValue` |

---

## 8. Примеры полных JSON-пакетов

### Полный пакет ошибки диалога

```json
{
  "header": {
    "message": "Too far from NPC",
    "hash": "abc123",
    "clientId": 7,
    "eventType": "dialogueError"
  },
  "body": {
    "errorCode": "OUT_OF_RANGE"
  }
}
```

### Полный пакет узла выбора с quest preview и заблокированным вариантом

```json
{
  "header": {
    "message": "success",
    "hash": "abc123",
    "clientId": 7,
    "eventType": "DIALOGUE_NODE"
  },
  "body": {
    "sessionId": "dlg_7_1709834400000",
    "npcId": 42,
    "nodeId": 20,
    "clientNodeKey": "npc.grimor.quest_hub",
    "type": "choice_hub",
    "speakerNpcId": 42,
    "choices": [
      {
        "edgeId": 12,
        "clientChoiceKey": "npc.grimor.accept_hard_quest",
        "conditionMet": false
      },
      {
        "edgeId": 13,
        "clientChoiceKey": "npc.grimor.accept_easy_quest",
        "conditionMet": true,
        "questPreview": {
          "questId": 5,
          "clientQuestKey": "quest.wolf_hunt",
          "firstStep": {
            "clientStepKey": "quest.wolf_hunt.step_0",
            "stepType": "kill",
            "target_slug": "forest_wolf",
            "count": 5,
            "current": 0
          },
          "rewards": [
            { "rewardType": "exp",  "isHidden": false, "amount": 500 },
            { "rewardType": "item", "isHidden": false, "item_slug": "wooden_staff", "quantity": 1 },
            { "rewardType": "item", "isHidden": true }
          ]
        }
      },
      {
        "edgeId": 14,
        "clientChoiceKey": "npc.grimor.leave",
        "conditionMet": true
      }
    ]
  }
}
```

Вариант с `conditionMet: false` — условие не выполнено. Клиент решает: показать серым или скрыть (задаётся в БД полем `hide_if_locked`).

`questPreview` присутствует только если ребро содержит действие `offer_quest`. Аналогично `turnInPreview` при `turn_in_quest`.

### Полный цикл сдачи квеста — все пакеты подряд

```json
// 1. Открытие диалога с NPC-поворот
{ "header": { "eventType": "npcInteract" }, "body": { "npcId": 42, "characterId": 101 } }

// 2. Сервер выбрал диалог "сдача квеста" (приоритет выше) → первая реплика
// (ребро contains turn_in_quest → в choices появится turnInPreview)
{
  "header": { "eventType": "DIALOGUE_NODE" },
  "body": {
    "sessionId": "dlg_7_...", "type": "line",
    "clientNodeKey": "npc.grimor.turnin_greeting",
    "choices": [ { "edgeId": 20, "clientChoiceKey": "npc.grimor.continue", "conditionMet": true } ]
  }
}

// 3. Игрок "Далее" → choice_hub с turnInPreview
{ "header": { "eventType": "dialogueChoice" }, "body": { "sessionId": "dlg_7_...", "edgeId": 20, "characterId": 101 } }

{
  "header": { "eventType": "DIALOGUE_NODE" },
  "body": {
    "sessionId": "dlg_7_...", "type": "choice_hub",
    "clientNodeKey": "npc.grimor.turnin_confirm",
    "choices": [
      {
        "edgeId": 21,
        "clientChoiceKey": "npc.grimor.handover",
        "conditionMet": true,
        "turnInPreview": {
          "questId": 5,
          "clientQuestKey": "quest.wolf_hunt",
          "rewards": [
            { "rewardType": "exp",  "isHidden": false, "amount": 500 },
            { "rewardType": "item", "isHidden": false, "item_slug": "wooden_staff", "quantity": 1 },
            { "rewardType": "item", "isHidden": false, "item_slug": "secret_amulet", "quantity": 1 }
          ]
        }
      }
    ]
  }
}

// 4. Игрок нажимает "Сдать"
{ "header": { "eventType": "dialogueChoice" }, "body": { "sessionId": "dlg_7_...", "edgeId": 21, "characterId": 101 } }

// 5. action-узел выполняет turn_in_quest → rewards → end-узел
// --- пакеты уведомлений ---
{ "header": { "eventType": "QUEST_UPDATE" }, "body": { "questId": 5, "questSlug": "wolf_hunt", "state": "turned_in" } }
{
  "header": { "eventType": "quest_turned_in" },
  "body": {
    "questId": 5,
    "clientQuestKey": "quest.wolf_hunt",
    "rewardsReceived": [
      { "rewardType": "exp",  "isHidden": false, "amount": 500 },
      { "rewardType": "item", "isHidden": false, "item_slug": "wooden_staff",  "quantity": 1 },
      { "rewardType": "item", "isHidden": false, "item_slug": "secret_amulet", "quantity": 1 }
    ]
  }
}
{ "header": { "eventType": "exp_received"  }, "body": { "amount": 500 } }
{ "header": { "eventType": "item_received" }, "body": { "itemId": 15, "quantity": 1 } }
{ "header": { "eventType": "item_received" }, "body": { "itemId": 22, "quantity": 1 } }

// 6. Граф дошёл до end → сессия закрыта
{ "header": { "eventType": "DIALOGUE_CLOSE" }, "body": { "sessionId": "dlg_7_..." } }
```

### Пример actionGroup с выдачей золота (конфигурация в БД)

```json
{
  "actions": [
    { "type": "turn_in_quest", "slug": "wolf_hunt" },
    { "type": "set_flag", "key": "grimor_quest_done", "bool_value": true },
    { "type": "give_gold", "amount": 50 }
  ]
}
```

### Пример conditionGroup с проверкой предмета и флага

```json
{
  "all": [
    { "type": "item", "item_id": 9, "gte": 3 },
    { "type": "flag", "key": "talked_to_captain", "eq": true }
  ]
}
```

### Пример conditionGroup с проверкой шага квеста

Разные ветки диалога на шаге 0 и шаге 1 одного квеста:

```json
// Ребро видно только когда квест активен И игрок на шаге 0
{
  "all": [
    { "type": "quest", "slug": "wolf_hunt_intro", "state": "active" },
    { "type": "quest_step", "slug": "wolf_hunt_intro", "step": 0 }
  ]
}

// Ребро видно только когда квест активен И игрок на шаге 1
{
  "all": [
    { "type": "quest", "slug": "wolf_hunt_intro", "state": "active" },
    { "type": "quest_step", "slug": "wolf_hunt_intro", "step": 1 }
  ]
}
```

### Пример actionGroup с провалом квеста

```json
{
  "actions": [
    { "type": "fail_quest", "slug": "wolf_hunt_intro" }
  ]
}
```

---

## 9. Ограничения и статус реализации

| Функциональность | Статус | Примечание |
|-----------------|:------:|------------|
| NPC спавн и данные | ✅ | Полностью |
| Диалоговый граф (узлы, рёбра, условия) | ✅ | Полностью |
| Выбор диалога по приоритету и условиям | ✅ | Полностью |
| Сессия диалога с TTL 5 минут | ✅ | Полностью |
| Действия диалога: флаги, квесты, предметы, опыт, золото | ✅ | Полностью |
| `questPreview` / `turnInPreview` в choices | ✅ | **Добавлено в v0.0.5** — resolved slugs + rewards |
| Скрытые награды (`isHidden`) | ✅ | **Добавлено в v0.0.5** — миграция 055 |
| `quest_offered` обогащён (`currentStep` + `rewards`) | ✅ | **Добавлено в v0.0.5** |
| `quest_turned_in` обогащён (`rewardsReceived` раскрыт) | ✅ | **Добавлено в v0.0.5** |
| `QUEST_UPDATE` обогащён (`currentStepEnriched` + `rewards`) | ✅ | **Добавлено в v0.0.5** |
| NPC Ambient Speech | ✅ | **Добавлено в v0.0.5** — см. [npc-ambient-speech-protocol.md](./npc-ambient-speech-protocol.md) |
| Квесты: принятие, прогресс, шаги, сдача | ✅ | Полностью |
| Квесты многошаговые (`quest_step` условие в диалоге) | ✅ | Добавлено в v0.0.4 |
| Провал квеста (`fail_quest` действие) | ✅ | Добавлено в v0.0.4 |
| Триггеры: убийство, сбор, разговор, достижение | ✅ | Полностью |
| Персистентность прогресса квестов | ✅ | Каждые 5с + при отключении |
| Флаги игрока при входе | ✅ | Реализовано в v0.0.3+ |
| Условие по предметам `item` | ✅ | Реализовано в v0.0.3+ |
| Золото Gold Coin | ✅ | Добавлено в v0.0.3+, item ID 16 |
| Торговля NPC (магазин, ремонт) | ✅ | Полностью, см. `client-server-protocol.md` |
| P2P-торговля между игроками | ✅ | Полностью, см. `client-server-protocol.md` |
| Скидка по репутации у торговцев | ✅ | Полностью |
| Блокировка NPC при репутации `enemy` | ✅ | Полностью |
| Обновление ambient pools при смене квеста/флага | ⏳ | Запланировано: пул пересчитывается при переподключении |
