# Client ↔ Server Protocol: Боевая система

**Версия документа:** v2.0  
**Актуально для:** chunk-server v0.0.5+

---

## Содержание

1. [Обзор боевого цикла](#1-обзор-боевого-цикла)
2. [Типы целей и кастеров](#2-типы-целей-и-кастеров)
3. [Инициализация скиллов при входе](#3-инициализация-скиллов-при-входе)
4. [Применение скилла / атаки](#4-применение-скилла--атаки)
5. [Broadcast: начало действия — `*Initiation`](#5-broadcast-начало-действия--initiation)
6. [Broadcast: результат действия — `*Result`](#6-broadcast-результат-действия--result)
7. [Синхронизация таймингов на клиенте](#7-синхронизация-таймингов-на-клиенте)
8. [DoT и HoT тики — `effectTick`](#8-dot-и-hot-тики--effecttick)
9. [Прерывание каста — `interruptCombatAction`](#9-прерывание-каста--interruptcombataction)
10. [AI-атаки мобов](#10-ai-атаки-мобов)
11. [Состояние моба — combatState](#11-состояние-моба--combatstate)
12. [Rate limiting](#12-rate-limiting)
13. [Полная последовательность событий](#13-полная-последовательность-событий)
14. [Таблица ошибок и валидаций](#14-таблица-ошибок-и-валидаций)

---

## 1. Обзор боевого цикла

```
Клиент                            Сервер                        Все клиенты
  │── playerAttack / skillUsage ─►│
  │                               │ Валидация: cooldown, GCD,
  │                               │ мана, мёртв ли, цель, дистанция
  │◄── {effectType}Initiation ────┤──────────────────────────────►│
  │◄── {effectType}Result ────────┤──────────────────────────────►│
  │◄── stats_update ──────────────│  (только инициатору: трата маны)
  │◄── experienceUpdate ─────────│  (если моб умер)
  │◄── itemDrop ──────────────────┤──────────────────────────────►│  (если моб умер)
```

### Ключевые принципы

**1. Оба пакета приходят одновременно** — `*Initiation` и `*Result` отправляются в одном тике сервера, вне зависимости от `castTime`. Клиент не ждёт `*Result` для начала анимации.

**2. `*Initiation` с `success: false` означает полный отказ** — анимацию не воспроизводить, `*Result` в этом случае **не придёт**. Сервер проверяет все условия до инициации, поэтому `success: true` в initiation гарантирует приход result.

**3. Хит-эффект определяется анимацией** — момент попадания задаётся хит-триггером внутри анимации (`animationName`). `*Result` приходит одновременно с `*Initiation` и содержит данные (урон, HP), которые применяются в момент срабатывания триггера.

**4. Все проверки — серверные** — клиент может делать optimistic UI (показывать GCD-иконку сразу), но авторитетным источником истины является сервер.

---

## 2. Типы целей и кастеров

### CombatTargetType (поле `targetType`)

| Значение | Строка | Описание |
|---------|--------|----------|
| `1` | `SELF` | Цель — сам кастер (хилы, баффы на себя) |
| `2` | `PLAYER` | Цель — другой игрок |
| `3` | `MOB` | Цель — моб |
| `4` | `AREA` | AoE — область вокруг кастера, `targetId` игнорируется |
| `5` | `NONE` | Нет цели (не используется клиентом) |

### CasterType (поле `casterType` в broadcast-пакетах)

| Значение | Строка | Описание |
|---------|--------|----------|
| `0` | `UNKNOWN` | Неизвестный источник |
| `1` | `PLAYER` | Атака инициирована игроком |
| `2` | `MOB` | Атака инициирована мобом (AI) |

---

## 3. Инициализация скиллов при входе

Сразу после успешного `joinGameCharacter` сервер присылает список скиллов персонажа.

**Направление:** Сервер → Клиент (только инициатору)  
**eventType:** `initializePlayerSkills`

```json
{
  "header": {
    "eventType": "initializePlayerSkills",
    "message": "Player skills initialized successfully",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "skills": [
      {
        "skillSlug": "basic_attack",
        "skillName": "Basic Attack",
        "skillLevel": 1,
        "skillEffectType": "damage",
        "coeff": 1.0,
        "flatAdd": 0,
        "cooldownMs": 0,
        "gcdMs": 1000,
        "castMs": 0,
        "costMp": 0,
        "maxRange": 200,
        "areaRadius": 0,
        "isPassive": false
      },
      {
        "skillSlug": "fireball",
        "skillName": "Fireball",
        "skillLevel": 2,
        "skillEffectType": "damage",
        "coeff": 2.5,
        "flatAdd": 10,
        "cooldownMs": 6000,
        "gcdMs": 1500,
        "castMs": 1500,
        "costMp": 30,
        "maxRange": 800,
        "areaRadius": 0,
        "isPassive": false
      },
      {
        "skillSlug": "iron_skin",
        "skillName": "Iron Skin",
        "skillLevel": 1,
        "skillEffectType": "buff",
        "coeff": 0.0,
        "flatAdd": 0,
        "cooldownMs": 0,
        "gcdMs": 0,
        "castMs": 0,
        "costMp": 0,
        "maxRange": 0,
        "areaRadius": 0,
        "isPassive": true
      }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `skillSlug` | string | Уникальный идентификатор скилла |
| `skillName` | string | Отображаемое имя |
| `skillLevel` | int | Текущий уровень скилла у персонажа |
| `skillEffectType` | string | `damage` / `heal` / `buff` / `debuff` |
| `coeff` | float | Множитель урона/хила от базовой атаки |
| `flatAdd` | float | Фиксированная добавка к урону/хилу |
| `cooldownMs` | int | Кулдаун скилла в мс (0 = нет кулдауна) |
| `gcdMs` | int | Глобальный кулдаун (GCD) в мс (0 = не использует GCD) |
| `castMs` | int | Время каста в мс (0 = мгновенный) |
| `costMp` | int | Стоимость в мане |
| `maxRange` | float | Максимальная дальность в единицах мира |
| `areaRadius` | float | Радиус AoE (0 = не AoE) |
| `isPassive` | bool | Пассивный скилл — не отображать на панели, эффект уже в `stats_update.attributes` |

---

## 4. Применение скилла / атаки

### 4.1 Базовая атака — `playerAttack`

**Направление:** Клиент → Сервер

```json
{
  "header": {
    "eventType": "playerAttack",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "characterId": 7,
    "skillSlug": "basic_attack",
    "targetId": 1001,
    "targetType": 3
  }
}
```

`skillSlug` необязателен — если отсутствует, сервер использует `"basic_attack"`.

### 4.2 Использование скилла — `skillUsage`

**Направление:** Клиент → Сервер

```json
{
  "header": {
    "eventType": "skillUsage",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "characterId": 7,
    "skillSlug": "fireball",
    "targetId": 1001,
    "targetType": 3
  }
}
```

| Поле | Тип | Обязательно | Описание |
|------|-----|-------------|----------|
| `characterId` | int | ✓ | ID персонажа |
| `skillSlug` | string | ✓ (для `skillUsage`) | Slug скилла |
| `targetId` | int | ✓ | UID моба / characterId игрока |
| `targetType` | int | ✓ | Тип цели (см. раздел 2) |

**Для AoE (`targetType: 4`):** `targetId` игнорируется, сервер бьёт всех в `skill.areaRadius`.  
**Для скилла на себя (`targetType: 1`):** `targetId` = `characterId` кастера.

### Что проверяет сервер перед initiation

Перед отправкой `*Initiation` сервер валидирует (в этом порядке):

| Проверка | Ошибка при провале |
|----------|--------------------|
| Скилл существует у персонажа | `Skill not found: {slug}` |
| Персонаж зарегистрирован в зоне | `Caster not found` |
| Скилл не на кулдауне | `Skill is on cooldown` |
| GCD не активен (если `gcdMs > 0`) | `Global cooldown active` |
| Достаточно маны | `Not enough mana` |
| Персонаж не в процессе каста | `Already casting` |

При любой ошибке приходит `*Initiation` с `success: false`, `*Result` **не отправляется**.

---

## 5. Broadcast: начало действия — `*Initiation`

Отправляется **всем** клиентам в зоне одновременно с `*Result`.

### Маппинг eventType по skillEffectType

| `skillEffectType` | `eventType` |
|-------------------|-------------|
| `damage` | `combatInitiation` |
| `heal` | `healingInitiation` |
| `buff` | `buffInitiation` |
| `debuff` | `debuffInitiation` |
| (иное) | `skillInitiation` |

> Клиент должен подписаться на **все** типы initiation, не только `combatInitiation`.

### Пример: мгновенная атака, success

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Basic Attack initiated",
    "status": "success",
    "timestamp": "2026-03-17 16:27:07.131",
    "version": "1.0"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 7,
      "targetId": 1001,
      "targetType": 3,
      "targetTypeString": "MOB",
      "skillName": "Basic Attack",
      "skillSlug": "basic_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "castTime": 0.0,
      "animationName": "skill_basic_attack",
      "animationDuration": 0.85,
      "serverTimestamp": 1773764827131,
      "castStartedAt": 1773764827131,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

### Пример: скилл с кастом (fireball, 1.5s), success

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Fireball initiated",
    "status": "success"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 7,
      "targetId": 1001,
      "targetType": 3,
      "targetTypeString": "MOB",
      "skillName": "Fireball",
      "skillSlug": "fireball",
      "skillEffectType": "damage",
      "skillSchool": "fire",
      "castTime": 1.5,
      "animationName": "cast_fireball",
      "animationDuration": 2.15,
      "serverTimestamp": 1773764827131,
      "castStartedAt": 1773764827131,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

### Пример: отказ (GCD активен)

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Basic Attack initiation failed",
    "status": "error"
  },
  "body": {
    "skillInitiation": {
      "success": false,
      "casterId": 7,
      "targetId": 1001,
      "targetType": 3,
      "skillSlug": "basic_attack",
      "errorReason": "Global cooldown active"
    }
  }
}
```

> При `success: false` — `*Result` **не придёт**. Анимацию не воспроизводить.

### Пример: heal initiation

```json
{
  "header": {
    "eventType": "healingInitiation",
    "message": "Skill Heal Light initiated",
    "status": "success"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetType": 1,
      "targetTypeString": "SELF",
      "skillName": "Heal Light",
      "skillSlug": "heal_light",
      "skillEffectType": "heal",
      "skillSchool": "holy",
      "castTime": 0.0,
      "animationName": "skill_heal_light",
      "animationDuration": 0.6,
      "serverTimestamp": 1773764827131,
      "castStartedAt": 1773764827131,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

### Поля объекта `skillInitiation`

| Поле | Тип | Присутствует | Описание |
|------|-----|-------------|----------|
| `success` | bool | всегда | Успешно ли инициировано |
| `casterId` | int | всегда | ID кастера |
| `targetId` | int | всегда | ID цели |
| `targetType` | int | всегда | Числовой тип цели |
| `targetTypeString` | string | всегда | Строковый тип цели |
| `skillSlug` | string | всегда | Slug скилла |
| `skillName` | string | при `success: true` | Имя скилла |
| `skillEffectType` | string | при `success: true` | `damage` / `heal` / `buff` / `debuff` |
| `skillSchool` | string | при `success: true` | `physical` / `fire` / `ice` и т.д. |
| `castTime` | float | при `success: true` | Время каста в сек, 0 = instant |
| `animationName` | string | при `success: true` | Ключ анимации |
| `animationDuration` | float | при `success: true` | Длительность анимации в сек |
| `serverTimestamp` | int64 | при `success: true` | Unix ms отправки пакета |
| `castStartedAt` | int64 | при `success: true` | Unix ms начала каста (= `serverTimestamp`) |
| `casterType` | int | при `success: true` | `1` = PLAYER, `2` = MOB |
| `casterTypeString` | string | при `success: true` | `"PLAYER"` / `"MOB"` |
| `errorReason` | string | при `success: false` | Причина отказа |

---

## 6. Broadcast: результат действия — `*Result`

Отправляется **всем** клиентам одновременно с `*Initiation`. Приходит **только** при `*Initiation.success: true`.

### Маппинг eventType по skillEffectType

| `skillEffectType` | `eventType` |
|-------------------|-------------|
| `damage` | `combatResult` |
| `heal` | `healingResult` |
| `buff` | `buffResult` |
| `debuff` | `debuffResult` |
| (иное) | `skillResult` |

### Пример: урон, успех

```json
{
  "header": {
    "eventType": "combatResult",
    "message": "Skill Basic Attack executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 1001,
      "targetType": 3,
      "targetTypeString": "MOB",
      "skillName": "Basic Attack",
      "skillSlug": "basic_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "serverTimestamp": 1773764827131,
      "damage": 45,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 155,
      "finalTargetMana": 100,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

### Пример: критический удар

```json
{
  "body": {
    "skillResult": {
      "success": true,
      "damage": 90,
      "isCritical": true,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 110
    }
  }
}
```

### Пример: промах

```json
{
  "body": {
    "skillResult": {
      "success": true,
      "damage": 0,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": true,
      "targetDied": false,
      "finalTargetHealth": 200
    }
  }
}
```

### Пример: блок

```json
{
  "body": {
    "skillResult": {
      "success": true,
      "damage": 10,
      "isCritical": false,
      "isBlocked": true,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 190
    }
  }
}
```

### Пример: убийство цели

```json
{
  "body": {
    "skillResult": {
      "success": true,
      "damage": 45,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": true,
      "finalTargetHealth": 0,
      "finalTargetMana": 0
    }
  }
}
```

После `targetDied: true`:
- Цель — моб: придут `mobDeath` (broadcast) и `experienceUpdate` (только атакующему)
- Цель — игрок: придёт `stats_update` с `health.current = 0`

### Пример: лечение — `healingResult`

```json
{
  "header": {
    "eventType": "healingResult",
    "message": "Skill Heal Light executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetType": 1,
      "targetTypeString": "SELF",
      "skillName": "Heal Light",
      "skillSlug": "heal_light",
      "skillEffectType": "heal",
      "skillSchool": "holy",
      "serverTimestamp": 1773764827131,
      "healing": 120,
      "finalTargetHealth": 350,
      "finalTargetMana": 45,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

### Пример: бафф — `buffResult`

```json
{
  "header": {
    "eventType": "buffResult",
    "message": "Skill Battle Fury executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetType": 1,
      "skillName": "Battle Fury",
      "skillSlug": "battle_fury",
      "skillEffectType": "buff",
      "skillSchool": "physical",
      "serverTimestamp": 1773764827131,
      "appliedEffects": ["battle_fury_phys_attack"],
      "finalTargetHealth": 350,
      "finalTargetMana": 30,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

После бафф/дебафф result приходит `stats_update` с обновлёнными `effective`-атрибутами и `activeEffects`.

### Пример: провал выполнения (редкий edge case, мана закончилась между initiation и execute)

```json
{
  "header": {
    "eventType": "combatResult",
    "message": "Skill Basic Attack execution failed",
    "status": "error"
  },
  "body": {
    "skillResult": {
      "success": false,
      "casterId": 7,
      "targetId": 1001,
      "targetType": 3,
      "skillSlug": "basic_attack",
      "skillEffectType": "damage",
      "serverTimestamp": 1773764827131,
      "errorReason": "Insufficient resources"
    }
  }
}
```

### Полные поля объекта `skillResult`

| Поле | Тип | Присутствует | Описание |
|------|-----|-------------|----------|
| `success` | bool | всегда | `true` — выполнено успешно |
| `casterId` | int | всегда | ID кастера |
| `targetId` | int | всегда | ID цели |
| `targetType` / `targetTypeString` | int/string | всегда | Тип цели |
| `skillSlug` | string | всегда | Slug скилла |
| `skillName` | string | всегда | Имя скилла |
| `skillEffectType` | string | всегда | `damage` / `heal` / `buff` / `debuff` |
| `skillSchool` | string | всегда | Школа: `physical` / `fire` / `ice` и т.д. |
| `serverTimestamp` | int64 | всегда | Unix ms генерации пакета |
| `casterType` / `casterTypeString` | int/string | всегда | `1`/`"PLAYER"` или `2`/`"MOB"` |
| `damage` | int | только `damage` + `success:true` | Нанесённый урон |
| `isCritical` | bool | только `damage` + `success:true` | Критический удар |
| `isBlocked` | bool | только `damage` + `success:true` | Заблокировано щитом |
| `isMissed` | bool | только `damage` + `success:true` | Промах |
| `targetDied` | bool | только `success:true` | Цель погибла |
| `finalTargetHealth` | int | только `success:true` | HP цели после применения |
| `finalTargetMana` | int | только `success:true` | Мана цели после применения |
| `healing` | int | только `heal` + `success:true` | Восстановленное HP |
| `appliedEffects` | string[] | только `buff`/`debuff` + `success:true` | Slugи наложенных эффектов |
| `errorReason` | string | только `success:false` | Причина провала |

---

## 7. Синхронизация таймингов на клиенте

### Поля времени

| Поле | Где | Описание |
|------|-----|----------|
| `serverTimestamp` | `*Initiation`, `*Result` | Unix ms момента формирования пакета на сервере |
| `castStartedAt` | `*Initiation` | Unix ms начала каста (= `serverTimestamp`) |
| `castTime` | `*Initiation` | Длительность каста в сек (0 = instant) |
| `animationDuration` | `*Initiation` | Полная длительность анимации в сек |

### Алгоритм на клиенте при получении `*Initiation` (success: true)

```
// 1. Запустить анимацию — хит-триггер внутри анимации определяет момент попадания
PlayAnimation(animationName, animationDuration)

// 2. Кастбар (если castTime > 0)
if (castTime > 0):
    T_server = skillInitiation.castStartedAt
    halfRtt = (GetCurrentUnixMs() - clientSendMs) / 2   // если clientSendMs доступен
    castbar_start = T_server - halfRtt
    castbar_end   = castbar_start + castTime * 1000
    ShowCastbar(castbar_start, castbar_end)

// 3. Сохранить данные результата — применяются в момент хит-триггера анимации
pending_result[casterId] = null  // заполним когда придёт *Result
```

### Алгоритм при получении `*Result`

```
// Сохраняем данные — анимационный хит-триггер применит их в нужный момент
pending_result[casterId] = skillResult
```

### Хит-триггер анимации (вызывается движком)

```
function OnAnimationHitTrigger(casterId):
    result = pending_result[casterId]
    if result != null:
        ApplyHitVisuals(result)
        pending_result.remove(casterId)
    // если result ещё null — пакет ещё не пришёл, повторить проверку через кадр
```

### ApplyHitVisuals

```
function ApplyHitVisuals(result):
    if result.isMissed:
        ShowFloatingText(target, "MISS")
    else if result.isBlocked:
        ShowFloatingText(target, "BLOCK " + result.damage)
        UpdateHealthBar(target, result.finalTargetHealth)
    else:
        if result.isCritical:
            ShowFloatingText(target, "CRIT! " + result.damage, big=true)
        else:
            ShowFloatingText(target, result.damage)
        UpdateHealthBar(target, result.finalTargetHealth)

    if result.targetDied:
        HandleDeath(target)
```

### Управление GCD и кулдаунами на клиенте

Клиент должен управлять кулдаунами **локально** для responsive UI, используя серверные данные как авторитет:

```
// При отправке playerAttack/skillUsage:
StartLocalGCD(skill.gcdMs)         // optimistic, мгновенно
StartLocalCooldown(skill.cooldownMs)

// При получении *Initiation с success: false:
// errorReason == "Global cooldown active" → GCD ещё активен, ресинхронизировать
// errorReason == "Skill is on cooldown"   → кулдаун ещё активен
// Сбросить локальный кулдаун обратно (он был проставлен оптимистично)
RevertLocalCooldown()

// При получении *Initiation с success: true:
// Подтвердить локальный кулдаун
// Пересчитать GCD от serverTimestamp по gcdMs из initializePlayerSkills
SyncCooldownFromServer(serverTimestamp, skill.gcdMs, skill.cooldownMs)
```

---

## 8. DoT и HoT тики — `effectTick`

**Направление:** Сервер → Broadcast (все клиенты)  
**eventType:** `effectTick`

### DoT (урон со временем)

```json
{
  "header": {
    "eventType": "effectTick",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "effectSlug": "poison",
    "effectTypeSlug": "dot",
    "value": 15,
    "newHealth": 270,
    "newMana": 80,
    "targetDied": false
  }
}
```

### HoT (лечение со временем)

```json
{
  "header": {
    "eventType": "effectTick",
    "status": "success"
  },
  "body": {
    "characterId": 7,
    "effectSlug": "regeneration",
    "effectTypeSlug": "hot",
    "value": 20,
    "newHealth": 310,
    "newMana": 80,
    "targetDied": false
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID персонажа-цели тика |
| `effectSlug` | string | Slug эффекта |
| `effectTypeSlug` | string | `dot` — урон, `hot` — лечение |
| `value` | float | Урон или хил за этот тик |
| `newHealth` | int | HP после тика |
| `newMana` | int | Мана после тика |
| `targetDied` | bool | Цель умерла от этого тика |

**Логика клиента:**
- Обновить HP-бар `characterId` до `newHealth`
- Показать floating combat text
- Если `targetDied: true` — обработать смерть

---

## 9. Прерывание каста — `interruptCombatAction`

> **⚠️ НЕ РЕАЛИЗОВАНО.** Пакет `interruptCombatAction` не зарегистрирован в `EventDispatcher` и хендлер отсутствует. Если по дизайну движение должно прерывать каст — клиент должен самостоятельно остановить анимацию и убрать кастбар, не отправляя пакет на сервер.

**Планируемый пакет (eщё не введён):**

**Направление:** Клиент → Сервер

```json
{
  "header": {
    "eventType": "interruptCombatAction",
    "clientId": 42,
    "hash": "abc123"
  },
  "body": {
    "characterId": 7
  }
}
```

Сервер прерывает каст, broadcast не отправляется. Клиент должен самостоятельно остановить анимацию и убрать кастбар.

> Движение (`moveCharacter`) **не** прерывает каст автоматически на сервере. Если по дизайну скилла движение должно прерывать каст — клиент посылает `interruptCombatAction`.

---

## 10. AI-атаки мобов

Мобы используют те же broadcast-пакеты (`casterType: 2`). Клиент обрабатывает их **идентично атакам игрока**.

> `combatInitiation` приходит в момент перехода CHASING → PREPARING_ATTACK (начало каста). `combatResult` — в момент перехода PREPARING_ATTACK → ATTACKING (конец каста, `castMs` позже). Клиент применяет данные из `combatResult` в момент срабатывания хит-триггера анимации ATTACKING.

### combatInitiation (моб атакует игрока)

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Claw Attack initiated",
    "status": "success"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 1000002,
      "targetId": 7,
      "targetType": 2,
      "targetTypeString": "PLAYER",
      "skillName": "Claw Attack",
      "skillSlug": "mob_claw_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "castTime": 0.5,
      "animationName": "skill_mob_claw_attack",
      "animationDuration": 1.1,
      "serverTimestamp": 1773764827131,
      "castStartedAt": 1773764827131,
      "casterType": 2,
      "casterTypeString": "MOB"
    }
  }
}
```

### combatResult (моб атакует игрока)

```json
{
  "header": {
    "eventType": "combatResult",
    "message": "Skill Claw Attack executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 1000002,
      "targetId": 7,
      "targetType": 2,
      "targetTypeString": "PLAYER",
      "skillName": "Claw Attack",
      "skillSlug": "mob_claw_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "serverTimestamp": 1773764827131,
      "damage": 22,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 228,
      "finalTargetMana": 80,
      "casterType": 2,
      "casterTypeString": "MOB"
    }
  }
}
```

После AI-атаки по игроку сервер отправляет `stats_update` игроку с обновлённым HP.

### combatAoeResult — результат AoE-атаки

**Направление:** Chunk Server → Broadcast (все клиенты)  
Отправляется вместо `combatResult` при `targetType: 4` (AREA). Содержит массив всех целей, попавших в AoE.

```json
{
  "header": {
    "eventType": "combatAoeResult",
    "message": "AoE skill executed"
  },
  "body": {
    "aoeResult": {
      "casterId": 7,
      "skillSlug": "thunder_clap",
      "skillName": "Thunder Clap",
      "skillEffectType": "damage",
      "skillSchool": "lightning",
      "serverTimestamp": 1773764827131,
      "finalCasterMana": 60,
      "casterType": 1,
      "casterTypeString": "PLAYER",
      "targets": [
        {
          "targetId": 1001,
          "targetType": 3,
          "targetTypeString": "MOB",
          "damage": 38,
          "isCritical": false,
          "isBlocked": false,
          "isMissed": false,
          "targetDied": false,
          "finalTargetHealth": 112
        },
        {
          "targetId": 1002,
          "targetType": 3,
          "targetTypeString": "MOB",
          "damage": 38,
          "isCritical": true,
          "isBlocked": false,
          "isMissed": false,
          "targetDied": true,
          "finalTargetHealth": 0
        }
      ]
    }
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `casterId` | int | ID кастера |
| `skillSlug` | string | Slug скилла |
| `finalCasterMana` | int | Мана кастера после использования |
| `casterType` / `casterTypeString` | int/string | 1/"PLAYER" или 2/"MOB" |
| `targets[]` | array | Список целей с результатами |
| `targets[].targetId` | int | ID цели |
| `targets[].damage` | int | Урон цели |
| `targets[].isCritical` | bool | Критический удар |
| `targets[].targetDied` | bool | Цель погибла |
| `targets[].finalTargetHealth` | int | HP цели после |

При `targetDied: true` в одном из элементов `targets[]` последуют `mobDeath` вroadcast и `experienceUpdate` кастеру.

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Claw Attack initiated",
    "status": "success"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 1000002,
      "targetId": 7,
      "targetType": 2,
      "targetTypeString": "PLAYER",
      "skillName": "Claw Attack",
      "skillSlug": "mob_claw_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "castTime": 0.5,
      "animationName": "skill_mob_claw_attack",
      "animationDuration": 1.1,
      "serverTimestamp": 1773764827131,
      "castStartedAt": 1773764827131,
      "casterType": 2,
      "casterTypeString": "MOB"
    }
  }
}
```

### combatResult (моб атакует игрока)

```json
{
  "header": {
    "eventType": "combatResult",
    "message": "Skill Claw Attack executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 1000002,
      "targetId": 7,
      "targetType": 2,
      "targetTypeString": "PLAYER",
      "skillName": "Claw Attack",
      "skillSlug": "mob_claw_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "serverTimestamp": 1773764827131,
      "damage": 22,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 228,
      "finalTargetMana": 80,
      "casterType": 2,
      "casterTypeString": "MOB"
    }
  }
}
```

После AI-атаки по игроку сервер отправляет `stats_update` игроку с обновлённым HP.

---

## 11. Состояние моба — combatState

Поле `combatState` присутствует в `spawnMobsInZone`, `zoneMoveMobs` и `mobMoveUpdate`.

| Значение | Имя | Описание |
|---------|-----|----------|
| `0` | `PATROLLING` | Патрулирует (случайные waypoint-ы) |
| `1` | `CHASING` | Преследует цель |
| `2` | `PREPARING_ATTACK` | Подготовка удара (castTime, моб заморожен) |
| `3` | `ATTACKING` | Анимация удара (swingMs, моб заморожен) |
| `4` | `ATTACK_COOLDOWN` | Ожидание кулдауна (моб заморожен) |
| `5` | `RETURNING` | Leash: возврат к спавну, **неуязвим**, рegen HP 10%/сек |
| `6` | `EVADING` | 2-секундное окно неуязвимости после возврата |
| `7` | `FLEEING` | Бежит от атакующего (порог HP настраивается per-mob) |

**Важно:**
- `RETURNING` и `EVADING` — моб неуязвим, урон по нему не проходит. Показывать HP-бар серым или скрывать кнопку атаки
- `PREPARING_ATTACK`, `ATTACKING`, `ATTACK_COOLDOWN` — моб неподвижен, позиция не меняется; не применять dead reckoning
- `FLEEING` — пороговое значение HP настраивается в шаблоне каждого моба (поле `fleeHpThreshold`); по умолчанию `0.0` (никогда не убегает)

> Полная документация по AI: переходы между состояниями, тайминги, threat table, social aggro, melee slots — в [mob-ai-movement-protocol.md](mob-ai-movement-protocol.md)

---

## 12. Rate limiting

Сервер принимает не более одного боевого запроса за `COMBAT_RATE_LIMIT_MS` (~100–200 мс) от одного клиента.

При превышении лимита возвращается **не `*Initiation`**, а отдельный error-пакет:

```json
{
  "header": {
    "eventType": "playerAttack",
    "message": "Error: Request too fast, slow down!",
    "status": "error"
  },
  "body": {
    "error": {
      "success": false,
      "errorMessage": "Request too fast, slow down!"
    }
  }
}
```

Клиент должен блокировать отправку новых запросов локально по GCD (`gcdMs`) и не полагаться на серверный ответ для разблокировки.

---

## 13. Полная последовательность событий

### Мгновенная атака игрока по мобу (моб умирает)

```
Клиент                               Сервер                     Все клиенты
  │── playerAttack ──────────────────►│
  │                                   │ ✓ cooldown, GCD, мана, жив ли
  │◄── combatInitiation (success:true)─┤─────────────────────────────►│
  │◄── combatResult (targetDied:true) ─┤─────────────────────────────►│
  │◄── stats_update (мана потрачена) ──│
  │                                    │ (async after result)
  │◄── mobDeath ───────────────────────┤─────────────────────────────►│
  │◄── experienceUpdate ──────────────│
  │◄── itemDrop ───────────────────────┤─────────────────────────────►│

  [клиент воспроизводит анимацию атаки; хит-триггер показывает урон и смерть моба]
```

### Скилл с кастом (fireball, castTime=1.5s)

```
Клиент                               Сервер                     Все клиенты
  │── skillUsage (fireball) ──────────►│
  │                                    │ ✓ все проверки
  │◄── combatInitiation ───────────────┤─────────────────────────────►│
  │◄── combatResult ───────────────────┤─────────────────────────────►│
  │                                    │  (оба пакета одновременно)
  │
  │  [кастбар на 1.5s с момента castStartedAt]
  │  [хит-триггер анимации → показать урон, обновить HP]
```

### Отказ из-за GCD

```
Клиент                               Сервер
  │── playerAttack ──────────────────►│
  │                                   │ ✗ GCD активен
  │◄── combatInitiation (success:false,
  │      errorReason:"Global cooldown active")
  │
  │  [не воспроизводить анимацию, показать иконку GCD]
  │  [*result не придёт]
```

### AI-атака моба по игроку

```
                                      Сервер                     Все клиенты
  │                                   │  (AI: подготовка атаки)
  │◄── combatInitiation (casterType=2)─┤─────────────────────────────►│
  │◄── combatResult ───────────────────┤─────────────────────────────►│
  │◄── stats_update ───────────────────│  (только атакованному игроку)
  │
  │  [запустить анимацию атаки моба]
  │  [хит-триггер анимации → показать урон на игроке, обновить HP-бар]
```

---

## 14. Таблица ошибок и валидаций

### В `*Initiation` (`skillInitiation.errorReason`)

| errorReason | Причина | Действие клиента |
|-------------|---------|-----------------|
| `Skill is on cooldown` | Скилл на кулдауне | Показать оставшееся время кулдауна |
| `Global cooldown active` | GCD активен | Ждать окончания GCD |
| `Not enough mana` | Не хватает маны | Показать иконку нехватки маны |
| `Already casting` | Уже идёт каст | Дождаться конца каста |
| `Skill not found: {slug}` | Скилл недоступен персонажу | Ошибка — обновить список скиллов |
| `Caster not found` | Кастер не в зоне | Ошибка соединения |

> При любой из этих ошибок `*Result` **не придёт**.

### В `*Result` (`skillResult.errorReason`) — редкие edge cases

| errorReason | Причина |
|-------------|---------|
| `Insufficient resources` | Мана иссякла между initiation и execute |
| `Invalid target` | Цель исчезла или не валидна |
| `Target is out of range` | Цель вышла из зоны досягаемости |
| `Caster not found` | Кастер был удалён |

> Эти ошибки возможны только при `success: true` в предшествующем `*Initiation`. Клиент должен отменить запланированный хит-эффект и, если нужно, откатить оптимистичные изменения UI.

### Rate limit (отдельный пакет, не `*Initiation`)

| errorMessage | Причина |
|--------------|---------|
| `Request too fast, slow down!` | Превышен rate limit (~100–200 мс между запросами) |
| `Cannot use skills while dead` | Персонаж мёртв |


---

