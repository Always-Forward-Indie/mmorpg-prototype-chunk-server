# План: Торговля и Система Износа

**Версия плана:** v1.1  
**Дата:** 2026-03-10  
**Текущая версия проекта:** v0.0.4

---

## Содержание

1. [Текущее состояние проекта](#1-текущее-состояние-проекта)
2. [Исправления перед началом](#2-исправления-перед-началом)
3. [Торговля Layer 1 — NPC Vendor](#3-торговля-layer-1--npc-vendor)
4. [Торговля Layer 2 — P2P Direct Trade](#4-торговля-layer-2--p2p-direct-trade)
5. [Система износа (Durability)](#5-система-износа-durability)
   - [5a. Настраиваемые константы через game_config](#5a-настраиваемые-константы-через-game_config)
   - [5b. Интеграция с диалоговой системой](#5b-интеграция-с-диалоговой-системой)
6. [Роадмап реализации](#6-роадмап-реализации)

---

## 1. Текущее состояние проекта

### Реализовано ✅

| Система | Описание |
|---------|----------|
| NPC спавн | Полностью |
| Диалоговая система | Граф, условия, действия, сессии |
| Квесты | Принятие, прогресс, шаги, сдача, провал |
| Валюта Gold Coin | item_id = 16, item_type = 8 (currency) |
| Персистентность | Квесты, флаги, позиция — каждые 5с + при отключении |

### Схемы в БД есть, код не написан ⚠️

| Таблица | Что есть | Чего не хватает |
|---------|----------|-----------------|
| `vendor_npc` | `npc_id`, `markup_pct` | Серверная логика |
| `vendor_inventory` | `item_id`, `stock_count`, `price_override` | Серверная логика |
| `currency_transactions` | Ledger со всеми `reason_type` | Серверная запись транзакций |
| `items.is_durable`, `items.durability_max` | Читается, шлётся клиенту | Логика убыли и починки |
| `player_inventory.durability_current` | Читается, шлётся клиенту | Обновление в игре |

### Структуры C++ заполнены ✅

- `ItemDataStruct`: `isDurable`, `durabilityMax`, `vendorPriceBuy`, `vendorPriceSell`
- `PlayerInventoryItemStruct`: `durabilityCurrent`
- `NPCDataStruct`: `npcType` поддерживает `"vendor"`, `"blacksmith"`

---

## 2. Исправления перед началом

### 2.1 `isTradable: false` у Gold Coin — исправить обязательно

**Проблема:** Золото, которое нельзя передать другому игроку, уничтожает игровую экономику. P2P-торговля без передачи золота невозможна.

**Действие:** В БД для `item_id = 16` (Gold Coin) установить `is_tradable = true`.

```sql
UPDATE items SET is_tradable = true WHERE id = 16;
```

---

## 3. Торговля Layer 1 — NPC Vendor

### Концепция

NPC с `npcType = "vendor"` продаёт предметы по `vendorPriceBuy` (с учётом `markup_pct` из `vendor_npc` и глобального `economy.vendor_buy_markup_global` из `game_config`).  
Игрок продаёт предметы NPC по `vendorPriceSell` (30–50% от `vendorPriceBuy`) — это **gold sink floor**.  
Дополнительный налог на продажу (`economy.vendor_sell_tax_pct`) настраивается через `game_config`. По умолчанию `0.0` (нет налога).

Взаимодействие открывается **двумя путями**: напрямую пакетом `openVendorShop` (кнопка в UI) или через диалоговое действие `open_vendor_shop` (RP-диалог NPC). Подробнее — раздел [5b](#5b-интеграция-с-диалоговой-системой).

---

### Сток и золото NPC

#### Золото NPC (для покупки у игроков) — бесконечное

NPC никогда не «кончаются деньги». Ограниченный кошелёк NPC — антипаттерн: добавляет фрустрацию без геймплейной ценности. Экономический контроль обеспечивается низкой ценой продажи (`vendorPriceSell = 30–50%`) и настраиваемым налогом в `game_config`. Так работают L2, WoW, FFXIV — это проверенное решение.

#### Сток — двухуровневый

| `stock_count` | `restock_amount` | Сценарий |
|:---:|:---:|---|
| `-1` | `0` | **Бесконечный запас** — зелья, базовое оружие, материалы. Большинство товаров. |
| `N > 0` | `0` | **Ограниченный, не пополняется** — разовые уникальные предметы (квестовые реагенты, legacy-предметы). |
| `N > 0` | `M > 0` | **Ограниченный с пополнением** — редкие рецепты, улучшения, специальные материалы. Создаёт дефицит и конкуренцию. |

Ограниченный сток с пополнением — ключевой инструмент экономического дизайна:
- **Дефицит** → игроки конкурируют за покупку → предмет воспринимается ценнее
- **Ожидание рестока** → retention loop: игрок возвращается проверить
- **Прогнозируемость** → игроки строят планы на основе интервала рестока

#### Схема БД — новые поля в `vendor_inventory`

```sql
ALTER TABLE vendor_inventory
    ADD COLUMN restock_amount       integer   NOT NULL DEFAULT 0,
    ADD COLUMN stock_max            integer   NOT NULL DEFAULT -1,
    ADD COLUMN restock_interval_sec integer   NOT NULL DEFAULT 3600,
    ADD COLUMN last_restock_at      timestamptz       DEFAULT NULL;

COMMENT ON COLUMN vendor_inventory.restock_amount IS
    'Количество единиц товара, добавляемых за один цикл рестока. 0 = не пополняется.';
COMMENT ON COLUMN vendor_inventory.stock_max IS
    'Максимальный сток после рестока. -1 = пополняется без верхней планки (на restock_amount за цикл). '
    'Обычно совпадает с начальным stock_count.';
COMMENT ON COLUMN vendor_inventory.restock_interval_sec IS
    'Индивидуальный интервал рестока этого товара у этого NPC (секунды). '
    'Зелья: 600 (10 мин). Редкий рецепт: 86400 (сутки). Игнорируется если restock_amount = 0.';
COMMENT ON COLUMN vendor_inventory.last_restock_at IS
    'Время последнего рестока этой позиции. NULL = ещё не было рестока (считается отправной точкой при старте сервера).';
```

#### Примеры конфигурации

| Товар | `stock_count` | `restock_amount` | `stock_max` | `restock_interval_sec` | Поведение |
|-------|:---:|:---:|:---:|:---:|---|
| Health Potion | 100 | 50 | 100 | 600 | Каждые 10 мин +50, не выше 100 |
| Iron Sword | -1 | 0 | -1 | — | Бесконечный, рестока нет |
| Rare Enchant Recipe | 3 | 1 | 3 | 86400 | Раз в сутки +1, не выше 3 |
| Legacy Relic | 1 | 0 | -1 | — | Разовый, не пополняется |

#### Логика рестока (Game Server scheduler)

Scheduler запускается **каждые 60 секунд** и проверяет все строки:

```sql
-- Game Server выполняет примерно это:
UPDATE vendor_inventory
SET
    stock_count = CASE
        WHEN stock_max = -1 THEN stock_count + restock_amount
        ELSE LEAST(stock_count + restock_amount, stock_max)
    END,
    last_restock_at = NOW()
WHERE
    restock_amount > 0
    AND stock_count != -1
    AND (
        last_restock_at IS NULL
        OR EXTRACT(EPOCH FROM (NOW() - last_restock_at)) >= restock_interval_sec
    )
RETURNING vendor_npc_id, item_id, stock_count;
```

`RETURNING` даёт список изменившихся позиций — Game Server отправляет **только их** в Chunk Server (`vendorStockUpdate`), не весь каталог.

После рестока Game Server отправляет Chunk Server'у обновлённые данные (`vendorStockUpdate`), Chunk Server обновляет in-memory. Это тот же паттерн, что используется для других данных при старте.

#### Поведение при `stock_count = 0`

Товар **остаётся в `VENDOR_SHOP`** с `stockCount: 0` — клиент показывает его серым с пометкой «Нет в наличии». Это лучший UX: игрок видит, что предмет существует и когда-то вернётся. Скрывать товар — ошибка: игрок не знает, у этого ли NPC он покупается.

### Схема взаимодействия

```
Client → Chunk: openVendorShop
Chunk → Client: VENDOR_SHOP (список товаров NPC)

Client → Chunk: buyItem
Chunk → Client: BUY_RESULT
  + item_received (уже есть)
  + gold_received / (списание золота без отдельного пакета, только GOLD_BALANCE_UPDATE)

Client → Chunk: sellItem
Chunk → Client: SELL_RESULT
  + GOLD_BALANCE_UPDATE
```

### Пакеты

#### `openVendorShop` — открыть магазин

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "openVendorShop", "clientId": 7 },
  "body": { "npcId": 55, "characterId": 101 }
}
```

**Ошибки:** `NPC_NOT_FOUND`, `OUT_OF_RANGE`, `NOT_A_VENDOR`

---

#### `VENDOR_SHOP` — содержимое магазина

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "success", "eventType": "vendorShop", "clientId": 7 },
  "body": {
    "npcId": 55,
    "npcSlug": "merchant_tom",
    "goldBalance": 500,
    "items": [
      {
        "itemId": 3,
        "slug": "iron_sword",
        "itemType": 1,
        "itemTypeSlug": "weapon",
        "rarityId": 1,
        "raritySlug": "common",
        "isDurable": true,
        "durabilityMax": 100,
        "isTradable": true,
        "isEquippable": true,
        "isUsable": false,
        "isQuestItem": false,
        "isContainer": false,
        "isHarvest": false,
        "isTwoHanded": false,
        "weight": 3.5,
        "equipSlot": 10,
        "equipSlotSlug": "main_hand",
        "levelRequirement": 1,
        "setId": 0,
        "setSlug": "",
        "allowedClassIds": [],
        "masterySlug": "sword_mastery",
        "priceBuy": 150,
        "priceSell": 60,
        "stockCurrent": -1,
        "stockMax": -1,
        "attributes": [
          { "id": 1, "slug": "attack", "value": 12 }
        ],
        "useEffects": []
      },
      {
        "itemId": 7,
        "slug": "health_potion",
        "itemType": 3,
        "itemTypeSlug": "consumable",
        "rarityId": 1,
        "raritySlug": "common",
        "isDurable": false,
        "durabilityMax": 0,
        "isTradable": true,
        "isEquippable": false,
        "isUsable": true,
        "isQuestItem": false,
        "isContainer": false,
        "isHarvest": false,
        "isTwoHanded": false,
        "weight": 0.1,
        "equipSlot": 0,
        "equipSlotSlug": "",
        "levelRequirement": 1,
        "setId": 0,
        "setSlug": "",
        "allowedClassIds": [],
        "masterySlug": "",
        "priceBuy": 20,
        "priceSell": 8,
        "stockCurrent": 50,
        "stockMax": 100,
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
    ]
  }
}
```

Каждый элемент в `items` содержит полный объект предмета (аналог §5 `client-item-features-protocol.md`) плюс торговые поля:

| Поле | Описание |
|------|----------|
| `priceBuy` | Цена покупки у NPC (уже с учётом `markup_pct` и репутационной скидки) |
| `priceSell` | Цена продажи NPC |
| `stockCurrent` | `-1` = бесконечный запас; `0` = временно нет в наличии; `N > 0` = текущий сток |
| `stockMax` | Максимальный сток при ресток. `-1` = без верхней планки |
| `killCount` | **Всегда `0` в магазине** — у вендора нет инстанс-данных предмета |

---

#### `buyItem` — купить у NPC

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "buyItem", "clientId": 7 },
  "body": { "npcId": 55, "itemId": 3, "quantity": 1, "characterId": 101 }
}
```

---

#### `BUY_RESULT` — результат покупки

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "success", "eventType": "BUY_RESULT", "clientId": 7 },
  "body": {
    "npcId": 55,
    "itemId": 3,
    "quantity": 1,
    "goldSpent": 150,
    "newGoldBalance": 350
  }
}
```

За `BUY_RESULT` следует `item_received` (уже реализован).

**Ошибки:** `INSUFFICIENT_GOLD`, `OUT_OF_STOCK`, `ITEM_NOT_IN_VENDOR`, `INVENTORY_FULL`

---

#### `sellItem` — продать NPC

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "sellItem", "clientId": 7 },
  "body": {
    "npcId": 55,
    "inventorySlotId": 12,
    "quantity": 1,
    "characterId": 101
  }
}
```

`inventorySlotId` — ID записи в `player_inventory` (не `item_id`): у игрока может быть несколько экземпляров одного предмета.

---

#### `SELL_RESULT` — результат продажи

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "success", "eventType": "SELL_RESULT", "clientId": 7 },
  "body": {
    "npcId": 55,
    "inventorySlotId": 12,
    "quantity": 1,
    "goldReceived": 60,
    "newGoldBalance": 410
  }
}
```

**Ошибки:** `ITEM_NOT_IN_INVENTORY`, `ITEM_NOT_TRADABLE`, `ITEM_EQUIPPED`

---

#### `vendorError` — ошибка торговли

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "Not enough gold", "eventType": "vendorError", "clientId": 7 },
  "body": { "errorCode": "INSUFFICIENT_GOLD" }
}
```

---

### Серверная логика

**Открытие магазина:**
1. Проверить: NPC существует, `isInteractable = true`, `npcType = "vendor"`
2. Проверить расстояние (≤ `npc.radius`)
3. Загрузить `vendor_inventory` для данного NPC (из in-memory, загружается при старте)
4. Применить `markup_pct` (per-vendor) + `economy.vendor_buy_markup_global` из `game_config` к `item.vendor_price_buy`
5. Отправить `VENDOR_SHOP` — включая предметы с `stockCount = 0` (для отображения «нет в наличии»)

**Покупка:**
1. Проверить `stock_count ≠ 0` (если `stock_count = 0` → ошибка `OUT_OF_STOCK`; если `-1` → бесконечный, пропустить)
2. Проверить баланс Gold Coin (`player_inventory` у Gold Coin ≥ `price * quantity`)
3. Atomically: списать золото, добавить предмет, уменьшить `stock_count` на `quantity` (если не `-1`)
4. Записать в `currency_transactions` (`reason_type = "vendor_buy"`, `amount = -goldSpent`)
5. Отправить `BUY_RESULT` + `item_received`
6. Flush в Game Server (gold + inventory + stock changes)

**Продажа:**
1. Проверить: предмет в инвентаре, quantity достаточно, `is_tradable = true`, предмет не надет
2. Применить налог: `goldReceived = floor(vendorPriceSell * quantity * (1 - economy.vendor_sell_tax_pct))`
3. Atomically: убрать предмет, добавить золото
4. Записать в `currency_transactions` (`reason_type = "vendor_sell"`, `amount = +goldReceived`)
5. Отправить `SELL_RESULT`
6. Flush в Game Server

**Ресток (Game Server scheduler):**
1. Запускается каждые **60 секунд** (проверяет, а не ждёт фиксированный интервал)
2. По SQL-запросу обновляет все строки `vendor_inventory`, у которых вышел их индивидуальный `restock_interval_sec`
3. Через `RETURNING` получает дельту — отправляет Chunk Server'у только изменившиеся стоки (`vendorStockUpdate`)
4. Chunk Server обновляет in-memory

---

## 4. Торговля Layer 2 — P2P Direct Trade

### Концепция

Прямой обмен предметами и золотом между двумя игроками. Оба должны быть в радиусе **300 единиц** друг от друга. Механика двойного подтверждения — ни один из игроков не может изменить оффер после того, как оба нажали «Подтвердить».

### Механика (порядок шагов)

```
1. Игрок A инициирует → tradeRequest → Игрок B получает TRADE_INVITE
2. Игрок B принимает → tradeAccept → оба получают TRADE_OPENED (создаётся сессия)
3. Оба выкладывают предметы/золото → tradeOfferUpdate → оба видят TRADE_STATE
4. Игрок A нажимает "Готово" → tradeConfirm → флаг confirmed = true у A
5. Игрок B нажимает "Готово" → tradeConfirm → флаг confirmed = true у B
6. Оба подтверждены → сервер финально проверяет инвентари → TRADE_COMPLETE (обоим)
   ИЛИ: один отменяет → tradeCancell → TRADE_CANCELLED (обоим)
```

**Важно:** шаг «выложить предметы» сбрасывает `confirmed = false` у обоих. Нельзя изменить оффер, если уже подтвердил.

### Пакеты

#### `tradeRequest` — запрос торговли

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "tradeRequest", "clientId": 7 },
  "body": { "targetCharacterId": 205, "characterId": 101 }
}
```

**Ошибки:** `TARGET_NOT_FOUND`, `OUT_OF_RANGE`, `TARGET_BUSY`, `ALREADY_TRADING`

---

#### `TRADE_INVITE` — приглашение к торговле

**Направление:** Chunk Server → Client (получает Игрок B)

```json
{
  "header": { "eventType": "TRADE_INVITE", "clientId": 42 },
  "body": {
    "sessionId": "trade_101_205_1709834400000",
    "fromCharacterId": 101,
    "fromCharacterName": "Алексей"
  }
}
```

---

#### `tradeAccept` / `tradeDecline`

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "tradeAccept", "clientId": 42 },
  "body": { "sessionId": "trade_101_205_1709834400000", "characterId": 205 }
}
```

```json
{
  "header": { "eventType": "tradeDecline", "clientId": 42 },
  "body": { "sessionId": "trade_101_205_1709834400000", "characterId": 205 }
}
```

---

#### `TRADE_OPENED` — торговля открыта (обоим)

**Направление:** Chunk Server → Client

```json
{
  "header": { "eventType": "TRADE_OPENED", "clientId": 7 },
  "body": {
    "sessionId": "trade_101_205_1709834400000",
    "partnerCharacterId": 205,
    "partnerName": "Мирослава"
  }
}
```

---

#### `tradeOfferUpdate` — обновить своё предложение

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "tradeOfferUpdate", "clientId": 7 },
  "body": {
    "sessionId": "trade_101_205_1709834400000",
    "characterId": 101,
    "offeredItems": [
      { "inventorySlotId": 15, "quantity": 1 },
      { "inventorySlotId": 22, "quantity": 3 }
    ],
    "offeredGold": 50
  }
}
```

Вызов этого пакета сбрасывает `confirmed = false` у **обоих** участников.

---

#### `TRADE_STATE` — текущее состояние сессии (обоим)

**Направление:** Chunk Server → Client

Каждый элемент в `myItems` / `theirItems` — полный объект предмета из инвентаря (тот же формат что `getPlayerInventory`, включая `attributes`, `useEffects`, `masterySlug`, `killCount`, `durabilityCurrent`). Поле `quantity` может быть меньше quantidade в слоте, если игрок предложил только часть стака.

```json
{
  "header": { "eventType": "tradeState", "clientId": 7 },
  "body": {
    "trade": {
      "sessionId": "trade_101_205_1709834400000",
      "myGold": 50,
      "theirGold": 0,
      "myGoldBalance": 350,
      "myItems": [
        {
          "id": 15,
          "itemId": 3,
          "slug": "iron_sword",
          "quantity": 1,
          "durabilityCurrent": 87,
          "durabilityMax": 100,
          "isEquipped": false,
          "killCount": 42,
          "masterySlug": "sword_mastery",
          "attributes": [
            { "id": 1, "slug": "attack", "value": 12 }
          ],
          "useEffects": []
        }
      ],
      "theirItems": [
        {
          "id": 89,
          "itemId": 7,
          "slug": "health_potion",
          "quantity": 5,
          "killCount": 0,
          "masterySlug": "",
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
      ],
      "myConfirmed": false,
      "theirConfirmed": false
    }
  }
}
```

> **Примечание по `eventType`:** реальный `eventType` в хедере — `tradeState` (camelCase), а не `TRADE_STATE`. Аналогично: `tradeCancelled` вместо `TRADE_CANCELLED`, `TRADE_OPENED` → `tradeOpened` будет выровнено при рефакторинге пакетов.

---

#### `tradeConfirm` — подтвердить обмен

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "tradeConfirm", "clientId": 7 },
  "body": { "sessionId": "trade_101_205_1709834400000", "characterId": 101 }
}
```

---

#### `TRADE_COMPLETE` — обмен совершён (обоим)

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "success", "eventType": "TRADE_COMPLETE", "clientId": 7 },
  "body": {
    "sessionId": "trade_101_205_1709834400000",
    "receivedItems": [
      { "itemId": 7, "slug": "health_potion", "quantity": 5 }
    ],
    "receivedGold": 0,
    "newGoldBalance": 0
  }
}
```

---

#### `tradeCancel` / `TRADE_CANCELLED`

**Направление:** Client → Chunk Server / Chunk Server → Client

```json
{
  "header": { "eventType": "tradeCancel", "clientId": 7 },
  "body": { "sessionId": "trade_101_205_1709834400000", "characterId": 101 }
}
```

```json
{
  "header": { "eventType": "TRADE_CANCELLED", "clientId": 7 },
  "body": {
    "sessionId": "trade_101_205_1709834400000",
    "reason": "PARTNER_CANCELLED"
  }
}
```

`reason`: `PARTNER_CANCELLED`, `TIMEOUT`, `PARTNER_DISCONNECTED`, `OUT_OF_RANGE`

---

### Серверная логика P2P

**Создание сессии:**
- `sessionId`: `"trade_{charA}_{charB}_{timestamp_ms}"`
- TTL: **60 секунд** без активности → автоотмена
- Один игрок — одна активная торговая сессия

**Финальная проверка перед обменом (оба confirmed):**
1. Оба игрока всё ещё онлайн
2. Оба всё ещё в радиусе 300 единиц
3. У каждого игрока есть все предложенные предметы в указанных количествах (re-validate в момент обмена)
4. У каждого игрока достаточно Gold Coin
5. У каждого достаточно слотов инвентаря для получаемых предметов

**Атомарный обмен:** все проверки → всё OK → swap, иначе `TRADE_CANCELLED { reason: "VALIDATION_FAILED" }`.

**Налог: нет.** P2P-торговля — прямой обмен между игроками. Налог здесь не нужен и подорвёт желание торговаться. Золотые стоки обеспечивают NPC-торговля (markup) и починка снаряжения.

---

## 5. Система износа (Durability)

### Текущий статус

Инфраструктура **полностью готова**:
- DB: `items.is_durable` (bool), `items.durability_max` (int), `player_inventory.durability_current` (int, NULL = не применимо)
- C++: `ItemDataStruct.isDurable/durabilityMax`, `PlayerInventoryItemStruct.durabilityCurrent`
- Чтение из БД и отправка клиенту уже реализованы

**Не реализовано:** логика изменения `durabilityCurrent` и починка.

---

### 5.1 Правила убыли прочности

#### Оружие (equipSlot = weapon)
- Теряет **1 прочность за каждый успешный удар**, нанесённый игроком
- Логика: при регистрации успешного hit-события в `CombatSystem` — если у экипированного оружия `isDurable = true`, уменьшить `durabilityCurrent -= 1`

#### Броня (equipSlot = head / chest / legs / boots / gloves)
- Теряет **1 прочность за каждый успешный удар, полученный** персонажем
- Логика: при получении урона — для каждого экипированного предмета с `isDurable = true` уменьшить `durabilityCurrent -= 1`

#### Смерть персонажа (death penalty)
- Экстра-штраф: все экипированные предметы с `isDurable = true` теряют дополнительно **N% от `durability_max`** (настраивается через `game_config`, ключ `durability.death_penalty_pct`)
- Формула: `durabilityCurrent = max(0, durabilityCurrent - ceil(durabilityMax * death_penalty_pct))`
- Значение по умолчанию: `0.05` (5%)

#### Состояние "Сломано" (`durability_current = 0`)
- Предмет остаётся в слоте, но его статы **не применяются** к персонажу
- Клиент получает уведомление `ITEM_BROKEN`
- Починить можно даже сломанный предмет

---

### 5.2 Пакеты Durability

#### `DURABILITY_UPDATE` — изменение прочности

**Направление:** Chunk Server → Client  
Отправляется при каждом изменении прочности экипированного предмета.

```json
{
  "header": { "eventType": "DURABILITY_UPDATE", "clientId": 7 },
  "body": {
    "updates": [
      { "inventorySlotId": 15, "itemId": 3, "durabilityCurrent": 87, "durabilityMax": 100 },
      { "inventorySlotId": 18, "itemId": 9, "durabilityCurrent": 54, "durabilityMax": 80 }
    ]
  }
}
```

Батчинг: все изменения за один игровой тик отправляются одним пакетом.

---

#### `ITEM_BROKEN` — предмет сломан

**Направление:** Chunk Server → Client

```json
{
  "header": { "eventType": "ITEM_BROKEN", "clientId": 7 },
  "body": { "inventorySlotId": 15, "itemId": 3, "slug": "iron_sword" }
}
```

---

#### `openRepairShop` — открыть ремонт

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "openRepairShop", "clientId": 7 },
  "body": { "npcId": 77, "characterId": 101 }
}
```

NPC с `npcType = "blacksmith"`. Ошибки: `NPC_NOT_FOUND`, `OUT_OF_RANGE`, `NOT_A_BLACKSMITH`

---

#### `REPAIR_SHOP` — список предметов к починке

**Направление:** Chunk Server → Client  
Показывает только экипированные предметы с `isDurable = true` у которых `durabilityCurrent < durabilityMax`.

```json
{
  "header": { "message": "success", "eventType": "REPAIR_SHOP", "clientId": 7 },
  "body": {
    "npcId": 77,
    "repairItems": [
      {
        "inventorySlotId": 15,
        "itemId": 3,
        "slug": "iron_sword",
        "durabilityCurrent": 45,
        "durabilityMax": 100,
        "repairCost": 110
      },
      {
        "inventorySlotId": 18,
        "itemId": 9,
        "slug": "leather_chest",
        "durabilityCurrent": 20,
        "durabilityMax": 80,
        "repairCost": 180
      }
    ],
    "repairAllCost": 290
  }
}
```

---

#### `repairItem` — починить конкретный предмет

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "repairItem", "clientId": 7 },
  "body": { "npcId": 77, "inventorySlotId": 15, "characterId": 101 }
}
```

---

#### `repairAll` — починить всё

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "repairAll", "clientId": 7 },
  "body": { "npcId": 77, "characterId": 101 }
}
```

---

#### `REPAIR_RESULT` — результат починки

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "success", "eventType": "REPAIR_RESULT", "clientId": 7 },
  "body": {
    "repairedItems": [
      { "inventorySlotId": 15, "itemId": 3, "durabilityMax": 100 }
    ],
    "goldSpent": 110,
    "newGoldBalance": 240
  }
}
```

Ошибки: `INSUFFICIENT_GOLD`, `ITEM_ALREADY_FULL`, `ITEM_NOT_EQUIPPED`

---

### 5.3 Формула стоимости починки

```
repair_cost = ceil((durability_max - durability_current) * base_repair_price)
```

Где `base_repair_price` — поле в таблице `items` (либо производная от `vendor_price_buy`). Простейший вариант без нового поля:

```
base_repair_price = vendor_price_buy / durability_max
repair_cost = ceil((durability_max - durability_current) * base_repair_price)
```

**Пример:** Iron Sword, `vendor_price_buy = 200`, `durability_max = 100`, `durabilityCurrent = 45`:
```
base_repair_price = 200 / 100 = 2.0
repair_cost = ceil((100 - 45) * 2.0) = 110 Gold
```

Полная починка всегда стоит `vendor_price_buy` (100% износ = цена покупки). Это разумный `gold sink`.

---

### 5.4 Персистентность prочности

- `durabilityCurrent` хранится в `player_inventory.durability_current`
- Обновляется flush-механизмом так же, как инвентарь — каждые **5 секунд** и при отключении
- При отключении: немедленный flush всех изменений (уже есть в системе)

---

### 5.5 Новый npcType: `blacksmith`

Добавить в существующий `npcType`-перечень. NPC-кузнец может быть как чисто кузнецом, так и совмещать роли (`vendor` + `blacksmith`) — для этого достаточно установить соответствующие таблицы в БД.

**Вариант совмещения:** один NPC может иметь и `vendor_npc`, и быть `blacksmith`. UI клиента предлагает выбор в диалоге: «Торговать» / «Починить снаряжение».

---

## 5a. Настраиваемые константы через `game_config`

Таблица `game_config` уже существует в БД и специально предназначена для геймплейных констант. Читается при старте Game Server и передаётся в Chunk Server. Изменения применяются без перезапуска через GM-команду `reload`.

### Новые ключи для торговли и износа

```sql
INSERT INTO game_config (key, value, value_type, description) VALUES
-- Торговля с NPC
('economy.vendor_sell_tax_pct',   '0.0',  'float',
 'Налог на продажу предметов торговцу NPC (0–1). 0 = без налога. '
 'Применяется сверх стандартной цены продажи. Gold sink при необходимости.'),

('economy.vendor_buy_markup_global', '0.0', 'float',
 'Глобальная надбавка к цене покупки у любого NPC-торговца (0–1), '
 'суммируется с vendor_npc.markup_pct. 0 = без надбавки.'),

-- Износ
('durability.death_penalty_pct',  '0.05', 'float',
 'Штраф прочности всех надетых предметов при смерти персонажа (0–1). '
 'Значение списывается от durability_max. 0.05 = минус 5% от макс. прочности.'),

('durability.weapon_loss_per_hit', '1',   'int',
 'Потеря прочности оружия за каждый успешный нанесённый удар. По умолчанию 1.'),

('durability.armor_loss_per_hit',  '1',   'int',
 'Потеря прочности каждого надетого元素 брони за каждый успешный полученный удар. По умолчанию 1.');
```

> Ключи формата `namespace.param_name` соответствуют соглашению проекта (combat.*, aggro.*).

---

## 5b. Интеграция с диалоговой системой

### Концепция

Открыть магазин или починку можно **двумя путями**:

1. **Прямой пакет** — клиент отправляет `openVendorShop` / `openRepairShop` напрямую (кнопка в UI, радиальное меню у NPC)
2. **Через диалог** — диалоговый граф NPC содержит `action`-узел с новым типом действия; сервер выполняет его и отправляет `VENDOR_SHOP` / `REPAIR_SHOP` вместе с `DIALOGUE_CLOSE`

Оба пути обрабатываются одной серверной логикой открытия магазина. Диалог — опциональный RP-слой.

### Новые типы действий в `actionGroup`

Расширение существующего перечня действий диалога:

| `type`             | Параметры | Описание |
|--------------------|-----------|----------|
| `open_vendor_shop` | —         | Закрывает диалог и открывает интерфейс магазина этого NPC |
| `open_repair_shop` | —         | Закрывает диалог и открывает интерфейс починки у этого NPC |

### Пример: NPC-кузнец с диалогом и починкой

Граф диалога кузнеца Торвальда:

```
line:  "Что нужно починить, странник?"
  └→ choice_hub:
       edge A: "Посмотри мою броню"    → action: open_repair_shop → DIALOGUE_CLOSE + REPAIR_SHOP
       edge B: "Куплю что-нибудь"      → action: open_vendor_shop → DIALOGUE_CLOSE + VENDOR_SHOP
       edge C: "Ничего, спасибо"       → end
```

**Пример `actionGroup` для ребра A:**
```json
{
  "actions": [
    { "type": "open_repair_shop" }
  ]
}
```

### Поведение сервера при `open_vendor_shop` / `open_repair_shop`

1. Сервер выполняет действие в `action`-узле диалога
2. Валидирует: `npcType` соответствует (`vendor` / `blacksmith`), дистанция ≤ `npc.radius`
3. Отправляет `VENDOR_SHOP` или `REPAIR_SHOP` клиенту
4. Закрывает диалоговую сессию → `DIALOGUE_CLOSE`

Клиент получает два пакета подряд и открывает нужный UI.

### NPC с совмещёнными ролями

Если один NPC имеет и `vendor_npc`, и `blacksmith` — диалог предлагает пользователю выбор. Клиент не знает о ролях NPC заранее; сервер сообщает о них через поле `npcType` в `spawnNPCs`. Клиент может показать соответствующие кнопки ещё до открытия диалога.

---

## 6. Роадмап реализации

### Приоритеты

| Приоритет | Задача | Зависимости |
|-----------|--------|-------------|
| 1 | Исправить `isTradable = true` у Gold Coin (SQL) | Нет |
| 2 | NPC Vendor — серверная логика (open/buy/sell) | Нет |
| 3 | NPC Vendor — пакеты и хендлеры | Приоритет 2 |
| 4 | Durability убыль — CombatSystem hook | Нет |
| 5 | Durability — DURABILITY_UPDATE + ITEM_BROKEN пакеты | Приоритет 4 |
| 6 | Blacksmith NPC — repairItem/repairAll | Приоритет 4 |
| 7 | P2P Trade — сессии, пакеты, логика | Приоритет 1 |
| 8 | currency_transactions запись | Приоритеты 2, 7 |

### Этапы

#### Этап 1 — Торговля с NPC (Layer 1)

- [ ] SQL: `isTradable = true` у Gold Coin
- [ ] SQL: добавить ключи `economy.*` и `durability.*` в `game_config` (без `vendor_restock_interval_sec` — интервал per-item)
- [ ] SQL: добавить поля `restock_amount`, `stock_max`, `restock_interval_sec`, `last_restock_at` в `vendor_inventory`
- [ ] Game Server: загрузка `vendor_npc` + `vendor_inventory` в память
- [ ] Game Server: передача `game_config` ключей `economy.*` и `durability.*` в Chunk Server
- [ ] Game Server: scheduler (раз в 60с) → SQL UPDATE по `restock_interval_sec` + `last_restock_at` → RETURNING → `vendorStockUpdate` в Chunk Server
- [ ] Chunk Server: принять данные вендоров от Game Server (`VendorDataStruct`)
- [ ] Chunk Server: хендлер `openVendorShop` → проверки → `VENDOR_SHOP`
- [ ] Chunk Server: хендлер `buyItem` → проверки → atomic update → `BUY_RESULT` + `item_received`
- [ ] Chunk Server: хендлер `sellItem` → проверки → atomic update → `SELL_RESULT`
- [ ] Chunk Server: запись в `currency_transactions` через Game Server
- [ ] Chunk Server: `vendorError` с кодами ошибок

#### Этап 2 — Износ снаряжения

- [ ] Chunk Server: в `CombatSystem` — хук на нанесение урона (оружие `-1 durability`)
- [ ] Chunk Server: в `CombatSystem` — хук на получение урона (броня `-1 durability`)
- [ ] Chunk Server: в обработчике смерти — штраф из `game_config[durability.death_penalty_pct]` от `durabilityMax` на все `isDurable` предметы
- [ ] Chunk Server: батчинг изменений + отправка `DURABILITY_UPDATE` (не чаще раза в тик)
- [ ] Chunk Server: проверка `durabilityCurrent == 0` → отправка `ITEM_BROKEN` + отключение статов
- [ ] Game Server: добавить `durability_current` в flush-поток инвентаря

#### Этап 3 — Починка у кузнеца

- [ ] DB: добавить `npcType = "blacksmith"` для нужных NPC
- [ ] Chunk Server: хендлер `openRepairShop` → расчёт стоимости починки → `REPAIR_SHOP`
- [ ] Chunk Server: хендлер `repairItem` → проверки → update durability → `REPAIR_RESULT`
- [ ] Chunk Server: хендлер `repairAll` → bulk repair → `REPAIR_RESULT`
- [ ] Chunk Server: диалоговые действия `open_vendor_shop` и `open_repair_shop` в `DialogueActionExecutor`

#### Этап 4 — P2P Trade

- [ ] Chunk Server: `TradeSessionManager` (аналогично `DialogueSessionManager`)
- [ ] Chunk Server: хендлер `tradeRequest` → проверки → `TRADE_INVITE` (второму игроку)
- [ ] Chunk Server: хендлеры `tradeAccept` / `tradeDecline`
- [ ] Chunk Server: хендлер `tradeOfferUpdate` → `TRADE_STATE` (обоим), сброс `confirmed`
- [ ] Chunk Server: хендлер `tradeConfirm` → check both confirmed → финальная валидация → `TRADE_COMPLETE` или `TRADE_CANCELLED`
- [ ] Chunk Server: хендлер `tradeCancel` + auto-cancel при таймауте / дисконнекте / выходе из радиуса
- [ ] Chunk Server: запись в `currency_transactions` (`reason_type = "trade"`) — только предметы, без налога

---

## Сводная таблица пакетов (выпуск)

### Client → Chunk Server

| Пакет | Система | Ключевые поля |
|-------|---------|---------------|
| `openVendorShop` | Vendor | `npcId`, `characterId` |
| `buyItem` | Vendor | `npcId`, `itemId`, `quantity`, `characterId` |
| `sellItem` | Vendor | `npcId`, `inventorySlotId`, `quantity`, `characterId` |
| `tradeRequest` | P2P Trade | `targetCharacterId`, `characterId` |
| `tradeAccept` | P2P Trade | `sessionId`, `characterId` |
| `tradeDecline` | P2P Trade | `sessionId`, `characterId` |
| `tradeOfferUpdate` | P2P Trade | `sessionId`, `offeredItems[]`, `offeredGold`, `characterId` |
| `tradeConfirm` | P2P Trade | `sessionId`, `characterId` |
| `tradeCancel` | P2P Trade | `sessionId`, `characterId` |
| `openRepairShop` | Durability | `npcId`, `characterId` |
| `repairItem` | Durability | `npcId`, `inventorySlotId`, `characterId` |
| `repairAll` | Durability | `npcId`, `characterId` |

### Chunk Server → Client

| Пакет | Система | Когда |
|-------|---------|-------|
| `VENDOR_SHOP` | Vendor | При открытии магазина |
| `BUY_RESULT` | Vendor | После покупки (+ `item_received`) |
| `SELL_RESULT` | Vendor | После продажи |
| `vendorError` | Vendor | Ошибка торговли |
| `TRADE_INVITE` | P2P Trade | Приглашение второму игроку |
| `TRADE_OPENED` | P2P Trade | Торговля открыта (обоим) |
| `TRADE_STATE` | P2P Trade | Обновление оффера (обоим) |
| `TRADE_COMPLETE` | P2P Trade | Обмен совершён (обоим) |
| `TRADE_CANCELLED` | P2P Trade | Отмена (обоим) |
| `DURABILITY_UPDATE` | Durability | Изменение прочности (батч) |
| `ITEM_BROKEN` | Durability | Предмет сломан |
| `REPAIR_SHOP` | Durability | Список предметов к починке |
| `REPAIR_RESULT` | Durability | Результат починки |
