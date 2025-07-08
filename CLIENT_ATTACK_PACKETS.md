# Client Attack Packet Examples

## Обзор

Примеры пакетов, которые клиент может отправлять серверу для инициации атак и боевых действий.

## Основные пакеты атак

### 1. Простая атака на цель

```json
{
    "eventType": "PLAYER_ATTACK",
    "data": {
        "actionId": 1,
        "targetId": 12345,
        "useAI": false
    }
}
```

**Описание:**
- `actionId`: ID способности/атаки (1 = базовая атака)
- `targetId`: ID цели для атаки
- `useAI`: false = игрок выбрал цель вручную

### 2. Атака с автовыбором цели

```json
{
    "eventType": "PLAYER_ATTACK",
    "data": {
        "actionId": 2,
        "useAI": true,
        "strategy": "WEAKEST"
    }
}
```

**Описание:**
- `actionId`: 2 = Fireball
- `useAI`: true = позволить серверу выбрать цель
- `strategy`: стратегия выбора цели

### 3. Атака по области (AOE)

```json
{
    "eventType": "PLAYER_ATTACK",
    "data": {
        "actionId": 5,
        "targetPosition": {
            "x": 100.5,
            "y": 0.0,
            "z": 200.3
        }
    }
}
```

**Описание:**
- `actionId`: 5 = Explosive Blast (AOE)
- `targetPosition`: позиция для атаки по области

### 4. Запуск комбо-последовательности

```json
{
    "eventType": "ATTACK_SEQUENCE_START",
    "data": {
        "sequenceName": "stun_combo",
        "targetId": 12345,
        "allowInterruption": true
    }
}
```

**Описание:**
- `sequenceName`: название последовательности атак
- `targetId`: основная цель для комбо
- `allowInterruption`: можно ли прервать последовательность

### 5. Отмена текущего действия

```json
{
    "eventType": "INTERRUPT_COMBAT_ACTION",
    "data": {
        "casterId": 98765,
        "reason": "PLAYER_CANCELLED"
    }
}
```

**Описание:**
- `casterId`: ID игрока, отменяющего действие
- `reason`: причина прерывания

## Продвинутые пакеты

### 1. Условная атака

```json
{
    "eventType": "PLAYER_ATTACK",
    "data": {
        "actionId": 3,
        "conditions": {
            "targetHealthBelow": 0.5,
            "selfHealthAbove": 0.3,
            "manaRequired": 20
        },
        "fallbackActionId": 1
    }
}
```

**Описание:**
- Использовать actionId=3 (Heal) только если:
  - Здоровье цели меньше 50%
  - Собственное здоровье больше 30%
  - Мана больше 20
- Иначе использовать fallbackActionId=1 (Basic Attack)

### 2. Множественная атака

```json
{
    "eventType": "PLAYER_ATTACK",
    "data": {
        "actionId": 1,
        "multiTarget": true,
        "maxTargets": 3,
        "targetCriteria": {
            "maxRange": 5.0,
            "preferredRoles": ["DPS", "HEALER"],
            "excludeAllies": true
        }
    }
}
```

**Описание:**
- Атаковать до 3 целей
- В радиусе 5 метров
- Предпочтительно DPS и лекарей
- Исключить союзников

### 3. Адаптивная стратегия

```json
{
    "eventType": "PLAYER_ATTACK",
    "data": {
        "useAI": true,
        "strategy": "ADAPTIVE",
        "preferences": {
            "aggressionLevel": 0.7,
            "riskTolerance": 0.5,
            "resourceConservation": 0.3
        }
    }
}
```

**Описание:**
- Использовать адаптивную стратегию ИИ
- С настраиваемыми параметрами поведения

## Пакеты управления стратегией

### 1. Смена боевой стратегии

```json
{
    "eventType": "SET_COMBAT_STRATEGY",
    "data": {
        "strategyName": "defensive",
        "duration": 30000
    }
}
```

**Описание:**
- Переключиться на защитную стратегию
- На 30 секунд (30000 мс)

### 2. Создание кастомной стратегии

```json
{
    "eventType": "CREATE_COMBAT_STRATEGY",
    "data": {
        "name": "custom_pvp",
        "pattern": "AGGRESSIVE",
        "targetStrategy": "SUPPORT_FIRST",
        "aggressionLevel": 0.9,
        "openerActions": [4, 2],
        "finisherActions": [1],
        "emergencyActions": [3]
    }
}
```

**Описание:**
- Создать кастомную стратегию для PvP
- Приоритет поддержке противника
- Высокая агрессивность

## Информационные запросы

### 1. Получить доступные цели

```json
{
    "eventType": "GET_AVAILABLE_TARGETS",
    "data": {
        "actionId": 2,
        "includeAllies": false,
        "maxRange": 20.0
    }
}
```

**Ответ сервера:**
```json
{
    "status": "success",
    "data": {
        "targets": [
            {
                "targetId": 12345,
                "distance": 15.2,
                "healthPercent": 0.8,
                "role": "DPS",
                "threatLevel": 75.0,
                "isValidTarget": true
            },
            {
                "targetId": 12346,
                "distance": 18.7,
                "healthPercent": 0.3,
                "role": "HEALER",
                "threatLevel": 90.0,
                "isValidTarget": true
            }
        ]
    }
}
```

### 2. Получить информацию о способностях

```json
{
    "eventType": "GET_AVAILABLE_ACTIONS",
    "data": {
        "includeOnCooldown": false
    }
}
```

**Ответ сервера:**
```json
{
    "status": "success",
    "data": {
        "actions": [
            {
                "actionId": 1,
                "name": "Basic Attack",
                "type": "BASIC_ATTACK",
                "damage": 20,
                "cooldown": 1.0,
                "range": 3.0,
                "resourceCost": 0,
                "isAvailable": true
            },
            {
                "actionId": 2,
                "name": "Fireball",
                "type": "SPELL",
                "damage": 50,
                "cooldown": 3.0,
                "range": 20.0,
                "resourceCost": 30,
                "resourceType": "MANA",
                "castTime": 2.5,
                "isAvailable": true
            }
        ]
    }
}
```

## Пакеты состояния боя

### 1. Получить текущие боевые действия

```json
{
    "eventType": "GET_COMBAT_STATUS",
    "data": {}
}
```

**Ответ сервера:**
```json
{
    "status": "success",
    "data": {
        "currentAction": {
            "actionId": 2,
            "actionName": "Fireball",
            "targetId": 12345,
            "state": "CASTING",
            "remainingTime": 1.2,
            "canBeInterrupted": true
        },
        "activeSequence": {
            "sequenceName": "stun_combo",
            "currentStep": 1,
            "totalSteps": 3,
            "remainingDelay": 0.3
        },
        "cooldowns": [
            {
                "actionId": 1,
                "remainingTime": 0.0
            },
            {
                "actionId": 3,
                "remainingTime": 5.2
            }
        ]
    }
}
```

## Обработка ошибок

### Типичные ошибки и ответы сервера

#### 1. Недостаточно ресурсов
```json
{
    "status": "error",
    "message": "Insufficient mana for action",
    "errorCode": "INSUFFICIENT_RESOURCES",
    "data": {
        "required": 30,
        "current": 15,
        "resourceType": "MANA"
    }
}
```

#### 2. Цель вне радиуса
```json
{
    "status": "error",
    "message": "Target out of range",
    "errorCode": "TARGET_OUT_OF_RANGE",
    "data": {
        "maxRange": 20.0,
        "actualDistance": 25.3,
        "targetId": 12345
    }
}
```

#### 3. Действие на кулдауне
```json
{
    "status": "error",
    "message": "Action is on cooldown",
    "errorCode": "ACTION_ON_COOLDOWN",
    "data": {
        "actionId": 2,
        "remainingTime": 2.1
    }
}
```

#### 4. Неверная цель
```json
{
    "status": "error",
    "message": "Invalid target for action",
    "errorCode": "INVALID_TARGET",
    "data": {
        "reason": "TARGET_IS_ALLY",
        "actionId": 1,
        "targetId": 12345
    }
}
```

## Уведомления от сервера

### 1. Начало боевого действия

```json
{
    "eventType": "COMBAT_ACTION_STARTED",
    "data": {
        "casterId": 98765,
        "actionName": "Fireball",
        "targetId": 12345,
        "castTime": 2.5,
        "animation": {
            "name": "cast_fireball",
            "duration": 2.5
        }
    }
}
```

### 2. Результат боевого действия

```json
{
    "eventType": "COMBAT_RESULT",
    "data": {
        "casterId": 98765,
        "targetId": 12345,
        "actionName": "Fireball",
        "damage": 47,
        "wasHit": true,
        "isCritical": false,
        "effects": []
    }
}
```

### 3. Прерывание действия

```json
{
    "eventType": "COMBAT_ACTION_INTERRUPTED",
    "data": {
        "casterId": 98765,
        "actionName": "Fireball",
        "reason": "DAMAGE_TAKEN",
        "interruptedAt": 1.2
    }
}
```

## Рекомендации по использованию

1. **Валидация на клиенте**: Проверяйте доступность действий локально для лучшего UX
2. **Предсказание результатов**: Показывайте ожидаемый урон до отправки
3. **Кэширование**: Кэшируйте информацию о способностях и целях
4. **Обработка ошибок**: Всегда обрабатывайте ошибки сервера
5. **Анимации**: Запускайте анимации сразу после отправки, прерывайте при ошибках
6. **Латенция**: Учитывайте задержку сети при расчете времени кастов

## Безопасность

- Никогда не доверяйте клиентским расчетам урона
- Все проверки дистанции и валидности делаются на сервере
- Сервер имеет окончательное слово по всем боевым взаимодействиям
- Клиент может только запрашивать действия, но не принуждать к их выполнению
