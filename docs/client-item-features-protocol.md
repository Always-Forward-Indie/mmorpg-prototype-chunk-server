# Client–Server Protocol: Item Features

These four features were added in one sprint:

| Feature | Client → Server `eventType` | Server → Client `eventType` |
|---|---|---|
| Drop item onto ground | `dropItem` | `itemDrop` (broadcast) + `getPlayerInventory` (owner) |
| Ground item despawn | — | `itemRemove` (broadcast) |
| Ground items on connect | — | `itemDrop` (once, snapshot) |
| Inventory gold amount | — | in every `getPlayerInventory` response |
| Use consumable item | `useItem` | `healingResult` (broadcast, если есть HP restore) + `stats_update` (owner) |

---

## 1. Drop Item onto Ground

### Client → Server

```json
{
  "eventType": "dropItem",
  "body": {
    "itemId": 42,
    "quantity": 1
  }
}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `itemId` | int | ✓ | Database item ID |
| `quantity` | int | ✓ | Must be ≥ 1 and ≤ amount in inventory |

**Validation (server-side):**
- Character must own at least the requested quantity.
- Item must be tradable (`isTradable = true`) and not a quest item (`isQuestItem = false`).
- The item is removed from inventory before the drop is spawned (same transaction as any other inventory removal, so `getPlayerInventory` fires automatically).

### Server → Client (broadcast to **all** connected clients)

Same packet as a mob loot drop — `eventType: "itemDrop"`:

```json
{
  "status": "success",
  "header": {
    "eventType": "itemDrop",
    "clientId": 0
  },
  "body": {
    "items": [
      {
        "uid": 7001,
        "itemId": 42,
        "quantity": 1,
        "canBePickedUp": true,
        "droppedByMobUID": 0,
        "droppedByCharacterId": 15,
        "reservedForCharacterId": 0,
        "reservationSecondsLeft": 0,
        "position": { "x": 134.5, "y": 0.0, "z": 88.2, "rotationZ": 0.0 },
        "item": { ...item object... }
      }
    ]
  }
}
```

> `droppedByCharacterId > 0` means the item was dropped by a player (not a mob).  
> `reservedForCharacterId` and `reservationSecondsLeft` are `0` for player-dropped items (anyone can pick them up immediately).

---

## 2. Ground Item Despawn (`itemRemove`)

Sent automatically when:
- A player picks up an item.
- The periodic cleanup task removes items older than 5 minutes.

### Server → Client (broadcast to **all** connected clients)

```json
{
  "status": "success",
  "header": {
    "eventType": "itemRemove"
  },
  "body": {
    "uids": [7001, 7002]
  }
}
```

| Field | Type | Notes |
|---|---|---|
| `uids` | int[] | Array of `uid` values to despawn |

The client should remove the corresponding world objects from the scene for every UID in the array.

---

## 3. Ground Items Snapshot on Connect

When a character successfully joins (after character data is loaded), the server sends a single `itemDrop` packet containing **all currently spawned ground items**. The format is identical to §1 but may contain multiple items.

```json
{
  "status": "success",
  "header": {
    "eventType": "itemDrop",
    "clientId": 12
  },
  "body": {
    "items": [
      { "uid": 7000, "itemId": 10, "quantity": 3, ... },
      { "uid": 7001, "itemId": 42, "quantity": 1, ... }
    ]
  }
}
```

The client should render all items in the `items` array on scene load. If the array is empty the body still contains `"items": []`.

---

## 4. Gold in Inventory Packets

Every `getPlayerInventory` response (on explicit request, on inventory change, on character join) now includes a top-level `gold` field in the body:

```json
{
  "status": "success",
  "header": {
    "eventType": "getPlayerInventory",
    "clientId": 12
  },
  "body": {
    "characterId": 15,
    "gold": 1250,
    "items": [ ... ]
  }
}
```

| Field | Type | Notes |
|---|---|---|
| `gold` | int | Total gold coins owned (sum of `gold_coin` stack quantity) |

Gold itself also appears as a normal item in the `items` array (slug `gold_coin`). The separate `gold` field is provided for convenience so the HUD can display it without iterating the whole inventory.

---

## 5. Item Object Shape (used in inventory and ground-drop packets)

```json
{
  "id": 42,
  "itemId": 42,
  "slug": "potion_hp_small",
  "isQuestItem": false,
  "isUsable": true,
  "itemType": 3,
  "itemTypeSlug": "consumable",
  "isContainer": false,
  "isDurable": false,
  "isTradable": true,
  "isEquippable": false,
  "isHarvest": false,
  "isTwoHanded": false,
  "weight": 0.1,
  "rarityId": 1,
  "raritySlug": "common",
  "stackMax": 99,
  "durabilityMax": 0,
  "durabilityCurrent": 0,
  "isEquipped": false,
  "vendorPriceBuy": 50,
  "vendorPriceSell": 10,
  "equipSlot": 0,
  "equipSlotSlug": "",
  "levelRequirement": 1,
  "setId": 0,
  "setSlug": "",
  "allowedClassIds": [],
  "masterySlug": "",
  "killCount": 0,
  "attributes": [],
  "useEffects": [
    {
      "effectSlug": "hp_restore",
      "attributeSlug": "hp",
      "value": 150.0,
      "isInstant": true,
      "durationSeconds": 0,
      "tickMs": 0,
      "cooldownSeconds": 30
    }
  ]
}
```

| Поле | Описание |
|------|----------|
| `masterySlug` | Slug мастерства, которое растёт при использовании оружия (напр. `"sword_mastery"`). Пустая строка для предметов без мастерства. |
| `killCount` | Количество убийств на конкретном экземпляре оружия (Item Soul). Только в инвентаре и при P2P-трейде — всегда `0` в магазине вендора. |
| `useEffects` | Всегда присутствует. Пустой массив `[]` для предметов с `isUsable: false`. |
| `attributes` | Статические бонусы предмета (урон, броня, интеллект и т.д.). Пустой массив если нет. |
| `allowedClassIds` | Пустой массив = нет ограничений по классу. |

---

## 6. Use Consumable Item

### Client → Server

```json
{
  "eventType": "useItem",
  "body": {
    "itemId": 42
  }
}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `itemId` | int | ✓ | Database item ID |

One unit of the item is consumed from the inventory. The server validates:
- Character is alive.
- Character owns the item.
- `isUsable = true` и `useEffects` не пустой.
- Per-item cooldown не активен (`cooldownSeconds` из первого use-effect).

**Тimed-эффекты** (`isInstant = false`) сохраняются в `player_active_effect` (game server) и переживают реконнект. `sourceType` таких эффектов всегда `"item"`.

---

### Server → Client (broadcast) — мгновенное восстановление HP

Если эффект восстанавливает HP мгновенно, все клиенты получают `healingResult` — тот же пакет, что и от хил-скиллов. Клиент обрабатывает его одинаково независимо от источника (зелье, скилл, предмет):

```json
{
  "status": "success",
  "header": {
    "eventType": "healingResult"
  },
  "body": {
    "skillResult": {
      "casterId": 15,
      "targetId": 15,
      "skillName": "Health Potion",
      "skillSlug": "potion_hp_small",
      "skillEffectType": "heal",
      "healing": 150,
      "finalTargetHealth": 480,
      "success": true
    }
  }
}
```

`casterId == targetId` — персонаж хилит сам себя.

---

### Server → Client (owner) — полный `stats_update`

После применения **любого** эффекта (HP, MP, баф, HoT) владелец получает один пакет `stats_update` — тот же самый, что присылается после надевания экипировки, релевела, респолна и т.д. Никакого специального "use item response" нет.

```json
{
  "status": "success",
  "header": {
    "eventType": "stats_update",
    "requestId": 0
  },
  "body": {
    "characterId": 15,
    "level": 10,
    "experience": { "current": 4500, "levelStart": 3000, "nextLevel": 6000, "debt": 0 },
    "health":     { "current": 480, "max": 500 },
    "mana":       { "current": 320, "max": 400 },
    "weight":     { "current": 12.5, "max": 50.0 },
    "attributes": [
      { "slug": "strength", "name": "Strength", "base": 20, "effective": 25 }
    ],
    "activeEffects": [
      {
        "slug": "hp_regen",
        "effectTypeSlug": "hot",
        "attributeSlug": "hp",
        "value": 20.0,
        "expiresAt": 1741910460
      },
      {
        "slug": "strength_buff",
        "effectTypeSlug": "buff",
        "attributeSlug": "strength",
        "value": 5.0,
        "expiresAt": 1741910520
      }
    ]
  }
}
```

Поле `activeEffects` — это полный список всех активных эффектов персонажа (от зелий, навыков, экипировки, воскрешения). Клиент должен заменить свой локальный список целиком при каждом `stats_update`.

| Поле в `activeEffects` | Тип | Описание |
|---|---|---|
| `slug` | string | Идентификатор эффекта (для иконки/тултипа) |
| `effectTypeSlug` | string | `"hot"` — лечение по тику, `"dot"` — урон по тику, `"buff"` — стат-бонус, `"debuff"` — стат-пенальти, `"cc"` — контроль |
| `attributeSlug` | string | Целевой атрибут: `"hp"`, `"mp"`, `"strength"` и т.д. |
| `value` | float | Величина тика (для hot/dot) или плоская добавка к атрибуту (для buff) |
| `expiresAt` | int64 | Unix timestamp (секунды) истечения эффекта; `0` = постоянный |

Тики HoT/DoT приходят отдельным пакетом `effectTick` (см. §9 ниже) — никаких дополнительных обработчиков, кроме реакции на этот тип, не нужно.

---

## 7. Dropped Item Object — Full Shape

```json
{
  "uid": 7001,
  "itemId": 42,
  "quantity": 1,
  "canBePickedUp": true,
  "droppedByMobUID": 0,
  "droppedByCharacterId": 15,
  "reservedForCharacterId": 0,
  "reservationSecondsLeft": 0,
  "position": {
    "x": 134.5,
    "y": 0.0,
    "z": 88.2,
    "rotationZ": 0.0
  },
  "item": { ...item object (§5)... }
}
```

| Field | Type | Notes |
|---|---|---|
| `uid` | int | Unique instance ID of the drop (used in `itemRemove`) |
| `droppedByMobUID` | int | `> 0` when dropped from a mob corpse; `0` for player drops |
| `droppedByCharacterId` | int | `> 0` when dropped by a player; `0` for mob drops |
| `reservedForCharacterId` | int | If `> 0`, only this character can pick it up until the reservation expires |
| `reservationSecondsLeft` | int | Seconds until the reservation lifts (`0` = freely available) |

---

## 8. Summary of New `eventType` Values

| Direction | `eventType` | Description |
|---|---|---|
| Client → Server | `dropItem` | Drop item from inventory to ground |
| Client → Server | `useItem` | Use consumable item |
| Server → Client | `itemDrop` | Spawn one or more ground items (also used for snapshot on join) |
| Server → Client | `itemRemove` | Despawn ground items by UID array |
| Server → Client | `healingResult` | Heal number broadcast when potion restores HP instantly |
| Server → Client | `stats_update` | Full stats + active effects + weight update (owner only, после useItem) |
| Server → Client | `effectTick` | One DoT/HoT tick (HP change + effect slug, broadcast, см. §9) |
| Server → Client | `getPlayerInventory` | Full inventory refresh (now includes `gold` field) |

---

## 9. Новые пакеты после рефакторинга боевой системы

### 9.1 `effectTick` — Server → Client (broadcast)

Отправляется каждый раз, когда срабатывает тик DoT или HoT эффекта у любого игрока в зоне. Клиент должен обновить HP персонажа с этим ID и отобразить floating text.

```json
{
  "header": {
    "eventType": "effectTick"
  },
  "body": {
    "characterId": 42,
    "effectSlug": "poison",
    "effectTypeSlug": "dot",
    "value": 5.0,
    "newHealth": 75,
    "newMana": 60,
    "targetDied": false
  }
}
```

| Поле | Тип | Описание |
|---|---|---|
| `characterId` | int | Персонаж, на которого сработал тик |
| `effectSlug` | string | Идентификатор эффекта (для иконки и floating text) |
| `effectTypeSlug` | string | `"dot"` — урон / `"hot"` — лечение |
| `value` | float | Абсолютное количество урона или лечения за этот тик |
| `newHealth` | int | HP персонажа после тика |
| `newMana` | int | Мана персонажа (не изменяется тиком, но передаётся для синхронизации) |
| `targetDied` | bool | `true` — персонаж умер от этого тика |

> Если `targetDied == true`, клиент должен обработать смерть так же, как при получении `combatResult` с `targetDied: true`.

---

### 9.2 Пакеты скиллов — `{type}Initiation` и `{type}Result`

**Инициация скилла (начало анимации каста)** — broadcast всем:

`eventType` зависит от `skillEffectType` навыка:

| `skillEffectType` | `eventType` инициации | `eventType` результата |
|---|---|---|
| `damage` | `combatInitiation` | `combatResult` |
| `heal` | `healingInitiation` | `healingResult` |
| `buff` | `buffInitiation` | `buffResult` |
| `debuff` | `debuffInitiation` | `debuffResult` |
| другой | `skillInitiation` | `skillResult` |

**Пакет инициации** (`combatInitiation` / `healingInitiation` / `buffInitiation` / ...):

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Fireball initiated"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 15,
      "targetId": 301,
      "targetType": 3,
      "targetTypeString": "MOB",
      "skillName": "Fireball",
      "skillSlug": "fireball",
      "skillEffectType": "damage",
      "skillSchool": "fire",
      "castTime": 1.5,
      "animationName": "CastFireball",
      "animationDuration": 1.5,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

**Пакет результата — damage** (`combatResult`):

```json
{
  "header": {
    "eventType": "combatResult",
    "message": "Skill Fireball executed successfully"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 15,
      "targetId": 301,
      "targetType": 3,
      "targetTypeString": "MOB",
      "skillName": "Fireball",
      "skillSlug": "fireball",
      "skillEffectType": "damage",
      "skillSchool": "fire",
      "damage": 248,
      "isCritical": true,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 152,
      "finalTargetMana": 0,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

**Пакет результата — heal** (`healingResult`):

```json
{
  "header": { "eventType": "healingResult" },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 15,
      "targetId": 15,
      "skillName": "Holy Light",
      "skillSlug": "holy_light",
      "skillEffectType": "heal",
      "healing": 180,
      "finalTargetHealth": 320,
      "finalTargetMana": 40,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

**Пакет результата — buff** (`buffResult`):

```json
{
  "header": { "eventType": "buffResult" },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 15,
      "targetId": 15,
      "skillName": "Battle Cry",
      "skillSlug": "battle_cry",
      "skillEffectType": "buff",
      "appliedEffects": ["battle_fury"],
      "finalTargetHealth": 320,
      "finalTargetMana": 35,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

> После `buffResult` / `debuffResult` кастер и цель получают `stats_update` с обновлённым списком `activeEffects`. Клиенту рекомендуется обновлять бафф-бар именно по `stats_update`, а пакет `buffResult` использовать только для анимации/эффекта.

---

### 9.3 Серверный кулдаун предметов

С текущей версии сервер принудительно проверяет `cooldownSeconds` на каждое использование предмета. Если предмет ещё на кулдауне — запрос молча игнорируется (клиент не получает пакет об ошибке). Клиент должен самостоятельно отображать кулдаун на кнопке и блокировать повторную отправку.

---

## 10. Система сбора лута с трупов (Harvest)

Harvest — механика сбора лута с трупов мобов. Труп остаётся в мире после `mobDeath` и может быть осмотрен и ограблен.

### Полный поток Harvest

```
Клиент                          Chunk Server
  │                                  │
  │──── getNearbyCorpses ────────────►│  (необязательно — если нужен список)
  │◄─── nearbyCorpsesResponse ────────│
  │                                  │
  │──── harvestStart {corpseUID} ────►│  (начать сбор конкретного трупа)
  │◄─── harvestStarted ───────────────│  (duration=3000ms — начать анимацию)
  │                                  │
  │  [3 секунды анимации сбора]
  │                                  │  (сервер автоматически завершает сбор через 3 сек)
  │◄─── harvestComplete ──────────────│  (список лута для подбора)
  │                                  │
  │──── corpseLootPickup ────────────►│  {corpseUID, requestedItems: [{itemId, qty}]}
  │◄─── corpseLootPickup (result) ────│  (добавлено в инвентарь)
  │◄─── getPlayerInventory ───────────│  (обновлённый инвентарь)
```

**Отмена (в любой момент до завершения):**
```
Клиент → harvestCancel → Chunk Server
Клиент ← harvestCancelled ← Chunk Server
```

**Авто-отмена:** Если игрок отойдёт дальше 50 юнитов от трупа во время сбора — сервер сам отменит и пришлёт `harvestCancelled`.

---

### 10.1 Получение списка ближайших трупов — `getNearbyCorpses`

**Направление:** Клиент → Сервер  
**eventType:** `getNearbyCorpses`

```json
{
  "header": {
    "eventType": "getNearbyCorpses",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {}
}
```

Позиция игрока берётся сервером из CharacterManager. Возвращает трупы в радиусе ~5 единиц от текущей позиции.

**Ответ сервера — `nearbyCorpsesResponse`:**

```json
{
  "header": {
    "eventType": "nearbyCorpsesResponse",
    "clientId": 42,
    "message": "Nearby corpses retrieved"
  },
  "body": {
    "count": 2,
    "corpses": [
      {
        "id": 1001,
        "mobId": 5,
        "positionX": 143.5,
        "positionY": 88.2,
        "hasBeenHarvested": false,
        "harvestedByCharacterId": 0,
        "currentHarvesterCharacterId": 0,
        "isBeingHarvested": false
      },
      {
        "id": 1002,
        "mobId": 7,
        "positionX": 145.0,
        "positionY": 90.1,
        "hasBeenHarvested": true,
        "harvestedByCharacterId": 7,
        "currentHarvesterCharacterId": 0,
        "isBeingHarvested": false
      }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | UID трупа (corpseUID для harvestStart) |
| `mobId` | int | ID шаблона моба (mob template) |
| `positionX/Y` | float | Позиция трупа |
| `hasBeenHarvested` | bool | Был ли уже собран |
| `harvestedByCharacterId` | int | Кто собрал (`0` = никто) |
| `currentHarvesterCharacterId` | int | Кто сейчас собирает (`0` = никто) |
| `isBeingHarvested` | bool | Активно ли идёт сбор |

---

### 10.2 Начало сбора — `harvestStart`

**Направление:** Клиент → Сервер

```json
{
  "header": {
    "eventType": "harvestStart",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "corpseUID": 1001
  }
}
```

**Успешный ответ — `harvestStarted`:**

```json
{
  "header": {
    "eventType": "harvestStarted",
    "clientId": 42,
    "message": "Harvest started successfully"
  },
  "body": {
    "type": "HARVEST_STARTED",
    "clientId": 42,
    "playerId": 42,
    "corpseId": 1001,
    "duration": 3000,
    "startTime": 1741789012345
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `duration` | int | Длительность анимации сбора в миллисекундах (всегда 3000) |
| `startTime` | int64 | Unix timestamp (ms) начала сбора |

Клиент должен воспроизвести анимацию сбора длиной `duration` мс и ждать `harvestComplete`.

**Ответ с ошибкой — `harvestError`:**

```json
{
  "header": {
    "eventType": "harvestError",
    "clientId": 42,
    "message": "Corpse not available"
  },
  "body": {
    "type": "HARVEST_ERROR",
    "clientId": 42,
    "playerId": 42,
    "corpseId": 1001,
    "errorCode": "CORPSE_NOT_AVAILABLE",
    "message": "Corpse not available for harvest"
  }
}
```

| `errorCode` | Причина |
|-------------|---------|
| `CORPSE_NOT_AVAILABLE` | Труп не существует, уже полностью собран или занят другим игроком |
| `HARVEST_FAILED` | Внутренняя ошибка начала сбора |

---

### 10.3 Отмена сбора — `harvestCancel`

**Направление:** Клиент → Сервер

```json
{
  "header": {
    "eventType": "harvestCancel",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {}
}
```

**Ответ (после отмены) — `harvestCancelled`:**

```json
{
  "header": {
    "eventType": "harvestCancelled",
    "clientId": 42,
    "message": "Harvest cancelled"
  },
  "body": {
    "type": "HARVEST_CANCELLED",
    "clientId": 42,
    "corpseId": 1001,
    "reason": "MANUAL_CANCEL"
  }
}
```

Клиент также получает `harvestCancelled` с `reason: "MOVED_TOO_FAR"` если сервер автоматически прервал сбор из-за движения.

---

### 10.4 Завершение сбора — `harvestComplete`

Отправляется сервером **автоматически** через 3 секунды после `harvestStarted`. Клиент **не** отправляет никакого запроса.

**Направление:** Сервер → Клиент (только начавшему сбор)

```json
{
  "header": {
    "eventType": "harvestComplete",
    "clientId": 42,
    "message": "Harvest completed - loot available for pickup"
  },
  "body": {
    "type": "HARVEST_COMPLETE",
    "clientId": 42,
    "playerId": 42,
    "corpseId": 1001,
    "success": true,
    "totalItems": 2,
    "availableLoot": [
      {
        "itemId": 15,
        "itemSlug": "wolf_pelt",
        "quantity": 2,
        "rarityId": 1,
        "raritySlug": "common",
        "itemType": "Material",
        "itemTypeSlug": "material",
        "weight": 0.5,
        "addedToInventory": false,
        "isHarvestItem": true
      },
      {
        "itemId": 16,
        "itemSlug": "wolf_fang",
        "quantity": 1,
        "rarityId": 2,
        "raritySlug": "uncommon",
        "itemType": "Material",
        "itemTypeSlug": "material",
        "weight": 0.1,
        "addedToInventory": false,
        "isHarvestItem": true
      }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `availableLoot` | array | Список доступного лута для подбора |
| `availableLoot[].addedToInventory` | bool | Всегда `false` — лут не в инвентаре до `corpseLootPickup` |
| `availableLoot[].isHarvestItem` | bool | `true` — предмет помечен как материал для сбора |

> **Важно:** Лут не попадает в инвентарь автоматически. Клиент должен показать список лута и дать игроку выбрать, что подбирать через `corpseLootPickup`.

---

### 10.5 Подбор лута с трупа — `corpseLootPickup`

**Направление:** Клиент → Сервер

```json
{
  "header": {
    "eventType": "corpseLootPickup",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "playerId": 7,
    "corpseUID": 1001,
    "requestedItems": [
      { "itemId": 15, "quantity": 2 },
      { "itemId": 16, "quantity": 1 }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `playerId` | int | ID персонажа (должен совпадать с `characterId` сессии — серверная проверка) |
| `corpseUID` | int | UID трупа, с которого собирается лут |
| `requestedItems` | array | Список запрошенных предметов: `itemId` + `quantity` |

**Ответ сервера (успех):**

```json
{
  "header": {
    "eventType": "corpseLootPickup",
    "clientId": 42,
    "message": "success"
  },
  "body": {
    "success": true,
    "pickedUpItems": [
      { "itemId": 15, "quantity": 2, "slug": "wolf_pelt" },
      { "itemId": 16, "quantity": 1, "slug": "wolf_fang" }
    ]
  }
}
```

После успешного `corpseLootPickup` сервер немедленно отправляет `getPlayerInventory` с обновлённым инвентарём.

**Ответ с ошибкой:**

```json
{
  "header": {
    "eventType": "corpseLootPickup",
    "clientId": 42
  },
  "body": {
    "success": false,
    "errorCode": "CORPSE_NOT_FOUND"
  }
}
```

| `errorCode` | Причина |
|-------------|---------|
| `CORPSE_NOT_FOUND` | Труп не найден или уже полностью разграблен |
| `SECURITY_VIOLATION` | `playerId` не соответствует персонажу сессии |
