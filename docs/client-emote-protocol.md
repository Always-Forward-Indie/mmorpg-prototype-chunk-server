# Emote System — Protocol Reference

Документ описывает протокол системы эмоций/анимаций персонажа: DB-схему,
поток событий между серверами, пакеты Chunk Server ↔ Client, примеры
реальных JSON-пакетов основанных на коде.

---

## Обзор системы

- **`emote_definitions`** — глобальный каталог доступных эмоций (хранится в БД).
- **`character_emotes`** — таблица разблокированных эмоций конкретного персонажа (хранится в БД).
- **`EmoteManager`** — in-memory runtime-кэш chunk-сервера: определения + per-player unlocks.
- Использование эмоции **не** сохраняется в БД — только транслируется всем клиентам зоны.

---

## DB-схема (migration 053)

```sql
CREATE TABLE emote_definitions (
    id            SERIAL PRIMARY KEY,
    slug          VARCHAR(64) UNIQUE NOT NULL,  -- ключ используемый во всех пакетах
    display_name  VARCHAR(128) NOT NULL,
    animation_name VARCHAR(128) NOT NULL,       -- имя анимации для клиента
    category      VARCHAR(64) NOT NULL DEFAULT 'general',
    is_default    BOOLEAN NOT NULL DEFAULT FALSE,
    sort_order    INTEGER NOT NULL DEFAULT 0,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE character_emotes (
    id           SERIAL PRIMARY KEY,
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    emote_slug   VARCHAR(64) NOT NULL REFERENCES emote_definitions(slug) ON DELETE CASCADE,
    unlocked_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (character_id, emote_slug)
);
```

Засеянные эмоции после миграции (13 штук):

| slug | display_name | category | is_default |
|------|-------------|----------|-----------|
| `sit` | Sit | general | ✅ |
| `wave` | Wave | general | ✅ |
| `bow` | Bow | general | ✅ |
| `laugh` | Laugh | general | ✅ |
| `cry` | Cry | general | ✅ |
| `point` | Point | general | ✅ |
| `salute` | Salute | social | ❌ |
| `clap` | Clap | social | ❌ |
| `shrug` | Shrug | social | ❌ |
| `taunt` | Taunt | social | ❌ |
| `dance_basic` | Dance | dance | ❌ |
| `dance_wild` | Wild Dance | dance | ❌ |
| `dance_slow` | Slow Dance | dance | ❌ |

Дефолтные эмоции автоматически выдаются всем существующим и новым персонажам.

---

## Поток событий

### Инициализация при подключении Chunk Server

```
Game Server                      Chunk Server
     │                                │
     │◄── [TCP connect] ──────────────│
     │◄── joinChunkServer ────────────│
     │                                │
     ├── GET_EMOTE_DEFINITIONS ───────►handleGetEmoteDefinitionsEvent
     │   (доп. к GET_TITLE_DEFINITIONS)
     │                                │
     │── setEmoteDefinitionsData ─────►GameServerWorker → SET_EMOTE_DEFINITIONS
     │                                │   → EmoteEventHandler::handleSetEmoteDefinitionsEvent
     │                                │       → EmoteManager::loadEmoteDefinitions()
```

### Вход персонажа в мир

```
Client                    Chunk Server                 Game Server
  │                            │                            │
  │── joinGameCharacter ───────►│                            │
  │                            ├── getPlayerEmotesData ─────►│
  │                            │   body: {characterId: 42}   │
  │                            │                            ├── grant_default_emotes($42)
  │                            │                            ├── get_player_emotes($42)
  │                            │◄── setPlayerEmotesData ─────┤
  │                            │   body: {characterId:42,    │
  │                            │     emotes:["sit","wave",…]}│
  │                            │                            │
  │                            ├── SET_PLAYER_EMOTES event   │
  │                            │   → EmoteEventHandler::handleSetPlayerEmotesEvent
  │                            │       → EmoteManager::loadPlayerEmotes(42, slugs)
  │                            │       → отправка player_emotes клиенту
  │◄── player_emotes ──────────┤                            │
```

### Использование эмоции клиентом

```
Client                    Chunk Server
  │                            │
  │── useEmote ────────────────►│ EventDispatcher::handleUseEmote
  │   body: {emoteSlug:"wave"} │   → creates UseEmoteRequestStruct
  │                            │   → pushes USE_EMOTE event
  │                            │
  │                            ├── EmoteEventHandler::handleUseEmoteEvent
  │                            │     validates: isUnlocked(charId, "wave")
  │                            │     if valid → broadcast emoteAction to zone
  │                            │
  │◄── emoteAction (broadcast) ┤
  │   (все клиенты зоны)       │
```

### Отключение игрока

```
Chunk Server
     │
     ├── DISCONNECT_CLIENT event
     │   → ClientEventHandler::handleDisconnectClientEvent
     │       → EmoteManager::unloadPlayerEmotes(characterId)  ← cleanup
```

---

## Пакеты

### `useEmote` — Client → Chunk Server

Клиент запрашивает воспроизведение эмоции.

```json
{
  "header": {
    "eventType": "useEmote",
    "clientId": 5,
    "hash": "abc123",
    "message": ""
  },
  "body": {
    "emoteSlug": "wave"
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `body.emoteSlug` | string | slug эмоции из `emote_definitions` |

**Ошибки:**
- Если `emoteSlug` не разблокирован для персонажа — запрос молча игнорируется (server-authoritative validation).
- Если `clientId` не найден или персонаж не загружен — игнорируется.

---

### `emoteAction` — Chunk Server → Client (broadcast, все клиенты зоны)

Трансляция воспроизведения эмоции всем клиентам зоны.

```json
{
  "header": {
    "eventType": "emoteAction",
    "clientId": 0,
    "hash": "",
    "message": "success"
  },
  "body": {
    "characterId": 42,
    "emoteSlug": "wave",
    "animationName": "Anim_Wave",
    "serverTimestamp": 1720000000000
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `body.characterId` | int | ID персонажа выполняющего эмоцию |
| `body.emoteSlug` | string | Идентификатор эмоции |
| `body.animationName` | string | Имя анимации для клиента (из EmoteManager) |
| `body.serverTimestamp` | int64 | Unix timestamp в миллисекундах |

---

### `player_emotes` — Chunk Server → Client (unicast, при входе в мир)

Список разблокированных эмоций персонажа.

```json
{
  "header": {
    "eventType": "player_emotes",
    "clientId": 5,
    "hash": "",
    "message": "success"
  },
  "body": {
    "characterId": 42,
    "emotes": [
      {
        "slug": "sit",
        "displayName": "Sit",
        "animationName": "Anim_Sit",
        "category": "general",
        "sortOrder": 0
      },
      {
        "slug": "wave",
        "displayName": "Wave",
        "animationName": "Anim_Wave",
        "category": "general",
        "sortOrder": 1
      },
      {
        "slug": "bow",
        "displayName": "Bow",
        "animationName": "Anim_Bow",
        "category": "general",
        "sortOrder": 2
      }
    ]
  }
}
```

Пакет содержит только разблокированные эмоции данного персонажа, отсортированные по `sortOrder`.

---

## Межсерверный протокол (Chunk ↔ Game Server)

### `getPlayerEmotesData` — Chunk Server → Game Server

```json
{
  "header": {
    "eventType": "getPlayerEmotesData",
    "clientId": 0,
    "hash": "",
    "message": ""
  },
  "body": {
    "characterId": 42
  }
}
```

### `setPlayerEmotesData` — Game Server → Chunk Server

```json
{
  "header": {
    "eventType": "setPlayerEmotesData",
    "clientId": 42,
    "hash": "",
    "message": "success"
  },
  "body": {
    "characterId": 42,
    "emotes": ["sit", "wave", "bow", "laugh", "cry", "point"]
  }
}
```

### `getEmoteDefinitionsData` — Chunk Server → Game Server (при подключении chunk)

```json
{
  "header": {
    "eventType": "getEmoteDefinitionsData",
    "clientId": 0,
    "hash": "",
    "message": ""
  },
  "body": {}
}
```

### `setEmoteDefinitionsData` — Game Server → Chunk Server

```json
{
  "header": {
    "eventType": "setEmoteDefinitionsData",
    "clientId": 0,
    "hash": "",
    "message": "success"
  },
  "body": {
    "emotes": [
      {
        "id": 1,
        "slug": "sit",
        "displayName": "Sit",
        "animationName": "Anim_Sit",
        "category": "general",
        "isDefault": true,
        "sortOrder": 0
      },
      {
        "id": 2,
        "slug": "wave",
        "displayName": "Wave",
        "animationName": "Anim_Wave",
        "category": "general",
        "isDefault": true,
        "sortOrder": 1
      }
    ]
  }
}
```

---

## Серверные компоненты

### EmoteManager (Chunk Server — `include/services/EmoteManager.hpp`)

Потокобезопасный runtime-кэш с `std::shared_mutex`.

| Метод | Описание |
|-------|----------|
| `loadEmoteDefinitions(defs)` | Загружает/обновляет глобальный каталог эмоций |
| `getAllDefinitions()` | Возвращает все определения (отсортированы по sortOrder) |
| `getEmoteDefinition(slug)` | Возвращает определение по slug (или nullopt) |
| `loadPlayerEmotes(charId, slugs)` | Кэширует список разблокированных эмоций персонажа |
| `unloadPlayerEmotes(charId)` | Удаляет кэш при отключении персонажа |
| `isUnlocked(charId, slug)` | Server-side проверка перед трансляцией эмоции |
| `getPlayerEmotes(charId)` | Возвращает список разблокированных slugs |

### EmoteEventHandler (Chunk Server — `include/events/handlers/EmoteEventHandler.hpp`)

| Метод | Триггер | Действие |
|-------|---------|---------|
| `handleSetEmoteDefinitionsEvent` | `SET_EMOTE_DEFINITIONS` | Парсит JSON, вызывает `EmoteManager::loadEmoteDefinitions` |
| `handleSetPlayerEmotesEvent` | `SET_PLAYER_EMOTES` | Парсит JSON, загружает в EmoteManager, отправляет `player_emotes` клиенту |
| `handleUseEmoteEvent` | `USE_EMOTE` | Валидирует `isUnlocked`, транслирует `emoteAction` зоне |
| `onPlayerDisconnect(charId)` | Disconnect | Вызывает `EmoteManager::unloadPlayerEmotes` |

---

## Добавление новых эмоций

1. Вставить строку в `emote_definitions` в БД:
   ```sql
   INSERT INTO emote_definitions(slug, display_name, animation_name, category, is_default, sort_order)
   VALUES ('my_emote', 'My Emote', 'Anim_MyEmote', 'social', FALSE, 100);
   ```
2. Выдать эмоцию конкретному персонажу:
   ```sql
   INSERT INTO character_emotes(character_id, emote_slug) VALUES (42, 'my_emote');
   ```
3. Перезапустить chunk-сервер (или реализовать горячую перезагрузку через `setEmoteDefinitionsData`).

Если `is_default = TRUE` — эмоция автоматически выдаётся всем персонажам при следующем входе в мир.
