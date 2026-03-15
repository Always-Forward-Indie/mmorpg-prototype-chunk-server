# План: Система Экипировки

**Версия плана:** v1.0  
**Дата:** 2026-03-11  
**Текущая версия проекта:** v0.0.4

---

## Содержание

1. [Текущее состояние](#1-текущее-состояние)
2. [Слоты экипировки — enum и схема БД](#2-слоты-экипировки--enum-и-схема-бд)
3. [Пакеты](#3-пакеты)
4. [Валидация при экипировке](#4-валидация-при-экипировке)
5. [Двуручное оружие](#5-двуручное-оружие)
6. [Ограничения по классу — полная логика](#6-ограничения-по-классу--полная-логика)
7. [Система износа — двухзонная модель](#7-система-износа--двухзонная-модель)
8. [Set Bonuses](#8-set-bonuses)
9. [Вес — решение](#9-вес--решение)
10. [Изменения в DB схеме](#10-изменения-в-db-схеме)
11. [Изменения в C++ структурах](#11-изменения-в-c-структурах)
12. [Роадмап реализации](#12-роадмап-реализации)

---

## 1. Текущее состояние

### Реализовано ✅

| Система | Описание |
|---------|----------|
| `ItemDataStruct` | `isEquippable`, `equipSlot`, `equipSlotSlug`, `levelRequirement`, `durabilityMax`, `attributes[]` |
| `ItemAttributeStruct.apply_on` | `"equip"` \| `"use"` — принцип работы бонусов заложен |
| `PlayerInventoryItemStruct.isEquipped` | bool-флаг, true когда запись есть в `character_equipment` |
| Таблица `character_equipment` | Существует в БД, JOIN-ится в `get_character_attributes` |
| SQL `get_character_attributes` | Суммирует бонусы всех equipped предметов через CTE `equip_bonus` |
| `GET_CHARACTER_ATTRIBUTES_REFRESH` | Game Server → Chunk Server: пересчёт статов |
| `SET_CHARACTER_ATTRIBUTES_REFRESH` | Chunk Server принимает обновлённые статы |
| Durability поля | `isDurable`, `durabilityMax` в ItemDataStruct; `durabilityCurrent` в PlayerInventoryItemStruct |
| `REPAIR_ITEM`, `REPAIR_ALL`, `OPEN_REPAIR_SHOP` | Event-типы задекларированы |
| `game_config` ключи для durability | `death_penalty_pct`, `weapon_loss_per_hit`, `armor_loss_per_hit` |
| `levelRequirement` | Читается из БД, хранится в памяти |

### Есть структуры, нет логики ⚠️

| Что отсутствует | Последствие |
|-----------------|-------------|
| Пакеты `equipItem` / `unequipItem` | Игрок **физически не может** надеть предмет |
| Пакет `getEquipment` / ответ `EQUIPMENT_STATE` | Клиент не знает текущую экипировку при входе |
| Enum слотов в C++ коде | Нельзя валидировать "занят ли слот" без запроса в БД |
| Enforcement `levelRequirement` | Число есть — проверки нет |
| Ограничения по классу | Любой класс надевает любой предмет |
| `isTwoHanded` | Двуручное оружие блокирует щит — логики нет |
| Двухзонная модель durability | Штрафы в warning zone не применяются |

---

## 2. Слоты экипировки — enum и схема БД

### C++ enum

```cpp
enum class EquipSlot : int {
    NONE       = 0,
    HEAD       = 1,
    CHEST      = 2,
    LEGS       = 3,
    FEET       = 4,
    HANDS      = 5,
    WAIST      = 6,
    NECKLACE   = 7,
    RING_1     = 8,
    RING_2     = 9,
    MAIN_HAND  = 10,
    OFF_HAND   = 11,
    CLOAK      = 12
};
```

Слаги (для JSON пакетов и БД): `head`, `chest`, `legs`, `feet`, `hands`, `waist`, `necklace`, `ring_1`, `ring_2`, `main_hand`, `off_hand`, `cloak`.

### Таблица `equip_slots` (если ещё нет)

```sql
CREATE TABLE IF NOT EXISTS equip_slots (
    id    serial      PRIMARY KEY,
    name  varchar(64) NOT NULL,
    slug  varchar(64) NOT NULL UNIQUE
);

INSERT INTO equip_slots (id, name, slug) VALUES
    (1,  'Head',      'head'),
    (2,  'Chest',     'chest'),
    (3,  'Legs',      'legs'),
    (4,  'Feet',      'feet'),
    (5,  'Hands',     'hands'),
    (6,  'Waist',     'waist'),
    (7,  'Necklace',  'necklace'),
    (8,  'Ring 1',    'ring_1'),
    (9,  'Ring 2',    'ring_2'),
    (10, 'Main Hand', 'main_hand'),
    (11, 'Off Hand',  'off_hand'),
    (12, 'Cloak',     'cloak');
```

---

## 3. Пакеты

### 3.1 `equipItem` — надеть предмет

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "equipItem", "clientId": 7 },
  "body": {
    "characterId": 101,
    "inventoryItemId": 55
  }
}
```

**Поля:**
- `inventoryItemId` — `player_inventory.id` (не `item_id`). Позволяет различать несколько стаков одного предмета.

**Ошибки:** `ITEM_NOT_IN_INVENTORY`, `ITEM_NOT_EQUIPPABLE`, `LEVEL_REQUIREMENT_NOT_MET`, `CLASS_RESTRICTION`, `SLOT_BLOCKED_BY_TWO_HANDED`, `EQUIP_FAILED`

---

### 3.2 `unequipItem` — снять предмет

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "unequipItem", "clientId": 7 },
  "body": {
    "characterId": 101,
    "equipSlotSlug": "main_hand"
  }
}
```

**Примечание:** снимаем по слоту, а не по ID предмета — слот всегда однозначно идентифицирует что снять.

**Ошибки:** `SLOT_EMPTY`, `UNEQUIP_FAILED`

---

### 3.3 `getEquipment` — запросить текущую экипировку

**Направление:** Client → Chunk Server

```json
{
  "header": { "eventType": "getEquipment", "clientId": 7 },
  "body": { "characterId": 101 }
}
```

Отдельный запрос нужен для случаев переоткрытия UI без перелогина. Chunk Server отвечает пакетом `EQUIPMENT_STATE`.

---

### 3.4 `EQUIPMENT_STATE` — текущая экипировка персонажа

**Направление:** Chunk Server → Client  
**Когда отправляется:**
1. Ответ на `getEquipment`
2. После `joinGameCharacter` (вместе с инвентарём)
3. После успешного `equipItem` / `unequipItem`

```json
{
  "header": {
    "message": "success",
    "eventType": "EQUIPMENT_STATE",
    "clientId": 7
  },
  "body": {
    "characterId": 101,
    "slots": {
      "main_hand": {
        "inventoryItemId": 55,
        "itemId": 12,
        "itemSlug": "iron_sword",
        "durabilityCurrent": 80,
        "durabilityMax": 100,
        "isDurabilityWarning": false
      },
      "chest": {
        "inventoryItemId": 61,
        "itemId": 8,
        "itemSlug": "leather_chest",
        "durabilityCurrent": 22,
        "durabilityMax": 100,
        "isDurabilityWarning": true
      },
      "head": null,
      "legs": null,
      "feet": null,
      "hands": null,
      "waist": null,
      "necklace": null,
      "ring_1": null,
      "ring_2": null,
      "off_hand": null,
      "cloak": null
    }
  }
}
```

**Примечание по `itemSlug`:** клиент привязывает визуал, иконку и название к слагу предмета — отдельные поля `modelId` / `iconSlug` не нужны.

`isDurabilityWarning` — вычисляется на сервере: `durabilityCurrent < durabilityMax * durability.warning_threshold_pct`. Клиент просто рисует индикатор красным/жёлтым, не считает сам.

---

### 3.5 `EQUIP_RESULT` — результат операции

**Направление:** Chunk Server → Client

```json
{
  "header": { "message": "success", "eventType": "EQUIP_RESULT", "clientId": 7 },
  "body": {
    "action": "equip",
    "inventoryItemId": 55,
    "equipSlotSlug": "main_hand",
    "swappedOutInventoryItemId": 33
  }
}
```

`swappedOutInventoryItemId` — заполняется если в слоте уже был предмет и он автоматически снят в инвентарь. `null` если слот был пуст.

---

## 4. Валидация при экипировке

Flow на Chunk Server при получении `equipItem`:

```
1. Найти PlayerInventoryItemStruct по inventoryItemId
   → ошибка ITEM_NOT_IN_INVENTORY если не найден или не принадлежит characterId

2. Найти ItemDataStruct по item_id
   → ошибка ITEM_NOT_EQUIPPABLE если isEquippable = false

3. Проверить levelRequirement
   → ошибка LEVEL_REQUIREMENT_NOT_MET если character.level < item.levelRequirement

4. Проверить class restrictions
   → ошибка CLASS_RESTRICTION если item.allowedClassIds не пуст
     И character.classId не входит в список

5. Проверить isTwoHanded (см. раздел 5)
   → ошибка SLOT_BLOCKED_BY_TWO_HANDED если нужно

6. Если слот занят — снять текущий предмет в инвентарь (auto-swap)

7. Сохранить в БД: INSERT INTO character_equipment
   + опционально DELETE старой записи если был auto-swap

8. Отправить Game Server: GET_CHARACTER_ATTRIBUTES_REFRESH
   → Game Server пересчитывает статы и шлёт SET_CHARACTER_ATTRIBUTES_REFRESH обратно

9. Отправить клиенту: EQUIP_RESULT + EQUIPMENT_STATE + STATS_UPDATE
```

---

## 5. Двуручное оружие

### Новое поле в ItemDataStruct

```cpp
bool isTwoHanded = false; // только для main_hand предметов
```

### Логика при equipItem в `main_hand`

- Если `item.isTwoHanded = true`:
  - Если в `off_hand` есть предмет → автоматически снять его в инвентарь
  - Заблокировать слот `off_hand` (записать в in-memory маппинг: `off_hand` заблокирован main_hand'ом)

- Если в `main_hand` уже было двуручное и теперь экипируется одноручное:
  - Разблокировать `off_hand`

### Блокировка off_hand

Chunk Server хранит in-memory per-character:

```cpp
std::unordered_map<int, bool> twoHandedActive; // characterId → true/false
```

При попытке экипировать в `off_hand` когда `twoHandedActive[characterId] = true` → ошибка `SLOT_BLOCKED_BY_TWO_HANDED`.

В ответе `EQUIPMENT_STATE` слот `off_hand` при двуручнике возвращается как:
```json
"off_hand": { "blockedByTwoHanded": true }
```

---

## 6. Ограничения по классу — полная логика

### Концепция

Ограничения **опциональные**: если у предмета нет записей в `item_class_restrictions` — надевает любой класс. Это правильный дефолт для прототипа: ограничения добавляются точечно только там, где нужны.

### Схема БД

```sql
CREATE TABLE IF NOT EXISTS item_class_restrictions (
    item_id  integer NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    class_id integer NOT NULL REFERENCES character_class(id) ON DELETE CASCADE,
    PRIMARY KEY (item_id, class_id)
);

-- Пример: полуторный меч только для воина и паладина
INSERT INTO item_class_restrictions (item_id, class_id) VALUES (42, 1), (42, 3);
```

### Загрузка данных (Game Server)

SQL при `get_items` расширить: подтянуть all class IDs per item через `array_agg` или отдельным запросом:

```sql
SELECT item_id, array_agg(class_id) AS allowed_class_ids
FROM item_class_restrictions
GROUP BY item_id;
```

Результат загружается в `ItemDataStruct`:

```cpp
std::vector<int> allowedClassIds; // пустой = без ограничений
```

### Данные персонажа в Chunk Server

В `CharacterDataStruct` (или аналогичной in-memory структуре Chunk Server'а) добавить `classId: int`. Сейчас `get_character` возвращает только `character_class` (name). Нужно добавить `character_class.id` в SELECT и прокидывать на Chunk Server при `joinGameCharacter`.

### Валидация при экипировке (код)

```cpp
if (!item.allowedClassIds.empty()) {
    bool allowed = std::find(
        item.allowedClassIds.begin(),
        item.allowedClassIds.end(),
        character.classId
    ) != item.allowedClassIds.end();

    if (!allowed) {
        return EquipError::CLASS_RESTRICTION;
    }
}
```

### Что видит клиент

В пакете инвентаря (`inventoryUpdate`) к каждому equippable предмету добавить поле `allowedClassIds: [1, 3]`. Пустой массив = без ограничений.

Клиент сам сравнивает с `localPlayer.classId` и:
- Если ограничение есть и класс не подходит → предмет в инвентаре показывается с иконкой замка и тултипом "Требует: Воин, Паладин"
- Попытка надеть такой предмет на клиенте блокируется до отправки на сервер (client-side pre-validation)
- Сервер всё равно валидирует независимо (client validation — только UX, не security)

---

## 7. Система износа — двухзонная модель

### Концепция

```
100% ──────────────────── 30% [WARNING] ──── 1% │ 0% [BROKEN]
     Полные статы предмета  │  Штраф -N%        │  Нет бонусов
```

- **Зона нормы (100% → 31%)**: предмет даёт полные статы. Нет тревожности, нет менеджмента.
- **Зона предупреждения (30% → 1%)**: заметный штраф к бонусам предмета. Яркий визуальный сигнал в UI. Игрок должен принять решение: продолжать или идти к кузнецу.
- **Сломан (0%)**: предмет не даёт никаких бонусов.

Пороговые значения и штраф **не хардкодятся** — все в `game_config`.

### Новые ключи в game_config

```sql
INSERT INTO game_config (key, value, value_type, description) VALUES
    ('durability.warning_threshold_pct', '0.30', 'float',
     'Порог зоны предупреждения. Если durabilityCurrent / durabilityMax < этого значения — применяется штраф. По умолчанию 30%.'),
    ('durability.warning_penalty_pct',   '0.15', 'float',
     'Штраф к бонусам предмета в зоне предупреждения (0–1). 0.15 = -15% от всех item attribute бонусов. По умолчанию 15%.')
ON CONFLICT (key) DO NOTHING;
```

### Модификация SQL запроса `get_character_attributes`

CTE `equip_bonus` расширяется для применения двухзонной логики:

```sql
equip_bonus AS (
    SELECT
        iam.attribute_id,
        SUM(
            CASE
                -- Сломан (durability = 0): бонус не считается
                WHEN pi.durability_current = 0 AND i.is_durable = true THEN 0
                -- Зона предупреждения: применяем штраф
                WHEN i.is_durable = true
                    AND pi.durability_current::float / NULLIF(i.durability_max, 0) < $warning_threshold
                THEN ROUND(iam.value * (1.0 - $warning_penalty))
                -- Норма или не дюрабл: полный бонус
                ELSE iam.value
            END
        )::int AS bonus
    FROM character_equipment ce
    JOIN player_inventory pi ON pi.id = ce.inventory_item_id
    JOIN items i ON i.id = pi.item_id
    JOIN item_attributes_mapping iam
        ON iam.item_id = pi.item_id AND iam.apply_on = 'equip'
    WHERE ce.character_id = $1
    GROUP BY iam.attribute_id
)
```

`$warning_threshold` и `$warning_penalty` подставляются из `GameConfigService` при построении запроса (не хардкод).

**Примечание:** Этот пересчёт происходит при `GET_CHARACTER_ATTRIBUTES_REFRESH`. Значит при ударе (убыль durability) нужно тригерить refresh если новое значение пересекло порог — не при каждом ударе (дорого), а только при пересечении границы 30%.

### Визуальный индикатор в EQUIPMENT_STATE

`isDurabilityWarning: bool` уже включён в пакет `EQUIPMENT_STATE` (раздел 3.4). Клиент рисует:
- `> 30%` → зелёный / нейтральный
- `≤ 30% и > 0%` → жёлтый/красный + цифра текущего durability
- `= 0%` → серый значок с иконкой "сломан"

### Убыль durability — триггеры

| Событие | Что теряет durability | Значение |
|---------|----------------------|----------|
| Успешная атака персонажа | `main_hand` (оружие) | `durability.weapon_loss_per_hit` |
| Получение удара персонажем | Каждый надетый броне-слот | `durability.armor_loss_per_hit` |
| Смерть персонажа | Все equipped durable items | `durabilityMax * durability.death_penalty_pct` |

Убыль применяется только к `isDurable = true` предметам.

После каждого изменения durability:
1. Обновить `PlayerInventoryItemStruct.durabilityCurrent` in-memory
2. Отправить клиенту `DURABILITY_UPDATE` (уже задекларирован в Event.hpp)
3. Если пересечён порог 30% или достигнут 0 → триггерить `GET_CHARACTER_ATTRIBUTES_REFRESH`
4. Персистировать через `SAVE_DURABILITY_CHANGE` (уже задекларирован в Game Server Event.hpp)

---

## 8. Set Bonuses

### Мотивация

Игрок у которого 3 из 4 предметов сета **целенаправленно гриндит** ради завершения. Это конкретная medium-term goal без написания квеста. Высокий ROI для retention при низких технических затратах.

### DB схема

```sql
CREATE TABLE IF NOT EXISTS item_sets (
    id    serial      PRIMARY KEY,
    name  varchar(128) NOT NULL,
    slug  varchar(128) NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS item_set_members (
    set_id  integer NOT NULL REFERENCES item_sets(id) ON DELETE CASCADE,
    item_id integer NOT NULL REFERENCES items(id)     ON DELETE CASCADE,
    PRIMARY KEY (set_id, item_id)
);

-- Бонусы по количеству надетых предметов сета
CREATE TABLE IF NOT EXISTS item_set_bonuses (
    id               serial      PRIMARY KEY,
    set_id           integer     NOT NULL REFERENCES item_sets(id) ON DELETE CASCADE,
    pieces_required  integer     NOT NULL, -- 2, 4, 6
    attribute_id     integer     NOT NULL REFERENCES entity_attributes(id),
    bonus_value      integer     NOT NULL  -- аддитивный бонус к стату
);
```

### Интеграция в get_character_attributes

Добавить CTE `set_bonus` после `equip_bonus`:

```sql
set_bonus AS (
    SELECT isb.attribute_id, SUM(isb.bonus_value)::int AS bonus
    FROM (
        -- Считаем сколько предметов каждого сета надето
        SELECT ism.set_id, COUNT(*) AS equipped_count
        FROM character_equipment ce
        JOIN player_inventory pi ON pi.id = ce.inventory_item_id
        JOIN item_set_members ism ON ism.item_id = pi.item_id
        WHERE ce.character_id = $1
        GROUP BY ism.set_id
    ) equipped_sets
    JOIN item_set_bonuses isb
        ON isb.set_id = equipped_sets.set_id
        AND equipped_sets.equipped_count >= isb.pieces_required
    GROUP BY isb.attribute_id
)
```

Добавить в финальный SELECT: `+ COALESCE(sb.bonus, 0)` и `LEFT JOIN set_bonus sb ON sb.attribute_id = ba.id`.

---

## 9. Вес — решение

`float weight = 0.0f` в `ItemDataStruct` есть, но систему переноса нет.

**Решение: реализовать, не удалять поле.**

Вес — один из немногих механизмов делающих `strength` атрибут значимым для non-воинских билдов (маг хочет носить больше зелий → берёт немного силы). Это emergent gameplay.

Минимальная реализация:
- Базовый лимит переноса: `baseCarryWeight = 50 + strength * 3` (формула из game_config)
- Текущий вес: сумма `weight * quantity` по всему инвентарю + equipped
- При превышении лимита: `OVERWEIGHT` debuff (ActiveEffect: `-30%` к скорости движения)
- Проверка при каждом `itemPickup` и `equipItem`

**game_config ключи:**

```sql
INSERT INTO game_config (key, value, value_type, description) VALUES
    ('carry_weight.base',            '50',  'int',   'Базовый лимит переноса до учёта силы.'),
    ('carry_weight.per_strength',    '3',   'float', 'Единиц веса за каждую единицу strength.'),
    ('carry_weight.overweight_speed_penalty', '0.30', 'float', 'Штраф к скорости при перегрузке (0–1). 0.30 = -30%.')
ON CONFLICT (key) DO NOTHING;
```

---

## 10. Изменения в DB схеме

### Новые таблицы

```sql
-- Слоты экипировки (если нет)
CREATE TABLE IF NOT EXISTS equip_slots ( ... ); -- см. раздел 2

-- Ограничения по классу
CREATE TABLE IF NOT EXISTS item_class_restrictions (
    item_id  integer NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    class_id integer NOT NULL REFERENCES character_class(id) ON DELETE CASCADE,
    PRIMARY KEY (item_id, class_id)
);

-- Сеты предметов
CREATE TABLE IF NOT EXISTS item_sets ( ... );           -- см. раздел 8
CREATE TABLE IF NOT EXISTS item_set_members ( ... );    -- см. раздел 8
CREATE TABLE IF NOT EXISTS item_set_bonuses ( ... );    -- см. раздел 8
```

### Изменения в существующих таблицах

```sql
-- Двуручное оружие
ALTER TABLE items ADD COLUMN IF NOT EXISTS is_two_handed boolean NOT NULL DEFAULT false;

-- Class restrictions в items (денормализованный кэш для быстрой загрузки)
-- Используем join table item_class_restrictions, items не меняем

-- game_config: новые ключи (см. разделы 7 и 9)
```

---

## 11. Изменения в C++ структурах

### ItemDataStruct

```cpp
bool isTwoHanded = false;
std::vector<int> allowedClassIds; // пустой = без ограничений
int setId = 0;                    // 0 = не входит в сет
std::string setSlug = "";
```

### CharacterDataStruct (Chunk Server)

```cpp
int classId = 0;     // нужен для валидации class restrictions
std::string classSlug = ""; // опционально, для строгих проверок
```

### Новая структура EquipmentSlotState (для EQUIPMENT_STATE пакета)

```cpp
struct EquipmentSlotItemStruct {
    int inventoryItemId = 0;
    int itemId = 0;
    std::string itemSlug = "";
    int durabilityCurrent = 0;
    int durabilityMax = 0;
    bool isDurabilityWarning = false;
    bool blockedByTwoHanded = false; // только для off_hand
};

// per-character in-memory в EquipmentManager
struct CharacterEquipmentStruct {
    int characterId = 0;
    std::unordered_map<std::string, EquipmentSlotItemStruct> slots; // slug → item
    bool twoHandedActive = false;
};
```

---

## 12. Роадмап реализации

| Приоритет | Задача | Зависимости |
|-----------|--------|-------------|
| **P0** | Enum `EquipSlot` в C++ | — |
| **P0** | Структура `CharacterEquipmentStruct` + `EquipmentManager` | EquipSlot enum |
| **P0** | Загрузка текущей экипировки при `joinGameCharacter` | EquipmentManager |
| **P0** | Пакет `EQUIPMENT_STATE` + отправка при join | EquipmentManager |
| **P0** | `equipItem` / `unequipItem` пакеты + валидация + DB мутация | EquipmentManager |
| **P0** | Enforcement `levelRequirement` в equip flow | — |
| **P0** | Добавить `classId` в CharacterDataStruct + загрузку | DB query change |
| **P1** | `isTwoHanded` поле + логика блокировки `off_hand` | EquipmentManager |
| **P1** | Class restrictions: DB таблица + `allowedClassIds` в ItemDataStruct | — |
| **P1** | Двухзонная durability в `get_character_attributes` SQL | game_config ключи |
| **P1** | Триггер `GET_CHARACTER_ATTRIBUTES_REFRESH` при пересечении порога | Durability logic |
| **P1** | `isDurabilityWarning` в `EQUIPMENT_STATE` | Двухзонная модель |
| **P2** | Set bonuses: DB схема + CTE в `get_character_attributes` | — |
| **P2** | Вес: расчёт + OVERWEIGHT debuff | game_config ключи |
| **P2** | `getEquipment` пакет (on-demand запрос экипировки) | EquipmentManager |
