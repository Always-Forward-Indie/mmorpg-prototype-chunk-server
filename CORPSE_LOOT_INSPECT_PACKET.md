# Пакет Инспектирования Лута в Трупе (corpseLootInspect)

## Описание
Пакет `corpseLootInspect` позволяет клиенту запросить список всех доступных предметов в конкретном трупе после завершения харвеста.

## Формат запроса от клиента

```json
{
  "header": {
    "eventType": "corpseLootInspect",
    "clientId": 3,
    "hash": "b07c16f3-023d-4277-9a37-5f6d53d75519",
    "message": "inspect corpse loot"
  },
  "body": {
    "playerId": 3,
    "corpseUID": 1000009
  }
}
```

### Параметры запроса:
- `playerId` (integer): ID игрока (должен совпадать с ID сессии для безопасности)
- `corpseUID` (integer): Уникальный ID трупа для инспектирования

## Формат ответа от сервера

### Успешный ответ:
```json
{
  "body": {
    "availableLoot": [
      {
        "description": "A basic skin of small animal.",
        "isHarvestItem": true,
        "itemId": 9,
        "itemSlug": "small_animal_skin",
        "itemType": "Resource",
        "name": "Small Animal Skin",
        "quantity": 1,
        "rarityId": 1,
        "rarityName": "Common",
        "weight": 0.0
      },
      {
        "description": "Fat extracted from animal.",
        "isHarvestItem": true,
        "itemId": 10,
        "itemSlug": "animal_fat",
        "itemType": "Resource",
        "name": "Animal Fat",
        "quantity": 2,
        "rarityId": 1,
        "rarityName": "Common",
        "weight": 0.0
      }
    ],
    "corpseUID": 1000009,
    "success": true,
    "totalItems": 2,
    "type": "CORPSE_LOOT_INSPECT"
  },
  "header": {
    "clientId": "3",
    "eventType": "corpseLootInspect",
    "hash": "b07c16f3-023d-4277-9a37-5f6d53d75519",
    "message": "Corpse loot retrieved successfully",
    "status": "success",
    "timestamp": "2025-08-21 11:29:07.025",
    "version": "1.0"
  }
}
```

### Ответы с ошибками:

#### Труп не найден:
```json
{
  "body": {
    "errorCode": "CORPSE_NOT_FOUND",
    "success": false
  },
  "header": {
    "eventType": "corpseLootInspect",
    "message": "Corpse not found",
    "status": "error"
  }
}
```

#### Труп не был заха рвещен:
```json
{
  "body": {
    "errorCode": "CORPSE_NOT_HARVESTED",
    "success": false
  },
  "header": {
    "eventType": "corpseLootInspect",
    "message": "Corpse has not been harvested yet",
    "status": "error"
  }
}
```

#### Попытка инспектирования чужого харвеста:
```json
{
  "body": {
    "errorCode": "NOT_YOUR_HARVEST",
    "success": false
  },
  "header": {
    "eventType": "corpseLootInspect",
    "message": "You can only inspect loot from corpses you harvested",
    "status": "error"
  }
}
```

#### Нарушение безопасности (playerId не совпадает):
```json
{
  "body": {
    "errorCode": "SECURITY_VIOLATION",
    "success": false
  },
  "header": {
    "eventType": "corpseLootInspect",
    "message": "Security violation: player ID mismatch",
    "status": "error"
  }
}
```

## Логика обработки на сервере

1. **Валидация запроса:**
   - Проверка совпадения `playerId` с ID сессии (безопасность)
   - Проверка существования трупа
   - Проверка что труп был заха рвещен
   - Проверка что игрок является владельцем харвеста

2. **Получение данных:**
   - Извлечение списка доступного лута из `HarvestManager::getCorpseLoot()`
   - Получение детальной информации о каждом предмете из `ItemManager`

3. **Формирование ответа:**
   - Создание массива предметов с полной информацией
   - Отправка ответа клиенту

## Использование

Этот пакет полезен для:
- Просмотра доступного лута перед подбором конкретных предметов
- Проверки что осталось в трупе после частичного подбора
- Планирования инвентарного пространства перед подбором

## Связанные пакеты
- `harvestStart` - начало харвеста
- `harvestComplete` - завершение харвеста 
- `corpseLootPickup` - подбор конкретных предметов из лута
