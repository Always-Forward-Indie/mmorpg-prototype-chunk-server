# 08. Прогрессия: опыт, уровни, статы, регенерация, мастерство, репутация

## Обзор

Прогрессия включает 6 взаимосвязанных систем:
- **Опыт и уровни**: XP → level up → stat bonuses
- **XP-долг**: Штраф за смерть, погашается из нового XP
- **Статы**: base + equipment + effects + mastery + item soul
- **OOC-регенерация**: Пассивное восстановление HP/MP вне боя
- **Мастерство**: Прогресс 0-100 за каждый тип оружия
- **Репутация**: Отношения с фракциями

---

## 8.1. Система опыта

### getCharacterExperience — Запрос текущего XP

#### Клиент → Сервер

```json
{
  "header": {
    "eventType": "getCharacterExperience",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400000,
      "requestId": "sync_1711709400000_42_300_abc"
    }
  },
  "body": {
    "characterId": 7
  }
}
```

#### Сервер → Unicast (characterExperience)

> **⚠ Внимание:** Этот ответ использует ключ `"event"` (не `"eventType"`) в заголовке — это особенность данного конкретного обработчика.

```json
{
  "header": {
    "event": "characterExperience",
    "status": "success",
    "timestamp": 1711709400050,
    "requestId": "sync_1711709400000_42_300_abc"
  },
  "body": {
    "characterId": 7,
    "currentLevel": 15,
    "currentExperience": 45000,
    "expForCurrentLevel": 40000,
    "expForNextLevel": 52000,
    "expInCurrentLevel": 5000,
    "expNeededForNextLevel": 7000,
    "progressToNextLevel": 0.714
  },
  "timestamps": {
    "serverRecvMs": 1711709400010,
    "serverSendMs": 1711709400050,
    "clientSendMsEcho": 1711709400000,
    "requestId": "sync_1711709400000_42_300_abc"
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `currentLevel` | int | Текущий уровень |
| `currentExperience` | int | Кумулятивный XP |
| `expForCurrentLevel` | int | XP на начало текущего уровня |
| `expForNextLevel` | int | XP для следующего уровня |
| `expInCurrentLevel` | int | XP набрано на текущем уровне |
| `expNeededForNextLevel` | int | XP осталось до следующего уровня |
| `progressToNextLevel` | float | Прогресс 0.0 - 1.0 |

---

### experience_update — Получение опыта

**Сервер → Unicast** (при каждом получении XP)

```json
{
  "header": {
    "eventType": "experience_update",
    "status": "success",
    "requestId": "exp_update_7",
    "timestamp": 1711709400100,
    "version": "1.0"
  },
  "body": {
    "characterId": 7,
    "experienceChange": 350,
    "oldExperience": 45000,
    "newExperience": 45350,
    "oldLevel": 15,
    "newLevel": 15,
    "expForCurrentLevel": 40000,
    "expForNextLevel": 52000,
    "reason": "mob_kill",
    "sourceId": 1234,
    "levelUp": false
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `experienceChange` | int | Количество полученного XP |
| `oldExperience` / `newExperience` | int | XP до и после |
| `oldLevel` / `newLevel` | int | Уровень до и после |
| `reason` | string | Источник XP (`"mob_kill"`, `"quest_reward"`, `"dialogue_action"`) |
| `sourceId` | int | ID источника (mobUID, questId) |
| `levelUp` | bool | Произошёл ли level up |

---

### Сигнал повышения уровня

**Отдельного пакета `levelUp` нет.** Level-up сигнализируется через поле `levelUp: true` в `experience_update` + немедленный `stats_update` с новыми значениями HP/MP и атрибутов.

Клиент должен:
1. При получении `experience_update` с `levelUp == true` — показать level-up анимацию/UI
2. Дождаться следующего `stats_update` — в нём будут обновлённые HP/MP/атрибуты

> Обработчик `eventType: "levelUp"` существует в коде сервера, но событие `LEVEL_UP`
> никогда не попадает в EventQueue — это зарезервированный механизм. Клиент **не должен**
> зависеть от пакета `levelUp`. Используйте `experience_update.levelUp` как единственный сигнал.

### Серверные действия при level up

1. **+10 HP** и **+5 MP** к максимуму за каждый уровень
2. Сохранение: `saveCharacterProgress` → game-server
3. Отправка: `experience_update` (unicast + broadcast) с `levelUp: true`
4. Отправка: `stats_update` (unicast, только повысившему уровень) с новыми статами

---

### Формула XP за моба

```
levelDiff = mobLevel - charLevel

if (levelDiff < -5):   multiplier = 0.1   (10%)
if (levelDiff < -2):   multiplier = 0.5   (50%)
if (levelDiff <= 2):   multiplier = 1.0   (100%)
if (levelDiff <= 5):   multiplier = 1.5   (150%)
if (levelDiff > 5):    multiplier = 2.0   (200%)

grantedXP = baseExp × multiplier
```

### Формула таблицы опыта

```
expForLevel(n) = BASE_EXP_PER_LEVEL × (EXP_MULTIPLIER ^ (n - 2))

// Default: BASE=100, MULTIPLIER=1.15
// Level 2: 100 XP
// Level 10: 100 × 1.15^8 ≈ 305 XP
// Level 50: 100 × 1.15^48 ≈ 86,860 XP
```

Максимальный уровень: **70** (`MAX_LEVEL`)

---

### Система XP-долга

При получении XP:
1. Если `experienceDebt > 0`:
   - `debtPayment = min(xpGained, experienceDebt)`
   - `experienceDebt -= debtPayment`
   - `remainingXP = xpGained - debtPayment`
2. Оставшийся XP идёт на прогресс уровней

> Долг не снижает уровень. Персонаж не может потерять уровень через долг.

---

## 8.2. stats_update — Полное обновление статов

**Сервер → Unicast** (при изменении статов)

| Триггер | Источник |
|---------|----------|
| Вход в зону — загрузка инвентаря | `handleSetPlayerInventoryEvent` |
| Вход в зону — загрузка активных эффектов | `handleSetPlayerActiveEffectsEvent` |
| Использование скилла (потрачена мана, получен урон) | `CombatSystem::executeCombatAction` |
| Тик DoT / HoT (изменение HP) | `CombatSystem::tickEffects` |
| Получение XP / level-up | `ExperienceManager::grantExperience` |
| Надевание / снятие предмета | `EquipmentEventHandler` |
| Перезагрузка атрибутов с game-server | `CharacterEventHandler::handleSetCharacterAttributesEvent` |
| Использование предмета (зелье, свиток) | `ItemEventHandler::handleUseItemEvent` |
| OOC-регенерация HP/MP (каждые ~4 с) | `RegenManager::tickRegen` |

```json
{
  "header": {
    "eventType": "stats_update",
    "status": "success",
    "requestId": "stats_update_7",
    "timestamp": 1711709400300,
    "version": "1.0"
  },
  "body": {
    "characterId": 7,
    "level": 16,
    "experience": {
      "current": 52500,
      "levelStart": 52000,
      "nextLevel": 65000,
      "debt": 0
    },
    "health": {
      "current": 310,
      "max": 310
    },
    "mana": {
      "current": 155,
      "max": 155
    },
    "weight": {
      "current": 18.5,
      "max": 74.0
    },
    "attributes": [
      { "slug": "strength",         "name": "Strength",         "base": 20, "effective": 25 },
      { "slug": "constitution",      "name": "Constitution",     "base": 10, "effective": 12 },
      { "slug": "wisdom",            "name": "Wisdom",           "base": 8,  "effective": 8  },
      { "slug": "agility",           "name": "Agility",          "base": 15, "effective": 17 },
      { "slug": "intelligence",      "name": "Intelligence",     "base": 12, "effective": 12 },
      { "slug": "physical_attack",   "name": "Physical Attack",  "base": 18, "effective": 33 },
      { "slug": "physical_defense",  "name": "Physical Defense", "base": 10, "effective": 22 },
      { "slug": "crit_chance",       "name": "Critical Chance",  "base": 5,  "effective": 8  },
      { "slug": "move_speed",        "name": "Move Speed",       "base": 5,  "effective": 5  }
    ],
    "activeEffects": [
      {
        "slug": "strength_buff_potion",
        "effectTypeSlug": "buff",
        "attributeSlug": "strength",
        "value": 5.0,
        "expiresAt": 1711709700000
      }
    ]
  },
  "timestamps": {
    "serverRecvMs": 1711709400290,
    "serverSendMs": 1711709400300
  }
}
```

### Расчёт effective атрибута

```
effective = base
          + Σ(equipment bonuses)           // из attributes предметов с apply_on="equip"
          + Σ(active effect bonuses)       // из activeEffects
          + item_soul_bonus                // бонус за kill count оружия
          + mastery_tier_bonus             // бонус за тиры мастерства
```

### Item Soul бонус

| Kill Count оружием | Бонус к primary атрибуту |
|--------------------:|:-------------------------|
| ≥ 50 | +1 |
| ≥ 200 | +2 |
| ≥ 500 | +3 |

---

## 8.3. Система мастерства (Mastery)

### Диапазон

`0.0` - `100.0` (float, процент)

### Прогресс при атаке

```
base_delta = config.mastery.base_delta   // default: 0.5

levelDiff = targetLevel - charLevel
if (levelDiff >= 3):  levelFactor = 2.0
if (levelDiff >= 1):  levelFactor = 1.5
if (levelDiff == 0):  levelFactor = 1.0
if (levelDiff >= -5): levelFactor = 0.5
if (levelDiff < -5):  levelFactor = 0.1

// Soft-cap: > 80 points = 0.3x замедление
if (currentValue > 80.0):
    levelFactor *= 0.3

delta = base_delta × levelFactor
newValue = min(currentValue + delta, 100.0)
```

### Тиры мастерства (автоматические бонусы)

| Тир | Порог | Бонус | Атрибут |
|-----|------:|-------|---------|
| T1 | 20.0 | +1% | `physical_attack` |
| T2 | 50.0 | +4% (кумулятивно +5%) | `physical_attack` |
| T3 | 80.0 | +3% | `crit_chance` |
| T4 | 100.0 | +2% | `parry_chance` |

При пересечении тира → `world_notification` типа `mastery_tier_up`:

```json
{
  "header": { "eventType": "world_notification", "status": "success" },
  "body": {
    "characterId": 7,
    "notificationId": "mastery_7_sword_t2",
    "notificationType": "mastery_tier_up",
    "priority": "medium",
    "channel": "toast",
    "text": "",
    "data": {
      "masterySlug": "sword_mastery",
      "tier": "sword_mastery_t2_damage"
    }
  }
}
```

### Персистентность мастерства

- Сохранение каждые **10 ударов** или при пересечении тира
- Пакет: `saveMastery` → game-server

---

## 8.4. Система репутации

### Тиры

| Тир | Диапазон значений | Ранг |
|-----|-------------------:|------|
| `enemy` | < -500 | 0 |
| `stranger` | -500 ... -1 | 1 |
| `neutral` | 0 ... 199 | 2 |
| `friendly` | 200 ... 499 | 3 |
| `ally` | ≥ 500 | 4 |

### Изменение репутации

Происходит через:
- Dialogue action `change_reputation`
- Mob kill (если настроено)
- Quest rewards

При изменении тира → callback, возможно `world_notification`.

### Нотификация при изменении (через диалог)

```json
{
  "type": "reputationChanged",
  "faction": "bandits",
  "delta": 50
}
```

### Влияние репутации

- `rep < -500` → блокировка диалогов с NPC фракции
- `rep >= 200` → скидка 5% у вендоров фракции
- Условия в диалогах: `{ "type": "reputation", "faction": "...", "tier": "friendly" }`

---

## 8.5. OOC-регенерация HP/MP

**Out-of-Combat (OOC) регенерация** — пассивное восстановление HP и MP, которое срабатывает каждые ~4 секунды, пока персонаж не в бою. Каждый тик, изменивший HP или MP, немедленно отправляет клиенту `stats_update`.

### Условия тика

| Условие | Описание |
|---------|----------|
| Персонаж жив | `currentHealth > 0` |
| Вне боя | С момента последнего боевого события прошло `> regen.disableInCombatMs` (дефолт **8000 мс**) |
| Не полные HP/MP | Хотя бы одно из значений ниже максимума |

> «Боевое событие» — это момент нанесения или получения урона (обновление `lastInCombatAt`). Вход в бой сбрасывает обратный отсчёт регенерации.

### Формулы

```
hpGain = regen.baseHpRegen + max(0, constitution × regen.hpRegenConCoeff)
mpGain = regen.baseMpRegen + max(0, wisdom      × regen.mpRegenWisCoeff)

newHp   = min(currentHp + hpGain, maxHp)
newMp   = min(currentMp + mpGain, maxMp)
```

`constitution` и `wisdom` — `effective` значения из последнего снапшота атрибутов персонажа (включают бонусы от экипировки и эффектов).

### Конфигурация регенерации

| Config-ключ | Дефолт | Описание |
|-------------|--------|----------|
| `regen.tickIntervalMs` | `4000` | Интервал тика в мс |
| `regen.baseHpRegen` | `2` | Базовое HP за тик |
| `regen.baseMpRegen` | `1` | Базовое MP за тик |
| `regen.hpRegenConCoeff` | `0.3` | Коэффициент Constitution → HP per tick |
| `regen.mpRegenWisCoeff` | `0.5` | Коэффициент Wisdom → MP per tick |
| `regen.disableInCombatMs` | `8000` | Задержка после боя (мс) |

### Примеры расчёта

| Constitution | Wisdom | hpGain/тик | mpGain/тик |
|:---:|:---:|:---:|:---:|
| 0 | 0 | 2 | 1 |
| 10 | 10 | 5 | 6 |
| 20 | 15 | 8 | 8 |
| 50 | 30 | 17 | 16 |

### Пакет на клиент

Регенерация не имеет отдельного пакета — клиент получает стандартный `stats_update` с обновлёнными `health.current` и/или `mana.current`. Никакой отдельной обработки не требуется.

```json
{
  "header": { "eventType": "stats_update", "status": "success", "requestId": "stats_update_7" },
  "body": {
    "characterId": 7,
    "level": 10,
    "health": { "current": 95, "max": 200 },
    "mana":   { "current": 42, "max": 80 }
  }
}
```

> Клиент должен обновлять UI (health bar, mana bar) при **любом** `stats_update`, независимо от причины.

---

## 8.6. Конфигурация

| Ключ | Default | Описание |
|------|---------|----------|
| `BASE_EXP_PER_LEVEL` | 100 | Базовый XP за уровень |
| `EXP_MULTIPLIER` | 1.15 | Множитель XP таблицы |
| `DEATH_PENALTY_PERCENT` | 0.1 | Штраф за смерть (10%) |
| `MAX_LEVEL` | 70 | Максимальный уровень |
| `regen.tickIntervalMs` | 4000 | Интервал тика регенерации (мс) |
| `regen.baseHpRegen` | 2 | Базовый HP за тик |
| `regen.baseMpRegen` | 1 | Базовый MP за тик |
| `regen.hpRegenConCoeff` | 0.3 | CON → HP per tick |
| `regen.mpRegenWisCoeff` | 0.5 | WIS → MP per tick |
| `regen.disableInCombatMs` | 8000 | Задержка после боя для начала регена |
| `mastery.base_delta` | 0.5 | Базовый прирост мастерства |
| `mastery.tier1_value` ... `tier4_value` | 20/50/80/100 | Пороги тиров |
| `mastery.db_flush_every_hits` | 10 | Интервал сохранения |
| `item_soul.tier1_kills` ... `tier3_kills` | 50/200/500 | Пороги Kill Count |
| `item_soul.tier1_bonus_flat` ... `tier3_bonus_flat` | 1/2/3 | Бонусы Item Soul |
