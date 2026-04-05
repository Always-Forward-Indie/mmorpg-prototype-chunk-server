# 09. Смерть и респаун

## Обзор

Server-authoritative система. Смерть обрабатывается CombatSystem при HP ≤ 0, респаун — по запросу клиента.

---

## 9.1. Триггер смерти

Когда `health <= 0` (в CombatSystem):
1. `characterData.isDead = true`
2. Прекращение атак и скиллов персонажа
3. Применение штрафов (см. ниже)
4. Регистрация трупа для харвеста (если убит мобом)

---

## 9.2. Штрафы за смерть

### XP-долг

```
penalty = min(currentExp × 0.1, currentExp - expForCurrentLevel)
characterData.experienceDebt += penalty
```

- **10%** текущего XP переводится в долг
- Не может опустить ниже начала текущего уровня
- Долг погашается из будущего XP (см. документ 08)

### Потеря прочности

```
durabilityLoss = ceil(durabilityMax × 0.05)   // 5% от максимума
```

Применяется ко **всем экипированным** предметам с `isDurable == true`.

### Resurrection Sickness

- Статусный эффект из шаблона `"resurrection_sickness"`
- Применяется автоматически при респауне
- Содержит модификаторы (обычно снижение урона и хила)
- Длительность настраивается в шаблоне

---

## 9.3. respawnRequest — Запрос респауна

### Клиент → Сервер

```json
{
  "header": {
    "eventType": "respawnRequest",
    "clientId": 42,
    "hash": "auth_token",
    "timestamps": {
      "clientSendMsEcho": 1711709400500,
      "requestId": "sync_1711709400500_42_300_abc"
    }
  },
  "body": {}
}
```

> `characterId` определяется серверной стороне из сессии клиента.

### Серверная обработка

1. Проверяет `isDead == true` (иначе ошибка `"not dead"`)
2. Удаляет все существующие эффекты `resurrection_sickness`
3. Загружает шаблон `StatusEffectTemplate("resurrection_sickness")`
4. Применяет модификаторы из шаблона как `ActiveEffect`
5. Определяет точку респауна: `getZoneRespawnPoint(deathPosition)`
6. Устанавливает:
   - `isDead = false`
   - `currentHealth = maxHealth`
   - `currentMana = maxMana`
   - `position = respawnPosition`
7. Применяет XP-штраф
8. Отправляет `respawnResult` клиенту
9. Отправляет `moveCharacter` broadcast (телепортация к точке респауна)
10. Отправляет `stats_update` с обновлёнными статами

### Сервер → Unicast (respawnResult)

```json
{
  "header": {
    "message": "Respawn successful",
    "hash": "",
    "clientId": 42,
    "eventType": "respawnResult"
  },
  "body": {
    "characterId": 7,
    "position": {
      "x": 50.0,
      "y": 50.0,
      "z": 0.0,
      "rotationZ": 0.0
    }
  },
  "timestamps": {
    "serverRecvMs": 1711709400510,
    "serverSendMs": 1711709400520,
    "clientSendMsEcho": 1711709400500
  }
}
```

### Сервер → Broadcast (moveCharacter — телепортация)

```json
{
  "header": {
    "message": "Character respawned",
    "hash": "",
    "clientId": 42,
    "eventType": "moveCharacter"
  },
  "body": {
    "character": {
      "id": 7,
      "position": {
        "x": 50.0,
        "y": 50.0,
        "z": 0.0,
        "rotationZ": 0.0
      }
    }
  },
  "timestamps": {
    "serverRecvMs": 1711709400510,
    "serverSendMs": 1711709400525
  }
}
```

---

## 9.4. Клиентская реализация

### При получении урона с `finalTargetHealth <= 0`

1. Показать экран смерти / затемнение
2. Отключить управление персонажем
3. Показать кнопку «Респаун»

### При клике «Респаун»

1. Отправить `respawnRequest`
2. Дождаться `respawnResult`
3. Переместить камеру и персонажа к `position` из ответа
4. Убрать экран смерти
5. Восстановить управление
6. Обновить UI здоровья/маны до максимума
7. Показать иконку `resurrection_sickness` из `stats_update`

---

## 9.5. Конфигурация

| Параметр | Default | Описание |
|----------|---------|----------|
| `DEATH_PENALTY_PERCENT` | 0.1 | XP-штраф (10%) |
| `durability.death_penalty_pct` | 0.05 | Потеря прочности (5%) |
| `resurrection_sickness` | *шаблон* | Настраивается через StatusEffectTemplate |
