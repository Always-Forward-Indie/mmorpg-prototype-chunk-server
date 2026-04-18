# Протокол AI мобов: движение и атака

**Версия документа:** v1.5  
**Дата:** 2026-03-21  
**Актуально для:** chunk-server v0.0.4+  
**Исходный код:** `MobAIController.cpp`, `MobMovementManager.cpp`

Документ описывает серверную логику AI мобов — конечный автомат состояний, тайминги переходов, логику угрозы (threat), социальную агрессию, систему melee-слотов и сетевые пакеты, которые клиент получает на каждом шаге.

---

## Содержание

1. [Состояния AI (конечный автомат)](#1-состояния-ai-конечный-автомат)
2. [Диаграмма переходов](#2-диаграмма-переходов)
3. [Подробное описание каждого состояния](#3-подробное-описание-каждого-состояния)
4. [Полная последовательность пакетов: жизненный цикл атаки](#4-полная-последовательность-пакетов-жизненный-цикл-атаки)
5. [Конфигурация AI: параметры мобов](#5-конфигурация-ai-параметры-мобов)
6. [Движение в патруле (PATROLLING)](#6-движение-в-патруле-patrolling)
7. [Преследование (CHASING)](#7-преследование-chasing)
8. [Возврат к спавну (RETURNING + EVADING)](#8-возврат-к-спавну-returning--evading)
9. [Бегство (FLEEING)](#9-бегство-fleeing)
10. [Архетипы AI (melee / caster)](#10-архетипы-ai-melee--caster)
11. [Социальная агрессия (isSocial)](#11-социальная-агрессия-issocial)
12. [Система угрозы (Threat Table)](#12-система-угрозы-threat-table)
13. [Melee-слоты (предотвращение толпы)](#13-melee-слоты-предотвращение-толпы)
14. [Сводная таблица пакетов](#14-сводная-таблица-пакетов)
15. [Руководство для разработчика клиента](#15-руководство-для-разработчика-клиента)
16. [Полные примеры JSON-пакетов](#16-полные-примеры-json-пакетов)

---

## 1. Состояния AI (конечный автомат)

| Значение | Имя | Движение | Уязвим |
|----------|-----|----------|--------|
| `0` | `PATROLLING` | случайный waypoint | да |
| `1` | `CHASING` | к цели, каждые 0.1 с | да |
| `2` | `PREPARING_ATTACK` | **нет** | да |
| `3` | `ATTACKING` | **нет** | да |
| `4` | `ATTACK_COOLDOWN` | **нет** | да |
| `5` | `RETURNING` | к точке спавна, каждые 0.15 с | **нет** |
| `6` | `EVADING` | **нет** | **нет** |
| `7` | `FLEEING` | от атакующего | да |

---

## 2. Диаграмма переходов

```
                         ┌─────────────────────┐
                         │      PATROLLING      │◄──────────────────┐
                         │  (random waypoints)  │                   │
                         └──────────┬───────────┘                   │
                     targetPlayerId > 0                             │
                 (агро по близости или ответ на атаку)              │
                              ↓                                     │
                         ┌─────────────────────┐    target умер     │
                         │       CHASING        │───────────────────►│
                         │  (движение к цели)   │                   │
                         └──────────┬───────────┘                   │
               ┌────────────────────┤                               │
               │ дистанция ≤         │ chase timeout /              │
               │ attackRange         │ за зону /                    │
               │ + слот свободен     │ цель слишком далека          │
               ↓                    ↓                               │
    ┌───────────────────┐   ┌───────────────────┐                   │
    │  PREPARING_ATTACK │   │     RETURNING      │                   │
    │  (castTime сек)   │   │  (движение к       │──┐               │
    └────────┬──────────┘   │   точке спавна,    │  │               │
    castTime │              │   HP regen 10%/с)  │  │               │
    истёк    │              └────────┬───────────┘  │               │
             ↓                      │ ≤ 10 units    │               │
    ┌───────────────────┐           │ от спавна     │               │
    │     ATTACKING      │          ↓               │               │
    │  (swingMs сек)    │   ┌───────────────────┐   │               │
    └────────┬──────────┘   │      EVADING       │   │               │
    swing    │              │  (2 с, нет урона)  │   │               │
    истёк    │              └────────┬───────────┘   │               │
             ↓                      │ 2 с истекло    │               │
    ┌───────────────────┐           └───────────────►┘               │
    │   ATTACK_COOLDOWN  │                                            │
    │  (cooldown сек)   │                                            │
    └────────┬──────────┘                                            │
    cooldown │ цель жива                                             │
    истёк    └───────────────────────────────────────────────────────►(CHASING)
             │ цель мертва
             └───────────────────────────────────────────────────────►(PATROLLING)

─── FLEEING (из любого состояния при HP% < fleeHpThreshold, если настроено) ───────────────►RETURNING
```

---

## 3. Подробное описание каждого состояния

### PATROLLING (0)

- Моб выбирает случайную точку (waypoint) внутри AABB зоны спавна.
- 70% вероятность продолжать предыдущее направление ±30°, 30% — перестроиться к waypoint.
- При достижении waypoint (расстояние < 2 units) — выбирается следующий случайный waypoint.
- Агрессивные мобы (`isAggressive = true`) сканируют игроков в радиусе `aggroRange`. При обнаружении: `targetPlayerId` → CHASING.
- Переход в CHASING также происходит если моб получил урон от игрока (через `handleMobAttacked`).

### CHASING (1)

- Каждые `chaseMovementInterval` (0.1 с) сервер делает шаг к позиции цели.
- При достижении `attackRange`: стоп — → PREPARING_ATTACK.
- Причины перехода в RETURNING:
  - Расстояние до цели > `aggroRange × chaseMultiplier`
  - `chaseDuration` (секунды) истёк
  - Моб вышел за `maxChaseFromZoneEdge` от границы зоны
- Если все melee-слоты вокруг цели заняты: `waitingForMeleeSlot = true`, моб паркуется за кольцом атаки — не движется, не атакует, ждёт свободного слота.

### PREPARING_ATTACK (2)

- Моб заморожен. Длительность = `castMs` выбранного скилла / 1000.0 секунды.
- В момент перехода CHASING → PREPARING_ATTACK:
  1. Сервер выбирает скилл (`selectAttackSkill`).
  2. Отправляет `combatInitiation` (broadcast) — клиент запускает кастбар.
  3. **Немедленно** отправляет `combatResult` (broadcast) — урон применяется сразу.
- По истечении `castMs` (переход PREPARING_ATTACK → ATTACKING): состояние меняется, урон уже не повторяется.
- Клиент использует `castTime` из `combatInitiation.skillInitiation.castTime` для кастбара.
- Если цель умерла во время касты → PATROLLING.

### ATTACKING (3)

- Моб заморожен. Длительность = `swingMs` выбранного скилла / 1000.0 секунды.
- `combatResult` был отправлен сразу при входе в PREPARING_ATTACK (вместе с `combatInitiation`).
- Клиент воспроизводит анимацию удара; хит-триггер внутри анимации применяет урон к HP-бару.

### ATTACK_COOLDOWN (4)

- Моб заморожен. Длительность = `max(cooldownMs, gcdMs)` / 1000.0 секунды (минимум 0.5 с).
- По истечении: цель жива → CHASING; цель мертва → PATROLLING; `isReturningToSpawn` → RETURNING.

### RETURNING (5)

- **Неуязвим**: урон игнорируется (проверяется в `SkillManager`).
- Угроза (threat) по-прежнему накапливается тихо, атаки игрока не прерывают возврат.
- Движется к `spawnPosition` каждые `returnMovementInterval` (0.15 с), шаг = `baseSpeedMax` units.
- **Regen HP**: 10% от `maxHealth` в секунду (broadcast `mobHealthUpdate` на каждый тик).
- За ≤ 10 units от точки спавна: `isReturningToSpawn = false` → EVADING.

### EVADING (6)

- Моб стоит неподвижно, **неуязвим**, длительность = 2 секунды.
- По истечении: → PATROLLING, выбирается новый waypoint.

### FLEEING (7)

- Активируется только если у моба настроен `fleeHpThreshold > 0.0` **и** его HP% ≤ этого порога.
- Цель бегства = направление **от** атакующего на расстояние `aggroRange × chaseMultiplier × 1.1f`.
- Длительность бегства ≤ `chaseDuration / 3` секунд (или пока атакующий жив).
- По истечении → RETURNING.

---

## 4. Полная последовательность пакетов: жизненный цикл атаки

### 4.1 Агрессивный моб видит игрока (proximity aggro)

```
Сервер (AI tick)                             Все клиенты
  │  Игрок входит в aggroRange моба
  │  combatState: PATROLLING → CHASING
  │
  │──► zoneMoveMobs / mobMoveUpdate ─────────────────────► clientId
  │    { "combatState": 1, "velocity": {...} }
  │    (моб начинает двигаться к игроку)
```

### 4.2 Моб достигает зоны атаки → удар

```
Сервер (AI tick)                             Все клиенты
  │  distance ≤ attackRange
  │  combatState: CHASING → PREPARING_ATTACK
  │  selectAttackSkill() → "mob_claw_attack"
  │
  │  Урон применяется НЕМЕДЛЕННО при входе в PREPARING_ATTACK
  │
  │──► combatInitiation ─────────────────────────────────► broadcast
  │    { casterId: 1001, targetId: 7,
  │      castTime: 0.5, skillSlug: "mob_claw_attack" }
  │
  │──► combatResult ─────────────────────────────────────► broadcast
  │    { casterId: 1001, targetId: 7, damage: 22,
  │      finalTargetHealth: 228, targetDied: false }
  │    (оба пакета отправлены одновременно, до истечения castTime)
  │
  │──► stats_update ─────────────────────────────────────► только targetId=7
  │    { currentHealth: 228 }
  │
  │──► mobMoveUpdate ────────────────────────────────────► clientId
  │    { combatState: 2, velocity: { dirX:0, dirY:0, speed:0 } }
  │    (моб заморожен, клиент запускает кастбар 0.5 сек)
  │
  │  castTime (0.5 с) истёк → combatState: PREPARING_ATTACK → ATTACKING
  │  (урон уже был применён при входе в PREPARING_ATTACK)
  │
  │──► mobMoveUpdate: { combatState: 3 }  ───────────────► clientId
  │
  │  swingMs истёк → combatState: ATTACKING → ATTACK_COOLDOWN
  │──► mobMoveUpdate: { combatState: 4 }  ───────────────► clientId
  │
  │  cooldown истёк → combatState: ATTACK_COOLDOWN → CHASING
  │──► mobMoveUpdate: { combatState: 1, velocity: {...} } ► clientId
  │    (новый цикл преследования/атаки)
```

### 4.3 Игрок убегает → leash

```
Сервер                                          Все клиенты
  │  distance > aggroRange × chaseMultiplier
  │  ИЛИ chaseDuration истёк
  │  combatState: CHASING → RETURNING
  │  targetPlayerId = 0
  │
  │──► mobTargetLost ────────────────────────────────────► broadcast
  │    { mobUID: 1001, lostTargetPlayerId: 7,
  │      positionX: 163.4, positionY: 95.2 }
  │
  │  (каждые 0.15 с движение к спавну)
  │──► mobMoveUpdate ────────────────────────────────────► clientId
  │    { combatState: 5, velocity: { dirX: -0.7, ... } }
  │
  │  (каждую секунду regen HP)
  │──► mobHealthUpdate ──────────────────────────────────► broadcast
  │    { mobUID: 1001, currentHealth: 160, maxHealth: 200 }
  │
  │  ≤ 10 units от спавна → combatState: RETURNING → EVADING
  │──► mobMoveUpdate: { combatState: 6, velocity: {0,0,0} } ► clientId
  │
  │  2 секунды → combatState: EVADING → PATROLLING
  │──► mobMoveUpdate: { combatState: 0, velocity: {...} } ──► clientId
```

### 4.4 Игрок убивает моба

```
Сервер                                          Все клиенты
  │  combatResult (targetDied: true)
  │──► combatResult ─────────────────────────────────────► broadcast
  │──► mobDeath ─────────────────────────────────────────► broadcast
  │    { mobUID: 1001, zoneId: 3 }
  │──► experienceUpdate ────────────────────────────────► только убийце
  │──► itemDrop (если есть) ─────────────────────────────► broadcast
```

### 4.5 Моб бежит (FLEEING, если настроено)

```
Вызов: checkAndTriggerFlee при HP ≤ fleeHpThreshold

Сервер                                          Все клиенты
  │  combatState: CHASING/ATTACKING → FLEEING
  │  isFleeing = true
  │  fleeTargetPosition = позиция от атакующего × fleeDistance
  │
  │──► mobMoveUpdate ────────────────────────────────────► clientId
  │    { combatState: 7, velocity: { dirX:..., speed:... } }
  │
  │  chaseDuration/3 истёк ИЛИ атакующий умер
  │  → RETURNING (с отправкой mobTargetLost)
```

---

## 5. Конфигурация AI: параметры мобов

### 5.1 Глобальный конфиг (MobAIConfig, задаётся в коде)

| Параметр | Значение по умолчанию | Описание |
|----------|-----------------------|----------|
| `chaseMovementInterval` | `0.1 с` | Интервал шага при преследовании |
| `returnMovementInterval` | `0.15 с` | Интервал шага при возврате |
| `chaseSpeedUnitsPerSec` | `450 units/с` | Скорость преследования (units/sec); переопределяется атрибутом моба `move_speed × 40` |
| `minimumMoveDistance` | `10 units` | Минимальный сдвиг для отправки `mobMoveUpdate` (снижено с 50, чтобы не терялись обновления при скорости ≈45 units/тик) |
| `maxChaseFromZoneEdge` | `1500 units` | Макс. расстояние от границы зоны при преследовании |
| `newTargetZoneDistance` | `150 units` | Макс. расстояние от зоны для поиска новой цели |

### 5.2 Параметры per-mob (из таблицы `mobs` в БД)

| Поле | По умолчанию | Описание |
|------|--------------|----------|
| `aggroRange` | `400 units` | Радиус агро (proximity aggro для агрессивных мобов) |
| `attackRange` | `150 units` | Дистанция атаки (переход CHASING → PREPARING_ATTACK) |
| `attackCooldown` | `2.0 с` | Базовый кулдаун (используется если у скилла `cooldownMs=0`) |
| `chaseMultiplier` | `2.0×` | `aggroRange × chaseMultiplier` = макс. дистанция преследования |
| `chaseDuration` | `30.0 с` | Максимальное время преследования до принудительного leash |
| `patrolSpeed` | `1.0` | Множитель скорости патруля (влияет на интервал шагов) |
| `isSocial` | `false` | Включить групповую агрессию (см. раздел 11) |
| `fleeHpThreshold` | `0.0` | Порог HP для бегства (0.0 = никогда не убегает) |
| `aiArchetype` | `"melee"` | Архетип AI: `melee`, `caster`, `ranged`, `support` |
| `radius` | `0` | Радиус коллизии (units); если 0 — используется `minSeparationDistance` |

### 5.3 Тайминги атаки (из скилла)

| Поле | Источник | Описание |
|------|----------|----------|
| `attackPrepareTime` | `skill.castMs / 1000` | Длительность PREPARING_ATTACK |
| `attackDuration` | `skill.swingMs / 1000` | Длительность ATTACKING |
| `postAttackCooldown` | `max(skill.cooldownMs, skill.gcdMs) / 1000` (мин. 0.5 с) | Длительность ATTACK_COOLDOWN |

Если у моба нет скилла с подходящей дистанцией, используются значения:  
`attackPrepareTime = 0`, `attackDuration = 0.3`, `postAttackCooldown = max(mob.attackCooldown, 0.5)`.

---

## 6. Движение в патруле (PATROLLING)

### Алгоритм waypoint patrol

1. Моб выбирает случайную точку (`patrolTargetPoint`) внутри AABB зоны (с отступом `borderThresholdPercent = 10%`).
2. Каждый тик:
   - 70% вероятность — продолжить предыдущее направление ±30° (direction inertia).
   - 30% вероятность — выровняться по направлению к waypoint ±15° (scatter).
3. При достижении waypoint (< 2 units) — выбирается следующий.
4. На границах зоны моб разворачивается к центру.

### Параметры шага (MobMovementParams)

| Параметр | Значение | Описание |
|----------|----------|----------|
| `baseSpeedMin` / `baseSpeedMax` | 80–140 units | Диапазон размера шага при патруле (и размер шага при RETURNING) |
| `maxStepSizeAbsolute` | 450 units | Максимальный шаг за тик |
| `minMoveDistance` | 120 units | Минимальный шаг (иначе пропуск) |
| `minSeparationDistance` | 140 units | Минимальное расстояние между мобами |
| `moveTimeMin` / `moveTimeMax` | 2–6 с | Начальная задержка патруля: сколько ждать до первого шага |
| `speedTimeMin` / `speedTimeMax` | 2–5 с | Интервал между шагами при патруле (пауза после каждого шага) |
| `initialDelayMax` | 1.0 с | Максимальная случайная задержка сразу после спавна |

### Dead reckoning при патруле

Между пакетами `mobMoveUpdate` клиент экстраполирует:
```
position += velocity.dirX × velocity.speed × deltaTime
position += velocity.dirY × velocity.speed × deltaTime
```
`velocity.speed` в `mobMoveUpdate` = `stepSize` (units за тик серверного тика).

---

## 7. Преследование (CHASING)

### Алгоритм chase

1. Каждые 0.1 с вычислить вектор к позиции цели (`characterManager.getCharacterById(targetId)`).
2. Если `distance > aggroRange × chaseMultiplier` → leash, `mobTargetLost`.
3. Если `distance > maxChaseFromZoneEdge` от границы зоны → leash, `mobTargetLost`.
4. Если `distance ≤ attackRange` → остановиться, запустить процесс атаки.
5. Скорость: берётся из атрибута `move_speed × 40` (units/sec), при отсутствии атрибута — `chaseSpeedUnitsPerSec` (450 units/sec). Шаг за тик: `chaseSpeed × chaseMovementInterval`, не более `maxStepSizeAbsolute`, и не больше расстояния до `attackRange - 2` units от цели (предотвращает проскок сквозь игрока).
6. При столкновении с другими мобами: deflection 30°/−30°/60°/−60°/90°/−90°.

### Скорость в `mobMoveUpdate` при преследовании

```
velocity.speed = chaseSpeedUnitsPerSec  (units/sec)
             = move_speed_attr × 40    (если атрибут задан)
             = 450.0                   (по умолчанию)
```

Скорость передаётся напрямую из вычисленного значения `mobChaseSpeed` и Used verbatim for Dead Reckoning на клиенте: `TargetPos = ServerPos + velocity × dt`.

---

## 8. Возврат к спавну (RETURNING + EVADING)

### RETURNING

- Шаг = `baseSpeedMax` units каждые 0.15 с.
- Направление строго к `spawnPosition`.
- Если `distance ≤ baseSpeedMax` (один шаг) — моб телепортируется в `spawnPosition` и сразу enters EVADING.
- Если `distance ≤ 10 units` — переход в EVADING.

### Regen HP

- Каждую секунду: `healAmount = maxHealth × 10% × deltaTime`.
- Broadcast `mobHealthUpdate` с новым HP.
- Регенерация останавливается при достижении `maxHealth` или гибели моба.

### EVADING

- 2 секунды неуязвимости (hardcoded `EVADE_DURATION = 2.0f`).
- Моб не двигается.
- По истечении: → PATROLLING, выбирается свежий waypoint.

---

## 9. Бегство (FLEEING)

**Условие активации**: `mob.fleeHpThreshold > 0.0` **и** `currentHealth / maxHealth ≤ fleeHpThreshold`.

Проверяется в состояниях: CHASING, PREPARING_ATTACK, ATTACKING, ATTACK_COOLDOWN.

**Алгоритм**:
1. Вектор бегства = нормализованный `(mobPos - attackerPos)`.
2. `fleeTargetPosition = mobPos + fleeVector × (aggroRange × chaseMultiplier × 1.1)`.
3. Моб движется к `fleeTargetPosition` по той же логике, что и RETURNING (функция `calculateReturnToSpawnMovement`).
4. Длительность бегства ≤ `chaseDuration / 3` секунд.
5. По истечении или если атакующий мёртв → RETURNING.

**Не активируется** если моб уже в RETURNING / EVADING / FLEEING.

---

## 10. Архетипы AI (melee / caster)

Поле `aiArchetype` в шаблоне моба:

### `melee` (по умолчанию)

- Стандартное поведение: CHASING → стоп в `attackRange` → атака.

### `caster`

Дополнительная логика backpedal (применяется в handlePlayerAggro):
- Если `distance < attackRange × 0.5` → моб **отходит** от цели к позиции `attackRange × 1.8`.
- `isBackpedaling = true`, `fleeTargetPosition` = позиция для отхода.
- Движение — та же функция, что и при FLEEING.
- Когда `distance ≥ attackRange × 0.5` → `isBackpedaling = false`, атака возобновляется.

Пример: маг-моб с `attackRange = 500` будет отходить если игрок подошёл ближе 250 units.

### `ranged`, `support`

Параметры существуют в коде, специфическая логика в текущей версии не отличается от `melee`. Задел для будущих механик.

---

## 11. Социальная агрессия (isSocial)

**Условие**: `mob.isSocial = true` **и** `damage > 0` (реальный удар, не цепное предупреждение).

**Алгоритм** (при `handleMobAttacked`):
1. При получении реального урона моб оповещает соседей в радиусе `mob.aggroRange`.
2. Фильтр: тот же `zoneId`, та же раса (`raceName`), не мёртвые, в состоянии PATROLLING.
3. Каждый подходящий сосед получает вызов `handleMobAttacked(neighborUID, attackerId, 0)` — без `damage`, чтобы не было рекурсии.
4. Соседи перейдут в CHASING при следующем AI-тике (оповещение только обновляет `targetPlayerId`).

---

## 12. Система угрозы (Threat Table)

У каждого моба хранится `threatTable: Map<playerId, threatPoints>`.

**Накопление**:
- При каждом попадании: `threat += max(1, damage)`.
- Пассивные мобы (`isAggressive = false`) также накапливают threat от атакующих.

**Выбор цели**:
- Если у нескольких игроков есть threat — выбирается игрок с наибольшим значением.
- Если threat нет — выбирается ближайший игрок в `aggroRange`.

**Decay (затухание)**:
- Каждые 100 мс: для игроков **вне** `aggroRange` threat умножается на `0.95^(deltaTime×10)` (≈ −50%/сек).
- Когда threat падает до 0 — игрок удаляется из таблицы.

**Сброс**: при переходе в RETURNING / EVADING — `threatTable` и `attackerTimestamps` очищаются.

---

## 13. Melee-слоты (предотвращение толпы)

Физическое ограничение числа мобов вокруг одного игрока в ближнем бою.

**Расчёт максимального числа слотов**:
```
kMobDiameter = mob.radius > 0 ? mob.radius × 2 : 140 units
maxMeleeSlots = max(1, floor(2π × attackRange / kMobDiameter))
```

**Логика**:
1. При переходе CHASING → PREPARING_ATTACK сервер подсчитывает число мобов в состояниях PREPARING_ATTACK / ATTACKING / ATTACK_COOLDOWN в радиусе `attackRange` вокруг цели.
2. Если `occupiedSlots >= maxMeleeSlots`:
   - `waitingForMeleeSlot = true`
   - Моб паркуется на `attackRange + 20 units` от цели — не движется, не атакует.
3. Каждый тик CHASING с `waitingForMeleeSlot = true` повторно проверяет счётчик. Когда слот освобождается — `waitingForMeleeSlot = false`, моб возобновляет движение.

**Пример**: моб с `attackRange = 150`, `radius = 30` → `maxMeleeSlots = floor(2π×150/60) = 15` (практически неограниченно). Моб с `attackRange = 150`, `radius = 70` → `maxMeleeSlots = floor(2π×150/140) ≈ 6`.

---

## 14. Сводная таблица пакетов

| Пакет | Направление | Триггер |
|-------|-------------|---------|
| `spawnMobsInZone` | Сервер → клиенту | Вход в игру (`joinGameCharacter`), новый спавн |
| `zoneMoveMobs` | Сервер → broadcast | Перемещение любого моба в зоне (полный снапшот) |
| `mobMoveUpdate` | Сервер → конкретному клиенту | Периодически (лёгкий пакет позиции/скорости/состояния). Содержит `stepTimestampMs` для RTT-компенсации. |
| `mobDeath` | Сервер → broadcast | Моб умер |
| `mobTargetLost` | Сервер → broadcast | Моб потерял цель (leash или смерть цели) |
| `mobHealthUpdate` | Сервер → broadcast | Leash regen (только в состоянии RETURNING, 1 раз/сек) |
| `combatInitiation` | Сервер → broadcast | Моб начинает атаку (CHASING → PREPARING_ATTACK) |
| `combatResult` | Сервер → broadcast | Результат атаки (отправляется **немедленно** вместе с `combatInitiation`, при входе в PREPARING_ATTACK) |
| `stats_update` | Сервер → атакованному | Обновление HP/Mana игрока после получения урона |

> Подробная документация пакетов `combatInitiation` и `combatResult`: [client-combat-protocol.md](client-combat-protocol.md) раздел 10.  
> Документация пакетов `spawnMobsInZone`, `zoneMoveMobs`, `mobMoveUpdate`, `mobDeath`, `mobTargetLost`, `mobHealthUpdate`: [client-world-systems-protocol.md](client-world-systems-protocol.md) разделы 7.1–7.7.

---

## 15. Руководство для разработчика клиента

### 15.1 Модель движения: extrapolation + position correction

Сервер **не отправляет** позицию на каждый кадр. Клиент должен **экстраполировать** позицию между пакетами:

```
// На каждом кадре:
mob.posX += mob.velocity.dirX * mob.velocity.speed * deltaTime
mob.posY += mob.velocity.dirY * mob.velocity.speed * deltaTime
```

`velocity.speed` — в **position-units/sec**. `dirX`/`dirY` — нормализованный вектор (длина ≤ 1).

**При получении `mobMoveUpdate`**: обновить `velocity` и выполнить мягкую коррекцию позиции (lerp к `posX/posY` из пакета за 2–4 кадра), чтобы не было телепортов.

> **Важно**: сервер отправляет `mobMoveUpdate` только если моб сдвинулся на ≥ **10 units** от последней отправленной позиции (`minimumMoveDistance`).  
> Исключение — смена боевого состояния: тогда пакет отправляется принудительно, даже без движения (`forceNextUpdate = true`).

---

### 15.2 Состояния: когда двигать, когда замораживать

| `combatState` | Клиентское поведение |
|---|---|
| `0` PATROLLING | ⚠️ **НЕ экстраполировать бесконечно.** Lerp к `position` из пакета за `stepSize/speed ≈ 1.0 сек`, затем **остановить velocity = 0**. Ждать следующего пакета. |
| `1` CHASING | Экстраполировать по `velocity`. Пакет каждые **~0.1 сек** (при движении ≥10 units). |
| `2` PREPARING_ATTACK | **Заморозить** (velocity.speed = 0). Показать анимацию каста. Кастбар = `castTime` из `combatInitiation`. **Урон уже применён** (`combatResult` пришёл вместе с `combatInitiation`). |
| `3` ATTACKING | **Заморозить**. Воспроизвести анимацию удара. |
| `4` ATTACK_COOLDOWN | **Заморозить**. Моб стоит. |
| `5` RETURNING | Экстраполировать по `velocity`. Пакет каждые **~0.15 сек**. |
| `6` EVADING | **Заморозить** на 2 сек, затем переход в PATROLLING. |
| `7` FLEEING | Экстраполировать по `velocity` (та же формула что и RETURNING). |

При переходе в состояние 2/3/4 сервер гарантированно присылает `mobMoveUpdate` с `speed = 0` — используйте это как сигнал остановки.

> **Почему патруль — особый случай**: сервер отправляет пакет патруля только когда моб реально делает шаг (раз в 2–6 сек). Нет отдельного пакета «моб остановился». Поэтому правило: анимировать к новой позиции за `stepSize/speed = 1.0 с`, затем держать. Не экстраполировать за это время.

---

### 15.3 Частота пакетов и ожидаемые паузы

| Режим | Частота (сервер) | Пакет придёт, если... | Клиент после пакета |
|---|---|---|---|
| Chase | раз в **0.1 сек** | моб двигался ≥ 10 units | экстраполировать ~0.1 сек |
| Return | раз в **0.15 сек** | моб двигался ≥ 10 units | экстраполировать ~0.15 сек |
| Patrol | раз в **2–6 сек** | моб двигался ≥ 10 units | lerp за 1.0 сек → стоп |
| Fleeing | раз в **0.1 сек** | моб двигался ≥ 10 units | экстраполировать ~0.1 сек |
| Смена состояния | **немедленно** | всегда (force update) | заморозить / разморозить |

При смене состояния пакет приходит даже если моб не двигался — `posX/posY` будут текущими координатами.

> **Финальный подход к attackRange**: последние ≤50 units перед атакой не сопровождаются отдельным `mobMoveUpdate`. `combatInitiation` приходит когда моб уже в `attackRange`. Клиент должен быть готов к тому, что `combatInitiation` может прийти без предшествующего пакета движения.

---

### 15.4 Инициализация при входе в зону

При входе игрока сервер присылает `spawnMobsInZone` — снапшот всех мобов с их текущими позициями, состоянием и `velocity`. Алгоритм инициализации:

1. Создать все мобы из пакета с указанными `posX`/`posY` и `combatState`.
2. Если `combatState` ∈ {`2`, `3`, `4`, `6`} — установить `velocity = (0, 0, 0)`, заморозить.
3. Начать экстраполяцию с момента создания.

---

### 15.5 Тайм-лайн атаки: что показывать клиенту

```
Время:  0ms               500ms     500ms   800ms      1300ms
        │                 │         │       │          │
        ├─────────────────┼─────────┼───────┼──────────┤
Server: combatInit        combatRes state=3 state=4    state=1 (resume chase)
        state=2           (урон!)  (swing) (cd=0.5s)
        (castTime=0.5s)
        ├─────────────────┤
Client: кастбар           HP бар↓  удар    cooldown   бег
        (0.5 сек)         (сразу)  анимация
```

- `combatInitiation` и `combatResult` приходят одновременно при входе в PREPARING_ATTACK.
- `combatInitiation.skillInitiation.castTime` — длина кастбара; урон уже применён на сервере, клиент может применить к HP-бару немедленно либо в хит-фрейм анимации.
- После `state = 4` (ATTACK_COOLDOWN) клиент ждёт следующего `combatState = 1` + `velocity ≠ 0` — это сигнал возобновления погони.

---

### 15.6 Типичные ошибки реализации

| Ошибка | Последствие | Правильное решение |
|---|---|---|
| Ожидать отдельный `combatResult` после каста | HP бар не обновляется | `combatResult` приходит **вместе с `combatInitiation`** (при входе в PREPARING_ATTACK) |
| Двигать моба в состояниях 2/3/4 | Моб "едет" сквозь игрока во время удара | Заморозить при `combatState` ∈ {2, 3, 4, 6} |
| Телепортировать моба при коррекции позиции | Дёрганое движение каждые 0.3 сек | Lerp к целевой позиции за 2–4 кадра |
| Не экстраполировать между пакетами | Моб движется рывками | Extrapolation по `velocity × deltaTime` |
| Не учитывать `minimumMoveDistance` | Пакетов нет при медленном движении — считать моба стоящим | Экстраполировать даже без новых пакетов |
| Экстраполировать бесконечно при `combatState=0` | Моб «улетает» при `speed=stepSize` на десятки секунд | Lerp за 1.0 сек → velocity = 0 → ждать следующего пакета |
| Ожидать пакет движения прямо перед `combatInitiation` | Последние ≤50 units подхода не отправляются | `combatInitiation` может прийти без предшествующего `mobMoveUpdate` |

---

## 16. Полные примеры JSON-пакетов

Все пакеты используют общую обёртку:
```json
{
  "header": { "eventType": "...", "status": "success", "timestamp": "...", "version": "1.0", ... },
  "body": { ... }
}
```

---

### 16.1 `spawnMobsInZone` — снапшот мобов при входе в зону

**Направление**: Сервер → конкретному клиенту  
**Триггер**: `joinGameCharacter`, либо сервер-пуш при коннекте

```json
{
  "header": {
    "message": "Spawning mobs success!",
    "hash": "",
    "clientId": 5,
    "eventType": "spawnMobsInZone",
    "status": "success",
    "timestamp": "2026-03-20T14:35:22.123Z",
    "version": "1.0"
  },
  "body": {
    "spawnZone": {
      "id": 1,
      "name": "Forest Zone",
      "shape": "RECT",
      "bounds": {
        "minX": 0.0, "maxX": 1000.0,
        "minY": 0.0, "maxY": 1000.0,
        "minZ": 0.0, "maxZ": 0.0
      },
      "center": { "x": 500.0, "y": 500.0 },
      "innerRadius": 0.0,
      "outerRadius": 0.0,
      "mobEntries": [
        { "mobId": 2, "maxCount": 5 }
      ],
      "totalSpawnCount": 5,
      "spawnedMobsCount": 2,
      "respawnTimeSec": 30,
      "spawnEnabled": true
    },
    "mobs": [
      {
        "id": 2,
        "uid": 1001,
        "zoneId": 1,
        "name": "Grey Wolf",
        "slug": "grey-wolf",
        "race": "wolf",
        "level": 3,
        "isAggressive": true,
        "isDead": false,
        "stats": {
          "health": { "current": 200, "max": 200 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 350.0, "y": 280.0, "z": 0.0, "rotationZ": 45.0 },
        "velocity": { "dirX": 0.707, "dirY": 0.707, "speed": 0.0 },
        "combatState": 0,
        "attributes": [
          { "id": 1, "name": "Strength",  "slug": "strength",  "value": 12 },
          { "id": 5, "name": "Attack",    "slug": "attack",    "value": 18 },
          { "id": 6, "name": "Defense",   "slug": "defense",   "value": 8  }
        ]
      },
      {
        "id": 2,
        "uid": 1002,
        "zoneId": 1,
        "name": "Grey Wolf",
        "slug": "grey-wolf",
        "race": "wolf",
        "level": 3,
        "isAggressive": true,
        "isDead": false,
        "stats": {
          "health": { "current": 145, "max": 200 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 520.0, "y": 410.0, "z": 0.0, "rotationZ": 180.0 },
        "velocity": { "dirX": -0.6, "dirY": 0.8, "speed": 420.0 },
        "combatState": 1,
        "attributes": [
          { "id": 1, "name": "Strength", "slug": "strength", "value": 12 },
          { "id": 5, "name": "Attack",   "slug": "attack",   "value": 18 },
          { "id": 6, "name": "Defense",  "slug": "defense",  "value": 8  }
        ]
      }
    ]
  }
}
```

> **Поля velocity при входе**: если `combatState` ∈ {2,3,4,6} — `speed` будет `0.0`. Клиент должен заморозить такого моба на указанной позиции и ждать `mobMoveUpdate`.

---

### 16.2 `zoneMoveMobs` — полный broadcast при движении

**Направление**: Сервер → broadcast (всем клиентам зоны)  
**Триггер**: плановый тик зоны (legacy) или при движении группы мобов  
**Примечание**: содержит те же поля что и `spawnMobsInZone`, но без `spawnZone`

```json
{
  "header": {
    "message": "Moving mobs success!",
    "hash": "",
    "clientId": 5,
    "eventType": "zoneMoveMobs",
    "status": "success",
    "timestamp": "2026-03-20T14:35:22.456Z",
    "version": "1.0"
  },
  "body": {
    "mobs": [
      {
        "id": 2,
        "uid": 1001,
        "zoneId": 1,
        "name": "Grey Wolf",
        "slug": "grey-wolf",
        "race": "wolf",
        "level": 3,
        "isAggressive": true,
        "isDead": false,
        "stats": {
          "health": { "current": 200, "max": 200 },
          "mana":   { "current": 0,   "max": 0   }
        },
        "position": { "x": 375.0, "y": 297.0, "z": 0.0, "rotationZ": 45.0 },
        "velocity": { "dirX": 0.707, "dirY": 0.707, "speed": 350.0 },
        "combatState": 0,
        "attributes": [
          { "id": 1, "name": "Strength", "slug": "strength", "value": 12 }
        ]
      }
    ]
  }
}
```

---

### 16.3 `mobMoveUpdate` — лёгкий пакет позиции (основной)

**Направление**: Сервер → конкретному клиенту (не broadcast)  
**Триггер**: моб сдвинулся ≥ 50 units ИЛИ сменил `combatState` (force update)  
**Частота**: ~3–4 раз/сек при chase, ~6–7 раз/сек при returning  
**Содержит**: только движение и состояние, без `stats`/`attributes`

#### При патруле (combatState = 0)
```json
{
  "header": {
    "message": "Mob movement update",
    "hash": "",
    "clientId": 5,
    "eventType": "mobMoveUpdate",
    "status": "success",
    "timestamp": "2026-03-20T14:35:22.789Z",
    "version": "1.0",
    "serverSendMs": 1742477722789
  },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 390.0, "y": 310.0, "z": 0.0, "rotationZ": 45.0 },
        "velocity": { "dirX": 0.6, "dirY": 0.8, "speed": 350.0 },
        "combatState": 0,
        "stepTimestampMs": 1742477722789
      }
    ]
  }
}
```

> **Поле `stepTimestampMs`**: Unix-миллисекунды момента, когда сервер вычислил этот шаг. Клиент может использовать его для RTT-компенсации: вычесть из текущего времени чтобы получить сколько мс назад был сделан шаг, и применить соответствующую экстраполяцию с учётом задержки.

#### При преследовании (combatState = 1)
```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477722789, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 450.0, "y": 380.0, "z": 0.0, "rotationZ": 90.0 },
        "velocity": { "dirX": 0.0, "dirY": 1.0, "speed": 450.0 },
        "combatState": 1,
        "stepTimestampMs": 1742477722789
      }
    ]
  }
}
```

#### Моб начинает атаку — PREPARING_ATTACK (combatState = 2)

Этот пакет приходит вместе с `combatInitiation` **и** `combatResult` — все три отправляются одновременно при входе в PREPARING_ATTACK.

```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477723100, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 480.0, "y": 415.0, "z": 0.0, "rotationZ": 90.0 },
        "velocity": { "dirX": 0.0, "dirY": 0.0, "speed": 0.0 },
        "combatState": 2,
        "stepTimestampMs": 1742477723100
      }
    ]
  }
}
```

#### Анимация удара — ATTACKING (combatState = 3)
```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477724100, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 480.0, "y": 415.0, "z": 0.0, "rotationZ": 90.0 },
        "velocity": { "dirX": 0.0, "dirY": 0.0, "speed": 0.0 },
        "combatState": 3,
        "stepTimestampMs": 1742477724100
      }
    ]
  }
}
```

#### Кулдаун — ATTACK_COOLDOWN (combatState = 4)
```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477724400, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 480.0, "y": 415.0, "z": 0.0, "rotationZ": 90.0 },
        "velocity": { "dirX": 0.0, "dirY": 0.0, "speed": 0.0 },
        "combatState": 4,
        "stepTimestampMs": 1742477724400
      }
    ]
  }
}
```

#### Возврат к спавну — RETURNING (combatState = 5)
```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477725000, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 440.0, "y": 370.0, "z": 0.0, "rotationZ": 270.0 },
        "velocity": { "dirX": -0.707, "dirY": -0.707, "speed": 200.0 },
        "combatState": 5,
        "stepTimestampMs": 1742477725000
      }
    ]
  }
}
```

> `speed` при RETURNING: `returnSpeedUnitsPerSec = 200 units/sec` (настраивается в `MobAIConfig`)

#### Evading — EVADING (combatState = 6)
```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477726000, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 350.0, "y": 280.0, "z": 0.0, "rotationZ": 0.0 },
        "velocity": { "dirX": 0.0, "dirY": 0.0, "speed": 0.0 },
        "combatState": 6,
        "stepTimestampMs": 1742477726000
      }
    ]
  }
}
```

#### Бегство — FLEEING (combatState = 7)
```json
{
  "header": { "eventType": "mobMoveUpdate", "status": "success", "serverSendMs": 1742477724800, "..." : "..." },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": { "x": 460.0, "y": 390.0, "z": 0.0, "rotationZ": 270.0 },
        "velocity": { "dirX": -0.8, "dirY": -0.6, "speed": 200.0 },
        "combatState": 7,
        "stepTimestampMs": 1742477724800
      }
    ]
  }
}
```

---

### 16.4 `mobTargetLost` — моб потерял цель

**Направление**: Сервер → broadcast  
**Триггер**: leash (цель слишком далека/timeout) или смерть цели

```json
{
  "header": {
    "message": "Mob lost target",
    "hash": "",
    "eventType": "mobTargetLost",
    "status": "success",
    "timestamp": "2026-03-20T14:35:25.000Z",
    "version": "1.0"
  },
  "body": {
    "mobUID": 1001,
    "mobId": 2,
    "lostTargetPlayerId": 7,
    "positionX": 480.0,
    "positionY": 415.0,
    "positionZ": 0.0,
    "rotationZ": 90.0
  }
}
```

> После этого пакета клиент должен ожидать `mobMoveUpdate` с `combatState: 5` (RETURNING).  
> `positionX/Y/Z` — позиция моба **в момент потери цели** (для коррекции рассинхрона).

---

### 16.5 `mobHealthUpdate` — реген HP при возврате

**Направление**: Сервер → broadcast  
**Триггер**: 1 раз в секунду пока `combatState = 5` (RETURNING), HP < max

```json
{
  "header": {
    "message": "Mob health updated",
    "hash": "",
    "eventType": "mobHealthUpdate",
    "status": "success",
    "timestamp": "2026-03-20T14:35:26.000Z",
    "version": "1.0"
  },
  "body": {
    "mobUID": 1001,
    "mobId": 2,
    "currentHealth": 160,
    "maxHealth": 200
  }
}
```

> Реген: `+10% maxHealth/сек`. Для волка (maxHealth=200): `+20 HP/сек`.  
> Пакет не приходит если HP уже максимально.

---

### 16.6 `mobDeath` — смерть моба

**Направление**: Сервер → broadcast  
**Триггер**: `currentHealth ≤ 0` (из `combatResult` с `targetDied: true`)

```json
{
  "header": {
    "message": "Mob died",
    "hash": "",
    "eventType": "mobDeath",
    "status": "success",
    "timestamp": "2026-03-20T14:35:28.000Z",
    "version": "1.0"
  },
  "body": {
    "mobUID": 1001,
    "zoneId": 1
  }
}
```

> После `mobDeath` клиент удаляет объект моба. Воспроизвести анимацию смерти можно по `combatResult.targetDied: true`, который приходит **раньше** `mobDeath`.

---

### 16.7 Сводная схема порядка пакетов при атаке

```
t=0ms   [combatInitiation]  casterId=1001, targetId=7, castTime=1.0
t=0ms   [combatResult]      casterId=1001, targetId=7, damage=35, finalTargetHealth=165
t=0ms   [stats_update]      → только игроку 7: currentHealth=165
t=0ms   [mobMoveUpdate]     uid=1001, combatState=2, velocity={0,0,0}

t=1000ms [mobMoveUpdate]    uid=1001, combatState=3, velocity={0,0,0}
t=1000ms [mobMoveUpdate]    uid=1001, combatState=4, velocity={0,0,0}  (swingMs=0)

t=1500ms [mobMoveUpdate]    uid=1001, combatState=1, velocity={dirX,dirY,speed}
                             ← cooldown 0.5s истёк, новый chase
```
