# 11. Чат и серверные уведомления

## Обзор

Чат — 3 канала с rate-limiting и server-side верификацией отправителя.
Уведомления — единая система `world_notification` для всех игровых событий.

---

## 11.1. chatMessage — Отправка сообщения

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "chatMessage",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400000,
      "requestId": "sync_1711709400000_42_400_abc"
    }
  },
  "body": {
    "channel": "local",
    "text": "Hello everyone!",
    "targetName": ""
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `channel` | string | `"local"`, `"zone"`, `"whisper"` |
| `text` | string | Текст сообщения (макс. 255 символов) |
| `targetName` | string | Имя получателя (только для `whisper`) |

### Каналы

#### LOCAL — Локальный чат

- Радиус: **50.0 единиц** (3D евклидово расстояние)
- Рассылка: только игрокам в радиусе
- Формула: `sqrt(dx² + dy² + dz²) ≤ 50.0`

#### ZONE — Зональный чат

- Рассылка: **всем подключённым** клиентам на chunk-сервере

#### WHISPER — Личное сообщение

- Рассылка: только целевому персонажу по имени
- Если цель не найдена → ошибка отправителю

### Сервер → Multicast/Broadcast/Unicast (chatMessage)

```json
{
  "header": {
    "eventType": "chatMessage",
    "clientId": 0,
    "message": "success"
  },
  "body": {
    "channel": "local",
    "senderName": "Alexei",
    "senderId": 7,
    "text": "Hello everyone!",
    "timestamp": 1711709400050
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `channel` | string | Канал доставки |
| `senderName` | string | Имя отправителя (**серверное**, не клиентское) |
| `senderId` | int | ID персонажа отправителя |
| `text` | string | Текст сообщения |
| `timestamp` | long | Серверная метка времени |

### Ошибка whisper (цель не найдена)

```json
{
  "header": {
    "eventType": "chatMessage",
    "clientId": 42,
    "message": "Player 'NonExistentPlayer' not found"
  },
  "body": null
}
```

### Rate-limiting

- Скользящее окно: `RATE_LIMIT_WINDOW_MS` (настраивается)
- Максимум сообщений за окно: `RATE_LIMIT_MAX` (настраивается)
- Отдельный bucket на каждый `clientId`
- При превышении: сообщение игнорируется

### Безопасность

- `senderName` **всегда** подставляется сервером из `CharacterManager`
- Клиент **не может** подменить имя отправителя
- `senderId` берётся из контекста сессии, а не из запроса
- Длина текста валидируется (макс. 255 символов)

---

## 11.2. world_notification — Серверные уведомления

### Универсальная структура

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "notificationId": "unique_notification_id",
    "notificationType": "<тип>",
    "priority": "<приоритет>",
    "channel": "<канал>",
    "text": "",
    "data": { /* специфичные данные */ }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | Получатель |
| `notificationId` | string | Уникальный ID уведомления |
| `notificationType` | string | Тип события |
| `priority` | string | `"low"`, `"medium"`, `"high"` |
| `channel` | string | `"silent"`, `"toast"`, `"banner"` |
| `text` | string | Текст (обычно пустой — клиент формирует из data) |
| `data` | object | Данные события |

### Каналы доставки

| Канал | Описание клиенту |
|-------|------------------|
| `silent` | Обновить данные без визуального уведомления |
| `toast` | Показать всплывающее уведомление |
| `banner` | Показать крупный баннер |

### Приоритеты

| Приоритет | Поведение |
|-----------|-----------|
| `low` | Может быть отложено / свёрнуто |
| `medium` | Показать при первой возможности |
| `high` | Показать немедленно, прервать текущие уведомления |

---

### Типы world_notification

#### bestiary_kill_update

```json
{
  "notificationType": "bestiary_kill_update",
  "priority": "low",
  "channel": "silent",
  "data": {
    "mobTemplateId": 15,
    "newKillCount": 47
  }
}
```

#### bestiary_tier_unlocked

```json
{
  "notificationType": "bestiary_tier_unlocked",
  "priority": "medium",
  "channel": "toast",
  "data": {
    "mobTemplateId": 15,
    "tier": 3,
    "newKillCount": 25,
    "categorySlug": "combat_info"
  }
}
```

#### mastery_tier_up

```json
{
  "notificationType": "mastery_tier_up",
  "priority": "medium",
  "channel": "toast",
  "data": {
    "masterySlug": "sword_mastery",
    "tier": "sword_mastery_t2_damage"
  }
}
```

---

### Нотификации из действий диалога

Отправляются с `eventType` равным значению поля `type`:

#### quest_offered

```json
{
  "header": { "eventType": "quest_offered" },
  "body": {
    "type": "quest_offered",
    "questId": 10,
    "clientQuestKey": "quest_wolf_hunt"
  }
}
```

#### quest_turned_in

```json
{
  "header": { "eventType": "quest_turned_in" },
  "body": {
    "type": "quest_turned_in",
    "questId": 10,
    "clientQuestKey": "quest_wolf_hunt"
  }
}
```

#### quest_failed

```json
{
  "header": { "eventType": "quest_failed" },
  "body": {
    "type": "quest_failed",
    "questId": 10,
    "clientQuestKey": "quest_wolf_hunt"
  }
}
```

#### item_received

```json
{
  "header": { "eventType": "item_received" },
  "body": {
    "type": "item_received",
    "itemId": 7,
    "quantity": 3
  }
}
```

#### exp_received

```json
{
  "header": { "eventType": "exp_received" },
  "body": {
    "type": "exp_received",
    "amount": 5000
  }
}
```

#### gold_received

```json
{
  "header": { "eventType": "gold_received" },
  "body": {
    "type": "gold_received",
    "amount": 500
  }
}
```

#### skill_learned

```json
{
  "header": { "eventType": "skill_learned" },
  "body": {
    "type": "skill_learned",
    "skillSlug": "shield_bash"
  }
}
```

#### learn_skill_failed

```json
{
  "header": { "eventType": "learn_skill_failed" },
  "body": {
    "type": "learn_skill_failed",
    "reason": "insufficient_sp",
    "skillSlug": "shield_bash"
  }
}
```

#### reputationChanged

```json
{
  "header": { "eventType": "reputationChanged" },
  "body": {
    "type": "reputationChanged",
    "faction": "bandits",
    "delta": 50
  }
}
```

#### openVendorShop (из диалога)

```json
{
  "header": { "eventType": "openVendorShop" },
  "body": {
    "type": "openVendorShop",
    "mode": "shop",
    "npcId": 42,
    "npcSlug": "merchant_john",
    "items": [ /* массив товаров */ ]
  }
}
```

#### openRepairShop (из диалога)

```json
{
  "header": { "eventType": "openRepairShop" },
  "body": {
    "type": "openRepairShop",
    "npcId": 42,
    "items": [
      {
        "inventoryItemId": 123,
        "itemId": 7,
        "itemName": "iron_sword",
        "durabilityCurrent": 45,
        "durabilityMax": 100,
        "repairCost": 150
      }
    ]
  }
}
```

---

## Рекомендации для клиента

### Обработка world_notification

```pseudocode
on world_notification(notif):
    switch notif.channel:
        "silent": updateDataOnly(notif)
        "toast":  showToast(notif.notificationType, notif.data)
        "banner": showBanner(notif.notificationType, notif.data)

    switch notif.notificationType:
        "bestiary_kill_update": updateBestiaryCounter(data.mobTemplateId, data.newKillCount)
        "bestiary_tier_unlocked": unlockBestiaryTier(data.mobTemplateId, data.tier)
        "mastery_tier_up": showMasteryAchievement(data.masterySlug, data.tier)
```

### Обработка dialogue action notifications

```pseudocode
on quest_offered: addQuestToJournal(questId, clientQuestKey, state="active")
on quest_turned_in: markQuestTurnedIn(questId)
on quest_failed: markQuestFailed(questId)
on item_received: showItemReceivedPopup(itemId, quantity)
on exp_received: showExpGainAnimation(amount)
on gold_received: showGoldGainAnimation(amount)
on skill_learned: addSkillToHotbar(skillSlug), showLearnedPopup()
on learn_skill_failed: showErrorMessage(reason, skillSlug)
on reputationChanged: updateReputationUI(faction, delta)
on openVendorShop: openVendorUI(npcId, items)
on openRepairShop: openRepairUI(npcId, items)
```
