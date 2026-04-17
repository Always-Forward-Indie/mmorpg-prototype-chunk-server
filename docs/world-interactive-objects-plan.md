# World Interactive Objects (WIO) — План Имплементации

**Версия документа:** v1.0  
**Дата:** 2026-04-16  
**Статус:** Design Plan — не имплементировано

---

## Содержание

1. [Что такое WIO и зачем](#1-что-такое-wio-и-зачем)
2. [Типология объектов](#2-типология-объектов)
3. [Ключевое архитектурное решение: Per-Player vs Global](#3-ключевое-архитектурное-решение)
4. [Связь с существующей архитектурой](#4-связь-с-существующей-архитектурой)
5. [База данных](#5-база-данных)
6. [Серверная архитектура](#6-серверная-архитектура)
7. [Протокол клиент-сервер](#7-протокол-клиент-сервер)
8. [Квестовая интеграция](#8-квестовая-интеграция)
9. [Клиентская часть (UE5)](#9-клиентская-часть-ue5)
10. [Фазы реализации](#10-фазы-реализации)
11. [Ограничения и риски](#11-ограничения-и-риски)

---

## 1. Что такое WIO и зачем

**World Interactive Object** — любой объект в игровом мире, с которым игрок может взаимодействовать, и который несёт игровую ценность. Это не NPC и не моб — это среда мира.

Примеры: следы на земле, труп NPC, бочка, сундук, рычаг, кристал, алтарь, телега, надпись на стене, пьедестал с выемкой под предмет, сигнальный огонь.

**Зачем:**
- Квесты становятся про мир, а не только про "убей X мобов" и "поговори с NPC"
- Даёт окружению нарратив без катсцен
- Создаёт точки интереса, которые тянут игрока исследовать карту
- Позволяет строить environmental storytelling (разбитая телега + труп + следы = история без текста)
- Добавляет разнообразие в quest step types без сложных новых систем

---

## 2. Типология объектов

Объекты отличаются **не внешним видом**, а **моделью состояния и поведением**.

---

### Тип A: Examine (Изучение)

**Примеры:** следы зверя, труп NPC, выбитая дверь, надпись на камне, старая карта на столе

**Модель состояния:** per-player — каждый игрок видит объект "нетронутым" и сам принимает решение об изучении. Флаг сохраняется для игрока, но объект в мире остаётся для других.

**Взаимодействие:**
- Подойти в зону interactionRadius
- Нажать взаимодействие (мгновенно или ~1–2 сек анимации)
- Открывается dialogue graph (как у NPC, только источник — объект)
- Результат: текст / флаг / продвижение квеста

**Особенности:**
- Никогда не "исчезает" из мира — объект статичен
- Может выдавать разный контент в зависимости от флагов и состояния квеста (стандартные dialogue conditions)
- Самый простой в реализации из всех типов

**Примечание:** "Труп для исследования" это NE то же что "труп моба для харвеста" — это статичный декоративный труп NPC как нарративный объект.

---

### Тип B: Search (Обыск)

**Примеры:** бочка, ящик, корзина, тайник в стене, поклажа путника

**Модель состояния:** global — объект имеет одно состояние для всех (active → depleted). После истощения недоступен до respawn таймера.

**Взаимодействие:**
- Подойти в зону interactionRadius
- Анимация обыска (~2–3 сек, прерывается при получении урона)
- Результат: набор предметов (item rolls как у лута с трупа через HarvestManager / LootManager)
- Объект переходит в состояние depleted (визуально открытый/пустой)
- Через respawnSec → снова active

**Особенности:**
- Первый пришёл — первый взял (намеренно, создаёт дефицит и срочность)
- Может быть пустым с определённым шансом
- Может требовать квестовый флаг/предмет для доступа
- При depleted — клиент показывает визуально иное состояние меша

---

### Тип C: Activate (Активация)

**Примеры:** рычаг, кристал-активатор, алтарь, кнопка, сигнальный костёр

**Модель состояния:** global — один объект, одно состояние для всего мира сервера.

**Взаимодействие:**
- Подойти в зону interactionRadius
- Анимация активации
- Результат: triggered event (открытие двери, появление моба, старт зонового ивента, снятие блокировки с другого WIO)
- Может иметь кулдаун перед повторной активацией

**Особенности:**
- **Самый опасный в плане гриферства** — продумать кто может активировать (условия)
- Может быть одноразовым (global) или перезаряжаемым
- Для chain-взаимодействий (рычаг A открывает путь к рычагу B) — координировать через флаги или object_state conditions
- Linked objects: объект A может зависеть от состояния объекта B

---

### Тип D: Use-With-Item (Применение предмета)

**Примеры:** пьедестал с выемкой под gem, алтарь требующий жертву, замок требующий ключ

**Модель состояния:** per-player или global — определяется контентом. Квестовый пьедестал — per-player. Замок в подземелье — global.

**Взаимодействие:**
- Условие: наличие нужного предмета в инвентаре (уже поддерживается `{"type":"item","item_id":X,"gte":1}`)
- При успехе: предмет потребляется (give_item с минусом через dialogue action) + эффект
- При провале: dialogue node объяснит что нужно

**Особенности:**
- Полностью реализуется через существующий dialogue graph + conditions/actions
- Минимальная новая серверная логика

---

### Тип E: Channeled Examine (Длительное действие)

**Примеры:** прослушивание ритуала, ожидание у костра, слежка за патрулём, декодирование знаков

**Модель состояния:** per-player

**Взаимодействие:**
- Удерживать взаимодействие N секунд (аналог cast time в бою)
- Прерывается при получении урона или движении игрока
- Прогресс-бар на клиенте
- Результат: флаг + квест-шаг + опциональный dialogue

**Особенности:**
- Единственный тип с cancel-логикой на сервере (channelTime + interrupt)
- Повторная попытка возможна немедленно если прервали

---

## 3. Ключевое Архитектурное Решение

### Per-Player State (Флаги на игроке)

Состояние хранится в `PlayerContextStruct::flagsBool` под ключом `wio_interacted_<objectId>`.

- Уже существующий механизм
- Игрок видит объект "свежим" всегда
- Работает для Examine, Channeled, Use-With-Item (квестовый вариант)
- Нет конкуренции между игроками
- DB: одна запись в таблице `player_flags`

### Global Object State (Состояние на объекте)

Состояние хранится в WorldObjectManager в памяти, персистится в отдельной таблице `world_object_states`.

- Нужна новая структура данных на сервере
- Один объект — одно состояние для всех игроков в чанке
- Работает для Search, Activate
- Требует broadcast всем игрокам в зоне при смене состояния
- Respawn timer на сервере (аналог cooldown у мобов)

### Правило выбора

```
Если объект — нарративный / квестовый → Per-Player
Если объект — ресурсный / механики мира → Global
```

---

## 4. Связь с существующей архитектурой

### Что переиспользуется без изменений

| Компонент | Как используется в WIO |
|-----------|----------------------|
| `DialogueGraphStruct` | Контент для Examine и Use-With-Item объектов. Объект просто становится "источником" диалога вместо NPC |
| `DialogueConditionEvaluator` | Проверка флагов, уровня, предметов перед взаимодействием |
| `DialogueActionExecutor` | set_flag, give_item, give_exp, offer_quest, advance_quest_step |
| `PlayerContextStruct::flagsBool` | Хранение per-player состояния WIO |
| `HarvestManager` / `LootManager` | Генерация лута для Search-объектов |
| `InventoryManager` | Выдача предметов при обыске |
| `QuestManager` | Проверка квест-шагов типа "interact" |

### Что нужно создать новое

| Компонент | Описание |
|-----------|----------|
| `WorldObjectDataStruct` | Статичное описание объекта (ID, тип, позиция, meshId, dialogueId, loot table id, respawnSec, interactionRadius, channelTimeSec, scope) |
| `WorldObjectInstanceStruct` | Runtime состояние (objectId, state: active/depleted/disabled, depletedAt, respawnAt) |
| `WorldObjectManager` | Хранит все объекты чанка, управляет состоянием, respawn тикером |
| Quest step type `"interact"` | Новое значение в `QuestStepStruct::stepType` с полями objectId / count |
| Quest step type `"use_item_on"` | Опционально — явный тип для Use-With-Item если dialogue approach не достаточен |
| Dialogue action `set_object_state` | Меняет глобальное состояние WIO (active / depleted / disabled) |
| Dialogue condition `object_state` | Проверяет текущее состояние WIO перед взаимодействием |
| `GameServerWorker` extension | Загрузка `world_objects` из БД при старте чанка (аналог loadNPCs) |

---

## 5. База данных

### Новая таблица: `world_objects`

Статичные определения объектов (Game Server загружает и пушит в Chunk Server при старте).

| Колонка | Тип | Описание |
|---------|-----|----------|
| `id` | int PK | Уникальный ID объекта |
| `slug` | varchar | Человекочитаемый ключ |
| `name_key` | varchar | Ключ локализации для отображения на клиенте |
| `object_type` | varchar | `examine` / `search` / `activate` / `use_with_item` / `channeled` |
| `scope` | varchar | `per_player` / `global` |
| `mesh_id` | varchar | Ссылка на меш/blueprnt в UE5 |
| `pos_x`, `pos_y`, `pos_z` | float | Позиция в мире |
| `zone_id` | int FK | Зона/чанк к которому принадлежит |
| `dialogue_id` | int FK nullable | Диалог-граф для Examine / Use-With-Item |
| `loot_table_id` | int FK nullable | Таблица лута для Search |
| `required_item_id` | int FK nullable | Предмет для Use-With-Item |
| `interaction_radius` | float | Дистанция подхода |
| `channel_time_sec` | int | Для Channeled — время удержания (0 для остальных) |
| `respawn_sec` | int | Время перезарядки (0 = одноразово или бесконечно active) |
| `is_active_by_default` | bool | Начальное состояние при старте сервера |
| `min_level` | int | Минимальный уровень для взаимодействия |
| `condition_group` | jsonb | Дополнительные условия (флаги, квест-стейт) |
| `notes` | text | Заметки дизайнера |

### Новая таблица: `world_object_states` (для global scope объектов)

Персистентное состояние между перезапусками сервера. Только для global объектов, per-player состояние хранится в `player_flags`.

| Колонка | Тип | Описание |
|---------|-----|----------|
| `object_id` | int FK PK | Ссылка на world_objects |
| `state` | varchar | `active` / `depleted` / `disabled` |
| `depleted_at` | timestamp nullable | Когда был использован |

### Расширение таблицы `quest_steps` (если она есть как отдельная)

Добавить поля для нового step type `interact`:

| Поле | Тип | Описание |
|------|-----|----------|
| `object_id` | int FK nullable | Для типа interact — какой объект |
| `object_count` | int | Сколько раз взаимодействовать (если count > 1) |

---

## 6. Серверная архитектура

### WorldObjectManager

Создаётся как новый сервис в `GameServices`, аналогично `NPCManager`.

**Ответственности:**
- Хранить все `WorldObjectDataStruct` чанка (static data, загружается один раз)
- Хранить `WorldObjectInstanceStruct` для global-объектов (runtime state)
- Предоставлять `getObjectById(id)` и `getObjectsInRadius(pos, radius)`
- Обрабатывать respawn тикер: периодически проверять depletedAt + respawnSec и восстанавливать объекты
- При смене состояния global объекта — возвращать данные для broadcast
- Thread-safe: `std::shared_mutex` как у остальных менеджеров

**Per-player состояние WorldObjectManager не хранит** — оно живёт исключительно в `PlayerContextStruct::flagsBool` под ключом `wio_seen_<objectId>` или `wio_interacted_<objectId>`.

### Новый EventHandler: WorldObjectEventHandler

Обрабатывает события взаимодействия с WIO:

**Входящие события (Client → Server):**
- `WORLD_OBJECT_INTERACT_REQUEST` — запрос на взаимодействие
- `WORLD_OBJECT_CHANNEL_CANCEL` — отмена channeled действия

**Исходящие события (Server → Client):**
- `WORLD_OBJECT_INTERACT_RESULT` — результат взаимодействия (success/fail + payload)
- `WORLD_OBJECT_STATE_UPDATE` — broadcast смены состояния global объекта (depleted/active)

### Логика обработки запроса взаимодействия

1. Получить `WorldObjectDataStruct` по objectId
2. Проверить дистанцию (игрок в interactionRadius?)
3. Проверить минимальный уровень
4. Для global объектов: проверить текущее состояние (не depleted и не disabled)
5. Для per-player объектов: нет ограничений по state
6. Вычислить `PlayerContextStruct` игрока
7. Выполнить `DialogueConditionEvaluator` на `condition_group` объекта
8. Если Examine / Use-With-Item: открыть dialogue session с `dialogueId` (как у NPC)
9. Если Search: запустить лут-генерацию через LootManager → выдать через InventoryManager → установить state=depleted → broadcast
10. Если Activate: выполнить linked action → broadcast state change → все observers уведомлены  
11. Если Channeled: зарегистрировать channel session с таймером, при завершении — как Examine результат
12. Квест: если objectId совпадает с текущим quest step типа `interact` → advance_quest_step

### Channeled объекты — дополнительная логика

- При старте channel: сервер регистрирует `ChannelSessionStruct` (characterId, objectId, startedAt, channelTimeSec)
- Тикер каждые N мс проверяет завершение
- Прерывается если: игрок получил урон, игрок отошёл дальше interactionRadius, игрок отправил cancel
- При прерывании: клиент получает `WORLD_OBJECT_INTERACT_RESULT` с `interrupted: true`

---

## 7. Протокол Клиент-Сервер

### Client → Chunk Server

#### `worldObjectInteractRequest`
Игрок инициирует взаимодействие с объектом.

```
header.eventType: "worldObjectInteractRequest"
body:
  objectId: int
```

#### `worldObjectChannelCancel`
Игрок прерывает channeled взаимодействие.

```
header.eventType: "worldObjectChannelCancel"
body:
  objectId: int
```

---

### Chunk Server → Client

#### `worldObjectInteractResult`
Ответ на попытку взаимодействия.

```
header.eventType: "worldObjectInteractResult"
body:
  objectId: int
  success: bool
  errorCode: string | null  // "too_far" | "depleted" | "level_too_low" | "condition_failed" | "interrupted"
  interactionType: string   // "examine" | "search" | "activate" | "channeled"
  channelTimeSec: int       // >0 если нужно держать взаимодействие
  // Для search:
  lootItems: [ { itemId, quantity } ] | null
  // Для examine/use_with_item: открывается dialogue через стандартный dialogueNode пакет
```

#### `worldObjectStateUpdate` (broadcast всем в чанке)
Смена глобального состояния объекта.

```
header.eventType: "worldObjectStateUpdate"
body:
  objectId: int
  state: "active" | "depleted" | "disabled"
  respawnAt: int | null   // Unix ms, когда объект снова станет active (null если нет respawn)
```

#### `spawnWorldObjects` (при входе игрока в чанк)
Начальный список всех WIO в чанке и их текущее состояние.

```
header.eventType: "spawnWorldObjects"
body:
  objects: [
    {
      objectId: int,
      slug: string,
      objectType: string,       // "examine" | "search" | "activate" | etc.
      meshId: string,
      posX: float, posY: float, posZ: float,
      interactionRadius: float,
      nameKey: string,          // ключ локализации
      state: "active" | "depleted" | "disabled",
      respawnAt: int | null,
      channelTimeSec: int       // 0 если не channeled
    }
  ]
```

---

### Game Server → Chunk Server (internal, при старте)

#### `SET_ALL_WORLD_OBJECTS`
Пуш всех статичных данных объектов при инициализации чанка. Аналог `SET_ALL_NPCS_LIST`.

```
Payload: [ WorldObjectDataStruct... ]
```

---

## 8. Квестовая Интеграция

### Новый тип квестового шага: `interact`

Расширение `QuestStepStruct`:

```
stepType: "interact"
targetId: objectId (int)
requiredCount: int  // 1 в большинстве случаев, >1 для "осмотри 3 алтаря"
description: string // ключ локализации "Изучи следы у реки"
```

### Как это работает

1. Quest step типа `interact` хранит `targetId = worldObjectId` и `requiredCount`
2. При успешном взаимодействии с WIO — `WorldObjectEventHandler` проверяет активный quest step игрока
3. Если `currentStep.stepType == "interact"` и `currentStep.targetId == objectId`  → incrementProgress + advanceStep если count достигнут
4. Аналогично тому, как kill-step считает убийства мобов

### Взаимодействие dialogue actions с WIO

Существующие actions работают без изменений:

- `set_flag` — поставить флаг что осмотрел конкретный объект
- `offer_quest` — предложить квест при изучении объекта (труп открывает расследование)
- `advance_quest_step` — форсированное продвижение шага
- `give_item` — найти предмет при изучении

Новые actions (только для WIO):

- `set_object_state` — менять состояние конкретного объекта по objectId (например, активация алтаря меняет состояние двери)

Новые conditions (только для WIO):

- `object_state` — проверить состояние объекта: `{"type":"object_state","object_id":15,"state":"depleted"}`

---

## 9. Клиентская часть (UE5)

### Blueprint / Actor структура

- Базовый Actor класс `BP_WorldInteractiveObject` с компонентами: StaticMesh, InteractionTrigger (SphereCollision), WidgetComponent (плашка-подсказка)
- Дочерние классы по типу: `BP_WIO_Examine`, `BP_WIO_Search`, `BP_WIO_Activate`, `BP_WIO_Channeled`
- Или один класс с enum-параметром типа (проще поддерживать)
- При получении `spawnWorldObjects` — спавн акторов по meshId и позиции

### Визуальные состояния

- `active` — стандартный вид
- `depleted` — альтернативный меш или материал (открытая бочка, потухший алтарь)
- `disabled` — скрыт или полупрозрачен
- Подсветка при нахождении в зоне interactionRadius (как у трупов для харвеста)

### Плашка-подсказка над объектом

- Появляется при входе игрока в interactionRadius
- Показывает nameKey (локализованное имя) + клавишу взаимодействия
- Для channeled объектов — показывает время удержания
- Скрывается при depleted / disabled

### Прогресс-бар для Channeled

- Появляется при старте channel session
- Прерывается при cancel или получении урона (сервер шлёт `interrupted: true`)
- Управляется отдельным компонентом, не блокирует движение интерфейса

### Broadcast state update

- При получении `worldObjectStateUpdate` — найти актор по objectId, применить визуальное состояние
- Показать brief FX (дым из бочки, вспышка кристала)

---

## 10. Фазы реализации

### Фаза 1 — Examine объекты (per-player)

**Scope:** Только Examine тип. Per-player состояние через флаги. Никаких global state.

**Сервер:**
1. Создать таблицу `world_objects` в БД (только поля для Examine: id, slug, name_key, object_type, mesh_id, pos_x/y/z, zone_id, dialogue_id, interaction_radius, min_level, condition_group)
2. Game Server: загружать `world_objects` из БД — новый loader аналог loadNPCs
3. Game Server: пушить `SET_ALL_WORLD_OBJECTS` в Chunk Server при старте
4. Chunk Server: создать `WorldObjectDataStruct` и `WorldObjectManager` (только getById, getByZone)
5. Chunk Server: создать `WorldObjectEventHandler` с обработкой `worldObjectInteractRequest`
6. Обработчик: проверка дистанции + level + conditions → открыть dialogue session с objectId вместо npcId
7. Quest step type `interact` в `QuestStepStruct` + проверка в `WorldObjectEventHandler`
8. Event: `WORLD_OBJECT_INTERACT_REQUEST` в `EventData` variant

**Клиент:**
1. Обработка пакета `spawnWorldObjects` — спавн акторов
2. Базовый `BP_WorldInteractiveObject` с плашкой и SphereCollision trigger
3. Отправка `worldObjectInteractRequest` при нажатии взаимодействия в зоне
4. Открытие стандартного диалогового UI (уже существует)

**Результат Фазы 1:** Квесты могут включать шаги "осмотри следы", "изучи труп", "прочитай надпись". Контент создаётся через Dialogue Graph как для NPC.

---

### Фаза 2 — Search объекты (global state + лут)

**Scope:** Добавить тип Search с глобальным состоянием, лутом и respawn.

**Сервер:**
1. Добавить в `world_objects` поля: loot_table_id, respawn_sec, is_active_by_default
2. Создать таблицу `world_object_states`
3. `WorldObjectInstanceStruct` в `WorldObjectManager` — runtime состояние (state, depletedAt)
4. Respawn ticker в WorldObjectManager (аналог mob respawn)
5. При успешном Search: запустить лут через LootManager, выдать через InventoryManager, установить depleted, broadcast
6. `worldObjectStateUpdate` broadcast при смене состояния
7. `spawnWorldObjects` пакет включает текущий state и respawnAt

**Клиент:**
1. Визуальные состояния меша (depleted вид)
2. Обработка `worldObjectStateUpdate` — обновить визуал
3. Отображение respawn countdown если нужно (опционально)

**Результат Фазы 2:** Бочки, ящики, тайники — добавляют resource-loop помимо mob loot.

---

### Фаза 3 — Activate и Use-With-Item

**Scope:** Активируемые объекты и объекты требующие предмет.

**Сервер:**
1. Новый action `set_object_state` в `DialogueActionExecutor`
2. Новое condition `object_state` в `DialogueConditionEvaluator`
3. Linked objects logic: один объект может менять state другого
4. required_item_id + consume предмета при активации

**Клиент:**
1. FX при активации (вспышка, звук, анимация меша)
2. Визуальная связь между linked объектами (если дверь открылась — анимировать)

**Результат Фазы 3:** Рычаги открывают двери, алтари требуют артефакты для квестов, кристалы активируются для зоновых ивентов.

---

### Фаза 4 — Channeled объекты

**Scope:** Длительное взаимодействие с прерыванием.

**Сервер:**
1. `ChannelSessionStruct` и хранилище активных сессий
2. Тикер для завершения channel sessions
3. Interrupt при уроне (интеграция с CombatSystem)
4. `worldObjectChannelCancel` пакет

**Клиент:**
1. Progress bar UX
2. Cancel по отпусканию кнопки или движении

**Результат Фазы 4:** Ритуалы, слежка, прослушивание — нарративные механики с tension.

---

## 11. Ограничения и Риски

### Griefing на global Activate объектах
- **Риск:** Игрок постоянно дёргает рычаг и мешает другим
- **Решение:** cooldown после активации. Condition на уровень / репутацию. Одноразовые объекты для ключевых моментов.

### Performance: getObjectsInRadius при каждом тике
- **Риск:** Если объектов много — O(N) поиск каждый тик
- **Решение:** WorldObjectManager хранит объекты с пространственным индексом (grid buckets) — заранее не нужно, только при >500 объектов на чанк

### Client-server desync на per-player Examine
- **Риск:** Клиент может показать "уже осмотрен" но сервер сбросил флаг
- **Решение:** Сервер — единственный источник правды. При `spawnWorldObjects` не передаём per-player состояние EXCEPT если нужно скрыть/изменить меш (тогда посылаем relevant flags).

### Слишком много WIO = потеря смысла
- **Дизайн-риск:** Если каждый камень интерактивен — игрок перестаёт на них реагировать
- **Правило:** WIO должен либо двигать квест, либо давать ресурс, либо рассказывать историю. Никаких "кликни чтобы прочитать лор который можно пропустить".

### Сложность создания контента
- **Риск:** Каждый WIO требует dialogue graph → медленно для контент-дизайнеров
- **Решение:** Шаблонные dialogue graphs для Examine ("просто флаг + текст") без ветвления. Создание через JSON конфиг без программирования.
