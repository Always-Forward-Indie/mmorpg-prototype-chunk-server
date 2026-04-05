# 01. Подключение, авторизация и сессия

## Общий поток подключения

Вход в игру разделён на **предварительный этап (Game Server)** и **4 фазы на Chunk Server**. Фазы 1–2 происходят пока клиент грузит карту; фаза 4 — только после того, как сцена полностью загружена (ACK от клиента).

> **Важно:** перед подключением к Chunk Server клиент обязан пройти через **Game Server** — получить адрес чанк-сервера и запустить предварительную загрузку данных персонажа.

```
Клиент              Game Server               Chunk-Сервер
  │                      │                        │
  │  ── PRE-PHASE: Game Server (TCP уже открыт) ──────────────
  ├─ joinGameClient ─────►│ header: {clientId, hash}│
  │  body: {characterId}  │                        │
  │                      ├── loadCharacter(DB) ───►│ setCharacterData push
  │◄─ joinGameClient ─────┤ body: {chunkServerData} │ (server-to-server,
  │   {ip, port, coords}  │                        │  async, до прихода клиента)
  │                      │                        │
  │  ── ФАЗА 1: идентификация на Chunk Server ─────────────────
  ├─ TCP connect ──────────────────────────────────►│
  ├─ joinGameClient ───────────────────────────────►│ header: {clientId, hash}
  │                                                 │ body: {} (clientId уже назначен)
  │◄── joinGameClient (broadcast) ─────────────────┤ регистрация сессии
  │                                                 │
  ├─ joinGameCharacter ────────────────────────────►│ body: {characterId}
  │◄── joinGameCharacter (bcast) ──────────────────┤ данные персонажа всем
  │                                                 │
  │  ── ФАЗА 2: приватные данные (пока грузится сцена) ────────
  │◄── initializePlayerSkills ─────────────────────┤ скиллы
  │◄── zone_entered ───────────────────────────────┤ зона (уведомление)
  │◄── getPlayerInventory ─────────────────────────┤ инвентарь (async от DB)
  │◄── equipmentState ─────────────────────────────┤ экипировка
  │◄── stats_update ───────────────────────────────┤ полные статы
  │◄── questUpdate / flagsUpdate ──────────────────┤ квесты, флаги (async)
  │◄── reputations / masteries ────────────────────┤ репутация, мастерство
  │                                                 │
  │  ── ФАЗА 3: ACK от клиента ────────────────────────────────
  ├─ playerReady ──────────────────────────────────►│ сцена загружена
  │◄── playerReady (ack) ──────────────────────────┤ подтверждение
  │                                                 │
  │  ── ФАЗА 4: world-state ───────────────────────────────────
  │◄── spawnNPCs ──────────────────────────────────┤ NPC мира
  │◄── spawnMobsInZone (×N) ───────────────────────┤ мобы всех зон
  │◄── nearbyItems ────────────────────────────────┤ предметы на земле
  │◄── PLAYER_EQUIPMENT_UPDATE×N ──────────────────┤ экипировка других игроков
  │                                                 │
  │  ── ГЕЙМПЛЕЙ ──────────────────────────────────────────────
  ├─ pingClient ───────────────────────────────────►│ keep-alive
  │◄── pingClient (pong) ──────────────────────────┤
  ├─ moveCharacter ────────────────────────────────►│ движение
  │◄── moveCharacter (broadcast) ──────────────────┤
  │              ... геймплей ...                   │
  ├─ [socket close] ───────────────────────────────►│
  │◄── disconnectClient (bcast) ───────────────────┤
```

---

## 1.0. joinGameClient — Game Server (предварительный шаг)

> Этот шаг выполняется **до** подключения к Chunk Server. Клиент обращается на Game Server, получает адрес чанк-сервера, и Game Server асинхронно предзагружает данные персонажа.

### Клиент → Game Server

```json
{
  "header": {
    "eventType": "joinGameClient",
    "clientId": 3,
    "hash": "auth_token_from_login_server",
    "timestamps": {
      "clientSendMsEcho": 1711709400000,
      "requestId": "sync_..."
    }
  },
  "body": {
    "characterId": 3
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `clientId` | int | ID клиента, полученный от Login Server |
| `hash` | string | Токен авторизации, полученный от Login Server |
| `body.characterId` | int | ID выбранного персонажа |

### Game Server → Клиент (unicast)

```json
{
  "header": {
    "eventType": "joinGameClient",
    "clientId": 3,
    "hash": "auth_token_from_login_server",
    "status": "success",
    "message": "Authentication success for user!",
    "timestamp": "2026-03-29 18:58:00.692",
    "version": "1.0"
  },
  "body": {
    "chunkServerData": {
      "chunkId": 1,
      "chunkIp": "192.168.50.50",
      "chunkPort": 27017,
      "chunkPosX": 0.0,
      "chunkPosY": 0.0,
      "chunkPosZ": 0.0,
      "chunkSizeX": 0.0,
      "chunkSizeY": 0.0,
      "chunkSizeZ": 0.0
    }
  }
}
```

**Серверная логика (Game Server):**
1. Валидация `clientId` и `hash`
2. Получение адреса Chunk Server из `ChunkManager` по `chunkId=1`
3. Загрузка данных персонажа из БД по `characterId`
4. Отправка `setCharacterData` на Chunk Server (server-to-server push, асинхронно)
5. Ответ клиенту с `chunkServerData`

> **Race condition**: `setCharacterData` может прийти на Chunk Server **после** `joinGameCharacter` от клиента. Chunk Server обрабатывает это через `pendingJoinRequests` — запрос ставится в очередь и выполняется, когда данные персонажа поступают.

---

## 1.1. joinGameClient — Регистрация клиента на Chunk Server

### Клиент → Chunk Server

```json
{
  "header": {
    "eventType": "joinGameClient",
    "clientId": 3,
    "hash": "auth_token_from_login_server",
    "timestamps": {
      "clientSendMsEcho": 1711709400000,
      "requestId": "sync_..."
    }
  },
  "body": {}
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `clientId` | int | ID, полученный от Login Server (не 0) |
| `hash` | string | Токен авторизации, полученный от Login Server |

### Chunk Server → Broadcast (все клиенты)

```json
{
  "header": {
    "eventType": "joinGameClient",
    "clientId": 3,
    "hash": "auth_token_from_login_server",
    "status": "success",
    "message": "Authentication success for user!",
    "timestamp": "2026-03-29 18:58:00.704",
    "version": "1.0"
  },
  "body": {}
}
```

**Серверная логика (Chunk Server):**
- Валидация: `clientId != 0` и `hash` не пустой
- `ClientManager::loadClientData` — регистрация сессии
- Привязка сокета клиента
- Broadcast всем подключённым клиентам

---

## 1.2. joinGameCharacter — Вход персонажа в мир

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "joinGameCharacter",
    "clientId": 42,
    "hash": "auth_token_from_login_server",
    "timestamps": {
      "clientSendMsEcho": 1711709400000,
      "requestId": "sync_1711709400000_42_001_abc"
    }
  },
  "body": {
    "id": 7
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `body.id` | int | ID персонажа из БД. **Именно `id`, не `characterId`** — сервер читает `body["id"]` |

### Сервер → Broadcast

```json
{
  "header": {
    "eventType": "joinGameCharacter",
    "clientId": 42,
    "status": "success",
    "message": "Authentication success for character!",
    "timestamp": "2026-03-29T14:30:00Z",
    "serverRecvMs": 1711709400010,
    "clientSendMs": 1711709400000,
    "serverSendMs": 1711709400012
  },
  "body": {
    "character": {
      "id": 7,
      "name": "Aragorn",
      "class": "Warrior",
      "race": "Human",
      "level": 5,
      "exp": {
        "current": 2400,
        "levelStart": 2000,
        "levelEnd": 3500
      },
      "stats": {
        "health": { "current": 250, "max": 250 },
        "mana": { "current": 100, "max": 100 }
      },
      "position": {
        "x": 100.5,
        "y": 200.3,
        "z": 0.0,
        "rotationZ": 1.57
      },
      "isDead": false
    }
  }
}
```

**Серверная логика при входе:**
1. Привязка `characterId` к `clientId` в `ClientManager`
2. Broadcast `joinGameCharacter` всем клиентам — остальные знают что вы вошли
3. Отправка данных **Фазы 2** только вам (пока клиент грузит сцену):
   - `initializePlayerSkills` — скиллы
   - `zone_entered` — уведомление о текущей зоне
   - запросы к game-server (async): квесты, флаги, инвентарь, эффекты, бестиарий, репутации, мастерство
4. Ответы game-server приходят асинхронно, сервер форвардит их клиенту по мере прихода

**Фаза 4 (world-state) НЕ отправляется сразу.** Дождитесь подтверждения от клиента через `playerReady` (см. §1.3).

---

## 1.3. playerReady — Подтверждение загрузки сцены (обязательный ACK)

> **ВАЖНО для разработчика клиента:**  
> Отправьте `playerReady` ровно один раз — когда игровая сцена полностью загружена и персонаж готов к игре (аналог `BeginPlay` / `OnWorldReady`).  
> Только после этого сервер пришлёт мобов, NPC, предметы на земле и экипировку других игроков.  
> До отправки `playerReady` вы уже получите скиллы, инвентарь, квесты и статы — обрабатывайте их в фоне.

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "playerReady",
    "clientId": 42,
    "hash": "auth_token_from_login_server"
  },
  "body": {
    "characterId": 7
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID вашего персонажа (тот же что в `joinGameCharacter`) |

### Сервер → Unicast (ACK)

```json
{
  "header": {
    "eventType": "playerReady",
    "status": "success",
    "message": "success",
    "clientId": 42
  },
  "body": {}
}
```

### Затем сервер автоматически отправляет (Фаза 4):

| Пакет | Описание |
|-------|----------|
| `spawnNPCs` | Все NPC мира (в радиусе 50 000 units от персонажа) |
| `spawnMobsInZone` × N | Все моб-зоны с живыми инстансами |
| `nearbyItems` | Предметы лежащие на земле |
| `PLAYER_EQUIPMENT_UPDATE` × K | Экипировка каждого уже онлайн-игрока |

**Серверная логика:**
- Устанавливает флаг `isWorldReady = true` в `ClientDataStruct`
- Дублированный `playerReady` игнорируется (idempotent)
- `broadcastEquipmentUpdate` другим клиентам при инвентарь-ответе от DB срабатывает только если `isWorldReady == true`

**Когда отправлять `playerReady` на клиенте:**
```
OnWorldReady / BeginPlay / LoadingScreenFinished → send playerReady
```

---

## 1.4. pingClient / pongClient — Keep-alive

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "pingClient",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400123,
      "requestId": "sync_1711709400123_42_100_abc"
    }
  },
  "body": {}
}
```

### Сервер → Unicast

```json
{
  "header": {
    "eventType": "pongClient",
    "clientId": 42,
    "status": "success"
  },
  "timestamps": {
    "serverRecvMs": 1711709400125,
    "serverSendMs": 1711709400126,
    "clientSendMsEcho": 1711709400123,
    "requestId": "sync_1711709400123_42_100_abc"
  }
}
```

**Для расчёта RTT на клиенте:**
```
RTT = currentTime - clientSendMsEcho
OneWay ≈ (serverRecvMs - clientSendMsEcho + currentTime - serverSendMs) / 2
```

---

## 1.5. moveCharacter — Обновление позиции

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "moveCharacter",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400200,
      "requestId": "sync_1711709400200_42_150_abc"
    }
  },
  "body": {
    "posX": 143.5,
    "posY": 88.2,
    "posZ": 0.0,
    "rotZ": 1.57
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `posX` | float | Координата X |
| `posY` | float | Координата Y |
| `posZ` | float | Координата Z |
| `rotZ` | float | Угол поворота (радианы) |

### Сервер → Broadcast (всем в зоне, **включая отправителя**)

```json
{
  "header": {
    "eventType": "moveCharacter",
    "message": "Movement success for character!",
    "clientId": 42,
    "status": "success",
    "serverRecvMs": 1711709400210,
    "clientSendMs": 1711709400200,
    "serverSendMs": 1711709400212
  },
  "body": {
    "character": {
      "id": 7,
      "position": {
        "x": 143.5,
        "y": 88.2,
        "z": 0.0,
        "rotationZ": 1.57
      }
    }
  }
}
```

### Сервер → Unicast (коррекция позиции при невалидном перемещении)

```json
{
  "header": {
    "eventType": "positionCorrection",
    "status": "error",
    "message": "Position validation failed",
    "clientId": 42
  },
  "body": {
    "characterId": 7,
    "position": {
      "x": 140.0,
      "y": 86.0,
      "z": 0.0,
      "rotationZ": 1.57
    }
  }
}
```

**Anti-cheat:** сервер проверяет дельту перемещения по `lastValidatedPosition` и `lastMoveSrvMs`. При невалидной дистанции — отправка коррекции.

**Алгоритм валидации скорости:**
```
moveSpeedStat   = attributes["move_speed"].value  // default 5
moveSpeedUnits  = moveSpeedStat × 40.0            // world-units/sec; default ~200 u/s
deltaMs         = serverRecvMs - lastMoveSrvMs
maxAllowedDist  = moveSpeedUnits × (deltaMs / 1000.0) × 1.3   // +30% buffer
actualDist      = sqrt(ΔX² + ΔY²)               // только горизонталь, вертикаль не ограничена
if (actualDist > maxAllowedDist) → positionCorrection
```

> При `positionCorrection` сервер сбрасывает `lastMoveSrvMs = 0`, чтобы следующий пакет пересчитывался с нуля и не попал в бесконечный rejec-цикл пока клиент обрабатывает телепорт.

---

## 1.6. disconnectClient — Отключение клиента

Триггерится при закрытии сокета клиентом. Нет отдельного запроса от клиента.

### Сервер → Broadcast

```json
{
  "header": {
    "eventType": "disconnectClient",
    "status": "success",
    "message": "Client disconnected"
  },
  "body": {
    "clientId": 42,
    "characterId": 7
  }
}
```

**Серверная логика:**
- Удаление из `ClientManager`
- Удаление из активных сессий
- Закрытие сокета

---

## 1.7. getConnectedCharacters — Список онлайн-игроков

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "getConnectedCharacters",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400500,
      "requestId": "sync_1711709400500_42_240_abc"
    }
  },
  "body": {}
}
```

### Сервер → Unicast

```json
{
  "header": {
    "eventType": "getConnectedCharacters",
    "status": "success",
    "clientId": 42
  },
  "body": {
    "characters": [
      {
        "clientId": 42,
        "character": {
          "id": 7,
          "name": "Aragorn",
          "class": "Warrior",
          "race": "Human",
          "level": 5,
          "position": { "x": 143.5, "y": 88.2, "z": 0.0, "rotationZ": 1.57 }
        }
      },
      {
        "clientId": 43,
        "character": {
          "id": 8,
          "name": "Legolas",
          "class": "Ranger",
          "race": "Elf",
          "level": 5,
          "position": { "x": 150.0, "y": 95.0, "z": 0.0, "rotationZ": 0.0 }
        }
      }
    ]
  }
}
```
