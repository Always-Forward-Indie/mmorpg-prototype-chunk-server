# Система Харвестинга (Harvest/Skinning System)

## Обзор

Система харвестинга позволяет игрокам собирать дополнительные ресурсы с трупов убитых мобов. Это система взаимодействия в реальном времени, которая требует от игрока находиться рядом с трупом и проводить некоторое время для сбора лута.

## Архитектура

### Основные компоненты

1. **HarvestManager** - основной сервис управления системой харвестинга
2. **HarvestEventHandler** - обработчик событий от клиентов
3. **HarvestableCorpseStruct** - структура данных трупа, доступного для харвестинга
4. **HarvestRequestStruct** - структура запроса на харвестинг от клиента
5. **HarvestProgressStruct** - структура отслеживания прогресса харвестинга

### Жизненный цикл системы

1. **Регистрация трупа** - когда моб умирает, его труп регистрируется как доступный для харвестинга
2. **Обнаружение трупов** - игрок может запросить список ближайших трупов
3. **Начало харвестинга** - игрок начинает процесс сбора с определенного трупа
4. **Прогресс харвестинга** - система отслеживает прогресс и проверяет условия
5. **Завершение/отмена** - харвестинг завершается успешно или отменяется

## API Клиент-Сервер

### События от клиента

#### 1. Запрос ближайших трупов
**Тип события:** `GET_NEARBY_CORPSES`

**Пример пакета от клиента:**
```json
{
  "eventType": "GET_NEARBY_CORPSES",
  "clientId": 12345,
  "data": {}
}
```

**Ответ сервера:**
```json
{
  "status": "success",
  "data": {
    "message": "Nearby corpses retrieved",
    "clientId": "12345",
    "eventType": "nearbyCorpsesResponse",
    "body": {
      "corpses": [
        {
          "id": 67890,
          "mobId": 15,
          "positionX": 1250.5,
          "positionY": 750.2,
          "hasBeenHarvested": false
        },
        {
          "id": 67891,
          "mobId": 23,
          "positionX": 1275.1,
          "positionY": 780.7,
          "hasBeenHarvested": true
        }
      ],
      "count": 2
    }
  }
}
```

#### 2. Начало харвестинга
**Тип события:** `HARVEST_START_REQUEST`

**Пример пакета от клиента:**
```json
{
  "eventType": "HARVEST_START_REQUEST",
  "clientId": 12345,
  "data": {
    "characterId": 12345,
    "playerId": 98765,
    "corpseUID": 67890,
    "playerPosition": {
      "positionX": 1248.3,
      "positionY": 748.9,
      "positionZ": 0.0
    }
  }
}
```

**Ответ сервера (успех):**
```json
{
  "status": "success",
  "data": {
    "type": "HARVEST_STARTED",
    "corpseId": 67890,
    "duration": 5000
  }
}
```

**Ответ сервера (ошибка):**
```json
{
  "status": "error",
  "data": {
    "type": "HARVEST_ERROR",
    "message": "Corpse not available for harvest"
  }
}
```

#### 3. Отмена харвестинга
**Тип события:** `HARVEST_CANCELLED`

**Пример пакета от клиента:**
```json
{
  "eventType": "HARVEST_CANCELLED",
  "clientId": 12345,
  "data": {}
}
```

**Ответ сервера:**
```json
{
  "status": "success",
  "data": {
    "type": "HARVEST_CANCELLED"
  }
}
```

### События от сервера

#### 1. Обновление прогресса харвестинга
**Автоматическая отправка каждые 500ms во время харвестинга**

```json
{
  "type": "HARVEST_PROGRESS_UPDATE",
  "corpseId": 67890,
  "progress": 65,
  "timeRemaining": 1750
}
```

#### 2. Завершение харвестинга
**Отправляется при успешном завершении**

```json
{
  "type": "HARVEST_COMPLETE",
  "corpseId": 67890,
  "items": [
    {
      "itemId": 201,
      "quantity": 3,
      "name": "Wolf Pelt"
    },
    {
      "itemId": 202,
      "quantity": 1,
      "name": "Wolf Fang"
    }
  ]
}
```

#### 3. Прерывание харвестинга
**Отправляется при принудительной отмене (игрок отошел слишком далеко, началась атака и т.д.)**

```json
{
  "type": "HARVEST_INTERRUPTED",
  "corpseId": 67890,
  "reason": "Player moved too far from corpse"
}
```

## Структуры данных

### HarvestableCorpseStruct
```cpp
struct HarvestableCorpseStruct
{
    int mobUID = 0;                  // Уникальный UID экземпляра моба
    int mobId = 0;                   // ID шаблона моба
    PositionStruct position;         // Позиция трупа
    std::chrono::steady_clock::time_point deathTime;  // Время смерти
    bool hasBeenHarvested = false;   // Был ли уже собран
    int harvestedByCharacterId = 0;  // Кем был собран (0 = никем)
    float interactionRadius = 150.0f; // Радиус взаимодействия
};
```

### HarvestRequestStruct
```cpp
struct HarvestRequestStruct
{
    int characterId = 0;      // ID персонажа (серверный)
    int playerId = 0;         // ID игрока (клиентский, для проверки)
    int corpseUID = 0;        // UID трупа для харвестинга
    PositionStruct playerPosition;  // Текущая позиция игрока
};
```

### HarvestProgressStruct
```cpp
struct HarvestProgressStruct
{
    int characterId = 0;
    int corpseUID = 0;
    std::chrono::steady_clock::time_point startTime;
    float harvestDuration = 3.0f;  // Время харвестинга в секундах
    bool isActive = false;
    PositionStruct startPosition;  // Позиция начала харвестинга
    float maxMoveDistance = 50.0f; // Максимальное расстояние перемещения
};
```

## Конфигурация системы

### Константы по умолчанию
- **Время харвестинга:** 3.0 секунды
- **Радиус взаимодействия:** 150.0 единиц
- **Максимальное расстояние перемещения:** 50.0 единиц
- **Время жизни трупа:** 600 секунд (10 минут)
- **Интервал обновления прогресса:** 500ms

### Настройка лута харвестинга

В структуре `MobLootInfoStruct` добавлено поле `isHarvestOnly`, которое определяет, может ли предмет выпасть только при харвестинге:

```cpp
struct MobLootInfoStruct
{
    int mobId = 0;
    int itemId = 0;
    float dropChance = 0.0f;
    int minQuantity = 1;
    int maxQuantity = 1;
    bool isHarvestOnly = false;  // Выпадает только при харвестинге
};
```

В структуре `ItemDataStruct` добавлено поле `isHarvest`, указывающее что предмет связан с харвестингом:

```cpp
struct ItemDataStruct
{
    // ... другие поля ...
    bool isHarvest = false;  // Предмет связан с харвестингом
};
```

## Валидация и безопасность

### Проверки при начале харвестинга
1. **Существование игрока** - проверка, что персонаж существует в системе
2. **Существование трупа** - проверка, что труп доступен для харвестинга
3. **Расстояние** - игрок должен находиться в радиусе взаимодействия
4. **Состояние трупа** - труп не должен быть уже собран
5. **Активность игрока** - игрок не должен уже заниматься харвестингом

### Проверки во время харвестинга
1. **Расстояние** - игрок не должен отходить дальше максимального расстояния
2. **Состояние персонажа** - проверка, что персонаж все еще в игре
3. **Прерывания** - проверка на боевые действия или другие активности

## Интеграция с другими системами

### Связь с системой лута
- Харвестинг использует существующую систему генерации лута
- Фильтрация предметов по флагу `isHarvestOnly`
- Автоматическое добавление предметов в инвентарь игрока

### Связь с системой инвентаря
- Проверка свободного места в инвентаре
- Автоматическое добавление собранных предметов
- Уведомления об изменениях инвентаря

### Связь с системой событий
- Регистрация обработчиков событий харвестинга
- Интеграция с планировщиком для периодических обновлений
- Отправка событий клиентам через NetworkManager

## Примеры использования

### Инициализация системы
```cpp
// В GameServices
harvestManager_.setInventoryManager(&inventoryManager_);

// Регистрация обработчика событий
eventDispatcher_.registerHandler(Event::HARVEST_START_REQUEST, 
    [&](const Event& event) { 
        harvestEventHandler_.handleHarvestStartRequest(event); 
    });
```

### Регистрация трупа при смерти моба
```cpp
// В MobManager при смерти моба
PositionStruct mobPosition = mobData.characterPosition;
gameServices_.getHarvestManager().registerCorpse(mobData.uid, mobData.id, mobPosition);
```

### Периодическое обновление системы
```cpp
// В основном игровом цикле (каждую секунду)
harvestManager_.updateHarvestProgress();
harvestManager_.cleanupOldCorpses(600); // Очистка старых трупов
```

## Файлы системы

### Заголовочные файлы
- `/include/services/HarvestManager.hpp` - Основной менеджер системы
- `/include/events/handlers/HarvestEventHandler.hpp` - Обработчик событий
- `/include/data/DataStructs.hpp` - Структуры данных (обновлены)

### Исходные файлы
- `/src/services/HarvestManager.cpp` - Реализация менеджера
- `/src/events/handlers/HarvestEventHandler.cpp` - Реализация обработчика событий

### Интеграция
- Обновлен `GameServices.hpp/cpp` для включения HarvestManager
- Обновлен `EventDispatcher.hpp/cpp` для регистрации событий харвестинга
- Обновлен `CMakeLists.txt` для компиляции новых файлов

## Возможные расширения

1. **Профессии и навыки** - разные типы харвестинга требуют разных навыков
2. **Инструменты** - использование специальных инструментов для харвестинга
3. **Качество лута** - зависимость качества от навыков игрока
4. **Групповой харвестинг** - возможность харвестинга в группе
5. **Анимации** - интеграция с системой анимаций персонажа
6. **Звуковые эффекты** - звуки процесса харвестинга

## Отладка и мониторинг

Система включает подробное логирование всех операций:
- Начало и завершение харвестинга
- Ошибки валидации
- Генерация лута
- Очистка старых трупов

Логи помогают отслеживать производительность системы и выявлять проблемы.

## Тестирование

### Рекомендуемые тестовые сценарии
1. **Базовый харвестинг** - убить моба, подойти к трупу, начать харвестинг
2. **Прерывание движением** - начать харвестинг, отойти слишком далеко
3. **Множественные трупы** - убить несколько мобов, проверить список ближайших
4. **Повторный харвестинг** - попытаться собрать уже собранный труп
5. **Истечение времени** - проверить очистку старых трупов

### Ожидаемое поведение
- Харвестинг должен занимать 3 секунды
- Игрок не должен уходить дальше 50 единиц от трупа
- Каждый труп можно собрать только один раз
- Трупы исчезают через 10 минут после смерти моба

### Progress Update
```json
{
    "eventType": "HARVEST_PROGRESS_UPDATE",
    "corpseUID": 12345,
    "progress": 0.66,
    "remainingTime": 1.0
}
```

### Harvest Complete
```json
{
    "eventType": "HARVEST_COMPLETE",
    "corpseUID": 12345,
    "success": true,
    "items": [
        {
            "itemId": 101,
            "quantity": 1,
            "name": "Wolf Pelt"
        }
    ]
}
```

### Get Nearby Corpses
```json
{
    "eventType": "getNearbyCorpses"
}
```

## Integration with Existing Systems

### ItemManager
- Enhanced to support `isHarvest` flag for items
- Enhanced loot tables with `isHarvestOnly` flag

### InventoryManager
- Used to add harvested items to player inventory
- Integrates with existing inventory update events

### EventQueue System
- Harvest events use the main game server event queue
- Progress updates sent to clients via existing network infrastructure

### Scheduler Integration
- Harvest progress updates run every 1 second
- Corpse cleanup runs every 10 minutes
- Integrated with existing ChunkServer scheduler

## SOLID Principles Applied

### Single Responsibility Principle (SRP)
- `HarvestManager`: Only handles harvest logic
- `HarvestEventHandler`: Only handles harvest events
- `HarvestableCorpseStruct`: Only represents corpse data

### Open/Closed Principle (OCP)
- New harvest system extends existing loot system without modifying it
- Event system extended with new harvest events
- Existing systems unaffected by harvest additions

### Liskov Substitution Principle (LSP)
- Harvest events follow same interface as other events
- HarvestEventHandler extends BaseEventHandler properly

### Interface Segregation Principle (ISP)
- Harvest system has focused, specific interfaces
- Clients only depend on methods they use

### Dependency Inversion Principle (DIP)
- HarvestManager depends on abstractions (ItemManager, Logger)
- High-level modules don't depend on low-level details

## Thread Safety

- Uses `std::shared_mutex` for read-write locks
- Separate mutexes for corpses and active harvests
- Thread-safe random number generation
- Integrates with existing thread-safe event system

## Error Handling

- Comprehensive validation for harvest prerequisites
- Graceful handling of disconnected clients
- Automatic cleanup of invalid harvest sessions
- Detailed logging for debugging and monitoring

## Performance Considerations

- Efficient distance calculations using squared distance where possible
- Batch processing of harvest updates
- Memory cleanup for old corpses and completed harvests
- Minimal impact on existing game performance

## Testing Scenarios

1. **Basic Harvest**: Kill mob → Start harvest → Complete harvest → Receive items
2. **Distance Validation**: Try harvest from too far away
3. **Movement Interruption**: Start harvest → Move away → Harvest cancelled
4. **Double Harvest**: Try to harvest same corpse twice
5. **Concurrent Harvests**: Multiple players harvest different corpses
6. **Disconnect During Harvest**: Player disconnects while harvesting
7. **Corpse Cleanup**: Verify old corpses are cleaned up properly

## Future Enhancements

### Possible Improvements
1. **Skill-based Harvesting**: Different harvest success rates based on player skills
2. **Tool Requirements**: Require specific items to harvest certain corpses
3. **Variable Harvest Times**: Different corpse types require different harvest durations
4. **Harvest Animations**: Client-side animation support
5. **Harvest Interruption Effects**: Penalties for interrupted harvests
6. **Multiple Harvest Attempts**: Allow limited re-harvesting of same corpse

### Database Integration
For persistent worlds, consider storing:
- Harvest statistics per player
- Corpse persistence across server restarts
- Harvest skill progression
- Historical loot data

## Troubleshooting

### Common Issues
1. **Corpses not appearing**: Check mob death event propagation
2. **Harvest not starting**: Verify distance and corpse availability
3. **Progress not updating**: Check scheduler task execution
4. **Items not received**: Verify inventory manager integration
5. **Memory leaks**: Monitor corpse cleanup and harvest session cleanup
