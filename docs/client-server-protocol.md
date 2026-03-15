# Client ↔ Server Protocol Reference

Документ описывает все пакеты данных, которыми обмениваются игровой клиент и чанк-сервер.
Охватывает систему экипировки, износ, ремонт, торговлю с NPC, P2P-торговлю и систему веса.

---

## Общий формат пакета

Все пакеты — это JSON-строки с разделителем `\n`. Структура всегда одинакова:

```json
{
  "header": {
    "eventType": "...",
    "clientId":  123,
    "hash":      "abc123",
    "message":   "success"
  },
  "body": { }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `header.eventType` | string | Идентификатор типа пакета |
| `header.clientId` | int | ID сессии клиента, присвоенный сервером при подключении |
| `header.hash` | string | Токен авторизации (выдаётся на этапе `joinGameClient`) |
| `header.message` | string | `"success"` или строка ошибки |
| `body` | object | Полезная нагрузка, специфичная для каждого пакета |

---

## 1. Подключение и инициализация сессии

### Полная последовательность входа в игру

```
Клиент                          Chunk Server                   Game Server
  │                                  │                               │
  │──── TCP connect ─────────────────►│                               │
  │                                  │  (присваивает clientId)        │
  │──── joinGameClient ──────────────►│                               │
  │                                  │  (проверка hash, loadClientData)
  │◄─── joinGameClient (broadcast) ───│  ← все клиенты видят
  │                                  │                               │
  │──── joinGameCharacter ───────────►│                               │
  │                                  │──── getPlayerQuests/Flags ────►│
  │                                  │──── getPlayerInventory ────────►│
  │                                  │──── getPlayerActiveEffects ─────►│
  │                                  │──── getPlayerPityData ──────────►│
  │                                  │──── getPlayerBestiaryData ──────►│
  │                                  │──── getPlayerReputationsData ───►│
  │                                  │──── getPlayerMasteriesData ─────►│
  │◄─── joinGameCharacter (broadcast) │  ← все клиенты видят нового персонажа
  │◄─── initializePlayerSkills ───────│  ← только вошедшему
  │◄─── spawnNPCs ────────────────────│  ← только вошедшему
  │◄─── itemDrop (snapshot) ──────────│  ← только вошедшему (наземные предметы)
  │◄─── spawnMobsInZone (×N зон) ─────│  ← только вошедшему (мобы всех зон, server-push)
  │◄─── world_notification zone_entered│  ← только вошедшему (текущая игровая зона)
  │◄─── stats_update ─────────────────│  ← только вошедшему (полные статы + инвентарь завершён)
  │◄─── getPlayerInventory ───────────│  ← только вошедшему
  │◄─── equipmentState ───────────────│  ← только вошедшему
```

### 1.1 Регистрация сессии — `joinGameClient`

**Направление клиент → сервер:**

```json
{
  "header": {
    "eventType": "joinGameClient",
    "clientId": 0,
    "hash": "abc123"
  },
  "body": {}
}
```

`hash` — токен авторизации, полученный с Login Server.

**Ответ сервера → broadcast (все клиенты):**

```json
{
  "header": {
    "eventType": "joinGameClient",
    "clientId": 42,
    "hash": "abc123",
    "message": "Authentication success for user!"
  },
  "body": {}
}
```

После получения сохрани `header.clientId` и `header.hash` — они нужны в каждом последующем пакете.

### 1.2 Вход персонажа — `joinGameCharacter`

**Направление клиент → сервер:**

```json
{
  "header": {
    "eventType": "joinGameCharacter",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "characterId": 7
  }
}
```

**Ответ сервера → broadcast (все клиенты):**

```json
{
  "header": {
    "eventType": "joinGameCharacter",
    "clientId": 42,
    "message": "Authentication success for character!"
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
        "mana":   { "current": 100, "max": 100 }
      },
      "position": { "x": 100.5, "y": 200.3, "z": 0.0, "rotationZ": 1.57 },
      "isDead": false
    }
  }
}
```

> **Важно:** `character` в `joinGameCharacter` содержит только базовые поля для отображения nameplate у других игроков. Полные статы (атрибуты, эффекты) получает только **сам вошедший** через `stats_update`.

**Обработка другими клиентами:** При получении `joinGameCharacter` — создать/обновить nameplate для `character.id` с именем, уровнем, расой, классом и позицией.

### 1.3 Отключение персонажа — `disconnectClient`

При штатном или аварийном отключении игрока сервер рассылает всем:

```json
{
  "header": {
    "eventType": "disconnectClient",
    "message": "Client disconnected"
  },
  "body": {
    "clientId": 42,
    "characterId": 7
  }
}
```

Удалить персонажа `characterId` из отображаемого мира.

### 1.4 Список предметов мира — `getItemsList` (внутренний)

Чанк-сервер получает каталог всех предметов от Game Server при запуске. Клиент этот пакет не получает — предметы всегда приходят с полными данными в инвентарных пакетах.

---

### 1.5 Движение персонажа — `moveCharacter`

### Запрос клиент → сервер

```json
{
  "header": {
    "eventType": "moveCharacter",
    "clientId": 42,
    "hash": "abc123"
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
| `posX` / `posY` / `posZ` | float | Позиция персонажа в мировом пространстве |
| `rotZ` | float | Поворот вокруг оси Z (в радианах) |

**Серверная валидация скорости:** Сервер сравнивает расстояние между текущей и предыдущей позицией с максимально допустимым за прошедшее время (на основе атрибута `move_speed` × 40.0 + 30% буфер). При нарушении — движение игнорируется и отправляется `positionCorrection`.

### Broadcast всем клиентам — `moveCharacter`

```json
{
  "header": {
    "eventType": "moveCharacter",
    "clientId": 42,
    "message": "Movement success for character!"
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

Все клиенты в зоне получают позицию персонажа `character.id` и применяют её для интерполяции.

### Коррекция позиции — `positionCorrection` ← сервер (только нарушителю)

Если сервер обнаружил превышение скорости — отправляет только виновному клиенту:

```json
{
  "header": {
    "eventType": "positionCorrection",
    "clientId": 42,
    "status": "error",
    "message": "Position validation failed"
  },
  "body": {
    "characterId": 7,
    "position": {
      "x": 140.0,
      "y": 85.0,
      "z": 0.0,
      "rotationZ": 1.57
    }
  }
}
```

Клиент должен немедленно переместить персонажа к указанной серверной позиции (телепортация без анимации).

> **Примечание:** Мёртвый персонаж не может двигаться — сервер вернёт ошибку `"Cannot move while dead"` в `header.message`.

---

## 2. Инвентарь

### 2.1 Запрос инвентаря — клиент → сервер

Не требует отдельного пакета от клиента. Сервер отправляет инвентарь автоматически после авторизации, а также в ответ на системные события.

### 2.2 Инвентарь персонажа — `getPlayerInventory` ← сервер

```json
{
  "header": { "eventType": "getPlayerInventory", "clientId": 42 },
  "body": {
    "characterId": 7,
    "items": [
      {
        "id":               101,
        "characterId":      7,
        "itemId":           5,
        "quantity":         3,
        "isEquipped":       false,
        "durabilityCurrent": 80,
        "slug":             "iron_sword",
        "isQuestItem":      false,
        "itemType":         2,
        "itemTypeName":     "Weapon",
        "itemTypeSlug":     "weapon",
        "isContainer":      false,
        "isDurable":        true,
        "isTradable":       true,
        "weight":           3.5,
        "rarityId":         1,
        "rarityName":       "Common",
        "raritySlug":       "common",
        "stackMax":         1,
        "durabilityMax":    100,
        "vendorPriceBuy":   150,
        "vendorPriceSell":  75,
        "equipSlot":        3,
        "equipSlotName":    "Main Hand",
        "equipSlotSlug":    "main_hand",
        "levelRequirement": 5,
        "attributes": [
          { "id": 1, "name": "Attack Power", "slug": "attack_power", "value": 12 }
        ]
      }
    ]
  }
}
```

> **Примечание.** Поля `name`, `description`, `isTwoHanded`, `allowedClassIds`, `setId`, `setSlug` **не** включаются в пакет инвентаря. Для отображения имени предмета клиент использует поле `slug` как ключ локализации (см. `docs/client-world-systems-protocol.md`). Эти поля есть только в каталоге предметов (который хранится на сервере). Клиент может использовать данные из `EQUIPMENT_STATE` и `EQUIP_RESULT` для понимания ограничений.

### 2.3 Обновление инвентаря — `inventoryUpdate` ← сервер (при изменениях)

Аналогичная структура с полем `items` — отправляется при добавлении/удалении предметов.

### 2.4 Подбор предмета с земли — клиент → сервер

`eventType: "itemPickup"` в теле запроса от клиента:

```json
{
  "header": { "eventType": "itemPickup", "clientId": 42, "hash": "abc123" },
  "body": {
    "characterId": 7,
    "itemUID":     9001,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

> **Примечание.** `characterId` используется сервером как проверка безопасности — сравнивается с ID персонажа, сохранённым в сессии. `itemUID` — уникальный идентификатор предмета на земле (поле `uid` из пакета `droppedItems`).

### 2.5 Результат подбора — `itemPickup` ← сервер (broadcast)

```json
{
  "header": { "eventType": "itemPickup" },
  "body": {
    "success":        true,
    "characterId":    7,
    "droppedItemUID": 9001
  }
}
```

Пакет рассылается **всем** клиентам в зоне. После успешного подбора тому, кто поднял предмет, также приходит `WEIGHT_STATUS` (см. секцию 4.3).

---

## 3. Система экипировки

### Слоты экипировки

| Slug | Описание |
|------|----------|
| `head` | Голова |
| `chest` | Грудь |
| `legs` | Ноги |
| `feet` | Обувь |
| `hands` | Перчатки |
| `waist` | Пояс |
| `necklace` | Ожерелье |
| `ring_1` | Кольцо 1 |
| `ring_2` | Кольцо 2 |
| `main_hand` | Основная рука |
| `off_hand` | Вспомогательная рука |
| `cloak` | Плащ |

### 3.1 Надеть предмет — клиент → сервер

```json
{
  "header": { "eventType": "equipItem", "clientId": 42, "hash": "abc123" },
  "body": {
    "inventoryItemId": 101
  }
}
```

`inventoryItemId` — это `player_inventory.id` (поле `id` в объекте инвентаря).

### 3.2 Снять предмет — клиент → сервер

```json
{
  "header": { "eventType": "unequipItem", "clientId": 42, "hash": "abc123" },
  "body": {
    "equipSlotSlug": "main_hand"
  }
}
```

### 3.3 Запрос состояния экипировки — клиент → сервер

```json
{
  "header": { "eventType": "getEquipment", "clientId": 42, "hash": "abc123" },
  "body": {}
}
```

### 3.4 Результат действия — `EQUIP_RESULT` ← сервер

Отправляется только тому клиенту, который инициировал действие.

**При успешном надевании:**

```json
{
  "header": { "eventType": "EQUIP_RESULT", "clientId": 42, "message": "success" },
  "body": {
    "equip": {
      "action":                   "equip",
      "inventoryItemId":          101,
      "equipSlotSlug":            "main_hand",
      "swappedOutInventoryItemId": null
    }
  }
}
```

Если слот был занят, `swappedOutInventoryItemId` содержит `id` предмета, который был автоматически снят и возвращён в инвентарь.

**При успешном снятии:**

```json
{
  "header": { "eventType": "EQUIP_RESULT", "clientId": 42, "message": "success" },
  "body": {
    "unequip": {
      "action":                   "unequip",
      "inventoryItemId":          101,
      "equipSlotSlug":            "main_hand",
      "swappedOutInventoryItemId": null
    }
  }
}
```

**При ошибке:**

```json
{
  "header": { "eventType": "EQUIP_RESULT", "clientId": 42, "message": "LEVEL_REQUIREMENT_NOT_MET" }
}
```

Возможные коды ошибок:

| Код | Причина |
|-----|---------|
| `ITEM_NOT_IN_INVENTORY` | Предмет не найден в инвентаре персонажа |
| `ITEM_NOT_EQUIPPABLE` | Предмет нельзя надевать |
| `LEVEL_REQUIREMENT_NOT_MET` | Уровень персонажа ниже требуемого |
| `CLASS_RESTRICTION` | Класс персонажа не допускает этот предмет |
| `SLOT_BLOCKED_BY_TWO_HANDED` | Слот off_hand заблокирован двуручным оружием |
| `EQUIP_FAILED` | Внутренняя ошибка |
| `SLOT_EMPTY` | Снять нечего (слот пуст) |

### 3.5 Состояние экипировки — `EQUIPMENT_STATE` ← сервер

Отправляется после каждого экипирования/снятия, после `getEquipment` и при заходе персонажа в зону.

```json
{
  "header": { "eventType": "EQUIPMENT_STATE", "clientId": 42, "message": "success" },
  "body": {
    "characterId": 7,
    "slots": {
      "head":      null,
      "chest":     {
        "inventoryItemId":  203,
        "itemId":           8,
        "itemSlug":         "iron_chest",
        "durabilityCurrent": 95,
        "durabilityMax":    100,
        "isDurabilityWarning": false,
        "blockedByTwoHanded": false
      },
      "main_hand": {
        "inventoryItemId":  101,
        "itemId":           5,
        "itemSlug":         "iron_sword",
        "durabilityCurrent": 80,
        "durabilityMax":    100,
        "isDurabilityWarning": false,
        "blockedByTwoHanded": false
      },
      "off_hand":  { "blockedByTwoHanded": true },
      "legs":      null,
      "feet":      null,
      "hands":     null,
      "waist":     null,
      "necklace":  null,
      "ring_1":    null,
      "ring_2":    null,
      "cloak":     null
    }
  }
}
```

Значения слота:
- `null` — слот пуст и свободен
- `{ "blockedByTwoHanded": true }` — слот заблокирован двуручным оружием (только для `off_hand`)
- объект с полями — слот занят предметом

**`isDurabilityWarning`** становится `true`, когда `durabilityCurrent / durabilityMax < durability.warning_threshold_pct` (по умолчанию 30%). Клиент должен отображать визуальное предупреждение.

### 3.6 Обновление атрибутов — `charAttributesUpdate` ← сервер

Автоматически отправляется после экипирования/снятия, так как бонусы от предметов и сет-бонусы пересчитываются на стороне БД.

```json
{
  "header": { "eventType": "charAttributesUpdate", "clientId": 42, "message": "Attributes updated" },
  "body": {
    "characterId": 7,
    "attributesData": [
      { "id": 1, "name": "Strength",    "slug": "strength",    "value": 25 },
      { "id": 2, "name": "Agility",     "slug": "agility",     "value": 18 },
      { "id": 3, "name": "Attack Power","slug": "attack_power","value": 145 }
    ]
  }
}
```

Клиент должен заменить весь набор атрибутов персонажа при получении этого пакета.

---

## 4. Система веса

Сервер рассчитывает суммарный вес всех предметов в инвентаре и сравнивает с лимитом переносимого груза.

**Формула лимита:**
```
weightLimit = carry_weight.base + strength * carry_weight.per_strength
```

Значения по умолчанию: `base = 50`, `per_strength = 3`.

При превышении лимита клиент **сам** должен применить штраф к скорости движения (`-30%` по умолчанию, конфиг `carry_weight.overweight_speed_penalty`). Сервер не ограничивает движение — он только информирует клиента.

### 4.1 Статус веса — `WEIGHT_STATUS` ← сервер

Отправляется после каждого события, изменяющего инвентарь:
- подбор предмета с земли
- экипирование/снятие предмета
- покупка/продажа у NPC (косвенно через `getPlayerInventory`)

```json
{
  "header": { "eventType": "WEIGHT_STATUS", "clientId": 42, "message": "success" },
  "body": {
    "characterId":   7,
    "currentWeight": 67.5,
    "weightLimit":   125.0,
    "isOverweight":  false
  }
}
```

Клиент должен:
1. Отображать `currentWeight / weightLimit` в UI.
2. Если `isOverweight = true` — применить штраф к скорости персонажа.
3. Если `isOverweight = false` — снять штраф.

---

## 5. Система износа

Предметы с `isDurable = true` имеют `durabilityMax > 0` и тратят прочность в бою. При достижении `0` предмет сломан.

### Когда снижается прочность

- **Атакующий** — оружие (`main_hand`) теряет `durability.weapon_loss_per_hit` за каждый успешный удар.
- **Защищающийся** — каждый надетый доспех теряет `durability.armor_loss_per_hit` при получении удара.
- **Смерть** — каждый надетый дурабельный предмет теряет `durability.death_penalty_pct * durabilityMax`.

Значения по умолчанию: `weapon_loss_per_hit = 1`, `armor_loss_per_hit = 1`, `death_penalty_pct = 0.05`.

### Порог предупреждения

Когда прочность пересекает порог `durability.warning_threshold_pct` (по умолчанию 30%) — сервер переотправляет `EQUIPMENT_STATE`, где `isDurabilityWarning = true` для затронутого слота. Все атрибуты пересчитываются с учётом штрафа `durability.warning_penalty_pct` (по умолчанию 15%).

Клиент получает свежий `EQUIPMENT_STATE` и `charAttributesUpdate` автоматически.

---

## 6. Ремонт у NPC

### 6.1 Открытие мастерской — клиент → сервер

```json
{
  "header": { "eventType": "openRepairShop", "clientId": 42, "hash": "abc123" },
  "body": {
    "npcId": 15,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

### 6.2 Список предметов для ремонта — `repairShop` ← сервер

```json
{
  "header": { "eventType": "repairShop", "clientId": 42 },
  "body": {
    "npcId": 15,
    "totalRepairCost": 120,
    "items": [
      {
        "inventoryItemId": 101,
        "itemId":          5,
        "name":            "Iron Sword",
        "durabilityMax":   100,
        "durabilityCurrent": 45,
        "repairCost":      75
      },
      {
        "inventoryItemId": 203,
        "itemId":          8,
        "name":            "Iron Chestplate",
        "durabilityMax":   100,
        "durabilityCurrent": 60,
        "repairCost":      45
      }
    ]
  }
}
```

Список включает только надетые предметы с `isDurable = true` и `durabilityCurrent < durabilityMax`.

### 6.3 Починить один предмет — клиент → сервер

```json
{
  "header": { "eventType": "repairItem", "clientId": 42, "hash": "abc123" },
  "body": {
    "npcId":           15,
    "inventoryItemId": 101,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

### 6.4 Результат ремонта предмета — `repairItemResult` ← сервер

```json
{
  "header": { "eventType": "repairItemResult", "clientId": 42, "message": "success" },
  "body": {
    "inventoryItemId": 101,
    "durabilityCurrent": 100,
    "goldSpent": 75
  }
}
```

После этого пакета автоматически приходит `EQUIPMENT_STATE` с обновлёнными данными слота.

### 6.5 Починить всё — клиент → сервер

```json
{
  "header": { "eventType": "repairAll", "clientId": 42, "hash": "abc123" },
  "body": {
    "npcId": 15,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

### 6.6 Результат починки всего — `repairAllResult` ← сервер

```json
{
  "header": { "eventType": "repairAllResult", "clientId": 42, "message": "success" },
  "body": {
    "totalGoldSpent": 120,
    "repairedItems": [
      { "inventoryItemId": 101, "durabilityCurrent": 100 },
      { "inventoryItemId": 203, "durabilityCurrent": 100 }
    ]
  }
}
```

---

## 7. Торговля с NPC (Vendor)

### 7.1 Открытие магазина — клиент → сервер

```json
{
  "header": { "eventType": "openVendorShop", "clientId": 42, "hash": "abc123" },
  "body": {
    "npcId": 10,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

Сервер проверяет, что персонаж находится в радиусе NPC. При выходе за радиус — ошибка `out_of_range`.

### 7.2 Содержимое магазина — `vendorShop` ← сервер

```json
{
  "header": { "eventType": "vendorShop", "clientId": 42 },
  "body": {
    "npcId": 10,
    "items": [
      {
        "itemId":       5,
        "name":         "Iron Sword",
        "slug":         "iron_sword",
        "itemTypeSlug": "weapon",
        "raritySlug":   "common",
        "stackMax":     1,
        "isDurable":    true,
        "isTradable":   true,
        "priceBuy":     150,
        "priceSell":    75,
        "stockCurrent": 10,
        "stockMax":     -1
      }
    ]
  }
}
```

`stockMax = -1` означает неограниченный запас. `stockCurrent = 0` — товар временно закончился.

`priceBuy` — цена покупки у NPC, `priceSell` — цена продажи NPC.

### 7.3 Купить предмет у NPC — клиент → сервер

```json
{
  "header": { "eventType": "buyItem", "clientId": 42, "hash": "abc123" },
  "body": {
    "npcId":    10,
    "itemId":   5,
    "quantity": 1,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

### 7.4 Результат покупки — `buyItemResult` ← сервер

```json
{
  "header": { "eventType": "buyItemResult", "clientId": 42, "message": "success" },
  "body": {
    "itemId":     5,
    "quantity":   1,
    "totalPrice": 150
  }
}
```

После успешной покупки также приходит `getPlayerInventory` с обновлённым инвентарём.

### 7.5 Продать предмет NPC — клиент → сервер

```json
{
  "header": { "eventType": "sellItem", "clientId": 42, "hash": "abc123" },
  "body": {
    "npcId":           10,
    "inventoryItemId": 205,
    "quantity":        1,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

`inventoryItemId` — это `player_inventory.id` конкретного слота.

### 7.6 Результат продажи — `sellItemResult` ← сервер

```json
{
  "header": { "eventType": "sellItemResult", "clientId": 42, "message": "success" },
  "body": {
    "goldReceived": 75
  }
}
```

---

## 8. P2P Торговля между игроками

Обмен между двумя персонажами в пределах `economy.trade_range` (по умолчанию 5 единиц).

> **TTL сессии:** Торговая сессия имеет таймаут **60 секунд** без активности. По истечении сессия закрывается автоматически, оба игрока получат `tradeCancelled`.

### Жизненный цикл торговой сессии

```
Игрок A → tradeRequest → сервер
Игрок B ← tradeInvite  ← сервер

Игрок B → tradeAccept → сервер
Оба     ← tradeState   ← сервер    (пустые предложения)

Игрок A → tradeOfferUpdate → сервер
Игрок B → tradeOfferUpdate → сервер
Оба     ← tradeState       ← сервер  (обновлённые предложения)

Игрок A → tradeConfirm → сервер   (confirmed A = true)
Оба     ← tradeState   ← сервер

Игрок B → tradeConfirm → сервер   (confirmed B = true)
Оба     ← tradeComplete ← сервер  (сделка завершена)
```

Любой из игроков может выйти через `tradeCancel` — оба получат `tradeCancelled`.

### 8.1 Инициация торговли — клиент → сервер

```json
{
  "header": { "eventType": "tradeRequest", "clientId": 42, "hash": "abc123" },
  "body": {
    "targetCharacterId": 12,
    "posX": 10.0, "posY": 0.0, "posZ": 25.0, "rotZ": 0.0
  }
}
```

### 8.2 Приглашение к торговле — `tradeInvite` ← сервер (целевому игроку)

```json
{
  "header": { "eventType": "tradeInvite", "clientId": 55, "status": "pending" },
  "body": {
    "fromCharacterId": 7,
    "fromCharacterName": "Aldric"
  }
}
```

### 8.3 Ответ на приглашение — клиент → сервер

```json
{
  "header": { "eventType": "tradeAccept", "clientId": 55, "hash": "def456" },
  "body": {
    "fromCharacterId": "7"
  }
}
```

`fromCharacterId` — строковое значение `characterId` инициатора (то, что пришло в `tradeInvite`). Для отказа используется `"eventType": "tradeDecline"` с тем же телом.

### 8.4 Отклонение торговли — `tradeDeclined` ← сервер (инициатору)

```json
{
  "header": { "eventType": "tradeDeclined", "clientId": 42, "status": "declined" },
  "body": {
    "byCharacterName": "Brom"
  }
}
```

### 8.5 Обновление предложения — клиент → сервер

```json
{
  "header": { "eventType": "tradeOfferUpdate", "clientId": 42, "hash": "abc123" },
  "body": {
    "sessionId": "trade_7_12_1741689600000",
    "gold":      50,
    "items": [
      { "inventoryItemId": 101, "itemId": 5, "quantity": 1 }
    ]
  }
}
```

### 8.6 Состояние торговой сессии — `tradeState` ← сервер (обоим участникам)

Отправляется после каждого обновления предложения и после принятия приглашения.

```json
{
  "header": { "eventType": "tradeState", "clientId": 42, "status": "success" },
  "body": {
    "trade": {
      "sessionId":     "trade_7_12_1741689600000",
      "myGold":        50,
      "theirGold":     0,
      "myConfirmed":   false,
      "theirConfirmed": false,
      "myItems": [
        { "inventoryItemId": 101, "itemId": 5, "quantity": 1, "name": "Iron Sword", "slug": "iron_sword" }
      ],
      "theirItems": []
    }
  }
}
```

`myGold`/`myItems` — предложение **текущего** получателя пакета. `theirGold`/`theirItems` — предложение другого участника. Пакет персонализированный — каждый игрок видит свою сторону как "my".

### 8.7 Подтверждение сделки — клиент → сервер

```json
{
  "header": { "eventType": "tradeConfirm", "clientId": 42, "hash": "abc123" },
  "body": {
    "sessionId": "trade_7_12_1741689600000"
  }
}
```

### 8.8 Завершение сделки — `tradeComplete` ← сервер (обоим)

```json
{
  "header": { "eventType": "tradeComplete", "clientId": 42, "status": "success" },
  "body": {
    "sessionId": "trade_7_12_1741689600000"
  }
}
```

После `tradeComplete` каждый участник автоматически получает `getPlayerInventory` с обновлённым инвентарём.

### 8.9 Отмена торговли — клиент → сервер

```json
{
  "header": { "eventType": "tradeCancel", "clientId": 42, "hash": "abc123" },
  "body": {
    "sessionId": "trade_7_12_1741689600000"
  }
}
```

### 8.10 Уведомление об отмене — `tradeCancelled` ← сервер (обоим)

```json
{
  "header": { "eventType": "tradeCancelled", "clientId": 42, "status": "cancelled" },
  "body": {
    "sessionId": "trade_7_12_1741689600000",
    "reason":    "cancelled_by_player"
  }
}
```

Возможные `reason`: `"cancelled_by_player"`, `"timeout"`, `"player_out_of_range"`, `"player_disconnected"`.

---

## 9. Сет-бонусы (Set Bonuses)

Сет-бонусы рассчитываются **автоматически на сервере** при каждом экипировании/снятии.

Клиент не получает отдельного пакета о принадлежности к сету. Эффект сет-бонуса отражается в атрибутах персонажа — клиент просто получает `charAttributesUpdate` с уже суммированными значениями (базовые + бонусы от предметов + бонусы от сетов).

Информацию о том, к какому сету принадлежит предмет, можно получить из полей инвентарного пакета (**не** включены в текущую версию `inventoryItemToJson`) или хранить в клиентском каталоге предметов. Для отображения прогресса сета (например, "2/4 предмета надеты") клиент должен самостоятельно считать надетые слоты по полю `setSlug` из данных предметов.

---

## 10. Общие правила ошибок

При ошибке `header.message` содержит код ошибки вместо `"success"`:

```json
{
  "header": {
    "eventType": "vendorShop",
    "clientId":  42,
    "message":   "out_of_range"
  }
}
```

Стандартные коды ошибок для операций с NPC:

| Код | Описание |
|-----|----------|
| `out_of_range` | Персонаж слишком далеко от NPC |
| `npc_not_found` | NPC не найден в зоне |
| `not_a_vendor` | NPC не является торговцем |
| `not_a_blacksmith` | NPC не является кузнецом |
| `insufficient_gold` | Недостаточно золота |
| `item_out_of_stock` | Товар закончился |
| `item_not_tradable` | Предмет нельзя продать |
| `item_not_in_inventory` | Предмет не в инвентаре |
| `item_fully_repaired` | Предмет не нуждается в ремонте |
| `vendor_no_inventory` | У NPC нет товаров |

---

## 11. Временны́е метки (Timestamps)

Некоторые пакеты (экипировка, торговля) содержат поле `timestamps` в теле. Оно используется для компенсации лага и должно передаваться как есть — сервер возвращает его в ответных пакетах.

```json
"timestamps": {
  "clientSendTime": 1741689600123,
  "serverReceiveTime": 0
}
```

Клиент заполняет `clientSendTime` (Unix ms), `serverReceiveTime` оставляет `0`.
