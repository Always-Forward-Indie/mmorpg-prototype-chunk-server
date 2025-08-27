# Документация пакетов для системы боя и скилов

## Обзор

Данный документ содержит описание всех пакетов, которые клиент должен отправлять серверу для использования скилов в бою. Система поддерживает атаки как на мобов, так и на других игроков с явным указанием типа цели.

## Архитектура пакетов

Все пакеты имеют единую структуру:
```json
{
  "header": {
    "message": "string",
    "hash": "string", 
    "clientId": "string",
    "eventType": "string"
  },
  "body": {
    // Специфичные данные для каждого типа события
  }
}
```

## Типы целей (targetType)

- `1` - SELF (самоцель - для бафов/лечения)
- `2` - PLAYER (другой игрок)
- `3` - MOB (моб/NPC)
- `4` - AREA (область на земле)
- `5` - NONE (без цели)

## События системы боя

### 1. Атака с использованием скила

**Тип события:** `playerAttack`

**Описание:** Игрок атакует цель используя указанный скил. Система автоматически определяет тип атаки и применяет соответствующие эффекты.

**Пример пакета - атака моба:**
```json
{
  "header": {
    "message": "attack mob with fireball",
    "hash": "user_session_hash_123",
    "clientId": "12345", 
    "eventType": "playerAttack"
  },
  "body": {
    "skillSlug": "fireball",
    "targetId": 999001,
    "targetType": 3
  }
}
```

**Пример пакета - атака игрока:**
```json
{
  "header": {
    "message": "attack player with lightning bolt",
    "hash": "user_session_hash_456",
    "clientId": "12345", 
    "eventType": "playerAttack"
  },
  "body": {
    "skillSlug": "lightning_bolt",
    "targetId": 67890,
    "targetType": 2
  }
}
```

**Пример пакета - базовая атака (без указания скила):**
```json
{
  "header": {
    "message": "basic attack on mob",
    "hash": "user_session_hash_789",
    "clientId": "12345", 
    "eventType": "playerAttack"
  },
  "body": {
    "targetId": 999002,
    "targetType": 3
  }
}
```

**Обязательные поля в body:**
- `targetId` (int) - ID цели (UID моба или characterId игрока)
- `targetType` (int) - Тип цели (2 = игрок, 3 = моб)

**Опциональные поля в body:**
- `skillSlug` (string) - Slug скила для использования (по умолчанию "basic_attack")

---

### 2. Использование скила (альтернативный способ)

**Тип события:** `skillUsage`

**Описание:** Прямое использование скила. Аналогично playerAttack, но может включать дополнительные эффекты (лечение, бафы, дебафы).

**Пример пакета - лечение себя:**
```json
{
  "header": {
    "message": "heal self",
    "hash": "user_session_hash_321",
    "clientId": "12345", 
    "eventType": "skillUsage"
  },
  "body": {
    "skillSlug": "heal",
    "targetId": 12345,
    "targetType": 1
  }
}
```

**Пример пакета - лечение союзника:**
```json
{
  "header": {
    "message": "heal ally",
    "hash": "user_session_hash_654",
    "clientId": "12345", 
    "eventType": "skillUsage"
  },
  "body": {
    "skillSlug": "greater_heal",
    "targetId": 54321,
    "targetType": 2
  }
}
```

**Пример пакета - дебаф на моба:**
```json
{
  "header": {
    "message": "debuff mob",
    "hash": "user_session_hash_987",
    "clientId": "12345", 
    "eventType": "skillUsage"
  },
  "body": {
    "skillSlug": "slow",
    "targetId": 999003,
    "targetType": 3
  }
}
```

**Обязательные поля в body:**
- `skillSlug` (string) - Slug скила для использования
- `targetId` (int) - ID цели
- `targetType` (int) - Тип цели

---

## Ответы сервера

### Успешная атака/использование скила

```json
{
  "header": {
    "message": "Attack successful",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "playerAttack"
  },
  "body": {
    "attackResult": {
      "status": "success",
      "skillSlug": "fireball",
      "targetId": 999001,
      "targetType": 3,
      "damage": 150,
      "healing": 0,
      "isCritical": false,
      "isBlocked": false,
      "isMissed": false
    }
  }
}
```

### Успешное лечение

```json
{
  "header": {
    "message": "Skill used successfully",
    "hash": "user_session_hash_321",
    "clientId": "12345",
    "eventType": "skillUsage"
  },
  "body": {
    "skillResult": {
      "status": "success",
      "skillSlug": "heal",
      "targetId": 12345,
      "targetType": 1,
      "damage": 0,
      "healing": 200,
      "isCritical": true,
      "isBlocked": false,
      "isMissed": false
    }
  }
}
```

### Ошибка - скил недоступен

```json
{
  "header": {
    "message": "Skill is on cooldown",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "playerAttack"
  },
  "body": {}
}
```

### Ошибка - цель вне радиуса действия

```json
{
  "header": {
    "message": "Target is out of range",
    "hash": "user_session_hash_456",
    "clientId": "12345",
    "eventType": "skillUsage"
  },
  "body": {}
}
```

### Ошибка - недостаточно маны

```json
{
  "header": {
    "message": "Not enough mana",
    "hash": "user_session_hash_789",
    "clientId": "12345",
    "eventType": "playerAttack"
  },
  "body": {}
}
```

### Ошибка - неверный тип цели

```json
{
  "header": {
    "message": "Invalid target type!",
    "hash": "user_session_hash_000",
    "clientId": "12345",
    "eventType": "playerAttack"
  },
  "body": {}
}
```

## Валидация на сервере

Сервер выполняет следующие проверки:

1. **Аутентификация:** Проверка hash и clientId
2. **Существование персонажа:** Проверка что персонаж клиента существует
3. **Существование цели:** Проверка что цель существует (игрок или моб)
4. **Тип цели:** Валидация корректности targetType
5. **Доступность скила:** Проверка что у персонажа есть указанный скил
6. **Ресурсы:** Проверка достаточности маны
7. **Кулдаун:** Проверка что скил не на перезарядке
8. **Дистанция:** Проверка что цель в радиусе действия скила
9. **Состояние цели:** Проверка что цель жива (для атак) или может быть излечена

## Примеры различных ситуаций

### Атака моба мечом
```json
{
  "header": {
    "eventType": "playerAttack",
    "clientId": "12345",
    "hash": "abc123"
  },
  "body": {
    "skillSlug": "sword_strike",
    "targetId": 999001,
    "targetType": 3
  }
}
```

### Магическая атака игрока
```json
{
  "header": {
    "eventType": "playerAttack",
    "clientId": "12345",
    "hash": "def456"
  },
  "body": {
    "skillSlug": "ice_bolt",
    "targetId": 67890,
    "targetType": 2
  }
}
```

### Самолечение
```json
{
  "header": {
    "eventType": "skillUsage",
    "clientId": "12345",
    "hash": "ghi789"
  },
  "body": {
    "skillSlug": "healing_potion",
    "targetId": 12345,
    "targetType": 1
  }
}
```

### Баф на союзника
```json
{
  "header": {
    "eventType": "skillUsage",
    "clientId": "12345",
    "hash": "jkl012"
  },
  "body": {
    "skillSlug": "blessing",
    "targetId": 54321,
    "targetType": 2
  }
}
```

## Безопасность и защита от читов

1. **Проверка владения скилом:** Сервер проверяет что у персонажа действительно есть указанный скил
2. **Валидация дистанции:** Нельзя атаковать цели вне радиуса действия скила  
3. **Проверка ресурсов:** Нельзя использовать скилы без достаточной маны
4. **Контроль кулдаунов:** Сервер отслеживает время перезарядки скилов
5. **Проверка состояния:** Нельзя атаковать мертвые цели или лечить живых сверх максимума
6. **Синхронизация ID:** targetId проверяется на соответствие реальным сущностям

## Рекомендации по реализации клиента

1. **Кэширование скилов:** Сохраняйте список доступных скилов локально
2. **Предварительная валидация:** Проверяйте доступность действий до отправки
3. **Показ кулдаунов:** Отображайте время до готовности скилов  
4. **Проверка ресурсов:** Показывайте когда недостаточно маны
5. **Индикация дистанции:** Подсвечивайте цели в радиусе действия
6. **Обработка ошибок:** Показывайте понятные сообщения об ошибках
7. **Анимации:** Воспроизводите анимации атак сразу после отправки пакета

## Различия между playerAttack и skillUsage

- **playerAttack** - предназначен для атакующих действий, поддерживает дефолтный скил
- **skillUsage** - более универсальный, подходит для любых скилов включая лечение и бафы
- Оба события обрабатываются одинаково на серверной стороне
- Выбор события зависит от семантики действия в вашем UI
