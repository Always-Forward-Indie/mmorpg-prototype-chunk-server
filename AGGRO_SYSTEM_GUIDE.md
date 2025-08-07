# Система агро и преследования мобов

## Обзор

Система агро позволяет мобам реагировать на атаки игроков, преследовать их и наносить ответный урон. Когда игрок атакует моба, моб автоматически начинает его преследовать и атаковать.

## Основные компоненты

### 1. MobMovementData (расширенная структура)

Новые поля для AI поведения:
- `targetPlayerId` - ID игрока, которого преследует моб
- `aggroRange` - дистанция агро (по умолчанию 300.0f)
- `attackRange` - дистанция атаки (по умолчанию 80.0f) 
- `returnRange` - дистанция возврата в зону спавна (по умолчанию 600.0f)
- `lastAttackTime` - время последней атаки
- `attackCooldown` - кулдаун между атаками (по умолчанию 2.0f секунды)
- `isReturningToSpawn` - флаг возврата в зону спавна
- `spawnPosition` - позиция спавна для возврата

### 2. MobMovementManager (новые методы)

#### AI Методы:
- `handleMobAttacked(mobUID, attackerPlayerId)` - вызывается когда игрок атакует моба
- `handlePlayerAggro()` - проверяет игроков в зоне агро
- `calculateChaseMovement()` - рассчитывает движение к цели
- `calculateReturnToSpawnMovement()` - рассчитывает возврат к спавну
- `canAttackPlayer()` - проверяет возможность атаки
- `executeMobAttack()` - выполняет атаку моба

#### Настройка:
- `setCharacterManager()` - устанавливает ссылку на менеджер персонажей

### 3. CharacterManager (новые методы)

- `getCharactersInZone(centerX, centerY, radius)` - получает игроков в зоне
- `getCharacterById(characterID)` - получает персонажа по ID
- `calculateDistance(pos1, pos2)` - вычисляет расстояние

## Логика работы

### 1. Инициализация агро

Когда игрок атакует моба:
1. CombatEventHandler обнаруживает атаку на моба
2. Вызывается `MobMovementManager::handleMobAttacked(mobUID, attackerPlayerId)`
3. Моб получает цель для преследования

### 2. Преследование игрока

В основном цикле движения мобов:
1. Проверяется, есть ли у моба цель (`targetPlayerId > 0`)
2. Если цель в зоне атаки - моб атакует
3. Если цель вне зоны атаки - моб движется к цели
4. Если цель слишком далеко - моб возвращается к спавну

### 3. Возврат к спавну

Моб возвращается к спавну когда:
- Игрок-цель удалился на расстояние больше `returnRange`
- Игрок отключился или умер
- Моб достиг позиции спавна (в пределах 50 единиц)

### 4. Атака моба

При атаке моба:
- Проверяется кулдаун атаки
- Рассчитывается урон: `baseDamage = 10 + (mob.level * 5)`
- Добавляется случайность: `damage = baseDamage + rand(baseDamage/2)`
- Урон применяется к игроку

## Пример использования

```cpp
// В GameServices конструкторе уже настроена связь:
mobMovementManager_.setCharacterManager(&characterManager_);

// При атаке игрока на моба (в CombatEventHandler):
if (actionPtr->targetType == CombatTargetType::MOB && actualDamage > 0)
{
    auto attackerData = gameServices_.getCharacterManager().getCharacterData(actionPtr->casterId);
    if (attackerData.characterId > 0)
    {
        gameServices_.getMobMovementManager().handleMobAttacked(actionPtr->targetId, attackerData.characterId);
    }
}

// Настройка параметров агро для моба:
MobMovementData mobData = mobMovementManager.getMobMovementData(mobUID);
mobData.aggroRange = 400.0f;     // Увеличить дистанцию агро
mobData.attackRange = 100.0f;    // Увеличить дистанцию атаки
mobData.attackCooldown = 1.5f;   // Уменьшить кулдаун атаки
mobMovementManager.updateMobMovementData(mobUID, mobData);
```

## Настройка параметров

### Параметры агро (по умолчанию):
- **aggroRange**: 300.0f - дистанция, на которой моб замечает игроков
- **attackRange**: 80.0f - дистанция атаки моба
- **returnRange**: 600.0f - дистанция, после которой моб возвращается к спавну
- **attackCooldown**: 2.0f - время между атаками в секундах

### Рекомендации по настройке:

#### Для слабых мобов:
- aggroRange: 200-250
- attackRange: 60-80
- attackCooldown: 2.5-3.0

#### Для обычных мобов:
- aggroRange: 300-350
- attackRange: 80-100
- attackCooldown: 2.0-2.5

#### Для сильных мобов/боссов:
- aggroRange: 400-500
- attackRange: 100-150
- attackCooldown: 1.0-1.5

## Интеграция с существующей системой

Система интегрирована со следующими компонентами:

1. **CombatEventHandler** - обнаруживает атаки на мобов и запускает агро
2. **MobInstanceManager** - управляет экземплярами мобов
3. **SpawnZoneManager** - управляет зонами спавна
4. **CharacterManager** - предоставляет информацию об игроках

## Будущие улучшения

1. **Система угроз** - различные уровни угрозы от разных игроков
2. **Групповое агро** - мобы помогают друг другу
3. **Различные типы AI** - пассивные, агрессивные, осторожные мобы
4. **Эффекты и анимации** - визуальные эффекты атак мобов
5. **Система оглушения** - возможность прервать преследование
6. **Лечение мобов** - восстановление здоровья при возврате к спавну

## Отладка

Для отладки системы агро добавлены логи:

- `[INFO] Mob UID: X aggroed on player Y` - моб получил агро
- `[INFO] Mob UID: X is now targeting player Y` - моб начал преследование
- `[DEBUG] Mob UID: X is chasing player Y` - моб преследует цель
- `[INFO] Mob UID: X is returning to spawn zone` - моб возвращается к спавну
- `[COMBAT] Mob UID: X attacks player Y for Z damage` - атака моба
- `[AGGRO] Mob X now targets player Y` - смена цели агро
