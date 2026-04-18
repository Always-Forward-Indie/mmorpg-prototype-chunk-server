# 03. Мобы и AI

## Обзор

Мобы управляются полностью серверной логикой. Клиент получает данные спавна, обновления позиций и событий смерти. Интерполяция движения выполняется клиентом на основе `position`, `velocity` и `stepTimestampMs`.

---

## 3.1. spawnMobsInZone — Данные мобов зоны

Отправляется в следующих случаях:
- **При `joinGameCharacter`** — unicast новому клиенту, все зоны с их мобами
- **Respawn-задача (каждые 30 сек)** — всем подключённым клиентам, когда в зоне появились новые мобы
- **Safety spawn-задача (каждые 5 мин)** — всем подключённым клиентам, защита от нехватки мобов

### Сервер → Unicast

```json
{
  "header": {
    "message": "Spawning mobs success!",
    "hash": "",
    "clientId": 42,
    "eventType": "spawnMobsInZone"
  },
  "body": {
    "spawnZone": {
      "id": 1,
      "name": "Forest Clearing",
      "shape": "RECT",
      "bounds": {
        "minX": 0.0,
        "maxX": 2000.0,
        "minY": 0.0,
        "maxY": 2000.0,
        "minZ": 0.0,
        "maxZ": 100.0
      },
      "center": { "x": 1000.0, "y": 1000.0 },
      "innerRadius": 0.0,
      "outerRadius": 0.0,
      "mobEntries": [
        { "mobId": 5, "maxCount": 10 }
      ],
      "totalSpawnCount": 10,
      "spawnedMobsCount": 8,
      "respawnTimeSec": 30,
      "spawnEnabled": true
    },
    "mobs": [
      {
        "id": 5,
        "uid": 1001,
        "zoneId": 1,
        "name": "Forest Wolf",
        "slug": "forest_wolf",
        "race": "Beast",
        "level": 3,
        "isAggressive": true,
        "isDead": false,
        "stats": {
          "health": { "current": 120, "max": 120 },
          "mana": { "current": 0, "max": 0 }
        },
        "position": {
          "x": 450.0,
          "y": 320.0,
          "z": 0.0,
          "rotationZ": 2.1
        },
        "velocity": {
          "dirX": 0.7,
          "dirY": 0.7,
          "speed": 100.0
        },
        "attributes": [
          { "id": 1, "name": "Physical Attack", "slug": "physical_attack", "value": 15 },
          { "id": 2, "name": "Physical Defense", "slug": "physical_defense", "value": 8 }
        ],
        "combatState": 0
      }
    ]
  }
}
```

### Поля моба

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | Шаблонный ID моба (mob_templates) |
| `uid` | int | Уникальный ID экземпляра |
| `zoneId` | int | ID зоны спавна |
| `name` | string | Отображаемое имя |
| `slug` | string | Уникальный идентификатор (для бестиария/локализации) |
| `race` | string | Раса моба |
| `level` | int | Уровень |
| `isAggressive` | bool | Агрессивный ли |
| `isDead` | bool | Мёртв ли |
| `stats.health.current/max` | int | Текущее/максимальное HP |
| `stats.mana.current/max` | int | Текущая/максимальная мана |
| `position` | object | Текущая позиция (x, y, z, rotationZ) |
| `velocity.dirX` | float | Нормализованное направление X |
| `velocity.dirY` | float | Нормализованное направление Y |
| `velocity.speed` | float | Скорость (единиц/сек) |
| `attributes` | array | Массив атрибутов |
| `combatState` | int | Состояние AI (см. ниже) |

### Поля зоны спавна

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | ID зоны |
| `name` | string | Имя зоны |
| `shape` | string | Форма зоны: `"RECT"` / `"CIRCLE"` / `"ANNULUS"` |
| `bounds` | object | AABB границы (minX, maxX, minY, maxY, minZ, maxZ). Для RECT — фактические границы; для CIRCLE/ANNULUS — описывающий прямоугольник |
| `center` | object | Центр зоны `{x, y}`. Для CIRCLE/ANNULUS — реальный центр; для RECT — геометрический центр AABB |
| `innerRadius` | float | Внутренний радиус (только ANNULUS). Спавн запрещён ближе этого радиуса к центру |
| `outerRadius` | float | Внешний радиус (CIRCLE и ANNULUS) |
| `mobEntries` | array | Конфигурация мобов зоны (один элемент = одна строка spawn_zone_mobs) |
| `mobEntries[].mobId` | int | Шаблонный ID моба |
| `mobEntries[].maxCount` | int | Макс. число мобов этого типа в зоне |
| `totalSpawnCount` | int | Суммарный лимит мобов (сумма `mobEntries[].maxCount`) |
| `spawnedMobsCount` | int | Текущее число живых мобов всех типов |
| `respawnTimeSec` | int | Минимальный интервал респавна среди всех типов (сек) |
| `spawnEnabled` | bool | Спавн активен |

#### Формы зон (shape)

| shape | Границы | Алгоритм выбора позиции |
|-------|---------|------------------------|
| `RECT` | `bounds.minX/maxX/minY/maxY` | Равномерный случайный в прямоугольнике |
| `CIRCLE` | `center` + `outerRadius` | `r = R·√u`, `θ = uniform [0, 2π)` |
| `ANNULUS` | `center` + `innerRadius` + `outerRadius` | `r = √(r_in² + u·(r_out² - r_in²))`, `θ = uniform [0, 2π)` — кольцевая зона вокруг деревни/ориентира |

---

## 3.2. mobMoveUpdate — Обновление позиции мобов

Пакетируется — один пакет содержит обновления всех мобов, у которых сработал rate limit в данном тике.

**Частота отправки (server-side rate limiting per mob):**
- Планировщик тикает каждые **50 мс**
- `PATROLLING` → не чаще **200 мс** между пакетами (клиент использует waypoint dead reckoning)
- Боевые состояния (CHASING, PREPARING_ATTACK, ATTACKING, ATTACK_COOLDOWN, RETURNING, FLEEING) → не чаще **100 мс**
- При смене состояния (`forceNextUpdate = true`) — **немедленно**, rate limit обходится
- Дополнительный фильтр: если моб сдвинулся менее чем на `minimumMoveDistance` (10 ед.) — пакет не отправляется

### Сервер → Unicast

```json
{
  "header": {
    "message": "Mob movement update",
    "hash": "",
    "clientId": 42,
    "eventType": "mobMoveUpdate",
    "serverSendMs": 1711709400500,
    "status": "success",
    "timestamp": "2026-03-21 15:07:43.790",
    "version": "1.0"
  },
  "body": {
    "mobs": [
      {
        "uid": 1001,
        "zoneId": 1,
        "position": {
          "x": 455.0,
          "y": 325.0,
          "z": 0.0,
          "rotationZ": 2.1
        },
        "velocity": {
          "dirX": 0.7,
          "dirY": 0.7,
          "speed": 100.0
        },
        "combatState": 0,
        "stepTimestampMs": 1711709400495,
        "waypoint": {
          "x": 500.0,
          "y": 400.0
        }
      }
    ]
  }
}
```

### Поля обновления движения

| Поле | Тип | Описание |
|------|-----|----------|
| `uid` | int | UID моба |
| `zoneId` | int | ID зоны |
| `position` | object | Текущая серверная позиция |
| `velocity.dirX` | float | Нормализованное направление X |
| `velocity.dirY` | float | Нормализованное направление Y |
| `velocity.speed` | float | Скорость (ед./сек) для dead reckoning на клиенте |
| `combatState` | int | Текущее AI-состояние |
| `stepTimestampMs` | int64 | Unix ms момента вычисления шага |
| `waypoint` | object | Целевая точка патрулирования (только при `combatState = 0`) |

### Интерполяция на клиенте

Поведение клиента зависит от значения `combatState` в пакете.

#### PATROLLING (combatState = 0) — waypoint dead reckoning

Если поле `waypoint` присутствует, клиент двигает моба **к точке waypoint** со скоростью `velocity.speed` ед./сек:

```
direction = normalize(waypoint - currentLocalPos)
currentLocalPos += direction × velocity.speed × deltaTime
```

Когда моб достигает `waypoint` до прихода следующего пакета — остановить его на месте (не перелетать точку). Следующий пакет придёт через ≤ 200 мс и задаст новую цель патруля.

Если поле `waypoint` отсутствует — использовать linear extrapolation через `dirX`/`dirY` (аналогично боевым состояниям).

#### CHASING / RETURNING / FLEEING (combatState = 1, 5, 7) — linear extrapolation

```
deltaTime  = (currentClientMs - stepTimestampMs) / 1000.0
predictedX = position.x + velocity.dirX × velocity.speed × deltaTime
predictedY = position.y + velocity.dirY × velocity.speed × deltaTime
```

Сервер присылает обновление каждые **100 мс**. Ограничить экстраполяцию максимум **200 мс** от `stepTimestampMs` — если за это время не пришёл новый пакет, остановить моба и ждать.

При получении нового пакета: резкий сдвиг позиции **недопустим** — применить blend (lerp / Hermite) от текущей предсказанной позиции к серверной за **100–150 мс**.

#### PREPARING_ATTACK / ATTACKING / ATTACK_COOLDOWN (combatState = 2, 3, 4) — нет движения

Сервер обнуляет `velocity.speed = 0`, `dirX = 0`, `dirY = 0` при входе в `PREPARING_ATTACK`. Клиент **не двигает моба** в этих состояниях — только воспроизводит анимацию каста/удара.

#### Смена состояния — немедленный snap + blend

При изменении `combatState` сервер отправляет пакет немедленно, минуя rate limit. Клиент должен:
1. Принять `position` из пакета как авторитетную серверную позицию
2. Сразу сменить анимацию в соответствии с новым `combatState`
3. Применить позицию через blend за **100–150 мс**, чтобы избежать телепортации

> `rotationZ` в пакете — значение в **градусах**, вычисленное на сервере как `atan2(dirY, dirX) × (180/π)`. При входе в `PREPARING_ATTACK` повёрнут к цели.

---

## 3.3. mobDeath — Смерть моба

### Сервер → Broadcast

```json
{
  "header": {
    "message": "Mob died",
    "hash": "",
    "eventType": "mobDeath"
  },
  "body": {
    "mobUID": 1001,
    "zoneId": 1
  }
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `mobUID` | int | UID убитого моба |
| `zoneId` | int | ID зоны |

**Серверный пайплайн при убийстве моба:**
1. Начисление XP (`experienceUpdate` → убийце)
2. Fellowship bonus (7% бонус XP со-атакующим)
3. Обновление Item Soul (kill count на оружии)
4. Квестовый триггер (`onMobKilled`)
5. Обновление бестиария (`bestiary_kill_update`)
6. Champion tracking (счётчик убийств в зоне)
7. Репутация (`repDeltaPerKill`)
8. Создание трупа (HarvestableCorpseStruct)
9. Генерация лута (DroppedItemStruct[])
10. Респавн по таймеру зоны

---

## 3.4. mobHealthUpdate — Обновление HP моба

### Сервер → Broadcast

```json
{
  "header": {
    "message": "Mob health updated",
    "hash": "",
    "eventType": "mobHealthUpdate"
  },
  "body": {
    "mobUID": 1001,
    "mobId": 5,
    "currentHealth": 85,
    "maxHealth": 120
  }
}
```

---

## 3.5. mobTargetLost — Моб потерял цель

Отправляется когда моб переходит в состояние RETURNING.

### Сервер → Broadcast

```json
{
  "header": {
    "message": "Mob lost target",
    "hash": "",
    "eventType": "mobTargetLost"
  },
  "body": {
    "mobUID": 1001,
    "mobId": 5,
    "lostTargetPlayerId": 7,
    "positionX": 455.0,
    "positionY": 325.0,
    "positionZ": 0.0,
    "rotationZ": 2.1
  }
}
```

---

## AI-состояния мобов (MobCombatState)

| Код | Состояние | Описание |
|-----|-----------|----------|
| 0 | `PATROLLING` | Патрулирование (случайные waypoints в зоне) |
| 1 | `CHASING` | Преследование цели |
| 2 | `PREPARING_ATTACK` | Подготовка к атаке |
| 3 | `ATTACKING` | Выполнение атаки |
| 4 | `ATTACK_COOLDOWN` | Кулдаун после атаки |
| 5 | `RETURNING` | Возврат к точке спавна |
| 6 | `EVADING` | Уклонение |
| 7 | `FLEEING` | Бегство (при низком HP) |

```
PATROLLING → [игрок в aggroRange] → CHASING
CHASING → [в attackRange, слот свободен] → PREPARING_ATTACK  ← урон рассчитывается и применяется здесь
PREPARING_ATTACK → [castTime вышло] → ATTACKING             (фаза визуальной анимации каста)
ATTACKING → [swingTime вышло] → ATTACK_COOLDOWN             (фаза визуальной анимации удара)
ATTACK_COOLDOWN → [cooldownTime вышло] → CHASING
CHASING → [цель далеко / потеряна / таймаут chaseDuration] → RETURNING
RETURNING → [достиг спавна] → EVADING → PATROLLING
CHASING / PREPARING_ATTACK / ATTACKING → [HP < fleeHpThreshold] → FLEEING (если порог > 0)
```

> **Важно:** урон применяется в момент перехода `CHASING → PREPARING_ATTACK`, одновременно с отправкой `combatInitiation` и `combatResult` клиенту. Состояния `PREPARING_ATTACK` и `ATTACKING` — чисто визуальные фазы (cast-анимация и swing-анимация соответственно).

### Рендерер на клиенте

| combatState | Визуальное поведение |
|-------------|---------------------|
| 0 | Спокойное перемещение, idle-анимация |
| 1 | Быстрое движение к цели, alert-анимация |
| 2 | Стойка атаки, подготовительная анимация |
| 3 | Анимация атаки |
| 4 | Стойка, ожидание |
| 5 | Движение к точке спавна, деаггр |
| 6 | Уклоняющееся движение |
| 7 | Бегство от цели, panic-анимация |

---

## Архетипы AI мобов (MobArchetype)

| Код | Архетип | Поведение |
|-----|---------|-----------|
| 0 | `MELEE` | Бежит в ближний бой, использует физ. скиллы |
| 1 | `CASTER` | Держит дистанцию, использует заклинания |
| 2 | `RANGED` | Держит дистанцию, использует дальнобойные атаки |
| 3 | `SUPPORT` | Хилит/баффит союзников |

### Caster — особое поведение (backpedal)

Если архетип моба `CASTER` и расстояние до цели **меньше `attackRange × 0.5`**, включается режим отступления: моб движется прочь от игрока до расстояния `attackRange × 1.8`, затем возобновляет атаку. На клиенте это выглядит как `combatState = 1` (CHASING), но в противоположном направлении — отдельная анимация не нужна, достаточно alert-стойки.

### Условия прекращения преследования

Сервер прекращает преследование при выполнении **любого** из трёх условий. При каждом из них отправляется `mobTargetLost` и моб переходит в `RETURNING`:

| Условие | Порог (по умолчанию) |
|---------|---------------------|
| Расстояние до цели > `aggroRange × chaseMultiplier` | ≈ 800 ед. ||
| Дистанция выхода за край спавн-зоны > `maxChaseFromZoneEdge` | 1500 ед. |
| Время преследования > `chaseDuration` (per-mob) | 30 сек |

### Melee slot queuing (crowding prevention)

Чтобы предотвратить скучивание мобов на одном игроке, сервер ограничивает число атакующих физической ёмкостью кольца вокруг цели:

```
maxSlots = floor(2π × attackRange / mobDiameter)
// пример при дефолтах: 2π × 150 / 140 ≈ 6 слотов
```

Лишние мобы паркуются на расстоянии `attackRange + 20` с флагом `waitingForMeleeSlot` и остаются в `CHASING` (combatState = 1), но с `velocity.speed = 0`. Отдельного пакета о переходе не приходит — это нормально. Для клиента: `combatState = 1` при `speed = 0` означает alert-стойку стоя (не idle, не движение).

---

## Параметры AI мобов (MobAIConfig)

| Параметр | По умолчанию | Описание |
|----------|-------------|----------|
| `aggroRange` | 400.0 | Радиус обнаружения цели (ед.) |
| `maxChaseDistance` | 800.0 | = `aggroRange × chaseDistanceMultiplier` |
| `returnToSpawnZoneDistance` | 1000.0 | Дистанция от границы зоны для возврата |
| `newTargetZoneDistance` | 150.0 | Зона сужения поиска новых целей у края зоны |
| `maxChaseFromZoneEdge` | 1500.0 | Макс. дистанция выхода за край зоны при преследовании |
| `attackRange` | 150.0 | Дистанция начала атаки (ед.) |
| `attackCooldown` | 2.0 | Кулдаун атаки (сек) |
| `chaseDistanceMultiplier` | 2.0 | Множитель: `maxChaseDistance = aggroRange × mult` |
| `chaseMovementInterval` | 0.1 (100 мс) | Интервал шага при CHASING / FLEEING |
| `returnMovementInterval` | 0.15 (150 мс) | Интервал шага при RETURNING |
| `chaseSpeedUnitsPerSec` | 450.0 | Скорость преследования по умолчанию (ед./сек) |
| `returnSpeedUnitsPerSec` | 200.0 | Скорость возврата к спавну (ед./сек) |
| `minimumMoveDistance` | 10.0 | Мин. дистанция сдвига для отправки пакета (ед.) |

**Скорость моба при преследовании** берётся из атрибута `move_speed` шаблона моба:

```
chaseSpeed = move_speed_attribute_value × 40.0
```

Если атрибут не задан — используется `chaseSpeedUnitsPerSec` (дефолт 450 ед./сек). Это значение передаётся клиенту как `velocity.speed` в каждом `mobMoveUpdate` и используется для dead reckoning.

**Размер одного шага за тик** (вычисляется на сервере, не передаётся клиенту напрямую):

```
stepSize = chaseSpeed × chaseMovementInterval    // 450 × 0.1 = 45 ед./тик
stepSize = min(stepSize, 450.0)                  // абсолютный cap
stepSize = min(stepSize, distance − attackRange) // без перелёта в цель
```

---

## Данные моба (полная структура MobDataStruct)

| Поле | Тип | Описание |
|------|-----|----------|
| `id` | int | Шаблонный ID |
| `uid` | int | Уникальный ID экземпляра |
| `zoneId` | int | ID зоны спавна |
| `name` | string | Имя моба |
| `slug` | string | Slug для локализации |
| `raceName` | string | Раса |
| `level` | int | Уровень |
| `currentHealth` | int | Текущее HP |
| `currentMana` | int | Текущая мана |
| `maxHealth` | int | Максимальное HP |
| `maxMana` | int | Максимальная мана |
| `baseExperience` | int | Базовый XP за убийство |
| `radius` | int | Радиус коллизии |
| `isAggressive` | bool | Агрессивность |
| `aggroRange` | float | Радиус агро |
| `attackRange` | float | Дальность атаки |
| `attackCooldown` | float | Кулдаун атаки (сек) |
| `chaseMultiplier` | float | Множитель преследования |
| `patrolSpeed` | float | Множитель скорости патруля |
| `isSocial` | bool | Групповой агро |
| `chaseDuration` | float | Макс. время преследования (сек) |
| `rankId` | int | Ранг моба |
| `rankCode` | string | `"normal"`, `"elite"`, `"boss"` |
| `rankMult` | float | Множитель XP |
| `fleeHpThreshold` | float | Порог бегства (0.0-1.0). `0` = не бежит |
| `aiArchetype` | string | `"melee"`, `"caster"`, `"ranged"`, `"support"` |
| `isChampion` | bool | Чемпион |
| `canEvolve` | bool | Может эволюционировать |
| `lootMultiplier` | float | Множитель лута |
| `isRare` | bool | Редкий моб |
| `rareSpawnChance` | float | Шанс появления редкого [0..1] |
| `rareSpawnCondition` | string | `"night"`, `"day"`, `"zone_event"` |
| `factionSlug` | string | Фракция |
| `repDeltaPerKill` | int | Дельта репутации за убийство |
| `biomeSlug` | string | Биом (напр. `"forest"`, `"cave"`) |
| `mobTypeSlug` | string | Тип (напр. `"beast"`, `"undead"`, `"humanoid"`) |
| `hpMin` | int | Мин. наблюдаемое HP (для бестиария Tier 1) |
| `hpMax` | int | Макс. наблюдаемое HP (для бестиария Tier 1) |

---

## Threat Table (серверная)

Мобы используют таблицу угрозы для выбора цели:

| Событие | Изменение угрозы |
|---------|------------------|
| Урон | `+ damage dealt` |
| Хил союзника | `+ healing / 2` |
| Decay (игрок вне `aggroRange`) | `× 0.95` каждые 100 мс (≈ −50% в сек) |

Моб атакует игрока с **наивысшей угрозой** в таблице. Фолбэк — ближайший игрок в `aggroRange`. Таблица угрозы сбрасывается при переходе в `RETURNING`.

### Выбор цели при обнаружении

При поиске новой цели (`PATROLLING` → `CHASING`) сервер:
1. Получает всех игроков в радиусе `aggroRange`
2. Выбирает игрока с **наибольшей угрозой** в таблице (если есть)
3. Фолбэк — **ближайший** живой игрок

Дополнительное условие: моб не ищет новые цели, если он находится у края своей спавн-зоны (в пределах `newTargetZoneDistance = 150 ед.` от границы).

### Социальное агро (`isSocial: true`)

При получении урона мобом с `isSocial = true`, сервер мгновенно оповещает всех соседей в радиусе `aggroRange` с **тем же `raceName`**, которые находятся в состоянии `PATROLLING`. Они переходят в `CHASING` с тем же целевым игроком. Распространение **не каскадируется** (соседи получают уведомление без дальнейшей цепочки).

Для клиента: возможна одновременная смена `combatState = 1` у группы мобов одного типа. Каждый получит отдельный `mobMoveUpdate` пакет с `forceNextUpdate` (пакет вне rate limit).

---

## Чемпионы (TimedChampionTemplate)

Чемпионы — усиленные мобы, появляющиеся по расписанию или при достижении порога убийств в зоне.

| Поле | Тип | Описание |
|------|-----|----------|
| `slug` | string | ID чемпиона |
| `gameZoneId` | int | Зона появления |
| `mobTemplateId` | int | Шаблон моба |
| `intervalHours` | int | Интервал респавна (часы) |
| `windowMinutes` | int | Длительность присутствия (мин) |
| `announceKey` | string | Ключ для анонса (world_notification) |

При появлении чемпиона — `world_notification` всем игрокам зоны.
