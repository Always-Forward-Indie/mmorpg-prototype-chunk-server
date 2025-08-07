# Система Движения Мобов - Полное Описание

## Обзор Системы

Система движения мобов в MMORPG прототипе реализует сложный ИИ с тремя основными состояниями:
- **Обычное блуждание** - случайное движение в спавн зоне
- **Преследование игрока** - активная охота за целью
- **Возврат к спавну** - принудительный возврат в зону спавна

## Структуры Конфигурации

### MobAIConfig - Главная конфигурация ИИ

```cpp
struct MobAIConfig {
    // === ДИСТАНЦИИ АГРО И ПРЕСЛЕДОВАНИЯ ===
    float aggroRange = 400.0f;             // Радиус обнаружения игрока
    float maxChaseDistance = 800.0f;       // Максимальное расстояние преследования
    float maxDistanceFromSpawn = 1200.0f;  // Критическое удаление от спавна
    float returnToSpawnThreshold = 900.0f; // Дистанция принудительного возврата
    float newTargetZoneThreshold = 600.0f; // Лимит для поиска новых целей
    
    // === БОЕВЫЕ ПАРАМЕТРЫ ===
    float attackRange = 150.0f;  // Дистанция атаки (большая, чтобы не налезать на игрока)
    float attackCooldown = 2.0f; // Перезарядка между атаками
    
    // === МНОЖИТЕЛИ ПОВЕДЕНИЯ ===
    float chaseDistanceMultiplier = 2.0f;     // aggroRange * 2.0 = дистанция отмены преследования
    float spawnDistanceMultiplier = 2.25f;    // aggroRange * 2.25 = дистанция возврата
    float newTargetDistanceMultiplier = 1.5f; // aggroRange * 1.5 = лимит для новых целей
    
    // === ИНТЕРВАЛЫ ДВИЖЕНИЯ ===
    float chaseMovementInterval = 0.3f;   // Быстрое движение при преследовании
    float returnMovementInterval = 0.15f; // Очень быстрое движение при возврате
    
    // === СЕТЕВАЯ ОПТИМИЗАЦИЯ ===
    float minimumMoveDistance = 50.0f; // Минимальное перемещение для отправки обновления
};
```

### MobMovementData - Индивидуальные данные моба

```cpp
struct MobMovementData {
    // === ТАЙМИНГИ ===
    float nextMoveTime = 0.0f;    // Когда моб может двигаться в следующий раз
    float lastMoveTime = 0.0f;    // Время последнего движения
    float lastAttackTime = 0.0f;  // Время последней атаки
    
    // === НАПРАВЛЕНИЕ И СКОРОСТЬ ===
    float movementDirectionX = 0.0f; // Направление по X (-1.0 до 1.0)
    float movementDirectionY = 0.0f; // Направление по Y (-1.0 до 1.0)
    float speedMultiplier = 1.0f;    // Множитель скорости
    float stepMultiplier = 0.0f;     // Множитель размера шага
    
    // === СОСТОЯНИЕ ИИ ===
    int targetPlayerId = 0;          // ID преследуемого игрока (0 = нет цели)
    bool isReturningToSpawn = false; // Флаг возврата к спавну
    PositionStruct spawnPosition;    // Запомненная позиция спавна
    
    // === КОПИИ КОНФИГУРАЦИИ (инициализируются из MobAIConfig) ===
    float aggroRange = 400.0f;
    float attackRange = 150.0f;
    float attackCooldown = 2.0f;
    float minimumMoveDistance = 50.0f;
    float returnRange = 900.0f;
};
```

## Система Интервалов Движения

### Как работают интервалы движения

Каждый моб имеет поле `nextMoveTime`, которое определяет, когда он может двигаться в следующий раз. Это предотвращает слишком частые обновления и создает реалистичное поведение.

#### Расчет следующего времени движения:

```cpp
// Текущее время игры
float currentTime = getCurrentGameTime();

// Различные интервалы для разных состояний:
if (преследование_игрока) {
    nextMoveTime = currentTime + aiConfig_.chaseMovementInterval; // 0.3 секунды
} else if (возврат_к_спавну) {
    nextMoveTime = currentTime + aiConfig_.returnMovementInterval; // 0.15 секунды
} else {
    // Обычное движение - случайный интервал от 7 до 40+ секунд
    nextMoveTime = currentTime + random(7.0f, 40.0f);
}
```

#### Проверка возможности движения:

```cpp
bool timeToMove = (currentTime >= movementData.nextMoveTime);

// Для агрессивных мобов с целями - дополнительная проверка минимального интервала
if (hasTarget && mob.isAggressive) {
    float minInterval = isReturningToSpawn ? 0.1f : 0.2f;
    timeToMove = (nextMoveTime == 0.0f || 
                  (currentTime - lastMoveTime) >= minInterval);
}
```

## Система Расчета Дистанций

### Основная функция расчета расстояния

```cpp
float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2) {
    float dx = pos1.positionX - pos2.positionX;
    float dy = pos1.positionY - pos2.positionY;
    return sqrt(dx * dx + dy * dy);
}
```

### Ключевые дистанции и их использование

#### 1. Дистанция до игрока
```cpp
float distanceToTarget = calculateDistance(mob.position, player.position);
```

#### 2. Дистанция до центра спавн зоны
```cpp
PositionStruct zoneCenter = {zone.posX, zone.posY, mob.position.positionZ, mob.position.rotationZ};
float distanceFromZone = calculateDistance(mob.position, zoneCenter);
```

#### 3. Дистанция до точки спавна моба
```cpp
float distanceFromSpawn = calculateDistance(mob.position, movementData.spawnPosition);
```

### Логика принятия решений на основе дистанций

#### Обнаружение игрока (Aggro):
```cpp
if (distanceToTarget <= aiConfig_.aggroRange) { // 400.0f
    // Игрок замечен, начать преследование
    startChasing(playerId);
}
```

#### Прекращение преследования:
```cpp
float maxChaseDistance = aiConfig_.aggroRange * aiConfig_.chaseDistanceMultiplier; // 400 * 2.0 = 800
if (distanceToTarget > maxChaseDistance) {
    // Игрок слишком далеко, прекратить преследование
    stopChasing();
}
```

#### Принудительный возврат к спавну:
```cpp
if (distanceFromZone > aiConfig_.maxDistanceFromSpawn) { // 1200.0f
    // Моб слишком далеко от зоны, принудительный возврат
    forceReturnToSpawn();
}
```

#### Возможность атаки:
```cpp
if (distanceToTarget <= aiConfig_.attackRange && // 150.0f
    (currentTime - lastAttackTime) >= aiConfig_.attackCooldown) { // 2.0f
    // Можно атаковать
    executeAttack();
}
```

#### Поиск новых целей:
```cpp
if (distanceFromZone <= aiConfig_.newTargetZoneThreshold) { // 600.0f
    // Моб достаточно близко к зоне, можно искать новые цели
    searchForNewTargets();
}
```

## Алгоритм Принятия Решений

### Приоритеты (по убыванию важности):

1. **Критический возврат к спавну**
   ```cpp
   if (distanceFromZone > maxDistanceFromSpawn) {
       return FORCE_RETURN_TO_SPAWN;
   }
   ```

2. **Атака текущей цели**
   ```cpp
   if (hasTarget && distanceToTarget <= attackRange && canAttack()) {
       return ATTACK_TARGET;
   }
   ```

3. **Продолжение преследования**
   ```cpp
   if (hasTarget && distanceToTarget <= maxChaseDistance) {
       return CHASE_TARGET;
   }
   ```

4. **Потеря цели и возврат**
   ```cpp
   if (hasTarget && distanceToTarget > maxChaseDistance) {
       return LOSE_TARGET_AND_RETURN;
   }
   ```

5. **Поиск новых целей**
   ```cpp
   if (!hasTarget && !isReturning && distanceFromZone <= newTargetZoneThreshold) {
       return SEARCH_NEW_TARGETS;
   }
   ```

6. **Обычное блуждание**
   ```cpp
   if (!hasTarget && !isReturning) {
       return NORMAL_WANDERING;
   }
   ```

## Оптимизации Производительности

### Сетевые оптимизации

**Отправка обновлений позиции:**
```cpp
bool shouldSendUpdate = false;
float movementDistance = calculateDistance(currentPosition, lastSentPosition);

if (movementDistance >= aiConfig_.minimumMoveDistance) { // 50.0f
    shouldSendUpdate = true;
    lastSentPosition = currentPosition;
}
```

### Временные оптимизации

**Разные частоты обновления для разных состояний:**
- Обычное блуждание: 7-40+ секунд между движениями
- Преследование: 0.3 секунды между движениями  
- Возврат к спавну: 0.15 секунды между движениями

### Вычислительные оптимизации

**Кэширование расчетов:**
- Дистанции рассчитываются только при необходимости
- Повторное использование уже вычисленных значений в рамках одного кадра
- Проверки состояний в порядке приоритета (самые важные первыми)

## Настройка Поведения

### Примеры настроек для разных типов мобов:

#### Агрессивный страж:
```cpp
MobAIConfig aggressiveGuard = {
    .aggroRange = 500.0f,              // Большой радиус обнаружения
    .attackRange = 120.0f,             // Ближний бой
    .chaseMovementInterval = 0.2f,     // Очень быстрое преследование
    .maxDistanceFromSpawn = 800.0f,    // Не уходит далеко от поста
    .attackCooldown = 1.5f             // Частые атаки
};
```

#### Патрульный:
```cpp
MobAIConfig patroller = {
    .aggroRange = 300.0f,              // Умеренный радиус
    .attackRange = 200.0f,             // Дальняя атака
    .chaseMovementInterval = 0.4f,     // Умеренная скорость
    .maxDistanceFromSpawn = 1500.0f,   // Может далеко преследовать
    .attackCooldown = 3.0f             // Редкие атаки
};
```

#### Мирный NPC:
```cpp
MobAIConfig peaceful = {
    .aggroRange = 0.0f,                // Не агрится
    .attackRange = 0.0f,               // Не атакует
    .chaseMovementInterval = 1.0f,     // Медленное движение
    .maxDistanceFromSpawn = 200.0f,    // Не покидает зону
    .attackCooldown = 0.0f             // Не атакует
};
```

## Отладка и Мониторинг

### Логирование ключевых событий:

```cpp
// Смена цели
logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) + 
           " found new target: " + std::to_string(playerId));

// Потеря цели
logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) + 
           " lost target (distance: " + std::to_string(distance) + 
           "/" + std::to_string(maxDistance) + "), returning");

// Атаки
logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + 
           " attacks player " + std::to_string(playerId) + 
           " for " + std::to_string(damage) + " damage");
```

### Метрики для мониторинга:
- Среднее время преследования
- Частота смены целей
- Дистанция отклонения от спавна
- Количество атак в минуту
- Процент времени в каждом состоянии

Эта система обеспечивает реалистичное и настраиваемое поведение мобов с четкими правилами перехода между состояниями и эффективными оптимизациями производительности.
