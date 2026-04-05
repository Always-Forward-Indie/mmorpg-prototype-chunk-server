# 02. Боевая система и скиллы

## Архитектура боевой системы

Бой использует **server-authoritative instant-execute** подход:

- Сервер **немедленно** вычисляет результат и отправляет оба пакета (`{type}Initiation` + `{type}Result`) практически одновременно.
- Скилл нельзя отменить (**non-cancellable by design**). Клиент использует `castMs` / `animationDuration` только для визуалного cast bar и синхронизации анимации с показом цифр урона.
- Урон на сервере уже применён в момент отправки; клиент отображает его по тригерру в анимации.

```
Клиент                          Сервер
  │                                │
  ├─ playerAttack ──────────────────»│ 1. Валидация + расчёт урона
  │«── {type}Initiation (bcast) ───┤ 2. Инициация (castBar на клиенте)
  │«── {type}Result (bcast) ───────․│ 3. Результат (урон применён)
  │    (по истечению animDuration   │
  │     показать цифры + изменить HP)  │
  │«── stats_update ───────────────┤ 4. Обновление статов
  │«── mobHealthUpdate ────────────┤ 5. HP моба
  │«── mobDeath ──────────────────┤ 6. Смерть (если убит)
  │«── experienceUpdate ───────────┤ 7. XP (при убийстве)
```

> **Заметка по `castMs`**: поле передаётся клиенту исключительно для визуального cast bar. Сервер не выжидает окончания каста — `{type}Result` приходит немедленно вместе с инициацией. Клиент использует `animationDuration` чтобы знать, когда показывать цифры урона / попадание проджектайла. **Отмена скилла невозможна.**

---

## 2.1. playerAttack — Инициация атаки / скилла

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "playerAttack",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400900,
      "requestId": "sync_1711709400900_42_300_abc"
    }
  },
  "body": {
    "attackerId": 7,
    "actionId": 0,
    "targetId": 100,
    "targetPosition": {
      "x": 150.0,
      "y": 95.0,
      "z": 0.0,
      "rotationZ": 0.0
    },
    "useAI": false,
    "strategy": "closest"
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `attackerId` | int | ID персонажа-атакующего |
| `actionId` | int | `0` = авто-выбор скилла, `>0` = конкретный `skill.id` |
| `targetId` | int | UID цели (моба). `0` = авто-выбор цели |
| `targetPosition` | object | Позиция для AoE-атак |
| `useAI` | bool | Использовать AI для выбора цели/скилла |
| `strategy` | string | `"closest"`, `"lowestHP"`, `"random"` |

---

## 2.2. Фаза инициации — `{type}Initiation`

Сервер проверяет все условия и при успехе начинает каст.

### Сервер → Broadcast

Имя `eventType` зависит от `skillEffectType`:

| skillEffectType | eventType Initiation | eventType Result |
|-----------------|---------------------|-----------------|
| `"damage"` | `combatInitiation` | `combatResult` |
| `"heal"` | `healingInitiation` | `healingResult` |
| `"buff"` | `buffInitiation` | `buffResult` |
| `"debuff"` | `debuffInitiation` | `debuffResult` |
| другие | `skillInitiation` | `skillResult` |

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
      "targetId": 100,
      "targetType": 2,
      "targetTypeString": "MOB",
      "casterType": 1,
      "casterTypeString": "PLAYER",
      "skillName": "Fireball",
      "skillSlug": "fireball",
      "skillEffectType": "damage",
      "skillSchool": "fire",
      "castTime": 1.5,
      "animationName": "cast_fireball",
      "animationDuration": 1.5,
      "cooldownMs": 8000,
      "gcdMs": 1500,
      "serverTimestamp": 1711709400910,
      "castStartedAt": 1711709400910,
      "errorReason": ""
    }
  }
}
```

**При ошибке валидации:**

```json
{
  "header": {
    "eventType": "combatInitiation",
    "message": "Skill Fireball initiated",
    "status": "error"
  },
  "body": {
    "skillInitiation": {
      "success": false,
      "casterId": 7,
      "skillSlug": "fireball",
      "errorReason": "Skill on cooldown"
    }
  }
}
```

### Поля инициации

| Поле | Тип | Описание |
|------|-----|----------|
| `success` | bool | Успех валидации |
| `casterId` | int | ID кастера |
| `targetId` | int | ID цели |
| `targetType` | int | `0`=SELF, `1`=PLAYER, `2`=MOB, `3`=AREA, `4`=NONE |
| `targetTypeString` | string | Строковое имя типа цели |
| `casterType` | int | `1`=PLAYER, `2`=MOB, `0`=UNKNOWN |
| `casterTypeString` | string | Строковое имя типа кастера |
| `skillName` | string | Локализованное имя скилла |
| `skillSlug` | string | Уникальный идентификатор скилла |
| `skillEffectType` | string | `"damage"`, `"heal"`, `"buff"`, `"debuff"` |
| `skillSchool` | string | `"physical"`, `"fire"`, `"ice"`, `"healing"` и т.д. |
| `castTime` | float | Длительность cast bar на клиенте в секундах (0 = инстант). **Сервер не ждёт — только для клиента.** |
| `animationName` | string | Имя анимации для клиента |
| `animationDuration` | float | Длительность анимации в секундах |
| `cooldownMs` | int | Кулдаун скилла в мс (для рендера иконки кулдауна) |
| `gcdMs` | int | Глобальный кулдаун в мс, запускаемый этим скиллом |
| `serverTimestamp` | int64 | Unix ms серверного времени |
| `castStartedAt` | int64 | Unix ms начала каста |
| `errorReason` | string | Причина ошибки (при `success: false`) |

### Возможные ошибки инициации

| Ошибка | Описание |
|--------|----------|
| `"Skill on cooldown"` | Скилл на кулдауне |
| `"Not enough mana"` | Недостаточно маны |
| `"Target out of range"` | Цель слишком далеко |
| `"Already casting"` | Уже кастуется другой скилл |
| `"Invalid target"` | Невалидная цель |
| `"Target is dead"` | Цель мертва |
| `"GCD active"` | Глобальный кулдаун активен |

---

## 2.3. Фаза результата — `{type}Result`

После завершения каста (или мгновенно для инстант-скиллов) сервер вычисляет результат.

### Сервер → Broadcast

```json
{
  "header": {
    "eventType": "combatResult",
    "message": "Skill Fireball executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 100,
      "targetType": 2,
      "targetTypeString": "MOB",
      "casterType": 1,
      "casterTypeString": "PLAYER",
      "skillName": "Fireball",
      "skillSlug": "fireball",
      "skillEffectType": "damage",
      "skillSchool": "fire",
      "serverTimestamp": 1711709402410,
      "damage": 85,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": false,
      "targetDied": false,
      "finalTargetHealth": 165,
      "finalTargetMana": 50,
      "finalCasterMana": 60
    }
  }
}
```

**Пример хила:**

```json
{
  "header": {
    "eventType": "healingResult",
    "message": "Skill Heal executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetType": 0,
      "targetTypeString": "SELF",
      "casterType": 1,
      "casterTypeString": "PLAYER",
      "skillName": "Heal",
      "skillSlug": "heal",
      "skillEffectType": "heal",
      "skillSchool": "healing",
      "serverTimestamp": 1711709402500,
      "healing": 120,
      "finalTargetHealth": 250,
      "finalTargetMana": 80
    }
  }
}
```

**Пример баффа:**

```json
{
  "header": {
    "eventType": "buffResult",
    "message": "Skill Battle Cry executed successfully",
    "status": "success"
  },
  "body": {
    "skillResult": {
      "success": true,
      "casterId": 7,
      "targetId": 7,
      "targetTypeString": "SELF",
      "casterTypeString": "PLAYER",
      "skillName": "Battle Cry",
      "skillSlug": "battle_cry",
      "skillEffectType": "buff",
      "appliedEffects": ["battle_fury"],
      "finalTargetHealth": 250,
      "finalTargetMana": 80
    }
  }
}
```

### Поля результата

| Поле | Тип | Присутствует | Описание |
|------|-----|-------------|----------|
| `success` | bool | Всегда | Успех выполнения |
| `casterId` | int | Всегда | ID кастера |
| `targetId` | int | Всегда | ID цели |
| `targetType` | int | Всегда | Числовой тип цели |
| `targetTypeString` | string | Всегда | Строковый тип цели |
| `casterType` | int | Всегда | Числовой тип кастера |
| `casterTypeString` | string | Всегда | Строковый тип кастера |
| `skillName` | string | Всегда | Имя скилла |
| `skillSlug` | string | Всегда | Slug скилла |
| `skillEffectType` | string | Всегда | Тип эффекта |
| `skillSchool` | string | Всегда | Школа магии |
| `serverTimestamp` | int64 | Всегда | Серверное время |
| `damage` | int | damage | Нанесённый урон |
| `isCritical` | bool | damage | Критический удар |
| `isBlocked` | bool | damage | Был заблокирован |
| `isMissed` | bool | damage | Промах |
| `targetDied` | bool | damage | Цель убита |
| `healing` | int | heal | Количество исцеления |
| `appliedEffects` | string[] | buff/debuff | Список применённых эффектов |
| `finalTargetHealth` | int | Всегда | HP цели после действия |
| `finalTargetMana` | int | Всегда | Mana цели после действия |
| `finalCasterMana` | int | Всегда | Остаток маны кастера после использования скилла |

---

## 2.4. effectTick — Тик DoT/HoT

Для периодических эффектов (яд, регенерация) сервер отправляет тик каждые `tickMs` мс.

### Сервер → Broadcast

```json
{
  "header": {
    "eventType": "effectTick"
  },
  "body": {
    "characterId": 7,
    "effectSlug": "poison_dot",
    "effectTypeSlug": "dot",
    "value": 15.0,
    "newHealth": 185,
    "newMana": 100,
    "targetDied": false
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `characterId` | int | ID цели эффекта |
| `effectSlug` | string | Slug эффекта (напр. `"poison_dot"`, `"regen_hot"`) |
| `effectTypeSlug` | string | `"dot"` (урон) или `"hot"` (лечение) |
| `value` | float | Количество урона/лечения за тик |
| `newHealth` | int | HP после тика |
| `newMana` | int | Mana после тика |
| `targetDied` | bool | Цель умерла от тика |

---

## 2.5. combatAoeResult — Батч-результат AoE-скилла

Вместо отдельного `combatResult` на каждую цель сервер отправляет один пакет со всеми поражёнными целями.

### Сервер → Broadcast

```json
{
  "header": {
    "eventType": "combatAoeResult",
    "message": "AoE skill executed"
  },
  "body": {
    "aoeResult": {
      "casterId": 7,
      "casterType": 1,
      "casterTypeString": "PLAYER",
      "skillSlug": "fire_storm",
      "skillName": "Fire Storm",
      "skillEffectType": "damage",
      "skillSchool": "fire",
      "serverTimestamp": 1711709410000,
      "finalCasterMana": 45,
      "targets": [
        {
          "targetId": 101,
          "targetType": 2,
          "targetTypeString": "MOB",
          "damage": 85,
          "isCritical": false,
          "isBlocked": false,
          "isMissed": false,
          "targetDied": false,
          "finalTargetHealth": 165
        },
        {
          "targetId": 102,
          "targetType": 2,
          "targetTypeString": "MOB",
          "damage": 184,
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
| `casterType` / `casterTypeString` | int/string | Тип кастера |
| `skillSlug` | string | Slug скилла |
| `skillEffectType` | string | Тип эффекта |
| `serverTimestamp` | int64 | Серверное время |
| `finalCasterMana` | int | Остаток маны кастера после AoE |
| `targets` | array | Массив всех поражённых целей |
| `targets[].targetId` | int | ID цели |
| `targets[].targetType` / `targetTypeString` | int/string | Тип цели |
| `targets[].damage` | int | Нанесённый урон |
| `targets[].isCritical` | bool | Критический удар |
| `targets[].isBlocked` | bool | Заблокировано |
| `targets[].isMissed` | bool | Промах |
| `targets[].targetDied` | bool | Цель убита |
| `targets[].finalTargetHealth` | int | HP цели после удара |

---

## 2.6. Атаки мобов

Мобы атакуют по AI — клиент получает те же пакеты `{type}Initiation` и `{type}Result`, но с `casterType: 2` (`"MOB"`).

```json
{
  "body": {
    "skillInitiation": {
      "casterId": 100,
      "casterType": 2,
      "casterTypeString": "MOB",
      "targetId": 7,
      "targetType": 1,
      "targetTypeString": "PLAYER",
      "skillSlug": "bite",
      "skillEffectType": "damage",
      "castTime": 0.0,
      "animationName": "mob_bite",
      "animationDuration": 0.5
    }
  }
}
```

---

## Формулы расчёта урона

### Полный пайплайн расчёта урона

```
1. МОДИФИКАТОР УРОВНЯ
   rawDiff = clamp(attacker.level - target.level, -10, +10)
   dmgMod = 1.0 + (rawDiff × 0.04)
   hitMod = rawDiff × 0.02

2. ПРОВЕРКА ПРОМАХА
   hitChance = 0.95 + (accuracy - evasion) × 0.01 + hitMod
   hitChance = clamp(hitChance, 0.05, 0.95)
   isMissed = random(0,1) > hitChance
   → Если промах: damage = 0, isMissed = true

3. БАЗОВЫЙ УРОН
   rawDmg = skill.flatAdd + (scaleStat × skill.coeff)
   variance = random(0.88, 1.12)    // ±12%
   baseDamage = max(1, rawDmg × variance)

4. КРИТИЧЕСКИЙ УДАР
   critChance = attacker.attributes["crit_chance"] / 100.0
   isCrit = random(0,1) < critChance
   scaledDmg = isCrit ? baseDamage × 2.0 : baseDamage

5. БЛОК
   blockChance = target.attributes["block_chance"] / 100.0
   isBlocked = random(0,1) < blockChance
   blockValue = target.attributes["block_value"]
   scaledDmg = max(0, scaledDmg - blockValue)

6. МОДИФИКАТОР УРОВНЯ
   scaledDmg = scaledDmg × dmgMod

7. РЕДУКЦИЯ ЗАЩИТОЙ
   defense = target.attributes["physical_defense" | "magical_defense"]
   reduction = defense / (defense + 7.5 × targetLevel)
   reduction = min(reduction, 0.85)    // кап 85%
   scaledDmg = scaledDmg × (1.0 - reduction)

8. ШКОЛЬНОЕ СОПРОТИВЛЕНИЕ
   resistance = target.attributes[school + "_resistance"] / 100.0
   resistance = min(resistance, 0.75)    // кап 75%
   totalDamage = scaledDmg × (1.0 - resistance)

ФИНАЛ: totalDamage = max(0, totalDamage)
```

### Формула хила (без защиты/резиста)

```
rawHeal = skill.flatAdd + (scaleStat × skill.coeff)
variance = random(0.90, 1.10)    // ±10%
healing = max(1, rawHeal × variance)
```

---

## Свойства скиллов (SkillStruct)

| Поле | Тип | Описание |
|------|-----|----------|
| `skillSlug` | string | Уникальный идентификатор |
| `skillName` | string | Отображаемое имя |
| `skillLevel` | int | Уровень скилла |
| `school` | string | Школа: `"physical"`, `"fire"`, `"ice"`, `"healing"` и т.д. |
| `skillEffectType` | string | `"damage"`, `"heal"`, `"buff"`, `"debuff"`, `"dot"` |
| `scaleStat` | string | Атрибут масштабирования (напр. `"strength"`, `"intelligence"`) |
| `coeff` | float | Коэффициент масштабирования |
| `flatAdd` | float | Фиксированная добавка к урону/хилу |
| `cooldownMs` | int | Кулдаун скилла (мс) |
| `gcdMs` | int | Глобальный кулдаун (мс) |
| `castMs` | int | Время каста (мс). `0` = инстант |
| `costMp` | int | Стоимость маны |
| `maxRange` | float | Максимальная дальность |
| `areaRadius` | float | Радиус AoE. `0` = один-на-одного |
| `swingMs` | int | Длительность анимации удара (мс). По умолчанию `300` |
| `animationName` | string | Имя анимации |
| `isPassive` | bool | Пассивный скилл (не используется активно) |
| `effects` | array | Список `SkillEffectDefinitionStruct` |

### SkillEffectDefinitionStruct

| Поле | Тип | Описание |
|------|-----|----------|
| `effectSlug` | string | ID эффекта (напр. `"poison_dot"`) |
| `effectTypeSlug` | string | `"buff"`, `"debuff"`, `"dot"`, `"hot"` |
| `attributeSlug` | string | Целевой атрибут (напр. `"hp"`, `"strength"`) |
| `value` | float | Модификатор стата или количество за тик |
| `durationSeconds` | int | Длительность. `0` = перманентный |
| `tickMs` | int | Интервал тика. `0` = не-тиковый |

---

## Активные эффекты (ActiveEffectStruct)

Баффы, дебаффы, DoT и HoT хранятся как `ActiveEffectStruct`:

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int64 | ID записи в БД |
| `effectId` | int | ID шаблона эффекта |
| `effectSlug` | string | Slug эффекта |
| `effectTypeSlug` | string | `"damage"`, `"dot"`, `"hot"`, `"stat_mod"` |
| `attributeId` | int | ID атрибута (`0` = не-стат) |
| `attributeSlug` | string | Целевой атрибут |
| `value` | float | Аддитивный модификатор или значение за тик |
| `sourceType` | string | `"quest"`, `"skill"`, `"item"`, `"dialogue"` |
| `expiresAt` | int64 | Unix timestamp истечения. `0` = перманентный |
| `tickMs` | int | Интервал тика (мс). `0` = не-тиковый |

Активные эффекты приходят в пакете `stats_update` в поле `activeEffects`:

```json
{
  "slug": "battle_fury",
  "effectTypeSlug": "buff",
  "attributeSlug": "physical_attack",
  "value": 18.0,
  "expiresAt": 1741787700
}
```

---

## Типы боевых действий (CombatActionType)

| Значение | Код | Описание |
|----------|-----|----------|
| `BASIC_ATTACK` | 1 | Базовая атака |
| `SPELL` | 2 | Заклинание |
| `SKILL` | 3 | Активный скилл |
| `CHANNELED` | 4 | Канальный скилл |
| `INSTANT` | 5 | Мгновенный скилл |
| `AOE_ATTACK` | 6 | AoE атака |
| `BUFF` | 7 | Бафф |
| `DEBUFF` | 8 | Дебафф |

## Типы целей (CombatTargetType)

| Значение | Код | Описание |
|----------|-----|----------|
| `SELF` | 1 | Себя |
| `PLAYER` | 2 | Игрок |
| `MOB` | 3 | Моб |
| `AREA` | 4 | Область |
| `NONE` | 5 | Нет цели |

## Состояния каста (CombatActionState)

| Значение | Код | Описание |
|----------|-----|----------|
| `INITIATED` | 1 | Инициирован |
| `CASTING` | 2 | Кастуется |
| `EXECUTING` | 3 | Выполняется |
| `COMPLETED` | 4 | Завершён |
| `INTERRUPTED` | 5 | Прерван |
| `FAILED` | 6 | Неудачен |

## Причины прерывания (InterruptionReason)

| Значение | Код | Описание |
|----------|-----|----------|
| `NONE` | 0 | Не прерван |
| `PLAYER_CANCELLED` | 1 | Отменён игроком |
| `MOVEMENT` | 2 | Прерван движением |
| `DAMAGE_TAKEN` | 3 | Прерван входящим уроном |
| `TARGET_LOST` | 4 | Цель потеряна |
| `RESOURCE_DEPLETED` | 5 | Ресурс исчерпан |
| `DEATH` | 6 | Смерть кастера |
| `STUN_EFFECT` | 7 | Стан |

---

## Инициализация скиллов при входе

### Сервер → Unicast (автоматически при joinGameCharacter)

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
        "flatAdd": 5.0,
        "cooldownMs": 1500,
        "gcdMs": 1000,
        "castMs": 0,
        "costMp": 0,
        "maxRange": 150.0,
        "isPassive": false
      },
      {
        "skillSlug": "fireball",
        "skillLevel": 1,
        "coeff": 1.5,
        "flatAdd": 20.0,
        "cooldownMs": 5000,
        "gcdMs": 1000,
        "castMs": 1500,
        "costMp": 30,
        "maxRange": 800.0,
        "isPassive": false
      }
    ]
  }
}
```

---

## Кулдаун-менеджмент

- **Per-skill cooldown**: индивидуальный для каждого скилла (`cooldownMs`)
- **Global Cooldown (GCD)**: общий для всех скиллов игрока (`gcdMs`)
- Проверка кулдауна — серверная, atomic (no TOCTOU)
- Проверка дальности: `distance = sqrt((cX-tX)² + (cY-tY)²) <= skill.maxRange × 100.0`
