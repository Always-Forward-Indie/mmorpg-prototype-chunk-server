# 07. Лут и харвест

## Обзор

Система лута двухэтапная:
1. **Мгновенный лут** — предметы падают на землю при смерти моба (видны всем)
2. **Харвест** — сбор дополнительных ресурсов с трупа (канализация 3 секунды)

---

## 7.1. Мгновенный лут при смерти моба

При смерти моба сервер автоматически:
1. Прокатывает drop-таблицу моба для предметов с `isHarvest == false`
2. Создаёт `DroppedItem` на земле у позиции трупа
3. Рассылает `itemDrop` broadcast

### Сервер → Broadcast (itemDrop)

```json
{
  "header": {
    "message": "Items dropped",
    "hash": "",
    "eventType": "itemDrop"
  },
  "body": {
    "droppedItems": [
      {
        "uid": 55001,
        "itemId": 15,
        "quantity": 2,
        "canBePickedUp": true,
        "droppedByMobUID": 1234,
        "droppedByCharacterId": 0,
        "reservedForCharacterId": 7,
        "reservationSecondsLeft": 60,
        "position": { "x": 200.0, "y": 150.0, "z": 0.0, "rotationZ": 0.0 },
        "item": { /* ItemDataStruct */ }
      }
    ]
  }
}
```

### Резервация лута

| Поле | Тип | Описание |
|------|-----|----------|
| `reservedForCharacterId` | int | Персонаж, нанёсший финальный удар (или высший threat). 0 = нет |
| `reservationSecondsLeft` | int64 | Секунд до снятия резервации |

Пока резервация активна, только зарезервированный персонаж может подобрать предмет.

---

## 7.2. Система харвеста

### Структуры

```cpp
struct HarvestProgressStruct {
    int characterId;
    int corpseUID;
    float duration;             // 3.0 секунды (configurable)
    float elapsed;
    PositionStruct startPosition;
    float maxMoveDistance;       // 50.0 единиц
};

struct HarvestableCorpseStruct {
    int corpseUID;
    int mobTemplateId;
    PositionStruct position;
    bool hasBeenHarvested;
    int currentHarvesterCharacterId;  // 0 = никто не канализирует
    // Таймер жизни трупа
};

struct CorpseLootStruct {
    int corpseUID;
    vector<CorpseLootItemStruct> items;  // Сгенерированный лут
    int64_t generatedAt;
};
```

---

### harvestStart — Начать сбор

> Не путать с `itemPickup`! Харвест — это отдельный процесс канализации для `isHarvest == true` предметов.

#### Клиент → Сервер

```json
{
  "header": {
    "eventType": "harvestStart",
    "clientId": 42,
    "hash": "auth_token"
  },
  "body": {
    "characterId": 7,
    "playerId": 1,
    "corpseUID": 1234
  }
}
```

#### Серверная валидация

1. Труп с `corpseUID` существует в `harvestableCorpses_`
2. Расстояние ≤ 150.0 единиц (`DEFAULT_INTERACTION_RADIUS`)
3. `hasBeenHarvested == false`
4. `currentHarvesterCharacterId == 0` (никто другой не канализирует)

#### Сервер → Broadcast (harvestStartBroadcast)

```json
{
  "header": {
    "eventType": "harvestStartBroadcast",
    "message": "Player started harvesting"
  },
  "body": {
    "type": "HARVEST_START_BROADCAST",
    "characterId": 7,
    "corpseUID": 1234,
    "position": { "x": 200.0, "y": 150.0, "z": 0.0 },
    "timestamp": 1711709400000
  }
}
```

#### Ошибки

```json
{
  "header": { "eventType": "harvestError" },
  "body": {
    "errorCode": "OUT_OF_RANGE",
    "corpseUID": 1234
  }
}
```

| Код | Причина |
|-----|---------|
| `CORPSE_NOT_FOUND` | Труп не существует или истёк |
| `OUT_OF_RANGE` | Слишком далеко (> 150.0) |
| `ALREADY_HARVESTED` | Труп уже собран |
| `ALREADY_BEING_HARVESTED` | Кто-то другой уже собирает |

---

### Процесс харвеста (серверный тик)

Каждый тик `updateHarvestProgress()`:
1. Увеличивает `elapsed` на deltaTime
2. Проверяет перемещение: `distance(currentPos, startPosition) <= 50.0`
3. Если превышено → `harvestCancelBroadcast`
4. Если `elapsed >= duration (3.0с)` → `completeHarvestAndGenerateLoot()`

---

### harvestComplete — Завершение

#### Сервер → Broadcast (harvestCompleteBroadcast)

```json
{
  "header": {
    "eventType": "harvestCompleteBroadcast",
    "message": "Player completed harvesting"
  },
  "body": {
    "type": "HARVEST_COMPLETE_BROADCAST",
    "characterId": 7,
    "corpseUID": 1234,
    "position": { "x": 200.0, "y": 150.0, "z": 0.0 },
    "timestamp": 1711709403000
  }
}
```

После завершения генерируется лут (`generateHarvestLoot`):
- Итерирует `mobLootInfo` для данного моба
- Берёт только предметы с `isHarvest == true`
- Для каждого: `random < dropChance` → предмет выпал
- Количество: `random(minQuantity, maxQuantity)`
- Лут сохраняется в `CorpseLootStruct`

---

### harvestCancel — Отмена

#### Клиент → Сервер

```json
{
  "header": { "eventType": "harvestCancel", "clientId": 42 },
  "body": {
    "characterId": 7,
    "corpseUID": 1234
  }
}
```

#### Сервер → Broadcast (harvestCancelBroadcast)

```json
{
  "header": {
    "eventType": "harvestCancelBroadcast",
    "message": "Player cancelled harvesting"
  },
  "body": {
    "type": "HARVEST_CANCEL_BROADCAST",
    "characterId": 7,
    "corpseUID": 1234,
    "reason": "player_cancelled",
    "timestamp": 1711709401500
  }
}
```

Причины отмены: `"player_cancelled"`, `"moved_too_far"`, `"player_died"`, `"interrupted"`

---

## 7.3. Осмотр лута трупа

### corpseLootInspect — Посмотреть содержимое

#### Клиент → Сервер

```json
{
  "header": { "eventType": "corpseLootInspect", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 7,
    "playerId": 1,
    "corpseUID": 1234
  }
}
```

#### Сервер → Unicast (corpseLootInspect)

```json
{
  "header": { "eventType": "corpseLootInspect", "status": "success" },
  "body": {
    "corpseUID": 1234,
    "items": [
      {
        "itemId": 20,
        "quantity": 3,
        "item": { /* ItemDataStruct */ }
      }
    ]
  }
}
```

---

### corpseLootPickup — Забрать лут

#### Клиент → Сервер

```json
{
  "header": { "eventType": "corpseLootPickup", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 7,
    "playerId": 1,
    "corpseUID": 1234,
    "requestedItems": [
      { "itemId": 20, "quantity": 3 }
    ]
  }
}
```

---

### getNearbyCorpses — Запрос трупов рядом

#### Клиент → Сервер

```json
{
  "header": { "eventType": "getNearbyCorpses", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 7,
    "playerId": 1
  }
}
```

#### Сервер → Unicast (nearbyCorpsesResponse)

```json
{
  "header": { "eventType": "nearbyCorpsesResponse", "status": "success" },
  "body": {
    "corpses": [
      {
        "corpseUID": 1234,
        "mobTemplateId": 15,
        "position": { "x": 200.0, "y": 150.0, "z": 0.0 },
        "hasBeenHarvested": false
      }
    ]
  }
}
```

---

## Генерация лута (MobLootInfo)

```cpp
struct MobLootInfoStruct {
    int mobTemplateId;
    int itemId;
    float dropChance;       // 0.0 - 1.0
    int minQuantity;
    int maxQuantity;
    bool isHarvestOnly;     // true = только через харвест, false = дроп при смерти
    string lootTier;        // "common", "uncommon", "rare" и т.д.
};
```

## Конфигурация

| Параметр | Default | Описание |
|----------|---------|----------|
| `harvest.duration` | 3.0 сек | Длительность каналки |
| `harvest.max_move_distance` | 50.0 | Макс. расстояние от точки начала |
| `harvest.interaction_radius` | 150.0 | Дистанция для начала харвеста |
