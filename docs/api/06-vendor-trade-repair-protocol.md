# 06. Вендоры, торговля между игроками и ремонт

## Обзор

Все три подсистемы обрабатываются в `VendorEventHandler`. Все операции unicast, server-authoritative с проверкой дистанции.

---

## 6.1. Система вендоров (NPC магазин)

### openVendorShop — Открыть магазин

#### Клиент → Сервер

```json
{
  "header": { "eventType": "openVendorShop", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "playerPosition": { "x": 100.0, "y": 200.0, "z": 50.0 },
    "timestamps": { "clientSendMsEcho": 1709834400000, "requestId": "..." }
  }
}
```

#### Сервер → Unicast (vendorShop)

```json
{
  "header": { "eventType": "vendorShop", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "npcId": 15,
    "npcSlug": "blacksmith_jim",
    "goldBalance": 5000,
    "items": [
      { "itemId": 5, "quantity": -1, "price": 150 },
      { "itemId": 8, "quantity": 10, "price": 75 }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `npcSlug` | string | Slug NPC-продавца |
| `goldBalance` | int | Текущее золото игрока |
| `items[].itemId` | int | ID шаблона предмета |
| `items[].quantity` | int | Остаток на складе. **-1 = бесконечный** |
| `items[].price` | int | Цена покупки (с учётом наценки и скидки за репутацию) |

#### Валидация

- NPC существует и `isInteractable`
- Расстояние: `sqrt(dx² + dy² + dz²) <= npc.radius + 2.0`

#### Ошибки

| Код | Причина |
|-----|---------|
| `vendor_not_found` | NPC не найден |
| `vendor_no_inventory` | У NPC нет товаров |
| `out_of_range` | Слишком далеко |

---

### buyItem — Купить предмет

#### Клиент → Сервер

```json
{
  "header": { "eventType": "buyItem", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "itemId": 5,
    "quantity": 3,
    "playerPosition": { "x": 100.0, "y": 200.0, "z": 50.0 },
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (buyItemResult)

```json
{
  "header": { "eventType": "buyItemResult", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "npcId": 15,
    "npcSlug": "blacksmith_jim",
    "itemId": 5,
    "quantity": 3,
    "totalPrice": 450
  }
}
```

---

### sellItem — Продать предмет

#### Клиент → Сервер

```json
{
  "header": { "eventType": "sellItem", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "inventoryItemId": 1234,
    "quantity": 2,
    "playerPosition": {},
    "timestamps": {}
  }
}
```

> **Важно**: `inventoryItemId` — это `player_inventory.id`, НЕ `itemId` шаблона!

#### Сервер → Unicast (sellItemResult)

```json
{
  "header": { "eventType": "sellItemResult", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "npcId": 15,
    "npcSlug": "blacksmith_jim",
    "goldReceived": 210
  }
}
```

---

### buyItemBatch / sellItemBatch — Пакетные операции

#### buyItemBatch: Клиент → Сервер

```json
{
  "header": { "eventType": "buyItemBatch", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "items": [
      { "itemId": 5, "quantity": 3 },
      { "itemId": 8, "quantity": 1 }
    ],
    "playerPosition": {},
    "timestamps": {}
  }
}
```

Лимит: **максимум 20 предметов** в батче.

#### Сервер → Unicast (buyItemBatchResult)

```json
{
  "header": { "eventType": "buyItemBatchResult", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "npcId": 15,
    "npcSlug": "blacksmith_jim",
    "totalGoldSpent": 525,
    "items": [
      { "itemId": 5, "quantity": 3, "totalPrice": 450 },
      { "itemId": 8, "quantity": 1, "totalPrice": 75 }
    ]
  }
}
```

#### sellItemBatch: Клиент → Сервер

```json
{
  "header": { "eventType": "sellItemBatch", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "items": [
      { "inventoryItemId": 1234, "quantity": 2 },
      { "inventoryItemId": 1235, "quantity": 1 }
    ],
    "playerPosition": {},
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (sellItemBatchResult)

```json
{
  "header": { "eventType": "sellItemBatchResult", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "npcId": 15,
    "npcSlug": "blacksmith_jim",
    "totalGoldReceived": 285,
    "items": [
      { "inventoryItemId": 1234, "goldReceived": 210 },
      { "inventoryItemId": 1235, "goldReceived": 75 }
    ]
  }
}
```

---

### Формулы цен

#### Покупка (игрок платит)

```
base_cost = vendorPriceBuy × quantity

markup = economy.vendor_buy_markup_pct       // default: 0.0
discount = 0.0
if (reputation >= reputation.vendor_discount_threshold)  // default: 200
    discount = reputation.vendor_discount_pct            // default: 0.05 (5%)

final_price = base_cost × (1.0 + markup - discount)
```

#### Продажа (игрок получает)

```
tax = economy.vendor_sell_tax_pct            // default: 0.0
effective_tax = max(0, tax - reputation_discount)
goldReceived = floor(vendorPriceSell × quantity × (1.0 - effective_tax))
```

#### Ошибки покупки/продажи

| Код | Причина |
|-----|---------|
| `insufficient_gold` | Не хватает золота |
| `not_tradable` | Предмет нельзя продать (`isTradable == false`) |
| `vendor_not_found` | NPC не найден |
| `out_of_range` | Далеко от NPC |

---

## 6.2. Система ремонта (Кузнец)

### openRepairShop — Открыть кузню

#### Клиент → Сервер

```json
{
  "header": { "eventType": "openRepairShop", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "playerPosition": {},
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (repairShop)

```json
{
  "header": { "eventType": "repairShop", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "npcId": 15,
    "npcSlug": "blacksmith_jim",
    "items": [
      {
        "inventoryItemId": 5678,
        "itemId": 50,
        "slug": "iron_sword",
        "durabilityMax": 100,
        "durabilityCurrent": 45,
        "repairCost": 55
      }
    ],
    "totalRepairCost": 135
  }
}
```

> Показываются только экипированные предметы с `isDurable == true` и `durabilityCurrent < durabilityMax`.

---

### repairItem — Починить один предмет

#### Клиент → Сервер

```json
{
  "header": { "eventType": "repairItem", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "inventoryItemId": 5678,
    "playerPosition": {},
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (repairItemResult)

```json
{
  "header": { "eventType": "repairItemResult", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "inventoryItemId": 5678,
    "durabilityCurrent": 100,
    "goldSpent": 55
  }
}
```

Ремонт **всегда** восстанавливает до `durabilityMax` (не инкрементально).

---

### repairAll — Починить всё

#### Клиент → Сервер

```json
{
  "header": { "eventType": "repairAll", "clientId": 42, "hash": "" },
  "body": {
    "characterId": 101,
    "npcId": 15,
    "playerPosition": {},
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (repairAllResult)

```json
{
  "header": { "eventType": "repairAllResult", "status": "success", "clientId": 42, "hash": "" },
  "body": {
    "repairedItems": [
      { "inventoryItemId": 5678, "durabilityCurrent": 100 },
      { "inventoryItemId": 5679, "durabilityCurrent": 50 }
    ],
    "totalGoldSpent": 135
  }
}
```

> **Важно**: `repairAll` останавливается на первой неудаче (например, закончилось золото). Уже отремонтированные предметы не откатываются.

---

### Формула стоимости ремонта

```
missing_durability = durabilityMax - durabilityCurrent
repairCost = ceil(vendorPriceBuy × missing_durability / durabilityMax)
```

**Пример**: vendorPriceBuy=100, durabilityMax=50, durabilityCurrent=20:
```
missing = 50 - 20 = 30
cost = ceil(100.0 × 30 / 50) = ceil(60.0) = 60 gold
```

### Ошибки ремонта

| Код | Причина |
|-----|---------|
| `not_a_blacksmith` | NPC не является кузнецом |
| `out_of_range` | Далеко от NPC |
| `item_not_equipped` | Предмет не экипирован |
| `not_durable` | Предмет не имеет прочности |
| `already_full` | Прочность уже максимальная |
| `insufficient_gold` | Не хватает золота |

---

## 6.3. Торговля между игроками (P2P Trade)

### Жизненный цикл торговой сессии

```
1. A → tradeRequest(B)     → B получает tradeInvite
2. B → tradeAccept          → Создаётся сессия, оба получают tradeState
3. A/B → tradeOfferUpdate   → Оба получают обновлённый tradeState
4. A → tradeConfirm         → confirmedA = true, оба получают tradeState
5. B → tradeConfirm         → confirmedB = true
   → Финальная валидация OK   → Оба получают tradeComplete
   → Финальная валидация FAIL → Оба получают tradeCancelled
6. A или B → tradeCancel    → Оба получают tradeCancelled
```

### TradeSessionStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `sessionId` | string | `"trade_{charA}_{charB}_{timestamp}"` |
| `charAId` / `charBId` | int | ID персонажей |
| `clientAId` / `clientBId` | int | ID клиентских сокетов |
| `offerA` / `offerB` | array | Предложенные предметы |
| `goldA` / `goldB` | int | Предложенное золото |
| `confirmedA` / `confirmedB` | bool | Подтверждение готовности |
| TTL | 60 сек | Сессия истекает по таймауту |

---

### tradeRequest — Предложить обмен

#### Клиент → Сервер

```json
{
  "header": { "eventType": "tradeRequest", "clientId": 7 },
  "body": {
    "characterId": 101,
    "targetCharacterId": 205,
    "playerPosition": { "x": 100.0, "y": 100.0, "z": 50.0 },
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (tradeInvite → целевому игроку)

```json
{
  "header": { "eventType": "tradeInvite", "status": "pending", "clientId": 42, "hash": "" },
  "body": {
    "fromCharacterId": 101,
    "fromCharacterName": "Alexei"
  }
}
```

---

### tradeAccept / tradeDecline

#### Клиент → Сервер

```json
{
  "header": { "eventType": "tradeAccept", "clientId": 42 },
  "body": {
    "characterId": 205,
    "sessionId": "101",
    "timestamps": {}
  }
}
```

> Поле `sessionId` при accept/decline — это **string** с `characterId` инициатора.

При **accept**: оба получают `tradeState`.
При **decline**: инициатор получает `tradeDeclined`:

```json
{
  "header": { "eventType": "tradeDeclined", "status": "declined", "clientId": 7, "hash": "" },
  "body": {
    "byCharacterName": "Borisova"
  }
}
```

---

### tradeOfferUpdate — Обновить предложение

#### Клиент → Сервер

```json
{
  "header": { "eventType": "tradeOfferUpdate", "clientId": 7 },
  "body": {
    "characterId": 101,
    "sessionId": "trade_101_205_1709834400000",
    "items": [
      { "inventoryItemId": 100, "itemId": 5, "quantity": 2 },
      { "inventoryItemId": 101, "itemId": 8, "quantity": 1 }
    ],
    "gold": 500,
    "timestamps": {}
  }
}
```

> **Важно**: При любом обновлении оффера `confirmedA` и `confirmedB` сбрасываются в `false`.

#### Сервер → Unicast (tradeState → обоим)

```json
{
  "header": { "eventType": "tradeState", "status": "success", "clientId": 7, "hash": "" },
  "body": {
    "trade": {
      "sessionId": "trade_101_205_1709834400000",
      "myGold": 500,
      "theirGold": 750,
      "myGoldBalance": 2500,
      "myItems": [
        {
          "inventoryItemId": 100,
          "itemId": 5,
          "quantity": 2,
          "slug": "iron_sword"
        }
      ],
      "theirItems": [
        {
          "inventoryItemId": 300,
          "itemId": 12,
          "quantity": 1,
          "slug": "healing_potion"
        }
      ],
      "myConfirmed": false,
      "theirConfirmed": false
    }
  }
}
```

> `tradeState` **адаптируется** для каждого получателя: `myItems`/`myGold` отражают предметы текущего игрока, `theirItems`/`theirGold` — партнёра.

---

### tradeConfirm — Подтвердить готовность

#### Клиент → Сервер

```json
{
  "header": { "eventType": "tradeConfirm", "clientId": 7 },
  "body": {
    "characterId": 101,
    "sessionId": "trade_101_205_1709834400000",
    "timestamps": {}
  }
}
```

Если только один подтвердил → оба получают `tradeState` с обновлёнными флагами `myConfirmed`/`theirConfirmed`.

Если оба подтвердили → финальная валидация:
- Все предметы всё ещё в инвентаре
- Все предметы `isTradable`
- Золота достаточно у обоих

**Успех**:
```json
{
  "header": { "eventType": "tradeComplete", "status": "success", "clientId": 7, "hash": "" },
  "body": {
    "sessionId": "trade_101_205_1709834400000"
  }
}
```

**Неудача**: Обоим → `tradeCancelled` с `reason: "offer_no_longer_valid"`

---

### tradeCancel — Отменить обмен

#### Клиент → Сервер

```json
{
  "header": { "eventType": "tradeCancel", "clientId": 7 },
  "body": {
    "characterId": 101,
    "sessionId": "trade_101_205_1709834400000",
    "timestamps": {}
  }
}
```

#### Сервер → Unicast (tradeCancelled → обоим)

```json
{
  "header": { "eventType": "tradeCancelled", "status": "cancelled", "clientId": 7, "hash": "" },
  "body": {
    "sessionId": "trade_101_205_1709834400000",
    "reason": "cancelled_by_player"
  }
}
```

---

### Ошибки P2P торговли

| Код | Причина |
|-----|---------|
| `cannot_trade_while_dead` | Один из игроков мёртв |
| `already_in_trade` | Инициатор уже в торговой сессии |
| `target_not_found` | Цель не найдена |
| `target_offline` | Цель не онлайн |
| `target_busy` | Цель уже торгует |
| `out_of_range` | Слишком далеко |
| `session_not_found` | Сессия не найдена |
| `not_in_session` | Персонаж не участвует в этой сессии |
| `invalid_offer_item` | Предмет не в инвентаре / не торгуемый / экипирован |
| `offer_no_longer_valid` | Финальная проверка не пройдена |

---

## Конфигурация

| Ключ конфига | Default | Описание |
|-------------|---------|----------|
| `economy.vendor_buy_markup_pct` | 0.0 | Наценка на покупку (%) |
| `economy.vendor_sell_tax_pct` | 0.0 | Налог на продажу (%) |
| `economy.trade_range` | 5.0 | Дистанция P2P торговли |
| `reputation.vendor_discount_threshold` | 200 | Порог репутации для скидки |
| `reputation.vendor_discount_pct` | 0.05 | Скидка (5%) |
