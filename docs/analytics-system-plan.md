# Analytics System — Plan

## Цель

Собирать игровую статистику во время плейтестов для последующего анализа через GM-панель.
Статистика позволяет ответить на базовые вопросы: где умирают игроки, как быстро прокачиваются,
какие квесты бросают, сколько сидят в игре, как устроена экономика.

---

## Архитектура

### Принцип

Chunk Server не имеет прямого подключения к базе данных и не должен его получать.
Вся аналитика идёт по существующему каналу Chunk Server → Game Server.
Game Server уже подключён к PostgreSQL и занимается персистентностью — он же пишет аналитику.

Для передачи аналитических событий вводится отдельный тип пакета `analytics_event`.
Это изолирует аналитику от persistence-логики (`saveInventoryChange`, `UPDATE_PLAYER_QUEST_PROGRESS` и т.д.)
и не загрязняет существующие обработчики.

### Поток данных

```
Chunk Server
  → (что произошло) → формирует AnalyticsEventStruct
  → отправляет пакет типа "analytics_event" на Game Server
  → Game Server получает, парсит event_type и payload
  → INSERT INTO game_analytics (...)
```

---

## База данных

### Таблица game_analytics

Единая таблица событий. Хранит все типы событий в одном месте.

Колонки:

- **id** — автоинкремент, первичный ключ
- **event_type** — строковый тип события (например: `player_death`, `level_up`, `quest_accept`)
- **character_id** — FK на characters, nullable (на случай если персонаж был удалён)
- **session_id** — идентификатор игровой сессии в формате `sess_{character_id}_{unix_ms}`, генерируется при входе персонажа в мир
- **level** — уровень персонажа на момент события, вынесен в отдельную колонку т.к. используется в большинстве запросов
- **zone_id** — зона/чанк где произошло событие
- **payload** — JSONB, содержит специфичные для конкретного event_type поля
- **created_at** — timestamp с timezone, DEFAULT NOW()

### Индексы

Необходимы индексы на: `event_type`, `(character_id, created_at DESC)`, `session_id`, `created_at DESC`, GIN-индекс на `payload`.

### Миграция

Создать файл `058_game_analytics.sql` в папке `docs/migrations/` по аналогии с существующими.
Таблица append-only — данные не обновляются и не удаляются вручную.

---

## Протокол пакета analytics_event

### Chunk Server → Game Server

Новый тип пакета в коммуникации между серверами.

Поля пакета:
- **eventType** — `"analytics_event"` (заголовок пакета, как у всех межсерверных сообщений)
- **analyticsType** — строка, конкретный тип игрового события (например `"player_death"`)
- **characterId** — int
- **sessionId** — string
- **level** — int, текущий уровень персонажа
- **zoneId** — int
- **payload** — JSON-объект со специфичными полями события

### Struct на Chunk Server

Создать `AnalyticsEventStruct` в `DataStructs.hpp`:
- analyticsType: string
- characterId: int
- sessionId: string
- level: int
- zoneId: int
- payload: nlohmann::json

### Event type на Chunk Server

Добавить `ANALYTICS_EVENT` в перечисление типов событий (`EventData.hpp` или аналог).

### Обработчик на Game Server

Добавить `analytics_event` в MessageHandler Game Server'а.
Создать `AnalyticsEventHandler` (или метод внутри существующего подходящего хендлера).
Хендлер парсит пакет и выполняет INSERT в `game_analytics`.

---

## Session ID

Session ID генерируется один раз при событии `joinGameCharacter` на Chunk Server.
Формат: `sess_{characterId}_{unix_timestamp_ms}`.
Хранится в `CharacterDataStruct` (или в отдельной runtime-структуре CharacterManager'а) на время пока персонаж онлайн.
Передаётся в каждом `AnalyticsEventStruct`.

---

## Какие события логировать

### Обязательные (плейтест-минимум)

**session_start** — при `joinGameCharacter`
- payload: пусто или `{}`

**session_end** — при disconnect / выходе из мира
- payload: `duration_sec`, `events_count` (сколько событий за сессию)

**level_up** — при получении нового уровня
- payload: `time_in_session_sec` (сколько секунд прошло с начала сессии)

**player_death** — при смерти персонажа
- payload: `killer_type` (mob/player/environment), `killer_slug`, `cause` (physical/magic/dot/fall)

**quest_accept** — при взятии квеста
- payload: `quest_slug`

**quest_complete** — при завершении квеста
- payload: `quest_slug`, `duration_sec` (время от accept до complete)

**quest_abandon** — при отказе от квеста (если такой механики нет явно — при failed)
- payload: `quest_slug`, `step_reached`

**mob_killed** — при убийстве моба игроком
- payload: `mob_slug`, `mob_level`

### Желательные (экономика и контент)

**item_acquired** — при получении предмета любым способом
- payload: `item_slug`, `source` (loot/quest/trade/vendor), `quantity`

**gold_change** — при любом изменении золота
- payload: `amount` (положительное = получено, отрицательное = потрачено), `source` (vendor_buy/vendor_sell/quest/trade/loot)

**skill_used** — при использовании скилла в бою (можно сэмплировать, не каждое применение)
- payload: `skill_slug`, `target_type` (mob/player), `hit` (bool), `damage`

### Опциональные (постплейтест)

**dialogue_path** — при каждом выборе в диалоге
- payload: `npc_id`, `dialogue_id`, `node_id`, `choice_index`

**title_earned** — при получении титула
- payload: `title_slug`

**respawn** — при возрождении после смерти
- payload: `respawn_type` (default/bind_point), seconds since death

---

## Где генерировать события в Chunk Server

| Событие | Место в коде |
|---|---|
| session_start / session_end | ClientEventHandler, при join/disconnect |
| level_up | ExperienceEventHandler |
| player_death | CombatEventHandler (при hp ≤ 0) или DeathRespawnHandler |
| quest_accept / complete / abandon | DialogueEventHandler (DialogueActionExecutor) |
| mob_killed | MobEventHandler (MOB_DEATH) — только если убийца — игрок |
| item_acquired | ItemEventHandler / HarvestEventHandler |
| gold_change | VendorEventHandler + DialogueActionExecutor (give_gold) |
| skill_used | CombatEventHandler (COMPLETE_COMBAT_ACTION) |

В каждом месте: сформировать `AnalyticsEventStruct`, заполнить поля, отправить через `GameServerWorker.sendDataToGameServer()`.

---

## Что НЕ нужно делать

- Не логировать позицию персонажа каждые N секунд — это не нужно для плейтеста и создаёт огромный объём
- Не логировать каждый тик DoT/HoT — только результаты (смерть, значимые события)
- Не логировать chat-сообщения без явной необходимости
- Не делать аналитику синхронной — INSERT в аналитику не должен блокировать игровой поток. Game Server пишет асинхронно или через отдельный worker

---

## Порядок реализации

1. Создать SQL миграцию `058_game_analytics.sql` — таблица + индексы
2. Добавить `AnalyticsEventStruct` в `DataStructs.hpp` на Chunk Server
3. Добавить `ANALYTICS_EVENT` в EventData
4. Добавить парсинг `analytics_event` в MessageHandler Game Server'а
5. Реализовать INSERT в Game Server (отдельный метод в Database или новый хендлер)
6. Добавить генерацию `session_id` при joinGameCharacter, сохранить в CharacterManager
7. Подключить `session_start` и `session_end` через ClientEventHandler
8. Подключить `level_up` и `player_death` — это самые ценные для плейтеста
9. Подключить quest-события через DialogueActionExecutor
10. Остальные события по мере готовности
