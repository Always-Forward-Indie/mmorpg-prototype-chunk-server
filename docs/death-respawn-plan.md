# Death & Respawn System — Plan

## Обзор

Система смерти и возрождения состоит из четырёх взаимосвязанных частей:

1. **Штрафы за смерть** (частично реализованы)
2. **Блокировка действий мёртвого игрока** (не реализовано)
3. **Воскрешение** — respawn в ближайшей respawn-зоне (не реализовано)
4. **Валидация скорости движения на сервере** (не реализовано)

---

## Часть 1 — Штрафы за смерть

### 1.1 Уже реализовано

- **XP penalty**: `-10%` от текущего опыта на уровне (`DEATH_PENALTY_PERCENT = 0.1`)
  - Защита: нельзя уйти ниже порога начала текущего уровня
  - Код: `ExperienceManager::calculateDeathPenalty()`, вызывается из `CombatSystem::handleTargetDeath()`
- **Durability penalty**: `-5%` от `durabilityMax` каждого экипированного `isDurable` предмета
  - Настраивается: `game_config` ключ `durability.death_penalty_pct` (default `0.05`)

### 1.2 Добавить: Experience Debt

**Суть:** вместо одномоментного отнятия XP — игрок накапливает "долг". Пока долг не погашен, 50% всего зарабатываемого опыта уходит на его погашение, а не в нормальный прогресс. Игрок **никогда не теряет уже заработанное** — он просто медленнее растёт после смерти.

**Что нужно:**

- Добавить поле в `CharacterDataStruct`:
  ```cpp
  int experienceDebt = 0;
  ```
- Добавить колонку в БД: `characters.experience_debt INT DEFAULT 0`
- В `ExperienceManager::grantExperience()` добавить логику:
  ```
  если experienceDebt > 0:
      в_погашение = min(gainedXP / 2, experienceDebt)
      experienceDebt -= в_погашение
      реальный_XP = gainedXP - в_погашение
  иначе:
      реальный_XP = gainedXP
  ```
- Вместо текущего `removeExperience("death_penalty")` — устанавливать `experienceDebt`
- Расширить пакет `stats_update` (поле `experience`):
  ```json
  "experience": {
    "current": 1500,
    "levelStart": 1000,
    "nextLevel": 2000,
    "debt": 300
  }
  ```
- Сохранять `experience_debt` в БД при каждом изменении

**Почему лучше жёсткого XP-штрафа:**
- Нет откатов прогресса — игроки не бросают персонажей после череды смертей
- Смерть всё равно стоит: следующий гринд менее эффективен
- Психологически мягче, выше retention rate

### 1.3 Добавить: Resurrection Sickness (дебафф после возрождения)

**Суть:** временный дебафф после смерти, снижающий атрибуты персонажа на N минут.

**Реализация:** стандартный `ActiveEffectStruct` — никаких архитектурных изменений не нужно, система уже готова.

```cpp
ActiveEffectStruct sickness;
sickness.effectSlug      = "resurrection_sickness";
sickness.effectTypeSlug  = "debuff";
sickness.attributeSlug   = "strength"; // + другие атрибуты отдельными эффектами
sickness.value           = -20.0f;
sickness.sourceType      = "death";
sickness.expiresAt       = nowUnixSec + 120; // 2 минуты
```

Накладывается в момент respawn. Передаётся клиенту автоматически через `CharacterStatsNotificationService::sendStatsUpdate()` в блоке `activeEffects` — клиент отображает иконку дебаффа с таймером обратного отсчёта.

---

## Часть 2 — Блокировка действий мёртвого игрока

**Проблема:** сейчас мёртвый игрок (`characterCurrentHealth <= 0`) может двигаться и применять скилы — сервер не проверяет состояние жизни перед обработкой этих запросов.

### 2.1 Блокировка движения

В `CharacterEventHandler::handleMoveCharacterEvent()` — добавить проверку в начало обработки:

```cpp
auto charData = gameServices_.getCharacterManager().getCharacterData(movementData.characterId);
if (charData.characterCurrentHealth <= 0) {
    // отклонить движение, опционально отправить ошибку клиенту
    return;
}
```

### 2.2 Блокировка скилов

В `CombatSystem::initiateSkillUsage()` — добавить проверку после получения данных кастера:

```cpp
if (characterData.characterId != 0 && characterData.characterCurrentHealth <= 0) {
    result.errorMessage = "Cannot use skills while dead";
    return result;
}
```

### 2.3 Блокировать также

- Взаимодействие с NPC/диалоги
- Подбор предметов с земли
- Торговые операции

Паттерн везде одинаковый: `getCharacterData(id).characterCurrentHealth <= 0` → отклонить.

---

## Часть 3 — Воскрешение (Respawn)

### 3.1 Respawn Zones

Вместо одной хардкодированной точки возрождения — зональная система. Каждая `respawn_zone` — это точка на карте (деревня, храм, лагерь) куда игрок может возродиться.

**Новая таблица в БД:**
```sql
CREATE TABLE respawn_zones (
    id          SERIAL PRIMARY KEY,
    name        VARCHAR(64) NOT NULL,
    x           FLOAT NOT NULL,
    y           FLOAT NOT NULL,
    z           FLOAT NOT NULL DEFAULT 0,
    zone_id     INT NOT NULL REFERENCES zones(id),
    is_default  BOOLEAN NOT NULL DEFAULT false  -- fallback если ни одна не найдена
);
```

**Новая структура данных:**
```cpp
struct RespawnZoneStruct {
    int id = 0;
    std::string name = "";
    PositionStruct position;
    int zoneId = 0;
    bool isDefault = false;
};
```

**Логика выбора точки возрождения:**
1. Загрузить все `respawn_zones` при старте chunk server (аналогично `SpawnZoneManager`)
2. При смерти игрока — найти ближайшую `respawn_zone` к точке гибели
3. Если игрок имеет `respawnPosition` (точка привязки) — использовать её вместо ближайшей
4. Fallback: зона с `is_default = true`

**Точка привязки (будущее расширение):**

Добавить в `CharacterDataStruct`:
```cpp
PositionStruct respawnPosition; // 0,0,0 = не задана, используется ближайшая respawn_zone
```

Меняется через взаимодействие с NPC-алтарём/церковью.

### 3.2 Пакет Respawn Request от клиента

Клиент после смерти отображает UI с кнопкой "Возродиться". По нажатию отправляет:

```json
{
  "eventType": "respawnRequest",
  "body": {}
}
```

### 3.3 Обработка на сервере

При получении `respawnRequest`:

```
1. Получить characterData
2. Проверить: characterCurrentHealth <= 0 (игрок действительно мёртв)
3. Найти respawn-точку (ближайшая зона или точка привязки)
4. Установить characterCurrentHealth = characterMaxHealth * 0.3  (30% HP)
5. Установить characterCurrentMana = characterCurrentMana * 0.3
6. Установить characterPosition = respawnPoint
7. Сохранить position и health в БД
8. Наложить Resurrection Sickness (ActiveEffectStruct, 2 мин)
9. Установить experienceDebt (см. часть 1.2)
10. Отправить клиенту:
    - пакет teleport/position с новыми координатами
    - stats_update со свежими HP/Mana/XP/activeEffects
```

---

## Часть 4 — Валидация скорости движения на сервере

**Проблема:** сервер слепо принимает любые координаты от клиента. Клиент может телепортироваться в произвольную точку карты.

**Правильная архитектура:**

```
СЕЙЧАС (неправильно):
  Клиент → [x, y, z] → Сервер принимает как истину

ДОЛЖНО БЫТЬ:
  Клиент → [направление + timestamp] → Сервер вычисляет допустимость
  Сервер = авторитетный источник позиции
```

**Что добавить в `CharacterManager` / обработчик движения:**

```cpp
// Данные для валидации (хранятся в CharacterDataStruct или отдельном кеше)
PositionStruct lastValidatedPosition;
int64_t lastMoveTimestampMs = 0;
```

**Алгоритм валидации:**

```
delta_ms = currentTimestamp - lastMoveTimestampMs
maxSpeed = getCharacterSpeed(characterId)          // из server-side атрибутов
tolerance = 1.3f                                   // 30% запас на лаг
maxAllowedDistance = maxSpeed * (delta_ms / 1000.0f) * tolerance

actualDistance = distance(newPosition, lastValidatedPosition)

если actualDistance > maxAllowedDistance:
    отклонить новую позицию
    телепортировать игрока обратно на lastValidatedPosition
иначе:
    принять, обновить lastValidatedPosition и lastMoveTimestampMs
```

**Замечание:** `timestamps` уже присутствует в `MovementDataStruct` и передаётся в событии — это готовая основа для lag compensation, просто не используется для валидации.

---

## Приоритеты реализации

| Приоритет | Задача | Сложность |
|---|---|---|
| 🔴 1 | Блокировка действий мёртвого игрока (движение + скилы) | Низкая |
| 🔴 2 | Базовый respawn: пакет от клиента + телепорт в default respawn_zone | Средняя |
| 🔴 3 | Таблица `respawn_zones` в БД + загрузка на сервере | Средняя |
| 🟡 4 | Resurrection Sickness через `ActiveEffectStruct` | Низкая |
| 🟡 5 | Experience Debt вместо жёсткого XP-штрафа | Средняя |
| 🟡 6 | Валидация скорости движения на сервере | Средняя |
| 🟢 7 | Точка привязки (`respawnPosition`) через NPC-алтари | Высокая |
