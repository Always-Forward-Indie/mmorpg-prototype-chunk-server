# Документация пакетов клиента для системы харвеста

## Обзор

Данный документ содержит описание всех пакетов, которые клиент должен отправлять серверу для взаимодействия с системой харвеста. Система харвеста позволяет игрокам собирать специальные ресурсы с трупов убитых мобов.

## Архитектура пакетов

Все пакеты имеют единую структуру:
```json
{
  "header": {
    "message": "string",
    "hash": "string", 
    "clientId": "string",
    "eventType": "string"
  },
  "body": {
    // Специфичные данные для каждого типа события
  }
}
```

## События системы харвеста

### 1. Запрос списка ближайших трупов

**Тип события:** `getNearbyCorpses`

**Описание:** Запрашивает у сервера список всех трупов мобов в радиусе игрока, которые можно харвестить.

**Пример пакета:**
```json
{
  "header": {
    "message": "get nearby corpses",
    "hash": "user_session_hash_123", 
    "clientId": "12345",
    "eventType": "getNearbyCorpses"
  },
  "body": {}
}
```

**Обработка на сервере:**
- Обрабатывается в `EventDispatcher::handleGetNearbyCorpses()`
- Создает событие `Event::GET_NEARBY_CORPSES`
- Передается в `HarvestEventHandler::handleGetNearbyCorpses()`

---

### 2. Начало харвестинга

**Тип события:** `harvestStart`

**Описание:** Начинает процесс харвестинга указанного трупа. Сервер проверяет расстояние до трупа, доступность для харвестинга и начинает таймер.

**Пример пакета:**
```json
{
  "header": {
    "message": "start harvest",
    "hash": "user_session_hash_123",
    "clientId": "12345", 
    "eventType": "harvestStart"
  },
  "body": {
    "corpseUID": 1001
  }
}
```

**Обязательные поля в body:**
- `corpseUID` (int) - Уникальный идентификатор трупа моба

**Обработка на сервере:**
- Обрабатывается в `EventDispatcher::handleHarvestStart()`
- Парсит `corpseUID` из `body`
- Создает `HarvestRequestStruct` с данными:
  - `characterId` - ID персонажа из серверной сессии
  - `playerId` - ID клиента
  - `corpseUID` - UID трупа из запроса
- Создает событие `Event::HARVEST_START_REQUEST`
- Передается в `HarvestEventHandler::handleHarvestStartRequest()`

---

### 3. Отмена харвестинга

**Тип события:** `harvestCancel`

**Описание:** Отменяет текущий процесс харвестинга для игрока.

**Пример пакета:**
```json
{
  "header": {
    "message": "cancel harvest",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "harvestCancel"
  },
  "body": {}
}
```

**Обработка на сервере:**
- Обрабатывается в `EventDispatcher::handleHarvestCancel()`
- Создает событие `Event::HARVEST_CANCELLED` с данными персонажа
- Передается в `HarvestEventHandler::handleHarvestCancel()`

---

### 4. Подбор лута с трупа

**Тип события:** `corpseLootPickup`

**Описание:** Подбирает определенные предметы из лута, сгенерированного после харвестинга трупа.

**Пример пакета:**
```json
{
  "header": {
    "message": "pickup corpse loot",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "corpseLootPickup"
  },
  "body": {
    "playerId": 12345,
    "corpseUID": 1001,
    "requestedItems": [
      {
        "itemId": 101,
        "quantity": 2
      },
      {
        "itemId": 102, 
        "quantity": 1
      }
    ]
  }
}
```

**Обязательные поля в body:**
- `playerId` (int) - ID игрока (для верификации безопасности)
- `corpseUID` (int) - UID трупа с которого подбирается лут
- `requestedItems` (array) - Массив предметов для подбора:
  - `itemId` (int) - ID предмета
  - `quantity` (int) - Количество для подбора

**Обработка на сервере:**
- Обрабатывается в `EventDispatcher::handleCorpseLootPickup()`
- Создает `CorpseLootPickupRequestStruct` с данными:
  - `characterId` - ID персонажа из серверной сессии  
  - `playerId` - ID из клиентского запроса (для верификации)
  - `corpseUID` - UID трупа
  - `requestedItems` - Вектор пар (itemId, quantity)
- Выполняет проверку безопасности: `playerId == characterId`
- Создает событие `Event::CORPSE_LOOT_PICKUP`
- Передается в `HarvestEventHandler::handleCorpseLootPickup()`

## Ответы сервера

### Успешное начало харвестинга
```json
{
  "header": {
    "message": "harvest started",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "harvestStarted"
  },
  "body": {
    "type": "HARVEST_STARTED",
    "clientId": 12345,
    "playerId": 12345,
    "corpseId": 1001,
    "harvestDuration": 3.0,
    "message": "Harvest started successfully"
  }
}
```

### Ошибка - игрок не найден
```json
{
  "header": {
    "message": "Player not found",
    "hash": "user_session_hash_123", 
    "clientId": "12345",
    "eventType": "harvestError"
  },
  "body": {
    "type": "HARVEST_ERROR",
    "clientId": 12345,
    "playerId": 12345,
    "corpseId": 1001,
    "errorCode": "PLAYER_NOT_FOUND",
    "message": "Player not found"
  }
}
```

### Ошибка - труп недоступен
```json
{
  "header": {
    "message": "Corpse not available",
    "hash": "user_session_hash_123",
    "clientId": "12345", 
    "eventType": "harvestError"
  },
  "body": {
    "type": "HARVEST_ERROR",
    "clientId": 12345,
    "playerId": 12345,
    "corpseId": 1001,
    "errorCode": "CORPSE_NOT_AVAILABLE",
    "message": "Corpse not available for harvest"
  }
}
```

### Список ближайших трупов
```json
{
  "header": {
    "message": "nearby corpses list",
    "hash": "user_session_hash_123",
    "clientId": "12345", 
    "eventType": "nearbyCorpsesList"
  },
  "body": {
    "type": "NEARBY_CORPSES_LIST",
    "corpses": [
      {
        "mobUID": 1001,
        "mobId": 5,
        "position": {
          "positionX": 100.5,
          "positionY": 200.3
        },
        "hasBeenHarvested": false,
        "harvestedByCharacterId": 0,
        "interactionRadius": 150.0
      }
    ]
  }
}
```

### Успешный подбор лута
```json
{
  "header": {
    "message": "corpse loot pickup successful",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "corpseLootPickupSuccess"
  },
  "body": {
    "type": "CORPSE_LOOT_PICKUP_SUCCESS",
    "characterId": 12345,
    "corpseUID": 1001,
    "pickedUpItems": [
      {
        "itemId": 101,
        "quantity": 2,
        "name": "Wolf Pelt"
      }
    ],
    "message": "Items picked up successfully"
  }
}
```

## Структуры данных сервера

### HarvestRequestStruct
```cpp
struct HarvestRequestStruct {
    int characterId = 0; // Server-side character ID from session
    int playerId = 0;    // Client-side player ID for verification  
    int corpseUID = 0;   // UID of the corpse to harvest
};
```

### HarvestableCorpseStruct
```cpp
struct HarvestableCorpseStruct {
    int mobUID = 0;                                  // Unique mob instance UID that died
    int mobId = 0;                                   // Template mob ID
    PositionStruct position;                         // Position of the corpse
    std::chrono::steady_clock::time_point deathTime; // When the mob died
    bool hasBeenHarvested = false;                   // Track if corpse has been harvested
    int harvestedByCharacterId = 0;                  // Track who harvested it (0 = no one)
    int currentHarvesterCharacterId = 0;             // Track who is currently harvesting (0 = no one)
    };
```

### CorpseLootPickupRequestStruct
```cpp
struct CorpseLootPickupRequestStruct {
    int characterId = 0;                             // Server-side character ID from session
    int playerId = 0;                                // Client-side player ID for verification
    int corpseUID = 0;                               // UID of the corpse to pickup from
    std::vector<std::pair<int, int>> requestedItems; // Vector of (itemId, quantity) pairs to pickup
};
```

## Особенности реализации

### Безопасность
- Все запросы проверяют валидность сокета клиента
- `playerId` из клиентского запроса сверяется с серверным `characterId`
- Позиция игрока берется из серверных данных, а не из клиентского запроса

### Обработка ошибок
- Проверка существования игрока
- Проверка доступности трупа для харвестинга
- Проверка расстояния до трупа
- Валидация JSON структуры запросов

### Производительность
- События собираются в батчи для оптимизации
- Используется `std::variant` для типобезопасности EventData
- Применяется `std::move` семантика где возможно

## Типичный workflow харвестинга

1. Клиент запрашивает список ближайших трупов (`getNearbyCorpses`)
2. Сервер отвечает списком доступных трупов
3. Клиент выбирает труп и отправляет запрос на харвестинг (`harvestStart`)
4. Сервер начинает процесс харвестинга и отвечает статусом
5. По завершении харвестинга сервер отправляет результат
6. Клиент запрашивает подбор лута (`corpseLootPickup`)
7. Сервер обрабатывает подбор и обновляет инвентарь

### Обработка отмены
- В любой момент клиент может отправить `harvestCancel`
- Сервер немедленно прекращает процесс харвестинга
};
```

### CorpseLootPickupRequestStruct
```cpp
struct CorpseLootPickupRequestStruct {
    int characterId = 0;                             // Server-side character ID from session
    int playerId = 0;                                // Client-side player ID for verification
    int corpseUID = 0;                               // UID of the corpse to pickup from
    std::vector<std::pair<int, int>> requestedItems; // Vector of (itemId, quantity) pairs to pickup
};
```

## Особенности реализации

### Безопасность
- Все запросы проверяют валидность сокета клиента
- `playerId` из клиентского запроса сверяется с серверным `characterId`
- Позиция игрока берется из серверных данных, а не из клиентского запроса

### Обработка ошибок
- Проверка существования игрока
- Проверка доступности трупа для харвестинга
- Проверка расстояния до трупа
- Валидация JSON структуры запросов

### Производительность
- События собираются в батчи для оптимизации
- Используется `std::variant` для типобезопасности EventData
- Применяется `std::move` семантика где возможно

## Типичный workflow харвестинга

1. Клиент запрашивает список ближайших трупов (`getNearbyCorpses`)
2. Сервер отвечает списком доступных трупов
3. Клиент выбирает труп и отправляет запрос на харвестинг (`harvestStart`)
4. Сервер начинает процесс харвестинга и отвечает статусом
5. По завершении харвестинга сервер отправляет результат
6. Клиент запрашивает подбор лута (`corpseLootPickup`)
7. Сервер обрабатывает подбор и обновляет инвентарь

### Обработка отмены
- В любой момент клиент может отправить `harvestCancel`
- Сервер немедленно прекращает процесс харвестинга

      "count": 2,
      "searchRadius": 5.0
    }
  }
}
```

**Поля corpse объекта:**
- `id` - уникальный ID трупа (для запросов харвестинга)
- `mobId` - ID шаблона моба (для определения типа лута)
- `mobName` - название моба (для отображения в UI)
- `positionX/Y/Z` - координаты трупа
- `hasBeenHarvested` - был ли труп уже собран
- `interactionRadius` - радиус взаимодействия
- `deathTime` - время смерти моба (Unix timestamp в миллисекундах)

---

### 2. Ответ на начало харвестинга

**Описание:** Сервер подтверждает или отклоняет запрос на начало харвестинга.

**Пакет (успех):**
```json
{
  "status": "success",
  "timestamp": 1692454800567,
  "data": {
    "type": "HARVEST_STARTED",
    "corpseId": 67890,
    "duration": 3000,
    "startTime": 1692454800567,
    "requiredDistance": 150.0,
    "maxMoveDistance": 50.0
  }
}
```

**Пакет (ошибка - труп недоступен):**
```json
{
  "status": "error",
  "timestamp": 1692454800567,
  "data": {
    "type": "HARVEST_ERROR",
    "error_code": "CORPSE_NOT_AVAILABLE",
    "message": "Corpse not available for harvest",
    "reason": "already_harvested"
  }
}
```

**Пакет (ошибка - слишком далеко):**
```json
{
  "status": "error",
  "timestamp": 1692454800567,
  "data": {
    "type": "HARVEST_ERROR",
    "error_code": "TOO_FAR_FROM_CORPSE",
    "message": "Player too far from corpse",
    "currentDistance": 175.5,
    "requiredDistance": 150.0
  }
}
```

**Возможные коды ошибок:**
- `CORPSE_NOT_AVAILABLE` - труп не существует или уже собран
- `TOO_FAR_FROM_CORPSE` - игрок слишком далеко от трупа
- `ALREADY_HARVESTING` - игрок уже занимается харвестингом
- `INVENTORY_FULL` - инвентарь игрока переполнен

---

### 3. Обновление прогресса харвестинга

**Описание:** Сервер периодически (каждые 500ms) отправляет обновления прогресса харвестинга.

**Пакет:**
```json
{
  "status": "info",
  "timestamp": 1692454801067,
  "data": {
    "type": "HARVEST_PROGRESS_UPDATE",
    "corpseId": 67890,
    "progress": 25.5,
    "timeElapsed": 765,
    "timeRemaining": 2235,
    "totalDuration": 3000
  }
}
```

**Поля:**
- `progress` - прогресс в процентах (0-100)
- `timeElapsed` - прошедшее время в миллисекундах
- `timeRemaining` - оставшееся время в миллисекундах
- `totalDuration` - общее время харвестинга в миллисекундах

---

### 4. Завершение харвестинга

**Описание:** Сервер уведомляет о успешном завершении харвестинга и полученном луте.

**Пакет:**
```json
{
  "status": "success",
  "timestamp": 1692454803567,
  "data": {
    "type": "HARVEST_COMPLETE",
    "corpseId": 67890,
    "harvestDuration": 3000,
    "items": [
      {
        "itemId": 201,
        "quantity": 3,
        "name": "Wolf Pelt",
        "rarity": "common",
        "addedToInventory": true
      },
      {
        "itemId": 202,
        "quantity": 1,
        "name": "Wolf Fang",
        "rarity": "uncommon",
        "addedToInventory": true
      }
    ],
    "totalItems": 2,
    "experienceGained": 15
  }
}
```

**Поля предмета:**
- `itemId` - ID предмета в базе данных
- `quantity` - количество полученных предметов
- `name` - название предмета
- `rarity` - редкость предмета
- `addedToInventory` - был ли предмет добавлен в инвентарь

---

### 5. Прерывание харвестинга

**Описание:** Сервер уведомляет о принудительном прерывании харвестинга.

**Пакет (игрок отошел слишком далеко):**
```json
{
  "status": "warning",
  "timestamp": 1692454802000,
  "data": {
    "type": "HARVEST_INTERRUPTED",
    "corpseId": 67890,
    "reason": "PLAYER_MOVED_TOO_FAR",
    "message": "Player moved too far from corpse",
    "currentDistance": 75.2,
    "maxDistance": 50.0,
    "progressLost": 45.6
  }
}
```

**Пакет (игрок начал атаку):**
```json
{
  "status": "warning",
  "timestamp": 1692454802000,
  "data": {
    "type": "HARVEST_INTERRUPTED",
    "corpseId": 67890,
    "reason": "PLAYER_STARTED_COMBAT",
    "message": "Harvest interrupted by combat",
    "progressLost": 67.3
  }
}
```

**Возможные причины прерывания:**
- `PLAYER_MOVED_TOO_FAR` - игрок отошел слишком далеко
- `PLAYER_STARTED_COMBAT` - игрок начал бой
- `PLAYER_DISCONNECTED` - игрок отключился
- `CORPSE_EXPIRED` - труп исчез (истекло время)
- `SERVER_ERROR` - ошибка сервера

---

### 6. Подтверждение отмены харвестинга

**Описание:** Сервер подтверждает получение запроса на отмену харвестинга.

**Пакет:**
```json
{
  "status": "success",
  "timestamp": 1692454802890,
  "data": {
    "type": "HARVEST_CANCELLED",
    "corpseId": 67890,
    "progressLost": 78.4,
    "reason": "MANUAL_CANCEL"
  }
}
```

---

## Рекомендации по реализации клиента

### 1. Обработка состояний
Клиент должен отслеживать следующие состояния харвестинга:
- `IDLE` - не занимается харвестингом
- `REQUESTING` - отправлен запрос на начало харвестинга
- `IN_PROGRESS` - процесс харвестинга активен
- `COMPLETING` - харвестинг завершается
- `CANCELLED` - харвестинг отменен

### 2. UI элементы
Рекомендуемые элементы пользовательского интерфейса:
- Прогресс-бар харвестинга
- Список ближайших трупов
- Кнопка отмены харвестинга
- Уведомления о полученном луте

### 3. Проверки на клиенте
Перед отправкой запроса проверьте:
- Находится ли игрок достаточно близко к трупу
- Не занимается ли игрок уже харвестингом
- Есть ли свободное место в инвентаре

### 4. Таймауты
Установите разумные таймауты для запросов:
- Запрос списка трупов: 5 секунд
- Запрос начала харвестинга: 3 секунды
- Общий таймаут харвестинга: 10 секунд

### 5. Обработка ошибок
Предусмотрите обработку всех возможных ошибок и показывайте понятные сообщения игроку.
