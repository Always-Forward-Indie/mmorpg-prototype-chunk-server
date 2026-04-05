# 04. Инвентарь, предметы и экипировка

## Обзор

Инвентарь — server-authoritative. Все операции проходят валидацию:
- Проверка владельца (`characterId == playerId`)
- Проверка существования предмета
- Проверка стаков, веса, уровня
- Моментальная персистентность в БД через game-server

---

## 4.1. getPlayerInventory — Запрос инвентаря

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "getPlayerInventory",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400450,
      "requestId": "sync_1711709400450_42_230_abc"
    }
  },
  "body": {
    "characterId": 7
  }
}
```

### Сервер → Unicast

```json
{
  "header": {
    "message": "Inventory retrieved successfully!",
    "hash": "auth_token",
    "clientId": 42,
    "eventType": "getPlayerInventory"
  },
  "body": {
    "characterId": 7,
    "gold": 1500,
    "items": [
      {
        "id": 101,
        "itemId": 5,
        "quantity": 1,
        "durabilityCurrent": 85,
        "durabilityMax": 100,
        "item": {
          "id": 5,
          "slug": "iron_sword",
          "itemTypeSlug": "weapon",
          "raritySlug": "common",
          "weight": 3.5,
          "stackMax": 1,
          "durabilityMax": 100,
          "vendorPriceBuy": 150,
          "vendorPriceSell": 50,
          "equipSlotSlug": "main_hand",
          "levelRequirement": 1,
          "isTwoHanded": false,
          "isEquippable": true,
          "isDurable": true,
          "isTradable": true,
          "isUsable": false,
          "isQuestItem": false,
          "attributes": [
            { "slug": "physical_attack", "name": "Physical Attack", "value": 12, "apply_on": "equip" }
          ],
          "useEffects": []
        }
      },
      {
        "id": 102,
        "itemId": 10,
        "quantity": 5,
        "durabilityCurrent": 0,
        "durabilityMax": 0,
        "item": {
          "id": 10,
          "slug": "health_potion",
          "itemTypeSlug": "consumable",
          "raritySlug": "common",
          "weight": 0.5,
          "stackMax": 20,
          "isUsable": true,
          "isEquippable": false,
          "isDurable": false,
          "useEffects": [
            {
              "effectSlug": "hp_restore",
              "attributeSlug": "hp",
              "value": 50.0,
              "isInstant": true,
              "durationSeconds": 0,
              "tickMs": 0,
              "cooldownSeconds": 30
            }
          ]
        }
      }
    ]
  }
}
```

### Поля PlayerInventoryItemStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | ID в player_inventory (PK базы данных) |
| `itemId` | int | Шаблон предмета (item_templates.id) |
| `quantity` | int | Количество в стаке |
| `durabilityCurrent` | int | Текущая прочность (0 = не дюрабельный) |
| `durabilityMax` | int | Макс. прочность |
| `item` | object | Вложенный ItemDataStruct (см. ниже) |

### Поля ItemDataStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | ID шаблона предмета |
| `slug` | string | Уникальный slug (ключ локализации) |
| `isQuestItem` | bool | Квестовый предмет |
| `itemTypeSlug` | string | `"weapon"`, `"armor"`, `"consumable"`, `"material"` и т.д. |
| `isDurable` | bool | Имеет прочность |
| `isTradable` | bool | Можно торговать |
| `isEquippable` | bool | Можно экипировать |
| `isUsable` | bool | Можно использовать из инвентаря |
| `isHarvest` | bool | Получается только харвестом |
| `weight` | float | Вес предмета |
| `raritySlug` | string | `"common"`, `"uncommon"`, `"rare"`, `"very_rare"`, `"legendary"` |
| `stackMax` | int | Макс. размер стака |
| `durabilityMax` | int | Макс. прочность |
| `vendorPriceBuy` | int | Цена покупки у NPC |
| `vendorPriceSell` | int | Цена продажи NPC |
| `equipSlotSlug` | string | Слот экипировки |
| `levelRequirement` | int | Требование по уровню |
| `isTwoHanded` | bool | Двуручное оружие |
| `allowedClassIds` | int[] | Ограничения по классам (пустой = все) |
| `setId` | int | ID сета (0 = нет) |
| `setSlug` | string | Slug сета |
| `masterySlug` | string | Мастерство (напр. `"sword_mastery"`) |
| `attributes` | array | Модификаторы атрибутов |
| `useEffects` | array | Эффекты использования |

### ItemAttributeStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `slug` | string | Slug атрибута (напр. `"physical_attack"`, `"strength"`) |
| `name` | string | Имя атрибута |
| `value` | int | Значение модификатора |
| `apply_on` | string | `"equip"` (при носке) или `"use"` (при использовании) |

### ItemUseEffectStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `effectSlug` | string | ID эффекта (`"hp_restore"`, `"strength_buff"`) |
| `attributeSlug` | string | Целевой атрибут (`"hp"`, `"mp"`, `"strength"`) |
| `value` | float | Количество восстановления / сила баффа |
| `isInstant` | bool | `true` = мгновенно, `false` = длительный |
| `durationSeconds` | int | Длительность (0 = мгновенно) |
| `tickMs` | int | Интервал тика для HoT (0 = не-тиковый) |
| `cooldownSeconds` | int | Кулдаун предмета |

---

## 4.2. itemPickup — Подбор предмета с земли

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "itemPickup",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400300,
      "requestId": "sync_1711709400300_42_200_abc"
    }
  },
  "body": {
    "characterId": 7,
    "itemUID": 12345
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа |
| `itemUID` | int | UID предмета на земле |

### Сервер → Broadcast (успех)

```json
{
  "header": {
    "message": "Item picked up",
    "hash": "",
    "eventType": "itemPickup"
  },
  "body": {
    "success": true,
    "characterId": 7,
    "droppedItemUID": 12345
  }
}
```

### Сервер → Broadcast (неудача)

```json
{
  "header": {
    "message": "Item pickup failed",
    "hash": "",
    "eventType": "itemPickup"
  },
  "body": {
    "success": false,
    "characterId": 7,
    "droppedItemUID": 12345
  }
}
```

**Валидация:**
- `playerId == characterId`
- DroppedItem с таким UID существует
- Проверка дистанции
- Проверка резервации (`reservedForCharacterId`)
- Проверка `canBePickedUp`

---

## 4.3. dropItem — Выбросить предмет

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "dropItem",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400350,
      "requestId": "sync_1711709400350_42_210_abc"
    }
  },
  "body": {
    "itemId": 5,
    "quantity": 1
  }
}
```

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
        "uid": 12346,
        "itemId": 5,
        "quantity": 1,
        "canBePickedUp": true,
        "droppedByMobUID": 0,
        "droppedByCharacterId": 7,
        "reservedForCharacterId": 0,
        "reservationSecondsLeft": 0,
        "position": { "x": 143.5, "y": 88.2, "z": 0.0, "rotationZ": 0.0 },
        "item": { /* ItemDataStruct */ }
      }
    ]
  }
}
```

### Поля DroppedItemStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `uid` | int | Уникальный ID на земле |
| `itemId` | int | ID шаблона |
| `quantity` | int | Количество |
| `canBePickedUp` | bool | Можно ли подобрать |
| `droppedByMobUID` | int | UID моба (0 = не моб) |
| `droppedByCharacterId` | int | ID персонажа (0 = не игрок) |
| `reservedForCharacterId` | int | Зарезервирован для персонажа (0 = нет) |
| `reservationSecondsLeft` | int64 | Секунд до снятия резервации |
| `position` | object | Позиция на земле |
| `item` | object | полный ItemDataStruct |

---

## 4.4. useItem — Использовать предмет

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "useItem",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400400,
      "requestId": "sync_1711709400400_42_220_abc"
    }
  },
  "body": {
    "itemId": 10
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `itemId` | int | ID предмета в инвентаре |

### Серверная обработка

1. Проверяет `isUsable` на предмете
2. Проверяет кулдаун (`cooldownSeconds`)
3. Применяет каждый `useEffects[]` из `ItemDataStruct`
4. Если `isInstant`: мгновенное восстановление HP/MP
5. Если не `isInstant`: создаёт `ActiveEffectStruct` с длительностью
6. Убирает 1 из стака (или удаляет предмет при `quantity == 1`)
7. Отправляет обновление инвентаря и хил-результат

### Сервер → Broadcast (healingResult для HP/MP предметов)

```json
{
  "header": {
    "message": "success",
    "eventType": "healingResult"
  },
  "body": {
    "skillResult": {
      "casterId": 7,
      "targetId": 7,
      "skillName": "Health Potion",
      "skillSlug": "health_potion",
      "skillEffectType": "heal",
      "healing": 50,
      "finalTargetHealth": 250,
      "success": true
    }
  }
}
```

---

## 4.5. itemRemove — Удаление предметов с земли

### Сервер → Broadcast

```json
{
  "header": {
    "message": "success",
    "eventType": "itemRemove"
  },
  "body": {
    "uids": [12345, 12346]
  }
}
```

---

## 4.6. nearbyItems — Предметы на земле рядом

### Сервер → Unicast (автоматически при входе)

```json
{
  "header": {
    "message": "Nearby items",
    "hash": "",
    "eventType": "nearbyItems"
  },
  "body": {
    "items": [
      {
        "uid": 12345,
        "itemId": 5,
        "quantity": 1,
        "canBePickedUp": true,
        "position": { "x": 200.0, "y": 150.0, "z": 0.0, "rotationZ": 0.0 },
        "item": { /* ItemDataStruct */ }
      }
    ],
    "playerPosition": { "x": 143.5, "y": 88.2, "z": 0.0 }
  }
}
```

---

## 4.7. WEIGHT_STATUS — Статус веса

### Сервер → Unicast (при изменении инвентаря)

```json
{
  "header": {
    "message": "success",
    "eventType": "WEIGHT_STATUS",
    "clientId": 42
  },
  "body": {
    "characterId": 7,
    "currentWeight": 18.5,
    "weightLimit": 74.0,
    "isOverweight": false
  }
}
```

---

## Экипировка

### Слоты экипировки (EquipSlot)

| Код | Slug | Описание |
|-----|------|----------|
| 0 | `none` | Нет |
| 1 | `head` | Голова |
| 2 | `chest` | Грудь |
| 3 | `legs` | Ноги |
| 4 | `feet` | Ступни |
| 5 | `hands` | Руки |
| 6 | `waist` | Пояс |
| 7 | `necklace` | Ожерелье |
| 8 | `ring_1` | Кольцо 1 |
| 9 | `ring_2` | Кольцо 2 |
| 10 | `main_hand` | Основная рука |
| 11 | `off_hand` | Вторая рука |
| 12 | `cloak` | Плащ |

---

## 4.8. equipItem — Экипировать предмет

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "equipItem",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709401900,
      "requestId": "sync_1711709401900_42_470_abc"
    }
  },
  "body": {
    "inventoryItemId": 101
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `inventoryItemId` | int | ID в player_inventory (не itemId!) |

### Сервер → Unicast (успех)

```json
{
  "header": {
    "message": "success",
    "eventType": "EQUIP_RESULT",
    "clientId": 42
  },
  "body": {
    "equip": {
      "action": "equip",
      "inventoryItemId": 101,
      "equipSlotSlug": "main_hand",
      "swappedOutInventoryItemId": 50
    }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `action` | string | `"equip"` |
| `inventoryItemId` | int | Экипированный предмет |
| `equipSlotSlug` | string | Слот, в который помещён |
| `swappedOutInventoryItemId` | int\|null | Предмет, который был снят (null если слот был пуст) |

### Сервер → Unicast (ошибка)

```json
{
  "header": {
    "message": "error",
    "eventType": "EQUIP_RESULT"
  },
  "body": {
    "reason": "LEVEL_REQUIREMENT_NOT_MET"
  }
}
```

### Возможные ошибки экипировки

| Код | Описание |
|-----|----------|
| `ITEM_NOT_IN_INVENTORY` | Предмет не найден в инвентаре |
| `ITEM_NOT_EQUIPPABLE` | Предмет нельзя экипировать |
| `LEVEL_REQUIREMENT_NOT_MET` | Не хватает уровня |
| `CLASS_RESTRICTION` | Класс не подходит |
| `SLOT_BLOCKED_BY_TWO_HANDED` | Слот off_hand заблокирован двуручным оружием |

**Серверная логика:**
1. Находит предмет в инвентаре по `inventoryItemId`
2. Определяет слот по `equipSlotSlug` предмета
3. Проверяет двуручное оружие (блокирует off_hand)
4. Пересчитывает атрибуты (добавляет `attributes` с `apply_on: "equip"`)
5. Сохраняет в character_equipment через game-server
6. Отправляет `WEIGHT_STATUS` и `stats_update`

---

## 4.9. unequipItem — Снять экипировку

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "unequipItem",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709401950,
      "requestId": "sync_1711709401950_42_480_abc"
    }
  },
  "body": {
    "equipSlotSlug": "main_hand"
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `equipSlotSlug` | string | Slug слота для снятия |

### Сервер → Unicast

```json
{
  "header": {
    "message": "success",
    "eventType": "EQUIP_RESULT",
    "clientId": 42
  },
  "body": {
    "equip": {
      "action": "unequip",
      "inventoryItemId": 101,
      "equipSlotSlug": "main_hand",
      "swappedOutInventoryItemId": null
    }
  }
}
```

---

## 4.10. getEquipment — Запрос экипировки

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "getEquipment",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709402000,
      "requestId": "sync_1711709402000_42_490_abc"
    }
  },
  "body": {}
}
```

### Сервер → Unicast

```json
{
  "header": {
    "message": "success",
    "eventType": "EQUIPMENT_STATE",
    "clientId": 42
  },
  "body": {
    "characterId": 7,
    "slots": {
      "main_hand": {
        "inventoryItemId": 101,
        "itemId": 5,
        "itemSlug": "iron_sword",
        "durabilityCurrent": 85,
        "durabilityMax": 100,
        "isDurabilityWarning": false,
        "blockedByTwoHanded": false
      },
      "chest": {
        "inventoryItemId": 102,
        "itemId": 12,
        "itemSlug": "leather_chest",
        "durabilityCurrent": 95,
        "durabilityMax": 100,
        "isDurabilityWarning": false,
        "blockedByTwoHanded": false
      }
    }
  }
}
```

### Поля EquipmentSlotItemStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `inventoryItemId` | int | ID в player_inventory |
| `itemId` | int | ID шаблона |
| `itemSlug` | string | Slug предмета |
| `durabilityCurrent` | int | Текущая прочность |
| `durabilityMax` | int | Макс. прочность |
| `isDurabilityWarning` | bool | Флаг низкой прочности |
| `blockedByTwoHanded` | bool | Слот заблокирован двуручным |

---

## 4.11. PLAYER_EQUIPMENT_UPDATE — Обновление экипировки

### Сервер → Unicast (при изменении экипировки)

```json
{
  "header": {
    "eventType": "PLAYER_EQUIPMENT_UPDATE",
    "message": "success",
    "clientId": 42
  },
  "body": {
    "characterId": 7,
    "slots": {
      "head": { /* EquipmentSlotItemStruct */ },
      "chest": { /* EquipmentSlotItemStruct */ },
      "main_hand": { /* EquipmentSlotItemStruct */ }
    }
  }
}
```

---

## Система прочности (Durability)

- При атаке оружием: `-1` прочность за удар
- При получении урона: `-1` прочность на случайном элементе экипировки
- При смерти: `-5%` от `durabilityMax` на всех экипированных предметах
- При `durabilityCurrent == 0`: предмет **не ломается**, но перестаёт давать бонусы
- Ремонт через NPC-кузнеца (см. документ 06)

### Item Soul (Kill Count)

Оружие отслеживает количество убийств (`killCount`). Сохраняется каждые 5 убийств или при достижении тира.

---

## Валюта (золото)

Золото хранится как специальный предмет с slug `"gold_coin"` в инвентаре. Поле `gold` в ответе `getPlayerInventory` — это суммарное количество `gold_coin` в инвентаре для удобства.
