# Client ↔ Server Protocol: Боевая система

**Версия документа:** v1.0  
**Актуально для:** chunk-server v0.0.4+

---

## Содержание

1. [Обзор боевого цикла](#1-обзор-боевого-цикла)
2. [Типы целей и кастеров](#2-типы-целей-и-кастеров)
3. [Инициализация скиллов при входе](#3-инициализация-скиллов-при-входе)
4. [Применение скилла / атаки](#4-применение-скилла--атаки)
5. [Broadcast: начало действия — `*Initiation`](#5-broadcast-начало-действия--initiation)
6. [Broadcast: результат действия — `*Result`](#6-broadcast-результат-действия--result)
7. [DoT и HoT тики — `effectTick`](#7-dot-и-hot-тики--effecttick)
8. [Прерывание каста — `interruptCombatAction`](#8-прерывание-каста--interruptcombataction)
9. [AI-атаки мобов](#9-ai-атаки-мобов)
10. [Состояние моба — combatState](#10-состояние-моба--combatstate)
11. [Спринт-анти-спам: rate limiting](#11-спринт-анти-спам-rate-limiting)
12. [Полная последовательность событий](#12-полная-последовательность-событий)
13. [Таблица ошибок](#13-таблица-ошибок)

---

## 1. Обзор боевого цикла

```
Клиент                          Сервер
  │──── playerAttack / skillUsage ────►│
  │                                    │  1. Проверка: cooldown, мана, кулдаун каста
  │◄─── {effectType}Initiation ────────│  2. Broadcast: начало действия (castTime)
  │                                    │
  │  (если castTime > 0 — анимация каста)
  │                                    │  3. Выполнение: урон/хил/эффект
  │◄─── {effectType}Result ────────────│  4. Broadcast: результат (HP изменилось)
  │◄─── stats_update ──────────────────│  5. Только владельцу: обновление HP/Mana
  │◄─── experience_update ─────────────│  6. (если моб умер) XP-награда
  │◄─── itemDrop ───────────────────── │  7. (если моб умер) Дроп предметов
```

**Два пакета на каждое боевое действие:**
- `*Initiation` — момент запуска; содержит `castTime` и `animationName` для воспроизведения анимации
- `*Result` — момент попадания; содержит итоговый урон, HP цели, крит/промах и т.д.

Для мгновенных скиллов (`castTime == 0`) оба пакета приходят практически одновременно, без паузы.

---

## 2. Типы целей и кастеров

### CombatTargetType (поле `targetType` в пакетах)

| Значение | Строка | Описание |
|---------|--------|----------|
| `1` | `SELF` | Цель — сам кастер (хилы, баффы) |
| `2` | `PLAYER` | Цель — другой игрок |
| `3` | `MOB` | Цель — моб |
| `4` | `AREA` | AoE — область вокруг кастера |
| `5` | `NONE` | Нет цели (не используется клиентом) |

### CasterType (поле `casterType` в broadcast-пакетах)

| Значение | Строка | Описание |
|---------|--------|----------|
| `1` | `PLAYER` | Действие инициировано игроком |
| `2` | `MOB` | Действие инициировано мобом (AI-атака) |
| `0` | `UNKNOWN` | Неизвестный источник |

---

## 3. Инициализация скиллов при входе

Сразу после успешного `joinGameCharacter` сервер присылает список доступных скиллов персонажа.

**Направление:** Сервер → Клиент (только инициатору)  
**eventType:** `initializePlayerSkills`

```json
{
  "header": {
    "eventType": "initializePlayerSkills",
    "message": "Player skills initialized successfully"
  },
  "body": {
    "characterId": 7,
    "skills": [
      {
        "skillSlug": "basic_attack",
        "skillLevel": 1,
        "coeff": 1.0,
        "flatAdd": 0,
        "cooldownMs": 0,
        "gcdMs": 1000,
        "castMs": 0,
        "costMp": 0,
        "maxRange": 200,
        "isPassive": false
      },
      {
        "skillSlug": "fireball",
        "skillLevel": 2,
        "coeff": 2.5,
        "flatAdd": 10,
        "cooldownMs": 6000,
        "gcdMs": 1500,
        "castMs": 1500,
        "costMp": 30,
        "maxRange": 800,
        "isPassive": false
      },
      {
        "skillSlug": "iron_skin",
        "skillLevel": 1,
        "coeff": 0.0,
        "flatAdd": 0,
        "cooldownMs": 0,
        "gcdMs": 0,
        "castMs": 0,
        "costMp": 0,
        "maxRange": 0,
        "isPassive": true
      }
    ]
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `skillSlug` | string | Уникальный идентификатор скилла |
| `skillLevel` | int | Текущий уровень скилла у персонажа |
| `coeff` | float | Множитель урона/хила от базовой атаки |
| `flatAdd` | float | Фиксированная добавка к урону/хилу |
| `cooldownMs` | int | Кулдаун скилла в миллисекундах |
| `gcdMs` | int | Глобальный кулдаун (GCD) в миллисекундах |
| `castMs` | int | Время каста в мс (0 = мгновенный) |
| `costMp` | int | Стоимость в мане |
| `maxRange` | float | Максимальная дальность (в единицах мира) |
| `isPassive` | bool | Пассивный скилл (не имеет кнопки активации) |

**Пассивные скиллы** (`isPassive: true`) не отображаются на панели умений — их эффекты уже учтены в `stats_update.attributes`.

---

## 4. Применение скилла / атаки

Клиент использует один из двух `eventType`:
- `playerAttack` — базовая атака; `skillSlug` не обязателен (по умолчанию `"basic_attack"`)
- `skillUsage` — конкретный скилл; `skillSlug` **обязателен**

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

Поле `skillSlug` необязательно для `playerAttack` — если отсутствует, используется `"basic_attack"`.

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
| `characterId` | int | ✓ | ID персонажа (серверная проверка принадлежности) |
| `skillSlug` | string | ✓* | Slug скилла (*обязателен для `skillUsage`) |
| `targetId` | int | ✓ | ID цели: UID моба, characterId игрока, или ID области |
| `targetType` | int | ✓ | Тип цели (см. раздел 2) |

#### Для AoE-скиллов (`targetType: 4`)
`targetId` не используется — сервер применяет скилл по всем целям в радиусе `skill.areaRadius` от кастера.

#### Для скиллов на себя (`targetType: 1`)
`targetId` — это `characterId` самого кастера.

---

## 5. Broadcast: начало действия — `*Initiation`

Отправляется **всем** клиентам в зоне. `eventType` зависит от типа скилла:

| `skillEffectType` | `eventType` |
|-------------------|-------------|
| `damage` | `combatInitiation` |
| `heal` | `healingInitiation` |
| `buff` | `buffInitiation` |
| `debuff` | `debuffInitiation` |
| (иное) | `skillInitiation` |

```json
{
  "header": {
    "message": "Skill Basic Attack initiated",
    "eventType": "combatInitiation"
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
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

#### Пример для скилла с кастом — `fireball`

```json
{
  "header": {
    "message": "Skill Fireball initiated",
    "eventType": "combatInitiation"
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
      "animationDuration": 2.2,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

#### Ошибочная инициация (cooldown, нет маны и т.д.)

```json
{
  "header": {
    "message": "Skill Fireball initiation failed",
    "eventType": "combatInitiation"
  },
  "body": {
    "skillInitiation": {
      "success": false,
      "casterId": 7,
      "targetId": 1001,
      "targetType": 2,
      "skillSlug": "fireball",
      "errorReason": "Skill is on cooldown"
    }
  }
}
```

**Поля объекта `skillInitiation`:**

| Поле | Тип | Описание |
|------|-----|----------|
| `success` | bool | Успешно ли инициировано действие |
| `casterId` | int | ID кастера |
| `targetId` | int | ID цели |
| `targetType` / `targetTypeString` | int/string | Тип цели |
| `skillSlug` | string | Slug скилла |
| `skillName` | string | Отображаемое имя скилла |
| `skillEffectType` | string | Тип эффекта: `damage`, `heal`, `buff`, `debuff` |
| `skillSchool` | string | Школа скилла: `physical`, `fire`, `ice` и т.д. |
| `castTime` | float | Время каста в секундах (0 = мгновенный) |
| `animationName` | string | Ключ анимации для воспроизведения |
| `animationDuration` | float | Длительность анимации в секундах |
| `casterType` / `casterTypeString` | int/string | Тип кастера (PLAYER/MOB) |
| `errorReason` | string | Причина неудачи (только при `success: false`) |

**Что делает клиент при получении `*Initiation`:**
- Если `success: false` — не воспроизводить анимацию, показать иконку ошибки (кулдаун, нет маны и т.д.)
- Если `success: true` и `castTime > 0` — начать анимацию каста длиной `animationDuration` секунд
- Если `success: true` и `castTime == 0` — начать анимацию атаки, сразу ждать `*Result`

---

## 6. Broadcast: результат действия — `*Result`

Отправляется **всем** клиентам в зоне после выполнения скилла. `eventType`:

| `skillEffectType` | `eventType` |
|-------------------|-------------|
| `damage` | `combatResult` |
| `heal` | `healingResult` |
| `buff` | `buffResult` |
| `debuff` | `debuffResult` |
| (иное) | `skillResult` |

### 6.1 Урон — `combatResult`

```json
{
  "header": {
    "message": "Skill Basic Attack executed successfully",
    "eventType": "combatResult"
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
      "hitDelay": 0.0,
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

#### Критический удар

```json
{
  "body": {
    "skillResult": {
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

#### Промах (Miss)

```json
{
  "body": {
    "skillResult": {
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

#### Блок

```json
{
  "body": {
    "skillResult": {
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

### 6.2 Убийство цели — `targetDied: true`

Когда `targetDied: true`:
- Если цель — моб: после `combatResult` придёт `mobDeath` (broadcast) и `experience_update` (владельцу)
- Если цель — игрок: после `combatResult` придёт `stats_update` с `health.current = 0`

```json
{
  "body": {
    "skillResult": {
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

### 6.3 Лечение — `healingResult`

Пакет `healingResult` используется как для скиллов-хилов, так и для потребляемых предметов (зелья HP).

```json
{
  "header": {
    "message": "Skill Heal executed successfully",
    "eventType": "healingResult"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetType": 1,
      "targetTypeString": "SELF",
      "skillName": "Heal",
      "skillSlug": "heal_light",
      "skillEffectType": "heal",
      "skillSchool": "holy",
      "hitDelay": 0.0,
      "healing": 120,
      "finalTargetHealth": 350,
      "finalTargetMana": 45,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

### 6.4 Наложение бафф/дебаффа — `buffResult` / `debuffResult`

```json
{
  "header": {
    "message": "Skill Battle Fury executed successfully",
    "eventType": "buffResult"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetType": 1,
      "targetTypeString": "SELF",
      "skillName": "Battle Fury",
      "skillSlug": "battle_fury",
      "skillEffectType": "buff",
      "skillSchool": "physical",
      "appliedEffects": ["battle_fury_phys_attack"],
      "finalTargetHealth": 350,
      "finalTargetMana": 30,
      "casterType": 1,
      "casterTypeString": "PLAYER"
    }
  }
}
```

После применения баффа/дебаффа приходит `stats_update` с обновлёнными `effective`-атрибутами и списком `activeEffects` (для иконок дебаффов/баффов).

**Полные поля объекта `skillResult`:**

| Поле | Тип | Описание |
|------|-----|----------|
| `success` | bool | Выполнение успешно |
| `casterId` | int | ID кастера |
| `targetId` | int | ID цели |
| `targetType` / `targetTypeString` | int/string | Тип цели |
| `skillSlug`, `skillName` | string | Идентификатор и имя скилла |
| `skillEffectType` | string | Тип: `damage`, `heal`, `buff`, `debuff` |
| `skillSchool` | string | Школа магии/физики |
| `hitDelay` | float | Задержка попадания в сек (для анимации) |
| `damage` | int | Урон (только для `damage`) |
| `healing` | int | Хил (только для `heal`) |
| `appliedEffects` | string[] | Наложенные эффекты (только для `buff`/`debuff`) |
| `isCritical` | bool | Крит (только для `damage`) |
| `isBlocked` | bool | Заблокировано щитом (только для `damage`) |
| `isMissed` | bool | Промах (только для `damage`) |
| `targetDied` | bool | Цель умерла после этого удара |
| `finalTargetHealth` | int | HP цели после применения |
| `finalTargetMana` | int | Мана цели после применения |
| `casterType` / `casterTypeString` | int/string | Тип кастера |
| `errorReason` | string | Причина неудачи (только при `success: false`) |

---

## 7. DoT и HoT тики — `effectTick`

Периодически тикующие эффекты (DoT — урон со временем, HoT — лечение со временем).

**Направление:** Сервер → Broadcast (все клиенты)  
**eventType:** `effectTick`

```json
{
  "header": {
    "eventType": "effectTick"
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

#### HoT пример

```json
{
  "header": {
    "eventType": "effectTick"
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
| `characterId` | int | ID цели (персонажа получающего тик) |
| `effectSlug` | string | Slug эффекта |
| `effectTypeSlug` | string | `dot` (урон) или `hot` (лечение) |
| `value` | int/float | Урон или лечение за тик |
| `newHealth` | int | HP цели после тика |
| `newMana` | int | Мана цели после тика |
| `targetDied` | bool | Цель умерла от этого тика |

**Логика на клиенте:**
- Отобразить всплывающее число (floating combat text) у `characterId`
- Обновить HP-бар цели до `newHealth`
- Если `targetDied: true` — обработать смерть (см. `client-death-respawn-protocol.md`)

---

## 8. Прерывание каста — `interruptCombatAction`

Клиент может прервать активный каст (например, при движении или нажатии кнопки отмены).

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

Сервер прерывает каст без broadcast-уведомления. Клиент просто прекращает анимацию каста на своей стороне.

> **Примечание:** Движение во время каста (`moveCharacter`) сервер **не** прерывает каст автоматически — клиент должен явно послать `interruptCombatAction` при движении, если это предусмотрено дизайном скилла.

---

## 9. AI-атаки мобов

Мобы атакуют получая те же broadcast-пакеты с `casterType: 2 (MOB)`. Клиент обрабатывает их идентично атакам игрока.

#### Пример AI-атаки моба по игроку

`combatInitiation`:
```json
{
  "header": {
    "message": "Skill Claw Attack initiated",
    "eventType": "combatInitiation"
  },
  "body": {
    "skillInitiation": {
      "success": true,
      "casterId": 1001,
      "targetId": 7,
      "targetType": 2,
      "targetTypeString": "PLAYER",
      "skillName": "Claw Attack",
      "skillSlug": "mob_claw_attack",
      "skillEffectType": "damage",
      "skillSchool": "physical",
      "castTime": 0.0,
      "animationName": "skill_mob_claw_attack",
      "animationDuration": 0.7,
      "casterType": 2,
      "casterTypeString": "MOB"
    }
  }
}
```

`combatResult`:
```json
{
  "header": {
    "message": "Skill Claw Attack executed successfully",
    "eventType": "combatResult"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 1001,
      "targetId": 7,
      "targetType": 2,
      "targetTypeString": "PLAYER",
      "skillSlug": "mob_claw_attack",
      "skillEffectType": "damage",
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

После AI-атаки по **игроку** сервер автоматически отправляет `stats_update` игроку с обновлённым HP.

---

## 10. Состояние моба — combatState

Поле `combatState` присутствует в пакетах `spawnMobsInZone` и `zoneMoveMobs`.

| Значение | Имя | Описание |
|---------|-----|----------|
| `0` | `PATROLLING` | Патрулирует в зоне спавна |
| `1` | `CHASING` | Преследует цель |
| `2` | `PREPARING_ATTACK` | Подготовка атаки (swing time) |
| `3` | `ATTACKING` | Атакует |
| `4` | `COOLDOWN` | Ожидание кулдауна после атаки |
| `5` | `RETURNING` | Возвращается на точку спавна (leash) |
| `6` | `EVADING` | Неуязвимый период после возвращения |
| `7` | `FLEEING` | Бежит при < 20% HP |

**Важно для клиента:**
- `RETURNING` и `EVADING` — моб неуязвим; визуально показывать HP-бар серым или скрывать
- `FLEEING` — анимация бегства

---

## 11. Спринт-анти-спам: rate limiting

Сервер ограничивает частоту боевых запросов: не более 1 запроса за `COMBAT_RATE_LIMIT_MS` миллисекунд с одного клиента (по умолчанию ~100–200 мс).

При превышении лимита:

```json
{
  "header": {
    "eventType": "playerAttack",
    "message": "Error: Request too fast, slow down!"
  },
  "body": {
    "error": {
      "success": false,
      "errorMessage": "Request too fast, slow down!"
    }
  }
}
```

Клиент должен блокировать отправку следующего запроса до истечения GCD (`gcdMs`) или анимации и не опираться на серверный ответ для блокировки.

---

## 12. Полная последовательность событий

### Мгновенная атака (basic_attack, mob умирает)

```
Клиент                              Сервер                          Все клиенты
  │── playerAttack ────────────────►│
  │                                 │  (валидация: cooldown, жив ли)
  │◄────────────── combatInitiation ─────────────────────────────────────────────►│
  │◄─────────────────────────────── combatResult (targetDied: true) ─────────────►│
  │◄────────────────────────────────── mobDeath ─────────────────────────────────►│
  │◄── experience_update ───────────│
  │◄── itemDrop ────────────────────────────────────────────────────────────────►│
  │◄─ stats_update (mob hit mob) ───│  (если урон получил mob-атакой ранее)
```

### Скилл с кастом (1.5 сек), цель выжила

```
Клиент (анимация каста)              Сервер                          Все клиенты
  │── skillUsage (fireball) ────────►│
  │                                  │  (castTime=1.5s — запись в ongoingActions)
  │◄────────────── combatInitiation (castTime=1.5) ──────────────────────────────►│
  │  [анимация каста 1.5 сек]
  │                                  │  (через 1.5 сек: executeSkillUsage)
  │◄────────────── combatResult ─────────────────────────────────────────────────►│
```

### AI-атака и смерть Игрока

```
                                     Сервер                         Все клиенты
  │                                  │  (AI: mob attacks player)
  │◄────────────── combatInitiation (casterType=MOB) ───────────────────────────►│
  │◄────────────── combatResult (targetDied: true) ─────────────────────────────►│
  │◄── stats_update (health.current=0, debt=N) ──────────────────────────────────│
  │  [показать экран смерти]
```

---

## 13. Таблица ошибок

Ошибки передаются через `success: false` в `skillInitiation.errorReason`:

| Сообщение | Причина |
|-----------|---------|
| `Skill is on cooldown` | Скилл ещё на кулдауне |
| `Not enough mana` | Недостаточно маны |
| `Already casting` | Уже идёт каст этого же кастера |
| `Skill not found: {slug}` | Скилл недоступен персонажу |
| `Caster not found` | Кастер не зарегистрирован в зоне |
| `Request too fast, slow down!` | Rate limit — слишком частые запросы |
| `Cannot use skills while dead` | Персонаж мёртв |
