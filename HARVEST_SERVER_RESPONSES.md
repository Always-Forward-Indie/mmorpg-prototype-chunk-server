# Ответы сервера системы харвеста

Конкретные примеры JSON пакетов, которые сервер отправляет клиенту в различных ситуациях.

---

## 1. ХАРВЕСТ НАЧАЛСЯ УСПЕШНО ✅

**Когда:** Игрок начал харвестинг трупа, все проверки пройдены

```json
{
  "status": "success",
  "data": {
    "header": {
      "message": "Harvest started successfully",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "harvestStarted"
    },
    "body": {
      "type": "HARVEST_STARTED",
      "clientId": 12345,
      "playerId": 12345,
      "corpseId": 1001,
      "duration": 3000,
      "startTime": 1692454812345
    }
  }
}
```

---

## 2. ХАРВЕСТ УСПЕШНО ЗАВЕРШЕН ✅

**Когда:** Харвестинг завершился, лут сгенерирован и доступен для подбора

```json
{
  "status": "success", 
  "data": {
    "header": {
      "message": "Harvest completed - loot available for pickup",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "harvestComplete"
    },
    "body": {
      "type": "HARVEST_COMPLETE",
      "clientId": 12345,
      "playerId": 12345,
      "corpseId": 1001,
      "success": true,
      "totalItems": 3,
      "availableLoot": [
        {
          "itemId": 101,
          "itemSlug": "wolf_pelt",
          "quantity": 2,
          "name": "Wolf Pelt",
          "description": "Soft fur from a gray wolf",
          "rarityId": 1,
          "rarityName": "Common",
          "itemType": "Material",
          "weight": 0.5,
          "addedToInventory": false,
          "isHarvestItem": true
        },
        {
          "itemId": 102,
          "itemSlug": "wolf_fang",
          "quantity": 1,
          "name": "Wolf Fang",
          "description": "Sharp fang from a wolf",
          "rarityId": 2,
          "rarityName": "Uncommon",
          "itemType": "Material",
          "weight": 0.1,
          "addedToInventory": false,
          "isHarvestItem": true
        }
      ]
    }
  }
}
```

---

## 3. ХАРВЕСТ ОТМЕНЕН ⏹️

**Когда:** Игрок отменил харвестинг (нажал ESC или начал другое действие)

```json
{
  "status": "success",
  "data": {
    "header": {
      "message": "Harvest cancelled",
      "hash": "user_session_hash_123", 
      "clientId": "12345",
      "eventType": "harvestCancelled"
    },
    "body": {
      "type": "HARVEST_CANCELLED",
      "clientId": 12345,
      "corpseId": 1001,
      "reason": "MANUAL_CANCEL"
    }
  }
}
```

---

## 4. ОШИБКА: ТРУП УЖЕ СОБРАН ❌

**Когда:** Игрок пытается харвестить труп, который уже был собран

```json
{
  "status": "error",
  "data": {
    "header": {
      "message": "Failed to start harvest",
      "hash": "user_session_hash_123",
      "clientId": "12345", 
      "eventType": "harvestError"
    },
    "body": {
      "type": "HARVEST_ERROR",
      "clientId": 12345,
      "playerId": 12345,
      "corpseId": 1001,
      "errorCode": "HARVEST_FAILED",
      "message": "Corpse has already been harvested"
    }
  }
}
```

---

## 5. ОШИБКА: СЛИШКОМ ДАЛЕКО ❌

**Когда:** Игрок пытается харвестить труп, находясь слишком далеко

```json
{
  "status": "error",
  "data": {
    "header": {
      "message": "Failed to start harvest",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "harvestError"
    },
    "body": {
      "type": "HARVEST_ERROR", 
      "clientId": 12345,
      "playerId": 12345,
      "corpseId": 1001,
      "errorCode": "HARVEST_FAILED",
      "message": "Too far from corpse (distance: 200.5, max: 150.0)"
    }
  }
}
```

---

## 6. ОШИБКА: ТРУП НЕ НАЙДЕН ❌

**Когда:** Игрок пытается харвестить несуществующий или удаленный труп

```json
{
  "status": "error",
  "data": {
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
}
```

---

## 7. СПИСОК БЛИЖАЙШИХ ТРУПОВ 📋

**Когда:** Игрок запросил список трупов для харвестинга поблизости

```json
{
  "status": "success",
  "data": {
    "header": {
      "message": "Nearby corpses retrieved",
      "clientId": "12345",
      "eventType": "nearbyCorpsesResponse"
    },
    "body": {
      "corpses": [
        {
          "id": 1001,
          "mobId": 5,
          "positionX": 120.5,
          "positionY": 340.2,
          "hasBeenHarvested": false,
          "harvestedByCharacterId": 0,
          "currentHarvesterCharacterId": 0,
          "isBeingHarvested": false
        },
        {
          "id": 1002,
          "mobId": 7, 
          "positionX": 145.3,
          "positionY": 298.7,
          "hasBeenHarvested": true,
          "harvestedByCharacterId": 54321,
          "currentHarvesterCharacterId": 0,
          "isBeingHarvested": false
        },
        {
          "id": 1003,
          "mobId": 12,
          "positionX": 98.1,
          "positionY": 367.9,
          "hasBeenHarvested": false,
          "harvestedByCharacterId": 0,
          "currentHarvesterCharacterId": 67890,
          "isBeingHarvested": true
        }
      ],
      "count": 3
    }
  }
}
```

---

## 8. УСПЕШНЫЙ ПОДБОР ЛУТА ✅

**Когда:** Игрок успешно подобрал предметы с трупа после харвестинга

```json
{
  "status": "success",
  "data": {
    "header": {
      "message": "Items picked up successfully",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "corpseLootPickup"
    },
    "body": {
      "success": true,
      "corpseUID": 1001,
      "pickedUpItems": [
        {
          "itemId": 101,
          "itemSlug": "wolf_pelt",
          "quantity": 2,
          "name": "Wolf Pelt",
          "description": "Soft fur from a gray wolf",
          "rarityId": 1,
          "rarityName": "Common",
          "itemType": "Material",
          "weight": 0.5
        }
      ],
      "remainingLoot": [
        {
          "itemId": 102,
          "itemSlug": "wolf_fang", 
          "quantity": 1,
          "name": "Wolf Fang"
        }
      ],
      "itemsPickedUp": 1
    }
  }
}
```

---

## 9. ОШИБКА: НЕ УДАЛОСЬ ПОДОБРАТЬ ЛУТ ❌

**Когда:** Не удалось подобрать лут (нет места в инвентаре, лут уже подобран, etc.)

```json
{
  "status": "error",
  "data": {
    "header": {
      "message": "Failed to pickup items",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "corpseLootPickup"
    },
    "body": {
      "success": false,
      "errorCode": "PICKUP_FAILED",
      "corpseUID": 1001
    }
  }
}
```

---

## 10. ОШИБКА: НАРУШЕНИЕ БЕЗОПАСНОСТИ ❌

**Когда:** playerId в запросе не совпадает с серверным characterId

```json
{
  "status": "error",
  "data": {
    "header": {
      "message": "Security violation: player ID mismatch",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "corpseLootPickup"
    },
    "body": {
      "success": false,
      "errorCode": "SECURITY_VIOLATION"
    }
  }
}
```

---

## Коды ошибок

| Код ошибки | Описание |
|------------|----------|
| `HARVEST_FAILED` | Общая ошибка харвестинга |
| `CORPSE_NOT_AVAILABLE` | Труп не найден или недоступен |
| `PLAYER_NOT_FOUND` | Игрок не найден на сервере |
| `PICKUP_FAILED` | Не удалось подобрать лут |
| `SECURITY_VIOLATION` | Нарушение безопасности |
| `CORPSE_NOT_FOUND` | Труп не найден |

## Типы событий

| Тип события | Описание |
|-------------|----------|
| `harvestStarted` | Харвест начался |
| `harvestComplete` | Харвест завершен |
| `harvestCancelled` | Харвест отменен |
| `harvestError` | Ошибка харвеста |
| `nearbyCorpsesResponse` | Список ближайших трупов |
| `corpseLootPickup` | Результат подбора лута |

---

## 🆕 ДОПОЛНИТЕЛЬНАЯ ФУНКЦИОНАЛЬНОСТЬ: Просмотр лута трупа

**Примечание:** Данная функциональность НЕ реализована в текущей версии сервера, но может быть легко добавлена.

### Запрос от клиента для просмотра лута

**Тип события:** `inspectCorpseLoot`

```json
{
  "header": {
    "message": "inspect corpse loot",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "inspectCorpseLoot"
  },
  "body": {
    "corpseUID": 1001
  }
}
```

### Ответ сервера с лутом трупа

```json
{
  "status": "success",
  "data": {
    "header": {
      "message": "Corpse loot information",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "corpseLootInspection"
    },
    "body": {
      "type": "CORPSE_LOOT_INSPECTION",
      "corpseUID": 1001,
      "hasLoot": true,
      "availableLoot": [
        {
          "itemId": 101,
          "itemSlug": "wolf_pelt",
          "quantity": 2,
          "name": "Wolf Pelt",
          "description": "Soft fur from a gray wolf",
          "rarityId": 1,
          "rarityName": "Common",
          "itemType": "Material",
          "weight": 0.5,
          "isHarvestItem": true
        },
        {
          "itemId": 102,
          "itemSlug": "wolf_fang",
          "quantity": 1,
          "name": "Wolf Fang",
          "description": "Sharp fang from a wolf",
          "rarityId": 2,
          "rarityName": "Uncommon",
          "itemType": "Material",
          "weight": 0.1,
          "isHarvestItem": true
        }
      ],
      "totalItems": 2
    }
  }
}
```

### Ответ, если лута нет

```json
{
  "status": "success",
  "data": {
    "header": {
      "message": "No loot available",
      "hash": "user_session_hash_123",
      "clientId": "12345",
      "eventType": "corpseLootInspection"
    },
    "body": {
      "type": "CORPSE_LOOT_INSPECTION",
      "corpseUID": 1001,
      "hasLoot": false,
      "availableLoot": [],
      "totalItems": 0,
      "message": "This corpse has no loot available"
    }
  }
}
```

### Как это реализовать на сервере

1. **Добавить новое событие в Event.hpp:**
   ```cpp
   INSPECT_CORPSE_LOOT,     // Client requests to view corpse loot
   ```

2. **Добавить обработчик в EventDispatcher.cpp:**
   ```cpp
   else if (context.eventType == "inspectCorpseLoot")
   {
       handleInspectCorpseLoot(context, socket);
   }
   ```

3. **Реализовать метод handleInspectCorpseLoot:**
   ```cpp
   void EventDispatcher::handleInspectCorpseLoot(const EventContext &context, 
                                                 std::shared_ptr<boost::asio::ip::tcp::socket> socket)
   {
       // Парсинг corpseUID из body
       // Создание события INSPECT_CORPSE_LOOT
       // Передача в HarvestEventHandler
   }
   ```

4. **Добавить обработчик в HarvestEventHandler:**
   ```cpp
   void HarvestEventHandler::handleInspectCorpseLoot(const Event &event)
   {
       // Получить corpseUID
       // Вызвать harvestManager.getCorpseLoot(corpseUID)
       // Отправить ответ клиенту
   }
   ```

**Код уже готов:** Метод `HarvestManager::getCorpseLoot()` уже существует и возвращает `std::vector<std::pair<int, int>>` с лутом трупа!
