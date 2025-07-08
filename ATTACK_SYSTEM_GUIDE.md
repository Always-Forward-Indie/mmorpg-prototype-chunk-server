# Attack System Documentation

## Обзор

Новая система атак предоставляет масштабируемую и гибкую архитектуру для обработки боевых взаимодействий в MMORPG. Система поддерживает:

- Интеллектуальный выбор цели
- Различные стратегии атаки
- Комбо-последовательности
- ИИ для NPC/мобов
- Прерывания и анимации
- Адаптивное поведение

## Архитектура

### Основные компоненты

1. **AttackSystem** - Центральный класс управления атаками
2. **AttackAction** - Определение отдельной атаки/способности
3. **AttackStrategy** - Стратегия поведения в бою
4. **CombatSequence** - Последовательность атак
5. **TargetCandidate** - Потенциальная цель с метриками

### Интеграция с существующей системой

```cpp
// В CombatEventHandler добавлена интеграция с AttackSystem
std::unique_ptr<AttackSystem> attackSystem_;

// Новые методы для обработки атак
void handlePlayerAttack(const Event &event);
void handleAIAttack(int characterId);
std::vector<TargetCandidate> getAvailableTargets(int attackerId, const TargetCriteria& criteria);
```

## Типы атак

### CombatActionType
- `BASIC_ATTACK` - Обычная атака оружием
- `SPELL` - Заклинание с временем каста
- `SKILL` - Классовая способность
- `CHANNELED` - Канализируемая способность (может быть прервана)
- `INSTANT` - Мгновенная способность
- `AOE_ATTACK` - Атака по области
- `BUFF` - Усиление себя или союзника
- `DEBUFF` - Ослабление противника

### Типы целей (CombatTargetType)
- `SELF` - Самоцель
- `PLAYER` - Другой игрок
- `MOB` - Моб/NPC
- `AREA` - Область на земле
- `NONE` - Без цели

## Стратегии выбора цели

### TargetSelectionStrategy
- `NEAREST` - Ближайший противник
- `WEAKEST` - Самый слабый по здоровью
- `STRONGEST` - Самый сильный по здоровью
- `MOST_DANGEROUS` - Самый опасный (высокий уровень угрозы)
- `SUPPORT_FIRST` - Приоритет лекарям/поддержке
- `RANDOM` - Случайная цель
- `PLAYER_PREFERENCE` - Предпочтения игрока
- `AI_TACTICAL` - Продвинутый ИИ

## Боевые роли

### CombatRole
- `TANK` - Поглощает урон, защищает союзников
- `DPS` - Наносит урон
- `HEALER` - Лечит и поддерживает союзников
- `SUPPORT` - Усиления, ослабления, утилиты
- `HYBRID` - Несколько ролей
- `CROWD_CONTROL` - Контроль противников

## Паттерны атак

### AttackPattern
- `AGGRESSIVE` - Высокий урон, высокий риск
- `DEFENSIVE` - Сбалансированный, фокус на выживание
- `SUPPORT` - Фокус на лечении/усилении союзников
- `CONTROL` - Фокус на ослаблениях/контроле толпы
- `BURST` - Высокий урон за короткое время
- `SUSTAINED` - Постоянный урон со временем
- `ADAPTIVE` - Изменяется в зависимости от ситуации

## Примеры использования

### Настройка базовых атак

```cpp
void setupAttacks() {
    AttackSystem attackSystem;
    
    // Базовая атака
    AttackAction meleeAttack;
    meleeAttack.actionId = 1;
    meleeAttack.name = "Basic Attack";
    meleeAttack.type = CombatActionType::BASIC_ATTACK;
    meleeAttack.maxRange = 3.0f;
    meleeAttack.baseDamage = 20;
    meleeAttack.cooldown = 1.0f;
    
    attackSystem.registerAction(meleeAttack);
    
    // Заклинание
    AttackAction fireball;
    fireball.actionId = 2;
    fireball.name = "Fireball";
    fireball.type = CombatActionType::SPELL;
    fireball.resourceType = ResourceType::MANA;
    fireball.resourceCost = 30;
    fireball.castTime = 2.5f;
    fireball.maxRange = 20.0f;
    fireball.baseDamage = 50;
    
    attackSystem.registerAction(fireball);
}
```

### Настройка стратегий

```cpp
void setupStrategies() {
    AttackSystem attackSystem;
    
    // Агрессивная стратегия
    AttackStrategy aggressive;
    aggressive.name = "aggressive";
    aggressive.pattern = AttackPattern::AGGRESSIVE;
    aggressive.targetStrategy = TargetSelectionStrategy::WEAKEST;
    aggressive.aggressionLevel = 0.9f;
    aggressive.openerActions = {2}; // Начать с fireball
    aggressive.finisherActions = {1}; // Добить базовой атакой
    
    attackSystem.registerStrategy(aggressive);
    
    // Защитная стратегия
    AttackStrategy defensive;
    defensive.name = "defensive";
    defensive.pattern = AttackPattern::DEFENSIVE;
    defensive.targetStrategy = TargetSelectionStrategy::MOST_DANGEROUS;
    defensive.aggressionLevel = 0.3f;
    defensive.emergencyActions = {3}; // Лечиться при низком HP
    
    attackSystem.registerStrategy(defensive);
}
```

### Атака игрока

```cpp
// Создание события атаки игрока
nlohmann::json attackRequest;
attackRequest["actionId"] = 2; // Fireball
attackRequest["targetId"] = targetId; // Конкретная цель

Event attackEvent(Event::Type::PLAYER_ATTACK, attackRequest, playerId, clientSocket);
combatHandler.handlePlayerAttack(attackEvent);
```

### ИИ для NPC

```cpp
// Настройка ИИ для NPC
attackSystem.setActiveStrategy(npcId, "adaptive");

// ИИ автоматически выберет цели и действия
combatHandler.handleAIAttack(npcId);
```

### Комбо-последовательности

```cpp
// Создание комбо
CombatSequence stunCombo;
stunCombo.name = "stun_combo";
stunCombo.actionIds = {4, 1, 1}; // Stun -> Attack -> Attack
stunCombo.sequenceDelay = 0.3f;
stunCombo.interruptible = true;

attackSystem.registerSequence(stunCombo);
attackSystem.startSequence(playerId, "stun_combo");
```

## Система прерываний

### InterruptionReason
- `PLAYER_CANCELLED` - Игрок отменил действие
- `MOVEMENT` - Игрок двинулся во время каста
- `DAMAGE_TAKEN` - Получил урон, прерывающий каст
- `TARGET_LOST` - Цель исчезла или вышла из радиуса
- `RESOURCE_DEPLETED` - Кончились ресурсы
- `DEATH` - Кастер умер
- `STUN_EFFECT` - Оглушение или подобный эффект

### Обработка прерываний

```cpp
// Прерывание действия
CombatActionStruct action;
action.interruptReason = InterruptionReason::MOVEMENT;
action.state = CombatActionState::INTERRUPTED;

Event interruptEvent(Event::Type::INTERRUPT_COMBAT_ACTION, action, playerId, clientSocket);
combatHandler.handleInterruptCombatAction(interruptEvent);
```

## Адаптивные стратегии

```cpp
// Стратегия, адаптирующаяся к ситуации
AttackStrategy adaptive;
adaptive.name = "adaptive";
adaptive.pattern = AttackPattern::ADAPTIVE;

adaptive.adaptStrategy = [](AttackStrategy& strategy, const CharacterStatusStruct& character) {
    float healthPercent = static_cast<float>(character.currentHealth) / character.maxHealth;
    
    if (healthPercent < 0.3f) {
        // Низкое здоровье: стать защитным
        strategy.aggressionLevel = 0.2f;
        strategy.targetStrategy = TargetSelectionStrategy::NEAREST;
    } else if (healthPercent > 0.8f) {
        // Высокое здоровье: стать агрессивным
        strategy.aggressionLevel = 0.9f;
        strategy.targetStrategy = TargetSelectionStrategy::WEAKEST;
    }
};
```

## Расчет урона

```cpp
int damage = attackSystem.calculateDamage(action, attacker, target);

// Учитывает:
// - Базовый урон способности
// - Вариативность урона (±10% по умолчанию)
// - Модификаторы атакующего (сила, магия)
// - Защиту цели
// - Кастомную логику расчета
```

## Система анимаций

```cpp
// Автоматическая отправка анимаций
CombatAnimationStruct animation;
animation.characterId = attackerId;
animation.animationName = action.animationName;
animation.duration = action.animationDuration;
animation.position = attackerPosition;
animation.targetPosition = targetPosition;

// Система автоматически отправляет анимации всем клиентам
```

## Интеграция с новыми событиями

### Новые типы событий
- `PLAYER_ATTACK` - Атака игрока
- `AI_ATTACK` - Атака ИИ
- `ATTACK_TARGET_SELECTION` - Выбор цели
- `ATTACK_SEQUENCE_START` - Начало последовательности
- `ATTACK_SEQUENCE_COMPLETE` - Завершение последовательности

### Структуры данных событий
- `AttackRequestStruct` - Запрос на атаку
- `AttackSequenceStruct` - Запрос на последовательность
- `TargetSelectionStruct` - Результат выбора цели

## Рекомендации по расширению

1. **Добавление новых типов атак**: Расширьте enum CombatActionType
2. **Новые стратегии**: Создайте новые AttackStrategy с кастомной логикой
3. **Специальные эффекты**: Используйте функции onHit/onMiss в AttackAction
4. **Интеграция с мобами**: Добавьте поддержку мобов в getAvailableTargets
5. **Балансировка**: Настройте параметры урона, кулдаунов и стоимости ресурсов

## Тестирование

Используйте AttackSystemExample для быстрой настройки и тестирования:

```cpp
AttackSystem attackSystem;
AttackSystemExample::setupBasicAttacks(attackSystem);
AttackSystemExample::setupBasicStrategies(attackSystem);
AttackSystemExample::setupDifferentRoles(attackSystem);

// Демонстрация атаки игрока
AttackSystemExample::demonstratePlayerAttack(combatHandler, playerId, targetId);

// Демонстрация ИИ
AttackSystemExample::demonstrateAIBehavior(attackSystem, npcId);
```

## Производительность

- Система оптимизирована для обработки множественных атак
- Кэширование результатов выбора цели
- Эффективное управление памятью с std::unique_ptr
- Минимальные копирования данных

## Безопасность

- Валидация всех входных данных
- Проверка доступности ресурсов
- Защита от читерства через серверную валидацию
- Ограничения на дистанцию и line of sight
