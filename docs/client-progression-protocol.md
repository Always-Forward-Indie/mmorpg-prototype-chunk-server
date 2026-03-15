# Client ↔ Server Protocol: Прокачка и прогрессия

**Версия документа:** v1.0  
**Актуально для:** chunk-server v0.0.4+

---

## Содержание

1. [Обзор систем прогрессии](#1-обзор-систем-прогрессии)
2. [Опыт и уровни](#2-опыт-и-уровни)
3. [Долг опыта (XP Debt)](#3-долг-опыта-xp-debt)
4. [Мастерство оружия (Mastery)](#4-мастерство-оружия-mastery)
5. [Репутация с фракциями](#5-репутация-с-фракциями)
6. [Душа предмета (Item Soul)](#6-душа-предмета-item-soul)
7. [Исследование зон (Zone Exploration)](#7-исследование-зон-zone-exploration)
8. [Чемпионы](#8-чемпионы)
9. [Зональные события (Zone Events)](#9-зональные-события-zone-events)
10. [Бестиарий](#10-бестиарий)
11. [Пити (Pity System)](#11-пити-pity-system)
12. [Пакет world_notification — референс](#12-пакет-world_notification--референс)

---

## 1. Обзор систем прогрессии

| Система | Пакеты клиенту | Хранение |
|---------|----------------|----------|
| XP/Уровень | `experience_update`, `stats_update` | Game Server (PostgreSQL) |
| XP Долг | в `stats_update` (поле `experience.debt`) | Game Server (PostgreSQL) |
| Мастерство | `world_notification` (тип `mastery_tier_up`) | Game Server (PostgreSQL) |
| Репутация | нет прямого пакета; влияет на диалоги/магазин | Game Server (PostgreSQL) |
| Душа предмета | в `stats_update` (`attributes.effective`) | Game Server (PostgreSQL) |
| Исследование зон | `world_notification` (тип `zone_explored`) | Game Server (флаги персонажа) |
| Чемпионы | `world_notification` (несколько типов) | Chunk Server (in-memory) |
| Зональные события | `world_notification` (тип `zone_event_start/end`) | Chunk Server (in-memory) |
| Бестиарий | `world_notification` + запрос `getBestiaryEntry` | Game Server (PostgreSQL) |
| Пити | `world_notification` (тип `pity_hint`) | Game Server (счётчики персонажа) |

---

## 2. Опыт и уровни

### 2.1 Получение XP — `experience_update`

Отправляется **только клиенту-владельцу** после получения опыта (убийство моба, выполнение квеста и т.д.).

**Направление:** Сервер → Клиент (только инициатору)  
**eventType:** `experience_update`

```json
{
  "header": {
    "eventType": "experience_update",
    "message": "Experience updated"
  },
  "body": {
    "characterId": 7,
    "experienceChange": 150,
    "oldExperience": 2400,
    "newExperience": 2550,
    "oldLevel": 5,
    "newLevel": 5,
    "expForCurrentLevel": 2000,
    "expForNextLevel": 3500,
    "levelUp": false,
    "reason": "mob_kill",
    "sourceId": 1001
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа |
| `experienceChange` | int | Начисленный XP (после вычета долга) |
| `oldExperience` | int | XP до начисления |
| `newExperience` | int | XP после начисления |
| `oldLevel` | int | Уровень до |
| `newLevel` | int | Уровень после (совпадает с `oldLevel` если нет левел-апа) |
| `expForCurrentLevel` | int | Накопленный XP на текущем уровне |
| `expForNextLevel` | int | Необходимо XP для следующего уровня |
| `levelUp` | bool | `true` если произошёл левел-ап |
| `reason` | string | Причина: `mob_kill`, `quest_complete`, `zone_explored` и т.д. |
| `sourceId` | int | ID источника (UID моба, ID квеста и т.д.) |

### 2.2 Левел-ап

При `levelUp: true`:
1. `experience_update` содержит `newLevel: oldLevel + 1`
2. Следом приходит `stats_update` с:
   - Увеличенным `health.max` (+10 HP за уровень)
   - Увеличенным `mana.max` (+5 MP за уровень)
   - Обновлёнными `base`-атрибутами (зависит от класса)

```json
{
  "header": {
    "eventType": "experience_update",
    "message": "Level up!"
  },
  "body": {
    "characterId": 7,
    "experienceChange": 400,
    "oldExperience": 3300,
    "newExperience": 3700,
    "oldLevel": 5,
    "newLevel": 6,
    "expForCurrentLevel": 0,
    "expForNextLevel": 5000,
    "levelUp": true,
    "reason": "mob_kill",
    "sourceId": 1023
  }
}
```

#### Последовательность при левел-апе

```
Убийство моба →
  ▶ combatResult (targetDied: true)
  ▶ experience_update (levelUp: true, newLevel: 6)
  ▶ stats_update (health.max увеличен, новые base-значения)
  ▶ itemDrop (если есть дроп)
```

---

## 3. Долг опыта (XP Debt)

System death-penalty: вместо потери набранного XP появляется долг.

### Как работает
1. **Смерть персонажа** → к `stats_update.experience.debt` добавляется штраф (N% от XP до следующего уровня)
2. **Последующие XP-награды** → 50% идут в погашение долга, 50% в фактический XP
3. **Долг погашен** → полный XP снова зачисляется напрямую

### Где видеть долг

Долг отображается в пакете `stats_update` (см. `stats-update-protocol.md`), поле `experience.debt`:

```json
{
  "body": {
    "experience": {
      "current": 2400,
      "expForCurrentLevel": 2000,
      "expForNextLevel": 3500,
      "level": 5,
      "debt": 800
    }
  }
}
```

### Расчёт фактического XP при наличии долга

Получено 150 XP, долг 800:
- Погашение долга: `150 * 0.5 = 75` → долг становится `800 - 75 = 725`
- Фактический XP: `150 - 75 = 75`
- В `experience_update.experienceChange` будет `75` (уже после вычета)

Получено 150 XP, долг 0:
- Весь XP зачисляется: `experienceChange = 150`

> **Для UI:** Показывать полосу долга под основной полосой XP. Долг тает при убийстве мобов. После смерти сообщать игроку о размере штрафа отдельным уведомлением.

---

## 4. Мастерство оружия (Mastery)

Пассивная системапрогресса использования конкретного типа оружия. Каждый тип оружия имеет `masterySlug` (например, `"sword"`, `"bow"`, `"staff"`).

### Прогресс

- Значение мастерства: `0.0 – 100.0`
- Прирост: каждый успешный удар/применение скилла оружием
- Пороги тиров: **20, 50, 80, 100**
- Сохраняется на сервере каждые 10 ударов или при пересечении порога

### Бонусы по тирам

| Тир | Порог | Бонус | Тип |
|-----|-------|-------|-----|
| T1 | 20 | +1% к `physical_attack` | permanent ActiveEffect |
| T2 | 50 | +4% к `physical_attack` | permanent ActiveEffect |
| T3 | 80 | +3% к `crit_chance` | permanent ActiveEffect |
| T4 | 100 | +2% к `parry_chance` | permanent ActiveEffect |

Бонусы накапливаются: при T3 активны все T1+T2+T3 (итого +5% physical_attack + 3% crit_chance).

### Уведомление повышения тира — `mastery_tier_up`

**Направление:** Сервер → Клиент (только владельцу, через `world_notification`)

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 7,
    "notificationType": "mastery_tier_up",
    "text": "Мастерство Меч достигло II тира!",
    "data": {
      "masterySlug": "sword",
      "masteryValue": 50.3,
      "tier": 2
    }
  }
}
```

Следом приходит `stats_update` с новыми значениями `attributes.effective` (бонус тира уже включён).

> **Для UI:** Показать анимацию/тост `"Мастерство [weapon] — Тир 2!"`. Значение мастерства не присылается в `stats_update`, только через это уведомление.

---

## 5. Репутация с фракциями

Система отношений персонажа с фракциями мира (торговцы, гильдии, расы).

### Тиры репутации

| Тир | Значение репутации | Эффекты |
|-----|--------------------|---------|
| `enemy` | < -500 | Заблокирован диалог с NPC фракции, атакуют |
| `stranger` | от -500 до < 0 | Базовые взаимодействия |
| `neutral` | от 0 до < 200 | Стандартные условия торговли |
| `friendly` | от 200 до < 500 | **-5% скидка** у торговцев фракции |
| `ally` | ≥ 500 | **-5% скидка** + расширенный ассортимент |

### Влияние на торговлю

При открытии магазина торговца (`openVendorShop`):
- Если репутация с фракцией торговца ≥ 200 → цены автоматически уменьшены на 5%
- Если репутация < -500 → сервер отклонит запрос, пакет `vendorShopData` не придёт

Цены в пакете `vendorShopData` уже рассчитаны с учётом репутации:

```json
{
  "header": { "eventType": "vendorShopData" },
  "body": {
    "vendorId": 50,
    "items": [
      {
        "itemSlug": "health_potion_small",
        "buyPrice": 95,
        "sellPrice": 20
      }
    ]
  }
}
```

_(Стандартная цена 100g, с скидкой 5% = 95g — клиент не нужно самому пересчитывать.)_

### Влияние на диалог

При запросе `npcInteract` к NPC фракции, с которой репутация < -500:
- Сервер возвращает `dialogueError` с кодом `BLOCKED_BY_REPUTATION`
- NPC не открывает диалог и не открывает магазин

```json
{
  "header": { "eventType": "dialogueError" },
  "body": {
    "errorCode": "BLOCKED_BY_REPUTATION",
    "factionSlug": "dark_cult"
  }
}
```

### Изменение репутации

Репутация изменяется через квестовые действия (`change_reputation` action в диалоге) или убийство мобов фракций. Никаких специальных пакетов не приходит — изменение отражается в следующем взаимодействии с NPC.

> **Для UI:** Текущие значения репутации нужно хранить на стороне клиента на основе событий квестов/диалогов, либо запрашивать отдельным API (если добавлено). Сейчас сервер не присылает "репутация обновлена" отдельным пакетом.

---

## 6. Душа предмета (Item Soul)

Оружие накапливает убийства, совершённые им. При достижении порогов оружие получает постоянные бонусы.

### Пороги и бонусы

| Тир души | Убийства | Бонус | Тип |
|----------|----------|-------|-----|
| T0 | 0–49 | Нет | — |
| T1 | 50–199 | +1 к основному атрибуту оружия (flat) | permanent |
| T2 | 200–499 | +2 к основному атрибуту оружия (flat) | permanent |
| T3 | 500+ | +3 к основному атрибуту оружия (flat) | permanent |

«Основной атрибут» — `attributeSlug` оружия (например, `physical_attack`).

### Как клиент получает данные

Душа предмета **не отображается в инвентарном пакете** отдельно. Бонус автоматически включён в `attributes.effective` пакета `stats_update` при надетом оружии.

Счётчик убийств хранится в инвентарном Item-объекте, поле `killCount`:

```json
{
  "itemSlug": "iron_sword",
  "itemInstanceId": 1234,
  "quantity": 1,
  "killCount": 74,
  "soulTier": 1
}
```

> **Примечание:** `soulTier` и `killCount` присутствуют в данных предмета только для оружия. Для неоружейных предметов эти поля отсутствуют.

### Визуализация

Рекомендуется отображать над иконкой оружия цветной индикатор Тира Души:
- T1: синий блеск
- T2: фиолетовый блеск
- T3: золотой блеск

---

## 7. Исследование зон (Zone Exploration)

При первом посещении именованной зоны персонаж получает награду опыта. При **каждом** пересечении границы зоны (в том числе при первом логине) клиент получает `zone_entered`.

### Триггер `zone_entered`

Сервер отслеживает последнюю известную игровую зону для каждого персонажа (in-memory). При каждом `moveCharacter`, если зона изменилась (или при `joinGameCharacter`), немедленно отправляет:

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 7,
    "notificationType": "zone_entered",
    "text": "",
    "data": {
      "zoneSlug":   "dark_forest",
      "minLevel":   10,
      "maxLevel":   20,
      "isPvp":      false,
      "isSafeZone": false
    }
  }
}
```

> `text` пуст — клиент локализует название зоны по `zoneSlug`.  
> Используется для обновления UI (название зоны, цвет рамки PvP/safe, уровень).

### Триггер `zone_explored`

Сервер отслеживает первое посещение через флаг `explored_{zoneSlug}` в данных персонажа. При входе в зону проверяется наличие флага:
- Флага нет → выдать XP, установить флаг
- Флаг есть → ничего

### Пакеты при первом посещении

1. `world_notification` с типом `zone_explored`:

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 7,
    "notificationType": "zone_explored",
    "text": "",
    "data": {
      "zoneSlug": "dark_forest",
      "xpGained": 300
    }
  }
}
```

> `text` пуст — используй `zoneSlug` для локализации названия зоны на клиенте.

2. `experience_update` с `reason: "zone_explored"`:

```json
{
  "header": {
    "eventType": "experience_update"
  },
  "body": {
    "characterId": 7,
    "experienceChange": 300,
    "reason": "zone_explored",
    "sourceId": 0
  }
}
```

> **Для UI:** Показать тост "Зона открыта: Тёмный лес" вместе с анимацией XP-прибавки.

---

## 8. Чемпионы

Особые усиленные мобы с дополнительным лутом и XP.

### Типы появления чемпионов

| Тип | Условие появления |
|-----|------------------|
| **Threshold** | После N убийств одного типа моба в зоне |
| **Timed** | Раз в N часов (scheduled) или с шансом каждый час (random) |
| **Survival** | Обычный моб, переживший 12+ часов без смерти |

### Характеристики чемпиона

- HP: 3× от обычного значения
- Урон: 1.5× от обычного
- XP: 2× от обычного
- Лут: ×1.5 к шансу и количеству предметов
- Время жизни: 30 минут (despawn автоматически)

### Как чемпион выглядит в пакетах мобов

В пакете `spawnMobsInZone` чемпион является обычным мобом с дополнительными полями:

```json
{
  "id": 5,
  "uid": 10023,
  "zoneId": 3,
  "name": "[Чемпион] Матёрый Волк",
  "slug": "wolf",
  "race": "Beast",
  "level": 8,
  "isAggressive": true,
  "isDead": false,
  "isChampion": true,
  "rankCode": "champion",
  "stats": {
    "health": { "current": 900, "max": 900 },
    "mana": { "current": 0, "max": 0 }
  },
  "position": { "x": 143.0, "y": 88.0, "z": 0.0, "rotationZ": 2.1 },
  "velocity": { "dirX": 0.0, "dirY": 0.0, "speed": 0.0 },
  "combatState": 0,
  "attributes": [
    { "attributeSlug": "physical_attack", "base": 45, "effective": 45 }
  ]
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `isChampion` | bool | `true` — это чемпион |
| `rankCode` | string | `"champion"` — для визуального выделения |

### Уведомления о чемпионах

Все уведомления приходят только клиентам в **той же игровой зоне** (gameZoneId), где происходит событие.

#### Скоро появится — `champion_spawned_soon`

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "notificationType": "champion_spawned_soon",
    "text": "Через несколько минут появится wolf!",
    "data": { "slug": "wolf" }
  }
}
```

#### Появился — `champion_spawned`

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "notificationType": "champion_spawned",
    "text": "[!] Матёрый Волк появился в зоне!",
    "data": { "mobSlug": "wolf", "uid": 10023 }
  }
}
```

После `champion_spawned` сервер рассылает стандартный `spawnMobsInZone` с данными чемпиона. Связывайте моба по `uid` из `data.uid`.

#### Убит — `champion_killed`

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "notificationType": "champion_killed",
    "text": "Aragorn убил Чемпиона!",
    "data": { "killerCharId": 7 }
  }
}
```

---

## 9. Зональные события (Zone Events)

Временные события в игровых зонах, усиливающие лут или спавн мобов.

### Типы триггеров

| Тип | Описание |
|-----|----------|
| `scheduled` | Запускается каждые N часов |
| `random` | Случайный шанс запуска каждый час |

### Что изменяет активное событие (серверная сторона)

| Параметр | Описание |
|----------|----------|
| `lootMultiplier` | Множитель дропа предметов и золота |
| `spawnRateMultiplier` | Множитель скорости respawn мобов в зоне |
| `mobSpeedMultiplier` | Множитель скорости передвижения мобов |

Клиент **не получает** числовые значения этих множителей — только уведомление.

### Начало события — `zone_event_start`

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "notificationType": "zone_event_start",
    "text": "Началось мировое событие: goblin_invasion",
    "data": {
      "eventSlug": "goblin_invasion",
      "durationSec": 3600,
      "gameZoneId": 1
    }
  }
}
```

### Конец события — `zone_event_end`

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "notificationType": "zone_event_end",
    "text": "Событие завершилось.",
    "data": { "eventSlug": "goblin_invasion" }
  }
}
```

> **Для UI:** Показать баннер/таймер активного события в зоне. При `durationSec` отсчитывать таймер от 0. При получении `zone_event_end` — скрыть баннер.

---

## 10. Бестиарий

Система отслеживания убийств мобов и разблокировки информации о них.

### Запрос записи бестиария

**Направление:** Клиент → Сервер  
**eventType:** `getBestiaryEntry`

```json
{
  "header": {
    "eventType": "getBestiaryEntry",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "characterId": 7,
    "mobSlug": "wolf"
  }
}
```

### Ответ — `bestiaryEntry`

```json
{
  "header": {
    "eventType": "bestiaryEntry",
    "message": "Bestiary entry found"
  },
  "body": {
    "mobSlug": "wolf",
    "mobName": "Волк",
    "killCount": 47,
    "tiers": [
      {
        "tier": 1,
        "killsRequired": 10,
        "isUnlocked": true,
        "revealedInfo": "Базовые характеристики и описание"
      },
      {
        "tier": 2,
        "killsRequired": 50,
        "isUnlocked": false,
        "revealedInfo": "Слабые стороны и дроп-лист"
      },
      {
        "tier": 3,
        "killsRequired": 100,
        "isUnlocked": false,
        "revealedInfo": "Редкий дроп и скрытые механики"
      }
    ]
  }
}
```

### Уведомление о разблокировке тира

При достижении порога убийств клиент опрашивает `getBestiaryEntry` при следующем открытии UI — отдельного push-уведомления о разблокировке тира нет.

---

## 11. Пити (Pity System)

Система гарантии редких дропов. Счётчик растёт с каждым убитым мобом конкретного типа без дропа редкого предмета.

### Типы пити

| Тип | Описание |
|-----|----------|
| `soft_pity` | Шанс дропа начинает нелинейно расти |
| `hard_pity` | Дроп гарантирован |

### Подсказка пити — `pity_hint`

Когда счётчик превышает порог `soft_pity`, сервер отправляет уведомление:

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 7,
    "notificationType": "pity_hint",
    "text": "Ты давно охотишься здесь... Удача должна улыбнуться",
    "data": {}
  }
}
```

При срабатывании `hard_pity` дроп выдаётся автоматически — уведомление не нужно (предмет появится в обычном `itemDrop`).

---

## 12. Пакет world_notification — референс

Все уведомления мира приходят одним `eventType: "world_notification"` с полем `body.notificationType` для различения типа.

### Таблица всех типов `world_notification`

| `notificationType` | `data` | Кому | Описание |
|--------|------|----------|---------|
| `mastery_tier_up` | `{masterySlug, masteryValue, tier}` | Только владельцу | Повышение тира мастерства |
| `zone_entered` | `{zoneSlug, minLevel, maxLevel, isPvp, isSafeZone}` | Только владельцу | Смена игровой зоны (отправляется при каждом входе, включая первый логин) |
| `zone_explored` | `{zoneSlug, xpGained}` | Только владельцу | Первое посещение зоны — выдаётся XP |
| `pity_hint` | `{}` | Только владельцу | Счётчик пити приближается к дропу |
| `champion_spawned_soon` | `{slug}` | Всем в игровой зоне | Чемпион вот-вот появится |
| `champion_spawned` | `{mobSlug, uid}` | Всем в игровой зоне | Чемпион появился |
| `champion_killed` | `{killerCharId}` | Всем в игровой зоне | Чемпион убит |
| `champion_despawned` | `{}` | Всем в игровой зоне | Чемпион исчез (taймаут 30 мин) |
| `zone_event_start` | `{eventSlug, durationSec, gameZoneId}` | Всем в игровой зоне | Началось зональное событие |
| `zone_event_end` | `{eventSlug}` | Всем в игровой зоне | Зональное событие завершилось |

### Общий формат пакета

```json
{
  "header": {
    "eventType": "world_notification",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "notificationType": "<тип из таблицы выше>",
    "text": "<локализованное сообщение для отображения>",
    "data": { }
  }
}
```

- `characterId` — присутствует только в личных уведомлениях (только владельцу); в broadcast-уведомлениях зоны отсутствует
- `text` — готовый текст для отображения (на русском, из конфигурации сервера)
- `data` — дополнительные данные для программной обработки (специфично для каждого типа)
