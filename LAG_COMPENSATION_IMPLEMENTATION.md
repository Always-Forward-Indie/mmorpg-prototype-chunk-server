# Lag Compensation System Implementation with RequestId

## Обзор

Была проведена комплексная модернизация chunk server для введения системы lag compensation с использованием timestamp-ов в миллисекундах и синхронизации пакетов через requestId. Система добавляет три ключевых временных метки и идентификатор запроса во все пакеты:

- **serverRecvMs**: Время получения пакета сервером (мс с эпохи)
- **serverSendMs**: Время отправки ответа сервером (мс с эпохи)
- **clientSendMsEcho**: Эхо временной метки из запроса клиента (мс с эпохи)
- **requestId**: Эхо идентификатора запроса для синхронизации пакетов (формат: sync_timestamp_session_sequence_hash)

## Изменения в структурах данных

### 1. Обновленная структура TimestampStruct
```cpp
struct TimestampStruct
{
    long long serverRecvMs = 0;      // Время получения пакета сервером
    long long serverSendMs = 0;      // Время отправки ответа сервером
    long long clientSendMsEcho = 0;  // Эхо временной метки клиента
    std::string requestId = "";      // Эхо идентификатора запроса для синхронизации
};
```

### 2. Обновленные структуры
Добавлен `TimestampStruct timestamps` в следующие структуры:
- `MovementDataStruct` - для движения персонажа
- `MessageStruct` - для общих сообщений
- `ClientDataStruct` - для данных клиента
- `ItemPickupRequestStruct` - для подбора предметов
- `CorpseLootPickupRequestStruct` - для лута с трупов
- `CorpseLootInspectRequestStruct` - для осмотра лута
- `HarvestRequestStruct` - для сбора ресурсов
- `EventContext` - для контекста событий
- `CombatActionStruct` - для боевых действий
- `CombatResultStruct` - для результатов боя
- `CombatActionPacket` - для пакетов боевых действий

## Новые утилиты

### TimestampUtils
Создан класс утилит для работы с временными метками и requestId:

```cpp
class TimestampUtils
{
public:
    static long long getCurrentTimestampMs();
    static TimestampStruct createReceiveTimestamp(long long clientSendMsEcho = 0, const std::string &requestId = "");
    static void setServerSendTimestamp(TimestampStruct &timestamps);
    static long long extractClientTimestamp(const nlohmann::json &requestJson);
    static std::string extractRequestId(const nlohmann::json &requestJson);
    static void addTimestampsToResponse(nlohmann::json &responseJson, const TimestampStruct &timestamps);
    static void addTimestampsToHeader(nlohmann::json &responseJson, const TimestampStruct &timestamps);
    static TimestampStruct createResponseTimestamp(long long clientSendMsEcho, long long serverRecvMs, const std::string &requestId = "");
    static TimestampStruct parseTimestampsFromRequest(const nlohmann::json &requestJson);
};
```
    static void addTimestampsToHeader(nlohmann::json &responseJson, const TimestampStruct &timestamps);
    static TimestampStruct createResponseTimestamp(long long clientSendMsEcho, long long serverRecvMs);
    static TimestampStruct parseTimestampsFromRequest(const nlohmann::json &requestJson);
};
```

## Изменения в сетевой части

### NetworkManager
- Добавлен новый метод `generateResponseMessage` с поддержкой timestamps
- Автоматическое добавление `serverSendMs` при отправке ответа
- Timestamps добавляются в header ответа

### JSONParser
- Добавлены методы для парсинга timestamps из JSON
- Поддержка множественных расположений timestamps (header, body, root)

### MessageHandler
- Новый метод `parseMessageWithTimestamps()` 
- Автоматическое создание `serverRecvMs` при получении сообщения

### ResponseBuilder
- Добавлена поддержка timestamps через метод `setTimestamps()`
- Автоматическое включение timestamps в header ответа

## Обновления в обработчиках событий

### CharacterEventHandler
Обновлены методы для использования timestamps:
- `handleJoinCharacterEvent()` - теперь включает timestamps в ответ
- `handleMoveCharacterEvent()` - поддержка lag compensation для движения

### BaseEventHandler
Добавлены новые методы с поддержкой timestamps:
- `sendSuccessResponse()` с timestamps
- `sendErrorResponse()` с timestamps
- `broadcastToAllClients()` с timestamps

## Логика работы системы

1. **Получение пакета**: 
   - Клиент отправляет пакет с `clientSendMs`
   - Сервер получает пакет и устанавливает `serverRecvMs` = текущее время
   - `clientSendMsEcho` = `clientSendMs` из запроса

2. **Обработка пакета**:
   - Сервер обрабатывает логику события
   - Timestamps сохраняются в структурах данных

3. **Отправка ответа**:
   - При генерации ответа устанавливается `serverSendMs` = текущее время
   - Все три значения включаются в header ответа

## Пример использования

```json
// Запрос от клиента
{
  "header": {
    "clientSendMs": 1756409198901,
    "eventType": "moveCharacter"
  },
  "body": {
    "position": {"x": 100, "y": 200, "z": 0}
  }
}

// Ответ сервера
{
  "header": {
    "serverRecvMs": 1756409198952,
    "serverSendMs": 1756409199000,
    "clientSendMsEcho": 1756409198901,
    "eventType": "moveCharacter",
    "status": "success"
  },
  "body": {
    "character": {
      "position": {"x": 100, "y": 200, "z": 0}
    }
  }
}
```

## Система RequestId для синхронизации пакетов

### Назначение
RequestId обеспечивает точную синхронизацию между запросами клиента и ответами сервера. Клиент генерирует уникальный идентификатор для каждого пакета в формате:
```
sync_1703123456789_123_7843_A1B2C3D4
```

Где:
- `sync` - префикс для идентификации типа пакета
- `1703123456789` - timestamp создания запроса
- `123` - ID сессии/соединения
- `7843` - порядковый номер пакета
- `A1B2C3D4` - хэш для уникальности

### Реализация

1. **Парсинг requestId**:
   - `TimestampUtils::extractRequestId()` - извлечение из JSON
   - `JSONParser::parseRequestId()` - парсинг из пакета
   - Поиск в header, body и root level

2. **Интеграция в TimestampStruct**:
   - Поле `requestId` добавлено в структуру
   - Автоматическое сохранение при создании timestamps

3. **Возврат в ответе**:
   - `requestIdEcho` включается в header/body ответа
   - Позволяет клиенту сопоставить ответ с исходным запросом

### Пример использования с RequestId

```json
// Запрос от клиента
{
  "header": {
    "requestId": "sync_1703123456789_123_7843_A1B2C3D4",
    "clientSendMs": 1756409198901,
    "eventType": "moveCharacter"
  },
  "body": {
    "position": {"x": 100, "y": 200, "z": 0}
  }
}

// Ответ сервера
{
  "header": {
    "requestIdEcho": "sync_1703123456789_123_7843_A1B2C3D4",
    "serverRecvMs": 1756409198952,
    "serverSendMs": 1756409199000,
    "clientSendMsEcho": 1756409198901,
    "eventType": "moveCharacter",
    "status": "success"
  },
  "body": {
    "requestIdEcho": "sync_1703123456789_123_7843_A1B2C3D4",
    "serverRecvMs": 1756409198952,
    "serverSendMs": 1756409199000,
    "clientSendMsEcho": 1756409198901,
    "character": {
      "position": {"x": 100, "y": 200, "z": 0}
    }
  }
}
```

## Результат

Система lag compensation с поддержкой requestId успешно реализована и интегрирована во все пакеты chunk server'а. Все ответы теперь содержат необходимые временные метки и идентификаторы запросов для корректных расчетов латентности и синхронизации пакетов на клиенте. Проект успешно компилируется и готов к тестированию.

## Следующие шаги

1. Обновить аналогичным образом game-server и login-server
2. Обновить клиентскую часть для использования новых timestamp-ов и requestId
3. Протестировать систему lag compensation в реальных условиях
4. Реализовать метрики для мониторинга latency

## Следующие шаги

1. Обновить аналогичным образом game-server и login-server
2. Обновить клиентскую часть для использования новых timestamp-ов
3. Протестировать систему lag compensation в реальных условиях
4. Добавить метрики для мониторинга латентности
