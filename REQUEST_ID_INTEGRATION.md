# Request ID Integration Guide

## Обзор

В дополнение к системе lag compensation была добавлена поддержка `requestId` для точной синхронизации пакетов между клиентом и сервером. Каждый пакет от клиента теперь может содержать уникальный идентификатор, который будет эхом возвращен в ответе.

## Формат RequestId

Клиент генерирует уникальный идентификатор в формате:
```
sync_1703123456789_123_7843_A1B2C3D4
```

Структура:
- **sync** - префикс типа (sync для синхронизации)
- **1703123456789** - timestamp создания запроса (мс)
- **123** - ID сессии или соединения
- **7843** - порядковый номер пакета в сессии
- **A1B2C3D4** - хэш для дополнительной уникальности

## Изменения в коде

### 1. TimestampStruct расширена
```cpp
struct TimestampStruct
{
    long long serverRecvMs = 0;
    long long serverSendMs = 0;
    long long clientSendMsEcho = 0;
    std::string requestId = "";  // ← Новое поле
};
```

### 2. TimestampUtils обновлены
Добавлены новые методы:
```cpp
// Извлечение requestId из JSON
static std::string extractRequestId(const nlohmann::json &requestJson);

// Создание timestamp с requestId
static TimestampStruct createReceiveTimestamp(long long clientSendMsEcho = 0, const std::string &requestId = "");

// Создание ответного timestamp с requestId
static TimestampStruct createResponseTimestamp(long long clientSendMsEcho, long long serverRecvMs, const std::string &requestId = "");
```

### 3. JSONParser расширен
Добавлены методы парсинга requestId:
```cpp
std::string parseRequestId(const char *data, size_t length);
std::string parseRequestId(const nlohmann::json &jsonData);
```

### 4. MessageHandler обновлен
Функция `parseMessageWithTimestamps` теперь извлекает requestId:
```cpp
// Parse timestamps and create receive timestamp with current server time
TimestampStruct parsedTimestamps = jsonParser_.parseTimestamps(data, messageLength);
std::string requestId = jsonParser_.parseRequestId(data, messageLength);
TimestampStruct serverTimestamps = TimestampUtils::createReceiveTimestamp(parsedTimestamps.clientSendMsEcho, requestId);
```

## Алгоритм работы

### 1. Клиент → Сервер
```json
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
```

### 2. Сервер обрабатывает
1. Извлекает `requestId` из пакета
2. Создает `TimestampStruct` с текущим временем и полученным `requestId`
3. Обрабатывает логику события
4. Подготавливает ответ

### 3. Сервер → Клиент
```json
{
  "header": {
    "requestIdEcho": "sync_1703123456789_123_7843_A1B2C3D4",
    "serverRecvMs": 1756409198952,
    "serverSendMs": 1756409199000,
    "clientSendMsEcho": 1756409198901,
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

## Преимущества RequestId

1. **Точная синхронизация**: Клиент может точно сопоставить ответ с исходным запросом
2. **Обработка дублей**: Предотвращение повторной обработки дублированных пакетов
3. **Диагностика**: Легкое отслеживание путешествия пакета в логах
4. **Асинхронность**: Поддержка множественных одновременных запросов
5. **Надежность**: Обнаружение потерянных или задержанных пакетов

## Использование в разных типах событий

### Движение персонажа
```json
// Клиент
{
  "header": {
    "requestId": "sync_1703123456789_123_7843_A1B2C3D4",
    "eventType": "moveCharacter"
  }
}

// Сервер
{
  "header": {
    "requestIdEcho": "sync_1703123456789_123_7843_A1B2C3D4",
    "eventType": "moveCharacter"
  }
}
```

### Боевые действия
```json
// Клиент
{
  "header": {
    "requestId": "sync_1703123456790_123_7844_B2C3D4E5",
    "eventType": "combatAction"
  }
}

// Сервер
{
  "header": {
    "requestIdEcho": "sync_1703123456790_123_7844_B2C3D4E5",
    "eventType": "combatActionResult"
  }
}
```

### Подбор предметов
```json
// Клиент
{
  "header": {
    "requestId": "sync_1703123456791_123_7845_C3D4E5F6",
    "eventType": "pickupItem"
  }
}

// Сервер
{
  "header": {
    "requestIdEcho": "sync_1703123456791_123_7845_C3D4E5F6",
    "eventType": "itemPickupResult"
  }
}
```

## Обратная совместимость

Система полностью обратно совместима:
- Если пакет не содержит `requestId`, поле остается пустым
- Ответ содержит `requestIdEcho` только если был получен `requestId`
- Старые клиенты продолжают работать без изменений

## Рекомендации для клиента

1. **Генерация уникальных ID**: Использовать комбинацию timestamp + sessionId + sequence + hash
2. **Кэширование запросов**: Сохранять отправленные пакеты для сопоставления с ответами
3. **Timeout механизм**: Удалять старые запросы из кэша по таймауту
4. **Порядковые номера**: Инкрементировать sequence для каждого нового пакета

## Пример клиентской реализации

```javascript
class PacketSynchronizer {
    constructor() {
        this.sessionId = Math.floor(Math.random() * 999);
        this.sequence = 0;
        this.pendingRequests = new Map();
    }

    generateRequestId() {
        const timestamp = Date.now();
        const seq = ++this.sequence;
        const hash = this.generateHash();
        return `sync_${timestamp}_${this.sessionId}_${seq}_${hash}`;
    }

    sendRequest(eventType, data) {
        const requestId = this.generateRequestId();
        const packet = {
            header: {
                requestId: requestId,
                clientSendMs: Date.now(),
                eventType: eventType
            },
            body: data
        };
        
        this.pendingRequests.set(requestId, packet);
        this.sendToServer(packet);
        
        // Auto-cleanup after timeout
        setTimeout(() => {
            this.pendingRequests.delete(requestId);
        }, 30000);
    }

    handleResponse(response) {
        const requestId = response.header.requestIdEcho;
        if (requestId && this.pendingRequests.has(requestId)) {
            const originalRequest = this.pendingRequests.get(requestId);
            this.pendingRequests.delete(requestId);
            
            // Calculate lag compensation
            const clientSendMs = originalRequest.header.clientSendMs;
            const serverRecvMs = response.header.serverRecvMs;
            const serverSendMs = response.header.serverSendMs;
            const clientRecvMs = Date.now();
            
            const networkLatency = (serverRecvMs - clientSendMs) + (clientRecvMs - serverSendMs);
            const serverProcessingTime = serverSendMs - serverRecvMs;
            
            this.processResponse(response, {
                networkLatency,
                serverProcessingTime,
                originalRequest
            });
        }
    }
}
```

## Мониторинг и диагностика

RequestId позволяет легко отслеживать производительность:

```bash
# Поиск всех операций с конкретным requestId
grep "sync_1703123456789_123_7843_A1B2C3D4" logs/*.log

# Анализ времени обработки
grep "requestId.*serverRecvMs.*serverSendMs" logs/*.log | awk '{print $serverSendMs - $serverRecvMs}'
```

## Заключение

Интеграция requestId обеспечивает надежную синхронизацию пакетов и открывает возможности для:
- Точных измерений latency
- Надежного обнаружения потерь пакетов
- Эффективной диагностики сетевых проблем
- Реализации сложных клиент-серверных протоколов

Система готова к использованию и тестированию.
