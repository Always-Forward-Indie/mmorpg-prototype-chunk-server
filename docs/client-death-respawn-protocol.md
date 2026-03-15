# Death & Respawn — Client Protocol

Описание всех изменений в протоколе после внедрения системы смерти/респавна.

---

## 1. Что изменилось в существующих пакетах

### 1.1 `setCharacterData` (при входе в мир)

Добавлено поле `experienceDebt` в тело ответа.

```json
{
  "header": {
    "status": "success",
    "message": "Join Game Character success for Client!",
    "hash": "abc123",
    "clientId": 42,
    "eventType": "setCharacterData"
  },
  "body": {
    "id": 7,
    "name": "Аргент",
    "class": "Warrior",
    "classId": 1,
    "race": "Human",
    "level": 15,
    "currentExp": 48200,
    "experienceDebt": 3500,
    "expForNextLevel": 60000,
    "currentHealth": 350,
    "maxHealth": 420,
    "currentMana": 80,
    "maxMana": 120,
    "posX": 120.5,
    "posY": 45.0,
    "posZ": 10.0,
    "rotZ": 0.0,
    "attributesData": [...],
    "skillsData": [...]
  }
}
```

**Что делать с `experienceDebt`**: сохранить в состоянии персонажа и отобразить в UI (см. раздел 4).

---

### 1.2 `stats_update` — добавлен `debt` в блок `experience`

```json
{
  "header": {
    "status": "success",
    "eventType": "stats_update",
    "requestId": "req-001"
  },
  "body": {
    "characterId": 7,
    "level": 15,
    "experience": {
      "current": 48200,
      "levelStart": 40000,
      "nextLevel": 60000,
      "debt": 3500
    },
    "health": { "current": 350, "max": 420 },
    "mana":   { "current":  80, "max": 120 },
    "weight": { "current":  12, "max":  80 },
    "attributes": [...],
    "activeEffects": [
      {
        "slug": "resurrection_sickness",
        "effectTypeSlug": "debuff",
        "attributeSlug": "",
        "value": -20.0,
        "expiresAt": 1741872600
      }
    ]
  }
}
```

**Что изменилось**: поле `experience.debt` — долг опыта (см. раздел 4).  
`activeEffects` уже существовал, но теперь может приходить с `resurrection_sickness`.

---

## 2. Новые пакеты: смерть

### 2.1 Когда персонаж погибает

Сервер присылает существующий пакет `stats_update` с `health.current = 0`.  
Дополнительно обновляется `experience.debt` (если штраф был наложен).

**Логика на клиенте при получении `health.current == 0`**:
1. Перевести персонажа в состояние **dead** (анимация смерти, коллайдер off).
2. Показать **экран смерти** (кнопка «Воскреснуть»).
3. Заблокировать все действия: движение, скиллы, подбор предметов, торговля, диалоги.
4. Отобразить новый долг опыта из `experience.debt`.

---

### 2.2 Ошибка при попытке сделать что-то будучи мёртвым

Если мёртвый игрок пытается двигаться, использовать скиллы или открыть торговлю — сервер вернёт ошибку:

```json
{
  "header": {
    "status": "error",
    "eventType": "moveCharacter",
    "message": "Cannot move while dead"
  },
  "body": {}
}
```

| Действие | `eventType` в ответе | `message` |
|---|---|---|
| Движение | `moveCharacter` | `"Cannot move while dead"` |
| Скилл | `skillUsage` | `"Cannot use skills while dead"` |
| Торговля | `tradeRequest` | `"cannot_trade_while_dead"` |

Клиент должен блокировать эти UI-элементы локально, не дожидаясь ошибки от сервера.

---

## 3. Новый пакет: запрос на респавн

**Направление**: Клиент → Сервер

```json
{
  "header": {
    "eventType": "respawnRequest",
    "clientId": 42,
    "hash": "abc123",
    "serverRecvMs": 0,
    "serverSendMs": 0,
    "clientSendMs": 1741872480000
  },
  "body": {
    "characterId": 7
  }
}
```

**Когда отправлять**: при нажатии кнопки «Воскреснуть» (только если персонаж мёртв, т.е. HP == 0).  
Никаких дополнительных параметров не требуется — сервер сам выбирает ближайшую зону.

---

## 4. Новый пакет: результат респавна

**Направление**: Сервер → Клиент (только инициатору)

```json
{
  "header": {
    "status": "success",
    "message": "Respawn successful",
    "hash": "abc123",
    "clientId": 42,
    "eventType": "respawnResult",
    "serverRecvMs": 1741872480120,
    "serverSendMs": 1741872480135,
    "clientSendMs": 1741872480000
  },
  "body": {
    "characterId": 7,
    "position": {
      "x": 0.0,
      "y": 0.0,
      "z": 200.0,
      "rotationZ": 0.0
    }
  }
}
```

**Что делать при получении `respawnResult`**:
1. Телепортировать персонажа в `body.position`.
2. Снять состояние **dead** (убрать экран смерти, включить управление).
3. Запросить актуальные статы — сервер сам пришлёт `stats_update` сразу после `respawnResult`.

Сразу после `respawnResult` придут:
- `stats_update` — HP/Mana восстановлены до 30% максимума, в `activeEffects` будет `resurrection_sickness`.
- `moveCharacter` — broadcast-обновление позиции персонажа для всех других клиентов.

---

## 5. Broadcast при респавне (другие клиенты)

Все остальные игроки в зоне получат:

```json
{
  "header": {
    "status": "success",
    "message": "Character respawned",
    "eventType": "moveCharacter",
    "clientId": 42,
    "serverRecvMs": 1741872480135,
    "serverSendMs": 1741872480135,
    "clientSendMs": 1741872480000
  },
  "body": {
    "character": {
      "id": 7,
      "position": {
        "x": 0.0,
        "y": 0.0,
        "z": 200.0,
        "rotationZ": 0.0
      }
    }
  }
}
```

Обрабатывать как обычное перемещение персонажа — телепорт на новую позицию.  
Если персонаж отображался как мёртвый (лежащая модель) — восстановить его в живое состояние.

---

## 6. Эффект «Болезнь Воскрешения» (Resurrection Sickness)

После респавна в `stats_update.body.activeEffects` придёт:

```json
{
  "slug": "resurrection_sickness",
  "effectTypeSlug": "debuff",
  "attributeSlug": "",
  "value": -20.0,
  "expiresAt": 1741872600
}
```

| Поле | Значение |
|---|---|
| `slug` | `"resurrection_sickness"` |
| `effectTypeSlug` | `"debuff"` |
| `attributeSlug` | `""` (визуальный дебафф, без конкретного атрибута) |
| `value` | `-20.0` (−20% к отображаемым характеристикам) |
| `expiresAt` | Unix timestamp (секунды) — время окончания |

Длительность: **120 секунд**.

**Что показывать в UI**:
- Отображать иконку эффекта дебаффа (подгружать на основе `slug` из таблицы).
- Подсказка при наведении курсором описания эффекта из таблицы.
- Таймер обратного отсчёта (вычисляется как `expiresAt - текущее_время`).

Клиент **не должен** самостоятельно применять -20% к базовым статам. Сервер присылает уже итоговые `effective` значения атрибутов в `stats_update`. Эффект нужен только для отображения иконки/таймера дебаффа.

---

## 7. Долг опыта (Experience Debt)

### Как работает

- При смерти накапливается **долг** вместо прямого вычитания XP.
- Долг не уменьшает текущий опыт — он показывает «сколько следующего опыта уйдёт в погашение».
- При каждом получении XP **50% уходит на погашение долга**, 50% засчитывается обычно.

### Пример

```
Текущий XP:    48200
Долг:           3500
Получено XP:   1000

→ На долг: min(1000/2, 3500) = 500
→ К опыту:  500
→ Итог:  XP = 48700, Долг = 3000
```

### Что отображать

В окне персонажа / HUD опыта:

```
XP: 48700 / 60000
Долг: 3000  ←  показывать красным на прогресс-баре или как-то визуально отображать
```

Если `debt == 0` — долг не отображать.

---

## 8. Полная последовательность событий при смерти и воскрешении

```
СМЕРТЬ:
  Сервер → Клиент:  stats_update  (health.current = 0, experience.debt = N)
  Клиент:           показать экран смерти, заблокировать управление

ВОСКРЕШЕНИЕ (через кнопку):
  Клиент → Сервер:  respawnRequest
  Сервер → Клиент:  respawnResult  (новая позиция)
  Сервер → Клиент:  stats_update   (HP/Mana 30%, activeEffects: resurrection_sickness)
  Сервер → Все:     moveCharacter   (broadcast новой позиции)
  Клиент:           телепорт, анимация воскрешения, показать дебафф, убрать экран смерти
```

---

## 9. Движение персонажа — протокол и серверная валидация

### 9.1 Пакет движения (Клиент → Сервер)

```json
{
  "header": {
    "eventType": "moveCharacter",
    "clientId": 42,
    "hash": "abc123",
    "serverRecvMs": 0,
    "serverSendMs": 0,
    "clientSendMs": 1741872480000
  },
  "body": {
    "characterId": 7,
    "posX": 125.3,
    "posY": 47.0,
    "posZ": 10.0,
    "rotZ": 1.57
  }
}
```

Поля `posX`, `posY`, `posZ` — мировые координаты. `rotZ` — вращение по оси Z в радианах.  
`characterId` в теле используется как дополнительный источник, если он не определён из сессии.

### 9.2 Broadcast ответ (Сервер → Все клиенты)

При успешной обработке сервер рассылает позицию **всем** подключённым клиентам:

```json
{
  "header": {
    "status": "success",
    "message": "Movement success for character!",
    "hash": "",
    "clientId": 42,
    "eventType": "moveCharacter",
    "serverRecvMs": 1741872480120,
    "serverSendMs": 1741872480122,
    "clientSendMs": 1741872480000
  },
  "body": {
    "character": {
      "id": 7,
      "position": {
        "x": 125.3,
        "y": 47.0,
        "z": 10.0,
        "rotationZ": 1.57
      }
    }
  }
}
```

### 9.3 Ошибка при движении мёртвого персонажа

Первая серверная проверка — жизненное состояние. Если `health.current == 0`:

```json
{
  "header": {
    "status": "error",
    "eventType": "moveCharacter",
    "message": "Cannot move while dead",
    "clientId": 42,
    "serverRecvMs": 1741872480120,
    "serverSendMs": 1741872480122,
    "clientSendMs": 1741872480000
  },
  "body": {}
}
```

### 9.4 Серверная валидация скорости перемещения

Сервер применяет **авторитарную anti-cheat проверку скорости** для каждого `moveCharacter` пакета.

**Алгоритм:**
1. Берётся атрибут `move_speed` из набора атрибутов персонажа (дефолт: **5 units/sec**).
2. Вычисляется `deltaMs = serverRecvMs_текущего - serverRecvMs_предыдущего` (используются серверные timestamps — не подделываемые клиентом).
3. `maxAllowedDistance = moveSpeed × (deltaMs / 1000) × 1.3` — 30% буфер на lag.
4. `actualDistance = √(Δx² + Δy² + Δz²)`.
5. Если `actualDistance > maxAllowedDistance` — позиция **отклоняется** (без broadcast), клиенту отправляется `positionCorrection`.

**Первый пакет** после join или respawn всегда принимается (сервер в состоянии `uninitialized`). Состояние сбрасывается после телепорта.

**При принятии** позиция записывается в два места: `lastValidatedPosition` (для следующей проверки) и основной позиционный стейт (`setCharacterPosition`).

### 9.5 Packet: `positionCorrection` (Server → Client, только отправителю)

Отправляется **только** при нарушении лимита скорости. Broadcast НЕ производится.

```json
{
  "header": {
    "status": "error",
    "eventType": "positionCorrection",
    "message": "Position validation failed",
    "clientId": 42,
    "serverRecvMs": 1741872480120,
    "serverSendMs": 1741872480122,
    "clientSendMs": 1741872480000
  },
  "body": {
    "characterId": 7,
    "position": {
      "x": 100.0,
      "y": 200.0,
      "z": 0.0,
      "rotationZ": 1.57
    }
  }
}
```

**Поведение клиента** при получении `positionCorrection`:
1. Телепортировать персонажа на `body.position` (серверная last-valid позиция).
2. Сбросить локальный prediction/движение к этой точке.
3. Не показывать ошибку пользователю (это технический correction, не gameplay-событие).

### 9.6 Логика на клиенте

**Отправка своего движения:**
1. Отправлять `moveCharacter` при каждом изменении позиции/вращения (можно с троттлингом, например раз в 50–100 мс).
2. **Не отправлять**, если персонаж мёртв (`health.current == 0`). Сервер всё равно отклонит пакет.

**Получение чужого движения:**
1. Найти персонажа по `body.character.id`.
2. Применить интерполяцию/lerp к новой позиции `body.character.position`.
3. Если персонаж ранее отображался как мёртвый, а пришёл `moveCharacter` с валидной позицией — это значит он воскрес (дополнительный сигнал к `respawnResult`/`stats_update`).

**Получение своего echo (если сервер прислал ваш же `moveCharacter`):**  
Сервер рассылает broadcast включая отправителя. Клиент должен игнорировать собственный `moveCharacter` при reconciliation или применять серверную позицию для коррекции drift.

---

## 10. Рекомендации по реализации

| Задача | Рекомендация |
|---|---|
| Определить «жив ли персонаж» | `characterData.currentHealth > 0` |
| Таймер Resurrection Sickness | Вычислять от `expiresAt` из `activeEffects`, обновлять каждую секунду |
| Долг XP в HUD | Показывать под шкалой XP только если `debt > 0` |
| Кнопка «Воскреснуть» | Активна только когда `currentHealth == 0` |
| Блокировка UI мёртвого | Движение, скиллы, торговля, подбор, диалоги |
| Позиция после респавна | Используется из `respawnResult.body.position`, не из `moveCharacter` |
| Троттлинг движения | Отправлять не чаще раза в 50–100 мс |
| Интерполяция чужих игроков | Lerp между текущей и новой позицией из `moveCharacter` broadcast |
| Reconciliation своей позиции | Применять серверную позицию из echo для коррекции drift |
| Обработка `positionCorrection` | Немедленный телепорт на `body.position`, сброс prediction |
