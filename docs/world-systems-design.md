# Дизайн: Мировые Системы и Игровой Опыт

**Версия документа:** v1.2  
**Дата:** 2026-03-14  
**Текущая версия проекта:** v0.0.4  
**Статус:** Living Document — план реализации и доработок

---

## Содержание

1. [Обзор и архитектура идей](#1-обзор-и-архитектура-идей)
2. [Этап 1 — Quick Wins (минимальный effort, высокий impact)](#2-этап-1--quick-wins)
3. [Этап 2 — Глубина мира](#3-этап-2--глубина-мира)
4. [Этап 3 — Экосистема мобов](#4-этап-3--экосистема-мобов)
5. [Этап 4 — Социальные системы и долгосрочный retention](#5-этап-4--социальные-системы)
6. [Связь между системами (общий loop)](#6-связь-между-системами)
7. [Итоговая таблица приоритетов](#7-итоговая-таблица-приоритетов)

---

## 1. Обзор и архитектура идей

Все системы в этом документе образуют единую экосистему. Они не независимые фичи — каждая усиливает остальные. Ключевой принцип: **меньше явного UI, больше поведения мира**.

```
Игрок гриндит мобов
        ↓
[Репутация] растёт с локальной фракцией
[Mastery]   растёт навык оружия
[Fellowship] бонус XP если рядом другой игрок
        ↓
[Threshold] Спавнится Чемпион / Zone Event
        ↓
[Pity] Повышенный шанс на редкий дроп после долгого фарма
[Item Soul] Предмет матереет от убийств
        ↓
[Rare Mob] Ночью / по условию появляется редкий моб
[Exploration] Впервые зашёл в новую зону → XP + флаг
        ↓
[Item→NPC] Редкий дроп открывает новую реплику диалога → квест
[Reputation] NPC реагирует иначе в зависимости от репы
[Bestiary] Бестиарий накапливает знания о мобах
```

---

## 2. Этап 1 — Quick Wins

> **Критерий включения:** минимальная новая архитектура, максимальный видимый эффект для игрока.  
> Всё строится на уже существующих примитивах (флаги, ActiveEffectStruct, диалоговый граф).

---

### 2.1 Exploration Rewards (Туман войны с наградой)

**Суть:** мир поделён на именованные зоны. Когда игрок **впервые** входит в зону —
небольшой XP burst и запись в журнале (`"Ты исследовал: Мёртвый Лес"`).
Флаг `explored_<zone_slug>` сохраняется в БД.

**Зачем:** мотивирует ходить везде, а не только гриндить один спавн. Anti-stagnation hook.

**Расширение:** флаг `explored_dead_forest == true` → новые ветки диалога с NPC.
Реализуется через существующее условие диалога:
```json
{ "type": "flag", "key": "explored_dead_forest", "eq": true }
```

**Технически — что уже есть:**
- Таблица `zones` (id, slug, name, min_level, max_level, is_pvp, is_safe_zone) в БД **существует**, но:
  - нет геометрических bounds (min_x/max_x/min_y/max_y) — нельзя определить в какой зоне стоит игрок
  - нет `exploration_xp_reward` колонки
  - таблица **нигде не загружается** (ни game server, ни chunk server не читают `zones`)
- `ZoneBounds` helper struct в chunk server уже есть (`include/data/DataStructs.hpp`) — умеет делать point-in-zone check
- `PlayerContextStruct::flagsBool` уже хранит флаги — `explored_<slug>` туда ложится без архитектурных изменений
- `ExperienceManager::grantExperience()` уже готов принимать причину — просто вызываем с `reason = "zone_explored"`

**Шаги реализации:**
1. **Миграция БД** — добавить колонки к `zones`:
   ```sql
   ALTER TABLE zones ADD COLUMN min_x FLOAT DEFAULT 0;
   ALTER TABLE zones ADD COLUMN max_x FLOAT DEFAULT 0;
   ALTER TABLE zones ADD COLUMN min_y FLOAT DEFAULT 0;
   ALTER TABLE zones ADD COLUMN max_y FLOAT DEFAULT 0;
   ALTER TABLE zones ADD COLUMN exploration_xp_reward INT DEFAULT 100;
   ```
   Заполнить bounds для существующих зон данными.
2. **Game Server** — добавить SQL prepared statement для чтения `zones` (аналогично `spawn_zones`). Добавить `GameZoneStruct` (id, slug, name, bounds, xp_reward). Отправлять загруженные зоны на chunk server при старте (аналогично spawn zones событию).
3. **Chunk Server** — добавить `GameZoneManager` (или расширить `SpawnZoneManager`) с методом `getZoneForPosition(pos) → optional<GameZoneStruct>`. Добавить обработчик события загрузки зон.
4. **Смена зоны** — в `CharacterEventHandler` или `MobMovementManager` (где уже отслеживается перемещение): при каждом обновлении позиции персонажа проверять `getZoneForPosition`. Если зона изменилась — проверить флаг `explored_<slug>` через `QuestManager::checkFlag`. Если флага нет → `grantExperience` + `setFlag` + отправить `worldNotification(zone_explored)`.
5. **Persist** — `setFlagBool` ✅ реализован в `QuestManager`: обновляет in-memory кэш и ставит в очередь на persist через game server.

**Статус:** ✅ Реализовано (migration 036, GameZoneManager, CharacterEventHandler, QuestManager::getFlagBool/setFlagBool)

---

### 2.2 Fellowship Bonus (Социальный множитель)

**Суть:** если в момент убийства моба **другой игрок тоже атаковал этого же моба**
за последние N секунд — оба получают небольшой бонус XP.
Общий лут не нужен, группа не нужна. Совместная охота — выгодна.

**Зачем:** создаёт органическое социальное взаимодействие без принуждения к группе.
Именно так строится комьюнити в ранних UO и early WoW.

**Ключевое уточнение:** бонус даётся только если оба игрока атаковали **именно этого** моба,
не просто находились рядом. Это исключает случайных прохожих и делает механику честной.

**Размер бонуса:** +5–8% XP (настраивается в конфиге). Намеренно небольшой —
ценность не в числе, а в социальном сигнале: "вместе выгоднее".

**Защита от злоупотреблений:**
- Каждый участник должен иметь `lastAttackOnTarget[mob_id]` < 15 секунд назад
- Бонус масштабируется на **всех** участников атаки (без ограничения числа) — честная механика, reward за реально совместную охоту
- ~~Персонаж рядом не должен быть тем же `account_id`~~ — anti-alt защита реализована: `ClientDataStruct::accountId` добавлен в chunk server, заполняется при join, используется в CombatSystem::fellowship для фильтрации co-attackers с одинаковым accountId

**Технически:**
- В `CombatSystem::handleMobDeath()`: получить список character_id из `mobMovData.attackerTimestamps` за последние `attack_window_sec`
- Убийца и каждый participant получают `give_exp(base_exp * fellowship_bonus_pct)`
- `attackerTimestamps` ведётся в `MobDataMovementStruct`, обновляется в `MobAIController` при каждом ударе
- Floating text клиенту: `"+Fellowship XP"` — ✅ через `sendWorldNotification` в `CombatSystem::handleMobDeath`
- Конфиг в `game_config`: `fellowship.bonus_pct = 0.07`, `fellowship.attack_window_sec = 15`

**Статус:** ✅ Реализовано (XP + floating text + anti-alt защита через accountId)

---

### 2.3 Item Soul — История Предмета (kill_count)

**Суть:** предмет накапливает `kill_count`. После достижения порогов — получает
суффикс и небольшой бонус атрибута.

| kill_count | Суффикс | Бонус |
|-----------|---------|-------|
| 0         | —        | —     |
| 50        | [Бывалый] | +1 к атрибуту |
| 200       | [Кровавый] | +2 к атрибуту + 5% crit |
| 500       | [Легендарный] | +3 к атрибуту + 8% crit |

**Важно:** бонус небольшой — ценность не в силе, а в **нарративе и эмоциональной привязке**.
Игрок не хочет продавать меч в котором 200 убийств.
Создаёт tension с системой durability: предмет стареет, но одновременно матереет.

**Технически:**
- Поле `kill_count INT DEFAULT 0` в таблице `inventory_items` **по instance id** строки
- `kill_count` привязан к конкретному экземпляру предмета, не к персонажу:
  - Игрок выбросил предмет — строка в `inventory_items` та же, `kill_count` не обнуляется
  - Другой игрок поднял — строка переезжает к нему (или `character_id` обновляется), `kill_count` сохраняется
  - Передал через торговлю — аналогично. История предмета принадлежит предмету, не владельцу.
- Обновляется в `CombatSystem::handleMobDeath()` для экипированного оружия убийцы
- `killCount` хранится в `PlayerInventoryItemStruct::killCount`, persist через callback → game server → `UPDATE player_inventory SET kill_count`
- Данные загружаются при старте из `player_inventory.kill_count` (migration 035)
- Суффикс генерируется на клиенте из локализации по диапазону `kill_count`
- **⚠ Tier-бонусы (+атрибут) ещё не применяются:** `CharacterStatsNotificationService` не читает `killCount` equipped weapon при расчёте effective stats — нужно добавить
- **DB write debounce** ✅ реализован: flush по tier-порогам или каждые N убийств (`item_soul.db_flush_every_kills`)
- Конфиг в `game_config`: `item_soul.tier1_kills = 50`, `item_soul.tier2_kills = 200`, `item_soul.tier3_kills = 500` и соответствующие бонусы

**Статус:** ✅ Реализовано. Данные собираются, tier-бонусы применяются к effective stats, debounce DB записи реализован.

> **Заметка об архитектуре зон:**  
> В БД существует три понятия:
> - `zones` (id, slug, name, min_level, max_level, is_pvp, is_safe_zone, min_x, max_x, min_y, max_y, exploration_xp_reward) — ✅ смысловые игровые зоны. **С v036: AABB bounds добавлены, загружаются и отправляются на chunk server.**
> - `spawn_zones` (posX/sizeX/posY/sizeY AABB) — зоны спавна мобов. Загружаются, используются.
> - `respawn_zones` (x/y/z точка, zone_id → zones.id) — точки воскрешения.
>
> Для Exploration Rewards нужно: добавить bounds к таблице `zones` (миграция), загрузить их на game server и отправить на chunk server при старте. Chunk server уже имеет `ZoneBounds` helper struct для point-in-zone check.

---

## 3. Этап 2 — Глубина мира

> **Критерий включения:** требует новых таблиц БД и/или новых пакетов, но не новых
> сервисов. Строится поверх уже спроектированных систем.

---

### 3.0 Универсальный пакет оповещений мира — `worldNotification`

**Суть:** единый серверный пакет для всех нарративных/событийных оповещений.
Охватывает: исследование зоны, появление редкого моба, спавн чемпиона, зональное событие,
pity-намёк и любые другие сообщения мира. Клиент отображает их в отдельном канале
(лента событий / floating text / звук — решает клиент).

**Формат пакета (Chunk Server → Client):**
```json
{
  "header": {
    "eventType": "worldNotification",
    "clientId": 42,
    "hash": "abc123",
    "message": "success"
  },
  "body": {
    "notificationType": "zone_explored",
    "scope": "personal",
    "text": "Ты исследовал: Мёртвый Лес",
    "data": {}
  }
}
```

**Поле `notificationType`** — машиночитаемый тип, клиент может менять стиль отображения:

| notificationType | scope | Когда отправляется |
|-----------------|-------|-------------------|
| `zone_explored` | `personal` | Игрок впервые входит в зону |
| `rare_mob_spawned` | `zone` | Спавн редкого моба в зоне |
| `champion_spawned` | `zone` | Спавн чемпиона (Threshold / Timed) |
| `champion_killed` | `zone` | Чемпион убит — кем |
| `zone_event_start` | `zone` | Начало зонального события |
| `zone_event_end` | `zone` | Конец зонального события |
| `pity_hint` | `personal` | Намёк на приближение редкого дропа |
| `survival_evolved` | `zone` | Моб эволюционировал (Survival Champion) |
| `world_announcement` | `global` | Глобальный анонс (будущий контент) |

**Поле `scope`:**
- `personal` — только этому игроку
- `zone` — всем игрокам в зоне
- `global` — всем на сервере

**Поле `data`:** опциональный объект с доп. данными (имя чемпиона, slug зоны и т.д.):
```json
"data": { "mobSlug": "phantom_wolf", "zoneSlug": "dark_forest" }
```

**Статус:** ✅ Реализовано. Пакет `worldNotification` уже существует в `CharacterStatsNotificationService::sendWorldNotification`; broadcast для всех клиентов (зональный scope) работает через `statsUpdateCallback_`.

---

### 3.1 Pity System (Нарастающий шанс дропа)

**Суть:** если игрок давно фармит моба и редкий предмет не выпал — шанс
постепенно растёт (мягкое pity), а на жёстком пороге — гарантированный дроп.

**Числа (стартовые, настраиваемые):**
```
Базовый шанс редкого дропа:   0.1% (1 из 1000)

Мягкое pity:   после 300 убийств без дропа — +0.005% за каждое следующее
Жёсткое pity:  на 800-м убийстве без дропа — гарантированный дроп (hard cap)
```

**Ключевые правила:**
- Pity **per player + per item_id**, не глобальное
- Pity **не показывать явно** игроку — максимум намёк: `"Ты давно охотишься здесь... Удача должна улыбнуться"`
- Pity **сохраняется между сессиями** (в PostgreSQL)
- Pity **сбрасывается после дропа**

**Технически:**
- Таблица: `character_pity(character_id, item_id, kill_count_without_drop)`
- В loot-расчёте: запросить pity counter → модифицировать шанс → при дропе сбросить
- Обновлять при каждом убийстве моба у которого есть редкий дроп

**Статус:** ✅ Реализовано (migration 037). `PityManager` — soft/hard pity, debounced persist через `savePityCounter`. `LootManager` интегрирован с pity-модификатором шанса дропа. Данные загружаются при логине через `getPlayerPityData`.

---

### 3.2 Item → NPC Dialogue Trigger

**Суть:** наличие определённого предмета в инвентаре открывает новые реплики у NPC.
NPC реагирует при открытии диалогового окна — не нужно "сдавать" предмет.

**Три уровня реализации:**

**Уровень 1 — Узнавание:**
NPC видит предмет и меняет тон. `"Откуда у тебя это? Я не видел такого знака уже двадцать лет..."`

**Уровень 2 — Составной триггер:**
Диалог открывается только при сочетании условий (предмет + уровень + квест).
```json
{ "all": [
    { "type": "item", "item_id": 88, "gte": 1 },
    { "type": "level", "gte": 10 },
    { "type": "quest", "slug": "wolf_hunt", "state": "not_started" }
]}
```

**Уровень 3 — Предмет как часть истории:**
Редкий дроп "Призрачный Клык" → старый охотник рассказывает историю
→ квест без убийств (захоронить Клык на поляне).
Предмет это не trigger для kill-quest, а ключ к narrative.

**Технически:**
- Тип условия `{ "type": "item", "item_id": X, "gte": 1 }` **присутствует** в `DialogueConditionEvaluator::evaluateInventory()` ✅
- **⚠ БАГ: `evaluateInventory` сейчас возвращает `true` для всех игроков.** Метод смотрит в `ctx.flagsInt["item_88"]`, но никто этот ключ не выставляет — ни `QuestManager`, ни `InventoryManager` не заполняют `PlayerContextStruct::flagsInt` данными инвентаря. Default — permissive (`return true`). Итог: NPC с item-conditioned диалогом виден **всем игрокам** независимо от инвентаря.

**Шаги реализации:**
1. **Фикс бага (критично):** при построении `PlayerContextStruct` (в `DialogueManager` или `CharacterManager`) заполнять `flagsInt["item_" + item_id] = quantity` для каждого предмета в инвентаре персонажа. Вызов через `InventoryManager::getInventory(characterId)`.
2. После шага 1 — создать диалоговые деревья с item-условиями и добавить соответствующие редкие предметы.

**Статус:** ✅ Исправлено (было исправлено ранее). `DialogueConditionEvaluator::evaluateInventory` корректно читает `ctx.flagsInt["item_N"]`, а `buildPlayerContext` заполняет это поле из инвентаря.

---

### 3.3 Бестиарий с постепенным раскрытием

**Суть:** у каждого монстра есть запись в бестиарии. Информация открывается по мере
накопления убийств. Сохраняет интерес к незнакомым мобам — игрок не знает что с них падает.

**Пороги раскрытия:**

| Убийства | Что открывается |
|----------|-----------------|
| 1        | Имя, тип, HP-диапазон, биом |
| 5        | Слабые стороны, устойчивости |
| 15       | Обычный дроп (trash loot) |
| 30       | Необычный дроп |
| 75       | Редкий дроп |
| 150+     | Очень редкий дроп (шанс в % — округлённо, не точно) |

**Важно:** очень редкий дроп показывается как `"~0.1%"`, не точное число — точность убивает азарт.

**Дополнительные источники знаний (избегает pure-grind):**
- **Scholar NPC** — платит золото, получает часть записи немедленно
- **Охотничьи записки** — редкий дроп с самого моба, мгновенно раскрывает кусок бестиария
- **Гильдия охотников** — после N убийств одного типа мобов выдаёт "охотничий ранг" + лор

**Технически:**
- Таблица `character_bestiary(character_id, mob_template_id, kill_count)`
- API: клиент запрашивает запись бестиария → сервер возвращает данные согласно порогам
- Bestiary-пакет: `{ "mobSlug": "wolf", "killCount": 32, "revealedTiers": [1,2,3,4] }`

**Статус:** ✅ Реализовано (migration 037). `BestiaryManager` — накапливает killCount по (characterId, mobTemplateId), немедленный persist через `saveBestiaryKill`. Данные загружаются при логине, клиент запрашивает запись через `getBestiaryEntry`.

---

### 3.4 Локализация предметов — ключи вместо строк в БД

**Суть:** хранить в БД не название предмета напрямую, а `name_key`.
Клиент берёт строку из локализационного файла по ключу. Упрощает мультиязычность.

**Текущее состояние:** названия хранятся в БД напрямую.

**Решение по description:** `description_key` не нужен — описание предметов достаточно
хранить только в локализационных файлах клиента. Сервер описанием не оперирует.

**Технически:**
- Миграция: `ALTER TABLE items ADD COLUMN name_key VARCHAR`
- Заполнить `name_key` для всех существующих предметов (например `item.iron_sword.name`)
- Клиент получает `name_key`, берёт перевод из локали; `name` остаётся как fallback
- Сервер продолжает хранить `name` на переходный период, постепенно переходя на `name_key`

**Статус:** ✅ Реализовано (migration 037). Поле `name_key VARCHAR DEFAULT NULL` добавлено в `items`. Chunk-server `DataStructs.hpp` и `JSONParser` обновлены. `InventoryManager` и `ItemEventHandler::itemToJson` включают `nameKey` в ответы клиенту.

---

## 4. Этап 3 — Экосистема мобов

> **Критерий включения:** требует нового сервиса или существенного расширения
> SpawnSystem. Меняет поведение мира.

---

### 4.0 ChampionManager — Унифицированная Архитектура Чемпионов

Все четыре системы Этапа 3 (Threshold, Survival, Rare, Timed) порождают или трансформируют
моб-инстансы с особым поведением. Выносить логику в `SpawnZoneManager` или напрямую в
`CombatSystem` — значит нарушать SRP и создавать скрытые зависимости.
**Решение:** новый сервис `ChampionManager` — единая точка ответственности за всё,
что связано с чемпионами и редкими мобами.

#### 4.0.1 Новые поля в DataStructs

**`MobDataStruct`** (добавить поля, `DataStructs.hpp`):
```cpp
// Champion / rare mob flags (Этап 3)
bool isChampion    = false;  // Оценивается в CombatSystem::handleMobDeath
bool canEvolve     = false;  // Из mob_templates.can_evolve → Survival Champion
bool hasEvolved    = false;  // Runtime: уже эволюционировал (не эволюционировать повторно)
int64_t spawnEpochSec = 0;   // Unix-timestamp момента спавна (для Survival Champion)
float lootMultiplier  = 1.0f;// Применяется в LootManager при генерации лута
```

**`GameZoneStruct`** (добавить поле, `DataStructs.hpp`):
```cpp
int championThresholdKills = 100; // Из zones.champion_threshold_kills
```

#### 4.0.2 API ChampionManager

Новые файлы: `include/services/ChampionManager.hpp`, `src/services/ChampionManager.cpp`.

```cpp
class ChampionManager {
public:
    ChampionManager(GameServices* gs);

    // === Threshold Champion ===
    // Вызывается из CombatSystem::handleMobDeath после каждого убийства моба
    void recordMobKill(int gameZoneId, int mobTemplateId);

    // === Timed Champion ===
    // Загружает шаблоны из SET_TIMED_CHAMPION_TEMPLATES (данные от Game Server)
    void loadTimedChampions(const std::vector<TimedChampionTemplate>& templates);

    // === Survival Champion ===
    // Проверяет всех живых мобов на эволюцию. Вызывается раз в 5 минут.
    void tickSurvivalEvolution();

    // === Timed Champion ===
    // Проверяет расписание. Вызывается раз в 30 секунд.
    void tickTimedChampions();

    // === Общий обработчик смерти чемпиона ===
    // Вызывается из CombatSystem::handleMobDeath если mob.isChampion == true
    void onChampionKilled(int champUid, int killerCharId,
                          const std::string& champSlug = "");

    // === Утилита спавна чемпиона (internal / для Timed) ===
    int spawnChampion(int mobTemplateId, int gameZoneId,
                      const std::string& namePrefix,
                      float lootMult, const std::string& slug = "");

private:
    GameServices* gs_;
    std::shared_ptr<spdlog::logger> log_;

    // Threshold: gameZoneId → (mobTemplateId → killCount)
    // in-memory only, resets on server restart
    std::unordered_map<int, std::unordered_map<int, int>> zoneKillCounters_;
    mutable std::mutex counterMutex_;

    // Активные инстансы чемпионов
    struct ChampionInstance {
        int uid;
        int gameZoneId;
        int baseTemplateId;
        std::string slug;        // непустой только для Timed Champions
        std::chrono::steady_clock::time_point spawnedAt;
        std::chrono::steady_clock::time_point despawnAt;
    };
    std::vector<ChampionInstance> active_;
    mutable std::mutex activeMutex_;

    // Timed champion templates
    struct TimedChampionTemplate {
        int id;
        std::string slug;
        int gameZoneId;
        int mobTemplateId;
        int intervalHours;
        int windowMinutes;
        int64_t nextSpawnAt;     // Unix timestamp
        std::string announceKey;
        bool preAnnounceSent = false;  // runtime flag, resets on each cycle
    };
    std::vector<TimedChampionTemplate> timedTemplates_;
    mutable std::mutex timedMutex_;

    // Helpers
    PositionStruct resolveChampionSpawnPoint(int gameZoneId) const;
    void broadcastToGameZone(int gameZoneId, const std::string& type,
                             const std::string& text, const nlohmann::json& data = {});
    void checkDespawnedChampions();
};
```

#### 4.0.3 Интеграция в CombatSystem::handleMobDeath

Добавить два блока после существующего блока Bestiary:

```cpp
// --- Threshold Champion kill counter ---
try {
    auto gameZone = gameServices_->getGameZoneManager()
                        .getZoneForPosition(mobData.position);
    if (gameZone.has_value())
        gameServices_->getChampionManager()
            .recordMobKill(gameZone->id, mobData.id);
} catch (...) {}

// --- Champion death notification ---
if (mobData.isChampion)
    gameServices_->getChampionManager()
        .onChampionKilled(mobId, killerId, mobData.slug);
```

#### 4.0.4 Спавн чемпиона — разрешение точки спавна

`SpawnZoneStruct::zoneId` — это ID *spawn zone*, не *game zone*.
Для нахождения точки спавна в нужной game zone используется:

1. Получить все spawn zones из `SpawnZoneManager::getMobSpawnZones()`
2. Для каждой проверить: центр `((posX+sizeX)/2, (posY+sizeY)/2)` попадает в bounds `GameZoneStruct`
3. Из найденных — выбрать random spawn zone, взять случайную точку внутри её AABB
4. Fallback: центр game zone bounds `((minX+maxX)/2, (minY+maxY)/2, 0.0f)`

Чемпион регистрируется через `MobInstanceManager::registerMobInstance`.
Его `zoneId` выставляется в нашу **spawn zone** ID (для совместимости с движком мобов).
`gameZoneId` хранится во `ChampionInstance` для broadcast и трекинга.

#### 4.0.5 Broadcast зоне

`CharacterStatsNotificationService::sendWorldNotification` сейчас поддерживает
`scope = "personal"` и broadcast на все подключёние клиенты.
Требуется добавить перегрузку:

```cpp
// Новая перегрузка: отправить worldNotification всем игрокам в game zone
void sendWorldNotificationToGameZone(int gameZoneId,
                                     const std::string& type,
                                     const std::string& text,
                                     const nlohmann::json& data = {});
```

Реализация: итерирует `CharacterManager::getAllCharacterIds()`,
для каждого проверяет `GameZoneManager::getZoneForPosition(charPos)`,
фильтрует по `gameZoneId`, отправляет personal notification.
Это O(n) по числу игроков — при сотнях игроков в зоне нормально для прототипа.

#### 4.0.6 Добавить в GameServices

```cpp
// GameServices.hpp
ChampionManager& getChampionManager();

// В ChunkServer при инициализации: добавить
auto championManager_ = std::make_unique<ChampionManager>(this);
// В scheduled tasks (ChunkServer::mainEventLoop):
//   каждые 5 мин → championManager_->tickSurvivalEvolution()
//   каждые 30 сек → championManager_->tickTimedChampions()
```

#### 4.0.7 Новые события (EventData.hpp)

```
SET_TIMED_CHAMPION_TEMPLATES  — Game Server → Chunk Server (при старте)
TIMED_CHAMPION_KILLED         — Chunk Server → Game Server (для обновления next_spawn_at в БД)
```

---

### 4.1 Threshold Champion (Откликающийся Чемпион)

**Суть:** после N убийств мобов одного типа в зоне (server-wide счётчик, не per-player)
спавнится именованный Чемпион. Анонс на всю зону:
`[!] В лесу что-то зашевелилось... Волки стали вести себя иначе.`

**Правила:**
- Счётчик **серверный**, не привязан к одному игроку — это социальное событие
- Счётчик сбрасывается после убийства Чемпиона
- Чемпион спавнится в фиксированной точке (или случайной из пула)
- Если Чемпион не убит за X минут — исчезает, счётчик сбрасывается наполовину
- Лут у Чемпиона: гарантированный необычный + шанс редкого

**Отличие от "моб убивает игроков → становится сильнее":**
Та механика — negative feedback loop (наказание). Эта — reward loop (событие как возможность).

**Технически (детальная реализация):**

**Kill counter flow:**
```
CombatSystem::handleMobDeath(mobId, killerId)
  → getZoneForPosition(mobData.position) → GameZoneStruct
  → ChampionManager::recordMobKill(gameZone.id, mobData.id /* template id */)
    → zoneKillCounters_[gameZoneId][mobTemplateId]++
    → если count >= gameZone.championThresholdKills:
        если нет active champion для этой zone/template → spawnThresholdChampion(...)
        counter → 0
```

**Спавн чемпиона:**
```cpp
void ChampionManager::recordMobKill(int gameZoneId, int mobTemplateId) {
    std::lock_guard lk(counterMutex_);

    // Не накапливать если чемпион этого типа уже активен в зоне
    for (auto& c : active_)
        if (c.gameZoneId == gameZoneId && c.baseTemplateId == mobTemplateId) return;

    auto& count = zoneKillCounters_[gameZoneId][mobTemplateId];
    ++count;

    auto zones = gs_->getGameZoneManager().getAllZones();
    auto it = std::find_if(zones.begin(), zones.end(),
                           [gameZoneId](const GameZoneStruct& z){ return z.id == gameZoneId; });
    if (it == zones.end()) return;

    if (count >= it->championThresholdKills) {
        count = 0;
        spawnChampion(mobTemplateId, gameZoneId, "[Чемпион] ", 1.5f);
    }
}
```

**Конструирование инстанса чемпиона:**
```cpp
int ChampionManager::spawnChampion(int mobTemplateId, int gameZoneId,
                                   const std::string& namePrefix,
                                   float lootMult, const std::string& slug) {
    auto& cfg = gs_->getGameConfigService();
    float hpMult  = cfg.getFloat("champion.hp_multiplier", 3.0f);
    float dmgMult = cfg.getFloat("champion.damage_multiplier", 1.5f);

    auto base = gs_->getMobManager().getMobById(mobTemplateId);
    if (base.id == 0) return 0;

    base.uid          = Generators::generateUID();
    base.name         = namePrefix + base.name;
    base.maxHealth    = static_cast<int>(base.maxHealth * hpMult);
    base.currentHealth= base.maxHealth;
    base.baseExperience = static_cast<int>(base.baseExperience * 2.0f);
    base.rankCode     = "champion";
    base.rankMult     = hpMult;
    base.isChampion   = true;
    base.lootMultiplier = lootMult;
    base.position     = resolveChampionSpawnPoint(gameZoneId);
    // Масштабирование атрибутов через dmgMult:
    // (вариант А: умножить physical_attack атрибут напрямую в base.attributes)

    // Зарегистрировать в движке
    gs_->getMobInstanceManager().registerMobInstance(base);

    // Despawn trigger
    int despawnMin = cfg.getInt("champion.despawn_minutes", 30);
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(activeMutex_);
        active_.push_back({base.uid, gameZoneId, mobTemplateId, slug,
                           now, now + std::chrono::minutes(despawnMin)});
    }

    // Broadcast
    broadcastToGameZone(gameZoneId, "champion_spawned",
        "[!] " + base.name + " появился в зоне!",
        {{"mobSlug", base.slug}, {"uid", base.uid}});
    log_->info("[Champion] Threshold champion uid={} spawned in gameZone={}",
               base.uid, gameZoneId);
    return base.uid;
}
```

**Despawn при истечении таймера** (вызывается из `tickTimedChampions` / отдельного метода):
```cpp
void ChampionManager::checkDespawnedChampions() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lk(activeMutex_);
    for (auto it = active_.begin(); it != active_.end(); ) {
        if (now >= it->despawnAt) {
            gs_->getMobInstanceManager().unregisterMobInstance(it->uid);
            broadcastToGameZone(it->gameZoneId, "champion_despawned",
                                "Чемпион исчез, не дождавшись вызова.");
            // Счётчик сбрасывается наполовину (по спеке)
            {
                std::lock_guard ck(counterMutex_);
                auto& cnt = zoneKillCounters_[it->gameZoneId][it->baseTemplateId];
                auto zones = gs_->getGameZoneManager().getAllZones();
                auto zit = std::find_if(zones.begin(), zones.end(),
                    [&](const GameZoneStruct& z){ return z.id == it->gameZoneId; });
                if (zit != zones.end())
                    cnt = zit->championThresholdKills / 2;
            }
            it = active_.erase(it);
        } else { ++it; }
    }
}
```

**onChampionKilled** сбрасывает `active_`, сбрасывает kill counter, объявляет зоне:
```cpp
void ChampionManager::onChampionKilled(int champUid, int killerCharId,
                                       const std::string& champSlug) {
    std::lock_guard lk(activeMutex_);
    auto it = std::find_if(active_.begin(), active_.end(),
                           [champUid](const ChampionInstance& c){ return c.uid == champUid; });
    if (it == active_.end()) return;

    int gameZoneId  = it->gameZoneId;
    int baseTemplate= it->baseTemplateId;
    std::string slug= it->slug;
    active_.erase(it);

    // Сброс kill counter
    { std::lock_guard ck(counterMutex_); zoneKillCounters_[gameZoneId][baseTemplate] = 0; }

    // Broadcast
    auto killerData = gs_->getCharacterManager().getCharacterData(killerCharId);
    std::string killerName = killerData.characterId != 0 ? killerData.name : "кто-то";
    broadcastToGameZone(gameZoneId, "champion_killed",
        killerName + " убил Чемпиона!",
        {{"killerCharId", killerCharId}});

    // Для Timed Champions: уведомить Game Server для обновления next_spawn_at
    if (!slug.empty())
        sendTimedChampionKilledToGameServer(slug, killerCharId);

    log_->info("[Champion] Champion uid={} killed by char={}", champUid, killerCharId);
}
```

**Лут у чемпиона:** `LootManager` получает `mob.lootMultiplier`. Добавить в
`LootManager::generateLoot(const MobDataStruct& mob)` умножение шанса дропа
на `mob.lootMultiplier`. Для champion `lootMultiplier = 1.5f` → +50% к шансам всех строк.
Эксклюзивный лут (if any) — отдельные строки в loot_table помеченные `is_champion_only = true`
(с `min_rank = "champion"` или флагом). Для прототипа достаточно умножителя.

**Статус:** ✅ Реализовано (`ChampionManager::recordMobKill` + `spawnChampion`, `zoneKillCounters_`, `LootManager::lootMultiplier`, broadcast через `sendWorldNotificationToGameZone`)

---

### 4.2 Survival Champion (Выживший Чемпион)

**Суть:** моб, который прожил X часов без убийства, автоматически эволюционирует:
HP +50%, урон +30%, меняется имя и визуал. Нарастающая угроза.

**Зачем:** мотивирует игроков охотиться на него пока он слабый. Tension без punishment.
Создаёт world state который все могут видеть — "тот самый волк на востоке уже 3 дня живёт".

**Технически (детальная реализация):**

**Новые поля в шаблоне моба (таблица `mob_templates`, миграция 038):**
```sql
ALTER TABLE mob_templates ADD COLUMN can_evolve BOOLEAN DEFAULT FALSE;
```

**Установка `spawnEpochSec` при спавне** (`SpawnZoneManager::spawnMobsInZone`):
```cpp
// В цикле создания моба — добавить:
mobInstance.canEvolve    = templateMob.canEvolve;       // скопировать из шаблона
mobInstance.spawnEpochSec = static_cast<int64_t>(std::time(nullptr));
```
Поле `canEvolve` копируется из шаблона и хранится в инстансе. `spawnEpochSec` — Unix
timestamp момента рождения. Оба поля нужно скопировать только один раз при спавне.

**Тикер эволюции (ChampionManager::tickSurvivalEvolution):**
```cpp
void ChampionManager::tickSurvivalEvolution() {
    const auto& cfg = gs_->getGameConfigService();
    const int evolveHours = cfg.getInt("survival_champion.evolve_hours", 12);
    const int64_t evolveThresholdSec = static_cast<int64_t>(evolveHours) * 3600;
    const float hpBonus  = cfg.getFloat("survival_champion.hp_bonus_pct", 0.5f);

    const int64_t nowEpoch = static_cast<int64_t>(std::time(nullptr));

    // MobInstanceManager нужен метод getAllLivingInstances() → vector<MobDataStruct>
    for (auto& mob : gs_->getMobInstanceManager().getAllLivingInstances()) {
        if (!mob.canEvolve || mob.hasEvolved || mob.isDead) continue;
        if (mob.spawnEpochSec == 0) continue;
        if (nowEpoch - mob.spawnEpochSec < evolveThresholdSec) continue;

        evolveSurvivalMob(mob.uid);
    }
}

void ChampionManager::evolveSurvivalMob(int mobUid) {
    const auto& cfg = gs_->getGameConfigService();
    float hpBonus = cfg.getFloat("survival_champion.hp_bonus_pct", 0.5f);

    auto mob = gs_->getMobInstanceManager().getMobInstance(mobUid);
    if (mob.uid == 0 || mob.hasEvolved) return;

    // Масштабировать HP: сохранить пропорцию текущего HP
    float hpRatio = (mob.maxHealth > 0)
                    ? static_cast<float>(mob.currentHealth) / mob.maxHealth
                    : 1.0f;
    int newMax = static_cast<int>(mob.maxHealth * (1.0f + hpBonus));
    int newCur = static_cast<int>(newMax * hpRatio);

    mob.maxHealth     = newMax;
    mob.currentHealth = newCur;
    mob.name          = "[Выживший] " + mob.name;
    mob.hasEvolved    = true;
    mob.isChampion    = true;   // теперь считается чемпионом (для loot + kill notify)
    mob.lootMultiplier= 1.3f;
    gs_->getMobInstanceManager().updateMobInstance(mob);

    // Зарегистрировать как активного чемпиона (для kill tracking)
    auto gameZone = gs_->getGameZoneManager().getZoneForPosition(mob.position);
    int gzId = gameZone.has_value() ? gameZone->id : 0;
    {
        std::lock_guard lk(activeMutex_);
        active_.push_back({mobUid, gzId, mob.id, "",
                           std::chrono::steady_clock::now(),
                           std::chrono::steady_clock::time_point::max()});
    }

    // Broadcast в зону
    broadcastToGameZone(gzId, "survival_evolved",
        mob.name + " прожил слишком долго. Он стал сильнее.",
        {{"uid", mobUid}, {"mobSlug", mob.slug}});

    log_->info("[Survival] Mob uid={} evolved after {}h alive", mobUid,
               gs_->getGameConfigService().getInt("survival_champion.evolve_hours", 12));
}
```

**Требуемое дополнение к MobInstanceManager:**
```cpp
// Новый метод: возвращает snapshot всех живых инстансов
// (не удерживает lock — копирует весь map)
std::vector<MobDataStruct> getAllLivingInstances() const;

// Новый метод: обновить существующий инстанс (для изменения HP, имени, флагов)
bool updateMobInstance(const MobDataStruct& updated);
```

**Интервал тикера:** 300 000 мс (5 минут). Регистрируется в `ChunkServer` задачей по таймеру.
Итерация по всем инстансам O(N_mobs) — при сотнях мобов в зоне нормально.

**Визуальное изменение:** `mob.name` меняется → всем клиентам в зоне надо переспавнить моба
(или отправить обновлённый пакет). Варианты:
- Отправить `mobSpawnUpdate` пакет только изменённому мобу (нужен новый пакет)
- Более простой способ: despawn + respawn того же uid с новыми данными (избыточно)
- MVP: `worldNotification` в зону сообщает об изменении; клиент обновит имя при следующем respawn

**Статус:** ✅ Реализовано (`ChampionManager::tickSurvivalEvolution` + `evolveSurvivalMob`, тикер 5 мин в `ChunkServer`, `MobDataStruct::canEvolve/hasEvolved/spawnEpochSec`, broadcast `survival_evolved`)

---

### 4.3 Редкие Мобы (Rare Spawn)

**Суть:** моб спавнится с очень низким шансом (или по условию) и даёт уникальный лут
которого нет нигде больше. Это не просто "хороший лут" — это узнаваемое явление мира.

**Три составляющих редкого моба:**
1. **Условие спавна** — ночь + зона + шанс, или: убито 50+ волков + ночь
2. **Узнаваемость** — особое имя, звук, отличный визуал. Игрок должен узнать его сразу
3. **Контекстный лут** — уникальный для этого моба. `Призрачный Волк` → `Призрачный Клык`
   который нигде больше не встречается и открывает диалог с NPC (система 3.2)

**Пример: Призрачный Волк**
- Спавн: ночь (day/night cycle) + зона Тёмный Лес + 2% шанс раз в 10 минут
- При спавне: broadcast только в зоне `"[Ночь] Призрачная тень промелькнула в лесу..."`
- Дроп: `Призрачный Клык` (unique, item_id: X) + увеличенный gold
- Бестиарий: отдельная запись, не смешивается с обычным волком

**Важно:** редкий моб не должен вызывать frustration. Если ночь закончилась — окей, будет следующая.

**Precondition:** требует day/night cycle (отдельная задача).

**Groundwork (можно заложить уже сейчас, не реализуя логику):**

Поля в `mob_templates` (миграция 038) — добавить чисто как data, без кода:
```sql
ALTER TABLE mob_templates ADD COLUMN is_rare             BOOLEAN DEFAULT FALSE;
ALTER TABLE mob_templates ADD COLUMN rare_spawn_chance   FLOAT   DEFAULT 0.0;
ALTER TABLE mob_templates ADD COLUMN rare_spawn_condition VARCHAR(30) DEFAULT NULL;
-- rare_spawn_condition: 'night' | 'day' | 'zone_event' | NULL (любое время)
```

Поле `is_rare` и `rareSpawnChance` добавить в `MobDataStruct` — загружаются из DB,
передаются на chunk server. Логика спавна (`RareSpawnManager`) — отдельный тикер,
реализуется после day/night cycle.

**Архитектурная заготовка для RareSpawnManager:**
- `tickRareSpawns(bool isNight)` — вызывается ChunkServer при смене дня/ночи
  или периодически с передачей текущего `isNight` флага
- Для каждого `SpawnZoneStruct::spawnMobId` с флагом `is_rare`:
  roll `rare_spawn_chance`, проверить `rare_spawn_condition`, если подходит → спавн
- Инстанс редкого моба регистрируется через ChampionManager (если is_champion_type)
  или напрямую через `MobInstanceManager`
- Строгое ограничение: 1 редкий моб одного типа в зоне одновременно

**Статус:** ⏳ Зависит от day/night cycle. Groundwork (DB columns) включается в миграцию 038.

---

### 4.4 Timed Spawn Champion (Мировое Событие по таймеру)

**Суть:** раз в N часов в зоне гарантированно спавнится именованный чемпион.
Время следующего появления известно (world events UI). Это **планируемое событие** —
игрок специально заходит в игру в определённое время.

**Зачем:** один из самых мощных retention hooks. Игрок планирует сессию вокруг события.
Именно так работали Field Bosses в BDO и World Bosses в WoW.

**Технически (детальная реализация):**

**Жизненный цикл на сервере:**
```
Startup:
  Game Server читает timed_champion_templates из БД
    → вычисляет next_spawn_at если NULL:
        next_spawn_at = last_killed_at + interval_hours * 3600 sec
        или NOW() + interval_hours * 3600 если last_killed_at NULL
    → отправляет SET_TIMED_CHAMPION_TEMPLATES на Chunk Server

Chunk Server (каждые 30 сек, tickTimedChampions):
  for each timedTemplate:
    now = Unix timestamp
    timeUntilSpawn = nextSpawnAt - now

    if timeUntilSpawn <= pre_announce_sec && !preAnnounceSent:
        broadcastToGameZone(gameZoneId, "champion_spawned_soon",
            "Через 5 минут появится " + announce_text + "!")
        preAnnounceSent = true

    if timeUntilSpawn <= 0 && state == WAITING:
        uid = spawnChampion(mobTemplateId, gameZoneId, "[!] ", 2.0, slug)
        state = SPAWNED
        preAnnounceSent = false

On champion kill (onChampionKilled, slug != ""):
    sendTimedChampionKilledToGameServer(slug, killerCharId)
    state[slug] = WAITING
    // next_spawn_at пересчитается на Game Server и вернётся заново

Game Server получает TIMED_CHAMPION_KILLED:
    UPDATE timed_champion_templates
       SET last_killed_at = NOW(),
           next_spawn_at  = NOW() + interval_hours * '1 hour'::interval
     WHERE slug = $1
    Переотправляет обновлённый SET_TIMED_CHAMPION_TEMPLATES (или точечный update)
```

**Структура `TimedChampionTemplate` (DataStructs.hpp):**
```cpp
struct TimedChampionTemplate {
    int id = 0;
    std::string slug;
    int gameZoneId = 0;      // zones.id (не spawn_zone!)
    int mobTemplateId = 0;
    int intervalHours = 6;
    int windowMinutes = 15;
    int64_t nextSpawnAt = 0; // Unix timestamp
    std::string announceKey; // ключ локализации для анонса
};
```

**Event payload `SET_TIMED_CHAMPION_TEMPLATES`:**
```json
{
  "header": { "eventType": "setTimedChampionTemplates" },
  "body":   { "templates": [
    { "id": 1, "slug": "alpha_wolf", "gameZoneId": 2, "mobTemplateId": 5,
      "intervalHours": 6, "windowMinutes": 15,
      "nextSpawnAt": 1720000000, "announceKey": "event.alpha_wolf.announce" }
  ]}
}
```

**Event payload `TIMED_CHAMPION_KILLED`** (Chunk → Game Server):
```json
{
  "header": { "eventType": "timedChampionKilled" },
  "body":   { "slug": "alpha_wolf", "killerCharId": 123, "killedAt": 1720003600 }
}
```

**Restart survival:** `next_spawn_at` хранится в БД на Game Server. При рестарте
chunk server получает актуальное `next_spawn_at` через `SET_TIMED_CHAMPION_TEMPLATES`.
Если сервер был down во время окна спавна — спавнит немедленно (timeUntilSpawn <= 0).

**Окно спавна (windowMinutes):** если чемпиона никто не убил за N минут — despawn,
`next_spawn_at = NOW() + interval_hours`. Реализуется через стандартный `despawnAt`
в `ChampionInstance` + обычный `checkDespawnedChampions`.

**Клиентский UI "Next World Event":**
- Chunk Server периодически (или по запросу) отправляет список `{slug, nextSpawnAt}`
- Клиент сам рассчитывает countdown
- Новый пакет `worldEventsSchedule` или добавление поля в status пакет

**Статус:** ✅ Реализовано (`ChampionManager::tickTimedChampions` (30 сек тикер), `loadTimedChampions`, `onChampionKilled` → `sendTimedChampionKilledToGameServer`, game server `UPDATE timed_champion_templates`, restart survival через `next_spawn_at` в БД)

---

## 5. Этап 4 — Социальные системы

> **Критерий включения:** фундаментальные системы, влияющие на поведение игроков
> на протяжении недель/месяцев. Высокий долгосрочный retention, высокий effort.

---

### 5.0 Архитектурный обзор Этапа 4

Три системы Этапа 4 — взаимно независимы в реализации, но сильно связаны через
существующий pipeline диалогов + `PlayerContextStruct`. Важно понимать пять
общих точек опоры:

#### 5.0.1 PlayerContextStruct — расширение

`PlayerContextStruct` сейчас: `flagsBool`, `flagsInt`, `questStates`, `questCurrentStep`,
`questProgress`. Для Этапа 4 нужно добавить:

```cpp
struct PlayerContextStruct {
    // ... существующие поля ...

    // Stage 4 — Reputation
    std::unordered_map<std::string, int> reputations; ///< faction_slug → value

    // Stage 4 — Mastery (для условий диалога: "ты владеешь мечом")
    std::unordered_map<std::string, float> masteries; ///< mastery_slug → value [0..100]
};
```

`buildPlayerContext` в `DialogueManager` / `CharacterManager` заполняет эти поля
из соответствующих менеджеров при каждом открытии диалога.

#### 5.0.2 Паттерн persistence для новых систем

Все три системы следуют одному паттерну (precedent: `PityManager`, `BestiaryManager`):
1. Chunk Server держит in-memory map `characterId → данные`
2. Загрузка при логине: Game Server → `SET_CHARACTER_<X>` event → Chunk Server менеджер
3. Изменение: менеджер обновляет in-memory + вызывает `saveCallback_(payload)`
4. `saveCallback_` = `GameServerWorker::sendDataToGameServer(...)` → Game Server SQL UPDATE
5. Выгрузка при дисконнекте: `unload(characterId)` после финального flush

#### 5.0.3 Новые типы условий и действий

| Тип | condition | action | Реализация |
|-----|-----------|--------|------------|
| `reputation` | ✅ новый | ✅ новый | `DialogueConditionEvaluator::evaluateReputation` |
| `mastery` | ✅ новый | — | `DialogueConditionEvaluator::evaluateMastery` |

Добавляются по минимальному шаблону существующих `evaluateFlag`/`executeSetFlag` —
это 15–20 строк кода каждый.

#### 5.0.4 Технический долг: `crit_chance` атрибут

Системы Skill Mastery и Item Soul (tier2/tier3) обещают бонус crit chance, но
атрибут `crit_chance` не существует в `entity_attributes` и не читается формулой.
**Решение — миграция 039:** добавить `crit_chance` как character attribute.
`CombatCalculator` получает логику: `effective_crit = base_crit + sum(modifiers[crit_chance])`.

#### 5.0.5 Фракции и моб → фракция mapping

Для системы репутации нужно знать фракцию убитого моба.
Добавить в `mob_templates`: `faction_slug VARCHAR DEFAULT NULL`.
Загружается в `MobDataStruct::factionSlug`, копируется при спавне.

---

### 5.1 Reputation System (Система Репутации)

**Суть:** у игрока есть числовое значение репутации с каждой фракцией мира.
Репутация меняет то как мир реагирует на персонажа.

**Уровни репутации:**
```
rep < -500   → "Враг"     — NPC стража атакуют при встрече, диалоги закрыты
rep -500..0  → "Незнакомец" — холодный приём, ограниченные диалоги
rep 0..200   → "Чужак"    — базовые диалоги
rep 200..500 → "Знакомый" — скидка у торговца, новые ветки диалога
rep 500+     → "Союзник"  — эксклюзивные квесты, уникальные предметы
```

**Что изменяет репутацию:**
- Убийство волков → +rep с фракцией `hunters` (связь с системой мобов)
- Убийство бандитов → +rep с `city_guard`, −rep с `bandit_guild`
- Выполнение/провал квеста → ±rep с выдавшим квест NPC/фракцией
- Убийство другого игрока → −rep с `city_guard` (мягкая anti-PK система)

**Технически (детальная реализация):**

#### DB Schema

```sql
-- Фракции мира (статичный справочник)
CREATE TABLE factions (
    id      SERIAL PRIMARY KEY,
    slug    VARCHAR(60) NOT NULL UNIQUE,
    name    VARCHAR(120) NOT NULL
);

-- Репутация персонажа с фракцией
CREATE TABLE character_reputation (
    character_id  INT NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    faction_slug  VARCHAR(60) NOT NULL,
    value         INT NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, faction_slug)
);
CREATE INDEX ON character_reputation (character_id);

-- Реакция фракции моба на убийство: faction_slug + delta
-- (хранится в mob_templates напрямую, не отдельная таблица)
ALTER TABLE mob_templates
    ADD COLUMN IF NOT EXISTS faction_slug       VARCHAR(60) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS rep_delta_per_kill INT         DEFAULT 0;
-- faction_slug: фракция к которой принадлежит моб
-- rep_delta_per_kill: δ репутации за убийство (обычно +5..+15, может быть 0)
```

#### ReputationManager

Новые файлы: `include/services/ReputationManager.hpp`, `src/services/ReputationManager.cpp`.

```cpp
class ReputationManager {
public:
    ReputationManager(GameServices* gs);

    /// Загрузить репутации персонажа при логине (из SET_CHARACTER_REPUTATIONS)
    void loadCharacterReputations(int characterId,
        const std::unordered_map<std::string, int>& reps);

    /// Выгрузить при дисконнекте
    void unloadCharacterReputations(int characterId);

    /// Получить текущее значение репутации. Возвращает 0 если запись не найдена.
    int getReputation(int characterId, const std::string& factionSlug) const;

    /// Изменить репутацию (δ может быть отрицательным).
    /// Обновляет in-memory + ставит в очередь persist через saveCallback_.
    void changeReputation(int characterId, const std::string& factionSlug, int delta);

    /// Заполнить PlayerContextStruct::reputations для condition evaluation.
    void fillReputationContext(int characterId, PlayerContextStruct& ctx) const;

    /// Получить строковый уровень репутации по значению.
    static std::string getTier(int value); // "enemy" | "stranger" | "neutral" | "friendly" | "ally"

    using SaveCallback = std::function<void(int /*characterId*/, const std::string& /*factionSlug*/, int /*value*/)>;
    void setSaveCallback(SaveCallback cb) { saveCallback_ = std::move(cb); }

private:
    GameServices* gs_;
    std::shared_ptr<spdlog::logger> log_;

    // characterId → (faction_slug → value)
    std::unordered_map<int, std::unordered_map<std::string, int>> data_;
    mutable std::shared_mutex mutex_;

    SaveCallback saveCallback_;
};
```

Уровни репутации (`getTier`):
```
value < -500   → "enemy"
-500 .. -1     → "stranger"
0   .. 199     → "neutral"
200 .. 499     → "friendly"
≥ 500          → "ally"
```

#### Интеграция в CombatSystem::handleMobDeath

```cpp
// --- Reputation: mob kill → faction rep change ---
try {
    if (!mobData.factionSlug.empty() && mobData.repDeltaPerKill != 0) {
        gs_->getReputationManager().changeReputation(
            killerId, mobData.factionSlug, mobData.repDeltaPerKill);
        // Опциональный feedback игроку при пересечении tier-порога
        // (проверяется внутри changeReputation если delta меняет tier)
    }
} catch (...) {}
```

#### Tier-пересечение → worldNotification

В `changeReputation` при пересечении порога уровня:
```cpp
std::string oldTier = getTier(oldValue);
std::string newTier = getTier(newValue);
if (oldTier != newTier) {
    gs_->getStatsNotificationService().sendWorldNotification(
        characterId, "reputation_tier_change",
        "Репутация с " + factionSlug + ": " + newTier,
        {{"faction", factionSlug}, {"tier", newTier}, {"value", newValue}});
}
```

#### Условие и действие в диалогах

**Condition** — `DialogueConditionEvaluator`:
```json
{ "type": "reputation", "faction": "hunters", "gte": 200 }
{ "type": "reputation", "faction": "city_guard", "lte": -500 }
```

```cpp
// DialogueConditionEvaluator.cpp — добавить в evaluateRule:
if (type == "reputation")
    return evaluateReputation(rule, ctx);

bool DialogueConditionEvaluator::evaluateReputation(
    const nlohmann::json& rule, const PlayerContextStruct& ctx)
{
    const std::string faction = rule.value("faction", "");
    auto it = ctx.reputations.find(faction);
    int value = (it != ctx.reputations.end()) ? it->second : 0;

    if (rule.contains("gte")) return value >= rule["gte"].get<int>();
    if (rule.contains("lte")) return value <= rule["lte"].get<int>();
    if (rule.contains("eq"))  return value == rule["eq"].get<int>();
    if (rule.contains("gt"))  return value >  rule["gt"].get<int>();
    if (rule.contains("lt"))  return value <  rule["lt"].get<int>();
    return true;
}
```

**Action** — `DialogueActionExecutor`:
```json
{ "type": "change_reputation", "faction": "hunters", "delta": 50 }
```

```cpp
// executeDispatch добавить:
else if (type == "change_reputation")
    executeChangeReputation(action, characterId, ctx, result);

void DialogueActionExecutor::executeChangeReputation(
    const nlohmann::json& action, int characterId,
    PlayerContextStruct& ctx, ActionResult& result)
{
    std::string faction = action.value("faction", "");
    int delta = action.value("delta", 0);
    if (faction.empty() || delta == 0) return;

    services_.getReputationManager().changeReputation(characterId, faction, delta);

    // Обновить ctx.reputations для последующих условий в той же сессии
    ctx.reputations[faction] = services_.getReputationManager()
                                   .getReputation(characterId, faction);
}
```

#### Загрузка при логине (Game Server → Chunk Server)

**Game Server**: при логине персонажа добавить SQL:
```sql
SELECT faction_slug, value FROM character_reputation WHERE character_id = $1
```
Упаковать в `SET_CHARACTER_REPUTATIONS` payload (аналогично `SET_PLAYER_FLAGS`).

**Chunk Server**: в `CharacterEventHandler::handleJoinCharacter` добавить:
```cpp
gs_->getReputationManager().loadCharacterReputations(
    characterId, reputationsFromEvent);
```

**Persist**: Game Server обрабатывает `REPUTATION_CHANGE` event:
```sql
INSERT INTO character_reputation (character_id, faction_slug, value)
VALUES ($1, $2, $3)
ON CONFLICT (character_id, faction_slug) DO UPDATE SET value = EXCLUDED.value;
```

#### Guard NPC Aggro (MVP implementation)

Полноценный NPC aggro когда rep < -500 требует NPCDataStruct с фракцией и проверки
при каждом движении. Для MVP — более простое решение: блокировать диалог и показывать
уведомление. В `DialogueManager::openDialogue`:

```cpp
// Проверить faction NPC и репутацию перед открытием диалога
if (!npc.factionSlug.empty()) {
    int rep = gs_->getReputationManager().getReputation(characterId, npc.factionSlug);
    if (rep < -500) {
        // Отправить отказ вместо диалога
        sendDialogueBlockedByReputation(clientId, npc.factionSlug);
        return;
    }
}
```
NPCDataStruct добавить: `std::string factionSlug = ""` (migration 039).
Полный aggro attack при rep < -500 — задача Этапа 5 (выходит за рамки прототипа).

#### Discount у торговца (MVP)

`VendorManager::calculateSellPrice` / `calculateBuyPrice` получает `characterId`.
Добавить: если rep ≥ 200 с faction торговца → цена покупки × 0.95, продажа × 1.05.
NPC faction = `npc.factionSlug`, rep читается из `ReputationManager`.

**Статус:** ✅ Реализовано (migration 039, ReputationManager, DialogueConditionEvaluator::evaluateReputation, DialogueActionExecutor::executeChangeReputation, CombatSystem reputation hook, CharacterEventHandler + ClientEventHandler load/unload, **DialogueEventHandler guard block** — `BLOCKED_BY_REPUTATION` при rep < -500 с NPC faction, **VendorEventHandler discount** — −5% buy / +5% sell при rep ≥ 200 через `getReputationDiscountPct` во всех операциях shop/buy/sell/batch)

---

### 5.2 Skill Mastery через использование (Use-based Progression)

**Суть:** конкретные навыки растут от использования, независимо от уровня персонажа.
Ты рубишь мечом → растёт `sword_mastery`. Медленно, с soft cap.

**Пример прогрессии `sword_mastery` (0–100):**
```
0–20:   стартовый уровень, без бонусов
20–50:  +damage modifier 1..5%
50–80:  разблокирует пассивку: +3% crit chance
80–100: разблокирует пассивку: шанс parry
```

**Защита от гринда на слабых мобах:**
Mastery растёт быстро против врагов своего уровня ±5, медленнее против слабых
(diminishing returns), быстрее против сильных (risk/reward).

**Технически (детальная реализация):**

#### DB Schema

```sql
-- Определения mastery (статичный справочник, загружается как game_config)
CREATE TABLE mastery_definitions (
    slug               VARCHAR(60)  NOT NULL PRIMARY KEY,
    name               VARCHAR(120) NOT NULL,
    weapon_type_slug   VARCHAR(60),  -- null = не weapon mastery (e.g. "archery", "magic")
    max_value          FLOAT        NOT NULL DEFAULT 100.0
);

-- Прогресс mastery персонажа
CREATE TABLE character_skill_mastery (
    character_id  INT          NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    mastery_slug  VARCHAR(60)  NOT NULL,
    value         FLOAT        NOT NULL DEFAULT 0.0,
    PRIMARY KEY (character_id, mastery_slug)
);
CREATE INDEX ON character_skill_mastery (character_id);
```

**Стартовые `mastery_definitions`:**
```sql
INSERT INTO mastery_definitions VALUES
('sword_mastery',  'Мастерство меча',   'sword',   100.0),
('axe_mastery',    'Мастерство топора', 'axe',     100.0),
('staff_mastery',  'Мастерство посоха', 'staff',   100.0),
('bow_mastery',    'Мастерство лука',   'bow',     100.0),
('unarmed_mastery','Рукопашный бой',    NULL,      100.0);
```

**Связь оружие → mastery_slug** хранится в `ItemDataStruct`:
```sql
ALTER TABLE items ADD COLUMN IF NOT EXISTS mastery_slug VARCHAR(60) DEFAULT NULL;
```
Оружие типа "sword" получает `mastery_slug = 'sword_mastery'`,
посох — `'staff_mastery'`, и т.д. Загружается в `ItemDataStruct::masterySlug`.

#### Прогресс Growth Formula

```
delta = base_delta × level_factor

base_delta = 0.5  (за каждый удар)

level_factor (уровень цели vs уровень персонажа):
  target_level - char_level >= +3  → 2.0   (risk/reward)
  target_level - char_level == +1..+2 → 1.5
  target_level == char_level          → 1.0   (нормальный прирост)
  char_level - target_level == 1..5  → 0.5   (diminishing returns)
  char_level - target_level >= 6     → 0.1   (фактически ничего)

Soft cap: при value > 80 delta *= 0.3   (последние 20 единиц — очень медленно)
```

#### Tier пороги и разблокировка бонусов

| mastery_slug | Порог | Бонус | Реализация |
|---|---|---|---|
| `sword_mastery` | 20 | +damage 1% | `ActiveEffectStruct` sourceType="mastery" |
| `sword_mastery` | 50 | +damage 5% | `ActiveEffectStruct` |
| `sword_mastery` | 80 | +crit_chance 3% | `ActiveEffectStruct` attributeSlug="crit_chance" |
| `sword_mastery` | 100 | +parry_chance 2% | `ActiveEffectStruct` attributeSlug="parry_chance" |

**Ключевое решение:** пассивки от mastery реализуются через **существующую** `ActiveEffectStruct`
с `expiresAt = 0` (permanent) и `sourceType = "mastery"`. Это позволяет не изобретать
новую систему — достаточно добавить `crit_chance` и `parry_chance` в `entity_attributes`
(migration 039) и считать их в `CombatCalculator`.

#### MasteryManager

Новые файлы: `include/services/MasteryManager.hpp`, `src/services/MasteryManager.cpp`.

```cpp
class MasteryManager {
public:
    MasteryManager(GameServices* gs);

    void loadDefinitions(const std::vector<MasteryDefinition>& defs);
    void loadCharacterMasteries(int characterId,
        const std::unordered_map<std::string, float>& masteries);
    void unloadCharacterMasteries(int characterId);

    /// Вызывается из CombatSystem после каждого успешного удара игрока по мобу.
    void onPlayerAttack(int characterId, const std::string& masterySlug,
                        int charLevel, int targetLevel);

    float getMasteryValue(int characterId, const std::string& slug) const;

    /// Заполнить PlayerContextStruct::masteries (для condition evaluation)
    void fillMasteryContext(int characterId, PlayerContextStruct& ctx) const;

    using SaveCallback = std::function<void(int characterId,
                                            const std::string& slug, float value)>;
    void setSaveCallback(SaveCallback cb) { saveCallback_ = std::move(cb); }

private:
    void checkAndApplyMilestone(int characterId, const std::string& masterySlug,
                                float oldValue, float newValue);

    float calculateDelta(float baseValue, int charLevel, int targetLevel) const;

    struct MasteryDefinition {
        std::string slug;
        float maxValue = 100.0f;
    };

    std::vector<MasteryDefinition> defs_;

    // characterId → (mastery_slug → value)
    std::unordered_map<int, std::unordered_map<std::string, float>> data_;
    mutable std::shared_mutex mutex_;

    GameServices* gs_;
    std::shared_ptr<spdlog::logger> log_;
    SaveCallback saveCallback_;
};
```

**Debounce persist:** писать в БД каждые 10 ударов или при пересечении milestone.
Аналогично ItemSoul debounce (проверенный паттерн).

```cpp
void MasteryManager::onPlayerAttack(int characterId, const std::string& masterySlug,
                                    int charLevel, int targetLevel) {
    std::unique_lock lk(mutex_);
    auto& val = data_[characterId][masterySlug];
    float delta = calculateDelta(val, charLevel, targetLevel);

    float oldValue = val;
    val = std::min(val + delta, 100.0f);

    ++hitCounters_[characterId][masterySlug]; // atomic counter для debounce

    checkAndApplyMilestone(characterId, masterySlug, oldValue, val); // без lock!

    if (tierCrossed || hitCounters_[characterId][masterySlug] % 10 == 0)
        saveCallback_(characterId, masterySlug, val);
}
```

**Milestone обнаружение:** тиры определяются в game_config:
```
mastery.sword.tier1 = 20   mastery.sword.tier1.effect = "sword_mastery_t1_damage"
mastery.sword.tier2 = 50   mastery.sword.tier2.effect = "sword_mastery_t2_damage"
mastery.sword.tier3 = 80   mastery.sword.tier3.effect = "sword_mastery_t3_crit"
mastery.sword.tier4 = 100  mastery.sword.tier4.effect = "sword_mastery_t4_parry"
```
При пересечении тира: применить `ActiveEffectStruct` (постоянный бафф) через
`CharacterManager::addActiveEffect(characterId, effect, sourceType="mastery")`.
Эффект пересчитывает `CharacterStatsNotificationService::sendStatsUpdate`.

#### Интеграция в CombatSystem

```cpp
// executeSkillUsage (после успешного применения к мобу):
try {
    if (targetType == CombatTargetType::MOB) {
        // Определить mastery slug из экипированного оружия
        auto weapon = gs_->getInventoryManager().getEquippedWeapon(casterId);
        if (weapon && !weapon->masterySlug.empty()) {
            auto mob = gs_->getMobInstanceManager().getMobInstance(targetId);
            auto caster = gs_->getCharacterManager().getCharacterData(casterId);
            gs_->getMasteryManager().onPlayerAttack(
                casterId, weapon->masterySlug,
                caster.characterLevel, mob.level);
        }
    }
} catch (...) {}
```

**⚠ Важно:** `PlayerInventoryItemStruct` нужно дополнить `std::string masterySlug`
(денормализация из `ItemDataStruct` для быстрого доступа без лукапа).
Альтернатива: `ItemManager::getItemById(weapon.itemId).masterySlug` — дешево при кеше.

#### Условие в диалогах

```json
{ "type": "mastery", "slug": "sword_mastery", "gte": 50 }
```
Открывает реплики у NPC оружейника: "Я вижу ты обращаешься с ним умело..."

```cpp
// DialogueConditionEvaluator — добавить:
if (type == "mastery")
    return evaluateMastery(rule, ctx);

bool evaluateMastery(const nlohmann::json& rule, const PlayerContextStruct& ctx) {
    std::string slug = rule.value("slug", "");
    auto it = ctx.masteries.find(slug);
    float value = (it != ctx.masteries.end()) ? it->second : 0.0f;
    if (rule.contains("gte")) return value >= rule["gte"].get<float>();
    if (rule.contains("lte")) return value <= rule["lte"].get<float>();
    return true;
}
```

**Статус:** ✅ Реализовано (migration 039, MasteryManager, CombatSystem mastery hook в executeSkillUsage, milestone ActiveEffects для crit_chance/parry_chance, CharacterEventHandler + ClientEventHandler load/unload)

---

### 5.3 Zone Events (Мировые события)

**Суть:** периодически в зоне происходит ambient event — временное изменение правил
зоны. О событии получают уведомление все игроки в зоне.

**Примеры событий:**

| Событие | Условие | Эффект | Длительность |
|---------|---------|--------|--------------|
| "Волчий Час" | Ночь | Все волки +30% скорость, лут x1.5 | 20 мин |
| "Нашествие" | Случайно 1–2 раза/день | Волна мобов, финальный Чемпион | До победы |
| "Торговый Обоз" | Раз в несколько часов | NPC-торговец с лимитированными товарами | 15 мин или пока товар не раскуплен |
| "Туман" | Погода (random) | Мобы сложнее видеть, зато лут +20% | 30 мин |

**Технически (детальная реализация):**

#### Типология событий и архитектура влияния

| Тип влияния | Затронутая система | Точка внедрения |
|---|---|---|
| Лут × multiplier | `LootManager` | `getLootMultiplier(gameZoneId)` → ZoneEventManager |
| Скорость мобов × multiplier | `MobMovementManager` | `getSpeedMultiplier(zoneId)` |
| Spawn rate modifier | `SpawnZoneManager` | `getSpawnRateMultiplier(gameZoneId)` → zadержка respawn |
| Волна мобов (Invasion) | `ChampionManager::spawnChampion` | по завершении волны |
| Временный NPC (торговый обоз) | `NPCManager` | spawn/despawn temporary NPC |
| Туман (визуал) | Клиент | только `worldNotification` данные |

Все типы влияния читаются **in-memory** из `ZoneEventManager` без блокировок на hot path.

#### ZoneEventManager

Новые файлы: `include/services/ZoneEventManager.hpp`, `src/services/ZoneEventManager.cpp`.

```cpp
struct ActiveZoneEvent {
    std::string slug;
    int gameZoneId;                       // 0 = global
    float lootMultiplier     = 1.0f;
    float spawnRateMultiplier = 1.0f;     // 1.0 = no change; 0.5 = 2x slower respawn
    float mobSpeedMultiplier  = 1.0f;
    std::chrono::steady_clock::time_point endsAt;
    std::string announceKey;
};

class ZoneEventManager {
public:
    ZoneEventManager(GameServices* gs);

    // === Шаблоны (загружаются из SET_ZONE_EVENT_TEMPLATES) ===
    void loadTemplates(const std::vector<ZoneEventTemplate>& templates);

    // === Активация / деактивация ===
    void startEvent(const std::string& slug, int overrideGameZoneId = 0);
    void endEvent(const std::string& slug);

    // === Tickers ===
    /// Проверяет истечение событий. Вызывается раз в 30 сек.
    void tickEventScheduler();

    // === Hot-path accessors (read-only, lock-free через atomic snapshot) ===
    float getLootMultiplier(int gameZoneId) const;
    float getMobSpeedMultiplier(int spawnZoneId) const;  // spawnZoneId → gameZoneId lookup
    float getSpawnRateMultiplier(int gameZoneId) const;

    // === Query ===
    std::vector<ActiveZoneEvent> getActiveEvents() const;
    bool hasActiveEvent(int gameZoneId) const;

private:
    struct ZoneEventTemplate {
        int id;
        std::string slug;
        int gameZoneId;            // 0 = любая зона
        std::string triggerType;   // 'scheduled' | 'random' | 'threshold' | 'manual'
        int durationSec;
        float lootMultiplier      = 1.0f;
        float spawnRateMultiplier = 1.0f;
        float mobSpeedMultiplier  = 1.0f;
        std::string announceKey;
        int intervalHours = 0;           // для scheduled
        float randomChancePerHour = 0.0f; // для random
        // Invasion-specific
        bool hasInvasionWave = false;    // если true → спавнить волну мобов
        int invasionMobTemplateId = 0;
        int invasionWaveCount = 0;
        int invasionChampionTemplateId = 0; // 0 = нет чемпиона
    };

    std::vector<ZoneEventTemplate> templates_;

    // Активные события: slug → event
    std::unordered_map<std::string, ActiveZoneEvent> active_;
    mutable std::shared_mutex mutex_;

    // Snapshot для lock-free чтения hot-path accessors
    // Обновляется при каждом start/end event через std::atomic<shared_ptr>
    struct EventSnapshot {
        std::unordered_map<int /*gameZoneId*/, ActiveZoneEvent> byZone;
    };
    std::atomic<std::shared_ptr<const EventSnapshot>> snapshot_;

    void rebuildSnapshot();  // вызывается после start/end, под mutex_

    // Scheduled ticker state
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastTriggerTime_;

    GameServices* gs_;
    std::shared_ptr<spdlog::logger> log_;
};
```

**Lock-free hot path:** `getLootMultiplier` / `getMobSpeedMultiplier` читают атомарный
`shared_ptr<const EventSnapshot>` — zero-mutex на hot path при каждом убийстве моба
и каждом тике движения. `rebuildSnapshot()` вызывается только при start/end event
(редкое событие, ~раз в час), обновляет snapshot под write mutex.

#### Интеграция — точки внедрения

**LootManager::generateLoot:**
```cpp
float lootMult = mob.lootMultiplier;  // champion multiplier (уже есть)
// + zone event multiplier
auto gameZone = gs_->getGameZoneManager().getZoneForPosition(mob.position);
if (gameZone.has_value())
    lootMult *= gs_->getZoneEventManager().getLootMultiplier(gameZone->id);
// apply lootMult to all drop chances
```

**MobMovementManager** — при вычислении скорости в `calculateMovement`:
```cpp
float speedMult = mobMovData.speedMultiplier;  // уже есть
speedMult *= gs_->getZoneEventManager().getMobSpeedMultiplier(mob.zoneId);
```

**SpawnZoneManager::canRespawnMob** (или аналог):
```cpp
// Применить spawnRateMultiplier к respawnTime
auto gameZone = gameZoneForSpawnZone(spawnZoneId);
float spawnMult = gs_->getZoneEventManager().getSpawnRateMultiplier(gameZoneId);
auto effectiveRespawn = respawnTime * spawnMult;  // > 1.0 = медленнее, < 1.0 = быстрее
```

#### Event Scheduler

В `tickEventScheduler` (каждые 30 сек):
```cpp
for (auto& tmpl : templates_) {
    if (active_.count(tmpl.slug)) continue; // уже активно

    if (tmpl.triggerType == "scheduled") {
        auto& last = lastTriggerTime_[tmpl.slug];
        if (now - last >= hours(tmpl.intervalHours))
            startEvent(tmpl.slug);
    }
    else if (tmpl.triggerType == "random") {
        // Шанс за тик: chance_per_hour / (3600 / 30) = chance / 120
        float tickChance = tmpl.randomChancePerHour / 120.0f;
        if (dist(gen) < tickChance)
            startEvent(tmpl.slug);
    }
}
// Завершить истёкшие события
for (auto it = active_.begin(); it != active_.end(); ) {
    if (now >= it->second.endsAt) {
        endEventInternal(it->first); // broadcast + cleanup
        it = active_.erase(it);
    } else { ++it; }
}
```

**`endEvent` broadcast:**
```cpp
gs_->getStatsNotificationService().sendWorldNotificationToGameZone(
    event.gameZoneId, "zone_event_end",
    "Событие завершилось.", {{"slug", event.slug}});
```

#### Invasion Event — волна мобов

При `startEvent("invasion_dark_forest")` с `hasInvasionWave = true`:
```
1. Спавнить invasionWaveCount мобов invasionMobTemplateId в зоне
   через ChampionManager::spawnChampion или прямой MobInstanceManager::registerMobInstance
2. Отслеживать UIDs волны в ActiveZoneEvent::waveUids
3. При deaths count == waveCount и invasionChampionTemplateId != 0:
   спавнить финального чемпиона
4. При смерти чемпиона → endEvent("invasion_dark_forest")
```

Финальный чемпион нашествия интегрируется через `ChampionManager::onChampionKilled`
с custom slug `"invasion_champion_dark_forest"`:
```cpp
void ZoneEventManager::onChampionKilled(const std::string& champSlug) {
    // Найти событие которое ждёт этого чемпиона
    for (auto& [slug, ev] : active_)
        if (ev.invasionChampionSlug == champSlug)
            endEvent(slug);
}
```

#### Временный NPC (Торговый Обоз)

`NPCManager` получает методы:
```cpp
int spawnTemporaryNPC(const NPCDataStruct& npcTemplate, int gameZoneId, int ttlSec);
void despawnTemporaryNPC(int npcId);
```
`startEvent("merchant_convoy_forest")` создаёт временный NPC с loot-shop диалогом.
`endEvent` вызывает `despawnTemporaryNPC`. Клиент получает `removeNPC` пакет.

#### Пакет для клиента

**Начало события:**
```json
{
  "header": { "eventType": "worldNotification" },
  "body": {
    "notificationType": "zone_event_start",
    "scope": "zone",
    "text": "Волчий Час наступил... Луна поднялась высоко.",
    "data": { "eventSlug": "wolf_hour", "durationSec": 1200, "gameZoneId": 2 }
  }
}
```
Клиент использует `durationSec` для отображения countdown в UI.

**Статус:** ✅ Реализовано (migration 039, ZoneEventManager, ZoneEvent ticker task в ChunkServer (ID 19, 30s), loot multiplier в LootManager, speed multiplier в MobMovementManager, zone_event_templates загружаются через game server при chunk join)

---

## 6. Связь между системами

Все системы образуют единый органический loop — это не набор фич, а слои одной экосистемы:

```
Игрок убивает волков рядом с другим игроком
  → [Fellowship] +12% XP обоим
  → [Mastery] растёт sword_mastery
  → [Reputation] +rep с фракцией охотников
  → [Bestiary] открываются новые уровни записи о волке
  → [Pity] накапливается счётчик для редкого дропа

Threshold достигнут в зоне (N убийств волков)
  → [Champion] спавнится Альфа-Волк, анонс в зону
  → Игроки объединяются, убивают
  → [Rare Drop] выпадает "Призрачный Клык" (pity помог)
  → [Item Soul] меч собравшего накапливает kill_count от Альфы

Ночью в зоне
  → [Rare Mob] шанс спавна Призрачного Волка
  → [Bestiary] отдельная запись — не волк, а призрак

С "Призрачным Клыком" в инвентаре
  → [Item→NPC] Старый Охотник говорит: "Откуда это у тебя?.."
  → Новая ветка квеста через уже существующий диалоговый граф

Впервые заходит в новую зону
  → [Exploration] XP burst + флаг
  → [Item→NPC / Reputation] новые диалоги для тех кто "там был"
```

---

## 7. Итоговая таблица приоритетов

| # | Система | Этап | Impact | Effort | Зависимости |
|---|---------|------|--------|--------|-------------|
| 0 | worldNotification пакет | 2 | ★★★★★ | ★★☆ | — (precondition для Этапа 3) |
| 1 | Fellowship Bonus | 1 | ★★★★ | ★★☆ | — |
| 2 | Exploration Rewards | 1 | ★★★★ | ★★☆ | Zone system |
| 3 | Item Soul (kill_count) | 1 | ★★★☆ | ★★☆ | — |
| 4 | Pity System | 2 | ★★★★★ | ★★★☆ | Loot system |
| 5 | Item → NPC Dialogue | 2 | ★★★★★ | ★★☆ | — (архитектура есть) |
| 6 | Бестиарий | 2 | ★★★★ | ★★★☆ | Loot data |
| 7 | Локализация предметов (name_key) | 2 | ★★★☆ | ★★★☆ | Миграция БД |
| 8 | Threshold Champion | 3 | ★★★★ | ★★★★ | SpawnService, worldNotification |
| 9 | Редкие мобы (Rare Spawn) | 3 | ★★★★ | ★★★☆ | Day/night cycle, worldNotification |
| 10 | Timed Spawn Champion | 3 | ★★★★★ | ★★★★ | SpawnService, worldNotification |
| 11 | Survival Champion | 3 | ★★★☆ | ★★★☆ | SpawnService, worldNotification |
| 12 | Reputation System | 4 | ★★★★★ | ★★★★☆ | Диалог условия |
| 13 | Skill Mastery | 4 | ★★★★ | ★★★★ | Passive skills plan |
| 14 | Zone Events | 4 | ★★★★★ | ★★★★★ | Event scheduler, worldNotification |

---

## 8. Конфигурируемые константы

Все числовые параметры систем не хардкодятся. Хранение зависит от природы параметра:

- **Глобальные игровые константы** → таблица `game_config` (уже существует, key-value,
  читается при старте Game Server и передаётся в Chunk Server)
- **Параметры конкретного экземпляра** (зоны, чемпиона, события) → колонки в соответствующей
  таблице-шаблоне этого экземпляра, рядом с остальными данными объекта

---

### 8.1 Глобальные константы → `game_config`

Параметры без привязки к конкретному объекту мира:

```sql
INSERT INTO game_config (key, value, value_type, description) VALUES
-- Fellowship
('fellowship.bonus_pct',           '0.07',    'float', 'Бонус XP за совместное убийство одного моба'),
('fellowship.attack_window_sec',   '15',      'int',   'Окно атаки: сколько секунд назад должен был атаковать 2й игрок'),

-- Item Soul
('item_soul.tier1_kills',          '50',      'int',   'Порог убийств для суффикса [Бывалый]'),
('item_soul.tier2_kills',          '200',     'int',   'Порог для [Кровавый]'),
('item_soul.tier3_kills',          '500',     'int',   'Порог для [Легендарный]'),
('item_soul.tier1_bonus_flat',     '1',       'int',   'Бонус к атрибуту на tier1'),
('item_soul.tier2_bonus_flat',     '2',       'int',   'Бонус к атрибуту на tier2'),
('item_soul.tier3_bonus_flat',     '3',       'int',   'Бонус к атрибуту на tier3'),
('item_soul.tier2_crit_pct',       '0.05',    'float', 'Бонус crit на tier2'),
('item_soul.tier3_crit_pct',       '0.08',    'float', 'Бонус crit на tier3'),

-- Pity System
('pity.soft_pity_kills',           '300',     'int',   'С какого убийства начинает расти шанс (soft pity)'),
('pity.hard_pity_kills',           '800',     'int',   'Гарантированный дроп (hard cap)'),
('pity.soft_bonus_per_kill',       '0.00005', 'float', 'Прибавка к шансу за каждое убийство после soft pity'),
('pity.hint_threshold_kills',      '500',     'int',   'С какого убийства отправлять worldNotification pity_hint'),

-- Bestiary
('bestiary.tier1_kills',           '1',       'int',   'Убийств для раскрытия: имя, тип, HP'),
('bestiary.tier2_kills',           '5',       'int',   'Убийств для раскрытия: слабости'),
('bestiary.tier3_kills',           '15',      'int',   'Убийств для раскрытия: обычный дроп'),
('bestiary.tier4_kills',           '30',      'int',   'Убийств для раскрытия: необычный дроп'),
('bestiary.tier5_kills',           '75',      'int',   'Убийств для раскрытия: редкий дроп'),
('bestiary.tier6_kills',           '150',     'int',   'Убийств для раскрытия: очень редкий дроп'),

-- Champion (общие множители, применяются к любому чемпиону)
('champion.hp_multiplier',         '3.0',     'float', 'Множитель HP чемпиона относительно базового моба'),
('champion.damage_multiplier',     '1.5',     'float', 'Множитель урона чемпиона'),
('champion.despawn_minutes',       '30',      'int',   'Через сколько минут чемпион исчезает если не убит'),

-- Survival Champion
('survival_champion.evolve_hours', '12',      'int',   'Через сколько часов жизни моб эволюционирует'),
('survival_champion.hp_bonus_pct', '0.5',     'float', 'Бонус HP при эволюции'),
('survival_champion.dmg_bonus_pct','0.3',     'float', 'Бонус урона при эволюции'),

-- Timed Champion (общий параметр анонса, интервал — per-champion в таблице)
('timed_champion.pre_announce_sec','300',     'int',   'За сколько секунд до спавна отправлять анонс');
```

---

### 8.2 Параметры экземпляров → их таблицы

**Зоны** (`zones`) — XP за исследование и threshold для чемпиона у каждой зоны свои:

```sql
ALTER TABLE zones ADD COLUMN exploration_xp_reward  INT  DEFAULT 100;
ALTER TABLE zones ADD COLUMN champion_threshold_kills INT DEFAULT 100;
-- exploration_xp_reward: сколько XP даётся за первое посещение данной зоны
-- champion_threshold_kills: сколько убийств мобов в зоне вызывает чемпиона
```

**Timed Champion шаблоны** (новая таблица `timed_champion_templates`) — расписание и зона per-champion:

```sql
CREATE TABLE timed_champion_templates (
    id              SERIAL PRIMARY KEY,
    slug            VARCHAR(60)  NOT NULL UNIQUE,
    zone_id         INT          NOT NULL REFERENCES zones(id),
    mob_template_id INT          NOT NULL,         -- какой моб спавнится
    interval_hours  INT          NOT NULL DEFAULT 6,
    window_minutes  INT          NOT NULL DEFAULT 15,
    next_spawn_at   TIMESTAMPTZ,                   -- сохраняет состояние между рестартами
    last_killed_at  TIMESTAMPTZ,
    announcement_key VARCHAR(120)                  -- ключ текста анонса для локализации
);
```

**Редкие мобы** (`rare_mob_templates` или колонки в mob_templates) — шанс и условия спавна per-mob:

```sql
ALTER TABLE mob_templates ADD COLUMN is_rare            BOOLEAN  DEFAULT FALSE;
ALTER TABLE mob_templates ADD COLUMN rare_spawn_chance  FLOAT    DEFAULT 0.0;
ALTER TABLE mob_templates ADD COLUMN rare_spawn_condition VARCHAR(30) DEFAULT NULL;
-- rare_spawn_condition: 'night', 'day', 'zone_event', NULL (любое время)
```

**Zone Events** (`zone_event_templates`) — параметры каждого события хранятся вместе с его определением:

```sql
CREATE TABLE zone_event_templates (
    id                  SERIAL PRIMARY KEY,
    slug                VARCHAR(60)  NOT NULL UNIQUE,
    zone_id             INT          REFERENCES zones(id),  -- NULL = любая зона
    trigger_type        VARCHAR(20)  NOT NULL,              -- 'scheduled', 'random', 'threshold'
    duration_sec        INT          NOT NULL,
    loot_multiplier     FLOAT        DEFAULT 1.0,
    spawn_rate_multiplier FLOAT      DEFAULT 1.0,
    announcement_key    VARCHAR(120),
    interval_hours      INT,                                -- для scheduled
    random_chance_per_hour FLOAT                           -- для random
);
```

---

### 8.3 Миграция 038 — Этап 3 (Экосистема мобов)

Все изменения Этапа 3 выносятся в одну миграцию. Файл: `migrations/038_stage3_mob_ecosystem.sql`.

```sql
-- ================================================================
-- Migration 038: Stage 3 — Mob Ecosystem (Champion + Rare mobs)
-- ================================================================

-- 1. Порог для Threshold Champion у каждой игровой зоны
ALTER TABLE zones
    ADD COLUMN IF NOT EXISTS champion_threshold_kills INT NOT NULL DEFAULT 100;

-- 2. Шаблоны Timed Champions (мировые боссы по расписанию)
CREATE TABLE IF NOT EXISTS timed_champion_templates (
    id               SERIAL PRIMARY KEY,
    slug             VARCHAR(60)  NOT NULL UNIQUE,
    zone_id          INT          NOT NULL REFERENCES zones(id) ON DELETE CASCADE,
    mob_template_id  INT          NOT NULL,
    interval_hours   INT          NOT NULL DEFAULT 6,
    window_minutes   INT          NOT NULL DEFAULT 15,
    next_spawn_at    TIMESTAMPTZ  DEFAULT NULL,
    last_killed_at   TIMESTAMPTZ  DEFAULT NULL,
    announcement_key VARCHAR(120) DEFAULT NULL
);

-- 3. Флаги для шаблонов мобов (Survival + Rare Spawn groundwork)
ALTER TABLE mob_templates
    ADD COLUMN IF NOT EXISTS can_evolve           BOOLEAN NOT NULL DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS is_rare              BOOLEAN NOT NULL DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS rare_spawn_chance    FLOAT   NOT NULL DEFAULT 0.0,
    ADD COLUMN IF NOT EXISTS rare_spawn_condition VARCHAR(30)      DEFAULT NULL;
-- can_evolve:            TRUE → моб может стать Survival Champion
-- is_rare:               TRUE → управляется RareSpawnManager (не стандартный спавн)
-- rare_spawn_chance:     Шанс спавна на каждую проверку [0.0 .. 1.0]
-- rare_spawn_condition:  'night' | 'day' | 'zone_event' | NULL
```

**Game Server — что нужно добавить:**
- SQL prepared statement: `SELECT * FROM timed_champion_templates`
- Новый event handler: `handleGetTimedChampionTemplatesEvent`
- Расчёт `next_spawn_at` если NULL (первый запуск): `NOW() + interval_hours * '1 hour'`
- Отправка `SET_TIMED_CHAMPION_TEMPLATES` на chunk server при старте (аналогично `SET_GAME_ZONES`)
- Обработчик `TIMED_CHAMPION_KILLED`: `UPDATE timed_champion_templates SET last_killed_at=NOW(), next_spawn_at=NOW()+...`
- В SQL для `mob_templates`: добавить `can_evolve, is_rare, rare_spawn_chance, rare_spawn_condition` в SELECT
- В `MobDataStruct` Game Server (include/data/DataStructs.hpp): добавить соответствующие поля

**Chunk Server — что нужно добавить:**
- `TimedChampionTemplate` struct → `DataStructs.hpp`
- Обработчик события `SET_TIMED_CHAMPION_TEMPLATES` → `ChunkEventHandler` или новый handler
- `ChampionManager` service (§4.0) → `include/services/ChampionManager.hpp` + `src/services/ChampionManager.cpp`
- `MobDataStruct`: поля `isChampion`, `canEvolve`, `hasEvolved`, `spawnEpochSec`, `lootMultiplier`
- `GameZoneStruct`: поле `championThresholdKills`
- `MobInstanceManager`: методы `getAllLivingInstances()`, `updateMobInstance(mob)`
- `CharacterStatsNotificationService`: метод `sendWorldNotificationToGameZone(gameZoneId, ...)`
- `SpawnZoneManager::spawnMobsInZone`: копировать `canEvolve`, `spawnEpochSec = now()`
- `CombatSystem::handleMobDeath`: два новых блока (Threshold kill counter + Champion death)
- `LootManager::generateLoot`: учёт `mob.lootMultiplier` при расчёте шансов
- `GameServices`: добавить `getChampionManager()`
- Scheduled tasks в `ChunkServer`: тикеры на 30 сек (timedChampions) и 5 мин (survivalEvolution)

---

### 8.4 Миграция 039 — Этап 4 (Социальные системы)

Файл: `migrations/039_stage4_social_systems.sql`.

```sql
-- ================================================================
-- Migration 039: Stage 4 — Reputation, Skill Mastery, Zone Events
-- ================================================================

-- 1. Фракции
CREATE TABLE IF NOT EXISTS factions (
    id          SERIAL PRIMARY KEY,
    slug        VARCHAR(60)  NOT NULL UNIQUE,
    name        VARCHAR(120) NOT NULL,
    default_rep INT          NOT NULL DEFAULT 0,
    -- Пороги тиров: enemies (<-500), hostile (-499..-1),
    -- neutral (0..199), friendly (200..699), honored (700..)
    tier_hostile   INT NOT NULL DEFAULT -500,
    tier_neutral   INT NOT NULL DEFAULT 0,
    tier_friendly  INT NOT NULL DEFAULT 200,
    tier_honored   INT NOT NULL DEFAULT 700
);

INSERT INTO factions (slug, name, default_rep)
VALUES
    ('guards_city',     'Стражи Города',  0),
    ('merchants_guild', 'Торговцы',       0),
    ('forest_spirits',  'Духи Леса',      0)
ON CONFLICT (slug) DO NOTHING;

-- 2. Репутация персонажей
CREATE TABLE IF NOT EXISTS character_reputation (
    character_id INT        NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    faction_slug VARCHAR(60) NOT NULL,
    value        INT        NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, faction_slug)
);

-- 3. Привязка мобов к фракциям (для расчёта rep delta при убийстве)
ALTER TABLE mob_templates
    ADD COLUMN IF NOT EXISTS faction_slug         VARCHAR(60) DEFAULT NULL,
    ADD COLUMN IF NOT EXISTS rep_delta_per_kill   INT         NOT NULL DEFAULT 0;
-- faction_slug = NULL → убийство не даёт репутации
-- rep_delta_per_kill = отрицательное значение (потеря), положительное (прирост)
-- Примеры: faction_slug='guards_city', rep_delta_per_kill=-10
--           faction_slug='forest_spirits', rep_delta_per_kill=5

-- 4. Определения мастерства оружия
CREATE TABLE IF NOT EXISTS mastery_definitions (
    id              SERIAL PRIMARY KEY,
    slug            VARCHAR(60)  NOT NULL UNIQUE,
    weapon_type_slug VARCHAR(60) NOT NULL,   -- соответствует items.weapon_type_slug
    name            VARCHAR(120) NOT NULL,
    max_value       FLOAT        NOT NULL DEFAULT 100.0,
    -- Tier milestones (JSON массив threshold->bonus):
    -- [{"threshold":20,"attr":"attack_physical","bonus":5},...]
    tier_milestones JSONB        NOT NULL DEFAULT '[]'
);

INSERT INTO mastery_definitions (slug, weapon_type_slug, name, max_value, tier_milestones)
VALUES
(
    'sword_mastery', 'sword', 'Мастерство меча', 100,
    '[
        {"threshold":20,"attr":"attack_physical","bonus":5},
        {"threshold":50,"attr":"attack_physical","bonus":12},
        {"threshold":80,"attr":"crit_chance","bonus":3},
        {"threshold":100,"attr":"attack_physical","bonus":20}
    ]'
),
(
    'bow_mastery', 'bow', 'Мастерство лука', 100,
    '[
        {"threshold":20,"attr":"attack_physical","bonus":4},
        {"threshold":50,"attr":"attack_physical","bonus":10},
        {"threshold":80,"attr":"crit_chance","bonus":3},
        {"threshold":100,"attr":"attack_physical","bonus":18}
    ]'
)
ON CONFLICT (slug) DO NOTHING;

-- 5. Мастерство персонажей
CREATE TABLE IF NOT EXISTS character_skill_mastery (
    character_id  INT        NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    mastery_slug  VARCHAR(60) NOT NULL,
    value         FLOAT      NOT NULL DEFAULT 0.0,
    PRIMARY KEY (character_id, mastery_slug)
);

-- 6. Тип оружия на предметах (для привязки к mastery_slug)
ALTER TABLE items
    ADD COLUMN IF NOT EXISTS weapon_type_slug VARCHAR(60) DEFAULT NULL;
-- NULL = не оружие; 'sword', 'bow', 'staff', etc.

-- 7. Фракция у NPC (для проверки guard aggro в диалогах)
ALTER TABLE npc_templates
    ADD COLUMN IF NOT EXISTS faction_slug VARCHAR(60) DEFAULT NULL;

-- 8. crit_chance как атрибут (tech debt §5.0.4)
-- Закрывает ТД: Item Soul tier3 и Mastery tier3 обещают crit_chance bonus
INSERT INTO entity_attributes (slug, name, default_value, is_percentage)
VALUES ('crit_chance', 'Шанс крита', 0.0, TRUE)
ON CONFLICT (slug) DO NOTHING;

-- 9. Шаблоны зональных событий
CREATE TABLE IF NOT EXISTS zone_event_templates (
    id                     SERIAL PRIMARY KEY,
    slug                   VARCHAR(60)  NOT NULL UNIQUE,
    game_zone_id           INT          NOT NULL,   -- зона где проходит событие
    name                   VARCHAR(120) NOT NULL,
    trigger_type           VARCHAR(20)  NOT NULL CHECK (trigger_type IN ('scheduled','random','threshold','manual')),
    duration_sec           INT          NOT NULL DEFAULT 1200,
    loot_multiplier        FLOAT        NOT NULL DEFAULT 1.0,
    spawn_rate_multiplier  FLOAT        NOT NULL DEFAULT 1.0,  -- <1 = быстрее, >1 = медленнее  
    mob_speed_multiplier   FLOAT        NOT NULL DEFAULT 1.0,
    announce_key           VARCHAR(120) DEFAULT NULL,
    -- Для scheduled
    interval_hours         INT          DEFAULT NULL,
    -- Для random
    random_chance_per_hour FLOAT        DEFAULT NULL,
    -- Для invasion wave
    has_invasion_wave      BOOLEAN      NOT NULL DEFAULT FALSE,
    invasion_mob_template_id INT         DEFAULT NULL,
    invasion_wave_count    INT          NOT NULL DEFAULT 0,
    invasion_champion_template_id INT    DEFAULT NULL
);

INSERT INTO zone_event_templates
    (slug, game_zone_id, name, trigger_type, duration_sec, mob_speed_multiplier, announce_key, random_chance_per_hour)
VALUES
    ('wolf_hour', 1, 'Волчий Час', 'random', 1200, 1.3, 'event.wolf_hour.start', 0.15)
ON CONFLICT (slug) DO NOTHING;

INSERT INTO zone_event_templates
    (slug, game_zone_id, name, trigger_type, duration_sec, loot_multiplier, announce_key, interval_hours)
VALUES
    ('merchant_convoy_forest', 1, 'Торговый Обоз', 'scheduled', 600, 1.0, 'event.merchant_convoy.start', 24)
ON CONFLICT (slug) DO NOTHING;
```

**Game Server — что нужно добавить:**
- SQL: загрузка `factions`, `mastery_definitions`, `zone_event_templates` при старте
- SQL: `SELECT character_id, faction_slug, value FROM character_reputation WHERE character_id=$1`
- SQL: `SELECT character_id, mastery_slug, value FROM character_skill_mastery WHERE character_id=$1`
- SQL: `INSERT/UPDATE character_reputation ON CONFLICT DO UPDATE`
- SQL: `INSERT/UPDATE character_skill_mastery ON CONFLICT DO UPDATE`
- Event handler: `GET_STATIC_DATA` — отправлять factions + mastery_definitions на Chunk Server при старте
- Event handler: `SET_CHARACTER_REPUTATIONS` — отправлять репутации при логине
- Event handler: `SET_CHARACTER_MASTERIES` — отправлять мастерства при логине
- Event handler: `UPDATE_REPUTATION` (chunk → game) — принять и сохранить reputation delta
- Event handler: `UPDATE_MASTERY` (chunk → game) — принять и сохранить mastery value  
- Event handler: `GET_ZONE_EVENT_TEMPLATES` — отправить `SET_ZONE_EVENT_TEMPLATES` на chunk server

**Chunk Server — что нужно добавить:**
- `PlayerContextStruct` (DataStructs.hpp): `unordered_map<string,int> reputations`, `unordered_map<string,float> masteries`
- `MobDataStruct` (DataStructs.hpp): `std::string factionSlug`, `int repDeltaPerKill`
- `NPCDataStruct` (DataStructs.hpp): `std::string factionSlug`
- `ReputationManager` service (§5.1) — новые файлы
- `MasteryManager` service (§5.2) — новые файлы
- `ZoneEventManager` service (§5.3) — новые файлы
- `DialogueConditionEvaluator`: типы `reputation`, `mastery`
- `DialogueActionExecutor`: типы `change_reputation`, `grant_mastery_xp`
- `GameServices`: добавить `getReputationManager()`, `getMasteryManager()`, `getZoneEventManager()`
- `ChunkServer`: scheduled task каждые 30 сек — `ZoneEventManager::tickEventScheduler()`

---

## Статус реализации и план доработок

### Итоговый статус по системам

| Система | Статус | Заметка |
|---------|--------|---------|
| Fellowship Bonus | ✅ Реализовано | XP + floating text через worldNotification + anti-alt (accountId в ClientDataStruct, фильтр в CombatSystem) |
| Item Soul (tier bonuses) | ✅ Реализовано | tier-бонусы к effective stats в `buildStatsUpdatePacket`, debounce DB записи |
| Exploration Rewards | ✅ Реализовано | GameZoneManager, AABB zone detection, первое посещение → XP + флаг + worldNotification |
| worldNotification пакет | ✅ Реализовано | `CharacterStatsNotificationService::sendWorldNotification` — personal + zone broadcast |
| Pity System | ✅ Реализовано | `PityManager`, soft/hard pity, `savePityCounter`, загрузка при логине |
| Item→NPC Dialogue | ✅ Баг закрыт | `buildPlayerContext` заполняет `flagsInt["item_N"]` из инвентаря |
| Бестиарий | ✅ Реализовано | `BestiaryManager`, `saveBestiaryKill`, `getBestiaryEntry`, загрузка при логине |
| Локализация (name_key) | ✅ Реализовано | `items.name_key`, `DataStructs::nameKey`, `JSONParser`, `InventoryManager`, `ItemEventHandler` |
| Threshold Champion | ✅ Реализовано | `ChampionManager::recordMobKill`, `spawnChampion`, счётчик сбрасывается при деспауне/убийстве |
| Survival Champion | ✅ Реализовано | `ChampionManager::tickSurvivalEvolution` (300s), `evolveSurvivalMob`, `getAllLivingInstances()` |
| Timed Champion | ✅ Реализовано | `ChampionManager::tickTimedChampions` (30s), preannounce, `sendTimedChampionKilledToGameServer`, migration 038 |
| Rare Spawn | ⏳ Groundwork | DB columns в migration 038 заложены. Логика — после day/night cycle |
| Reputation System | ✅ Реализовано | §5.1 migration 039, ReputationManager, dialogue + combat integration, guard block (BLOCKED_BY_REPUTATION, rep < −500), vendor discount (−5% buy / +5% sell при rep ≥ 200) |
| Skill Mastery | ✅ Реализовано | §5.2 migration 039, MasteryManager, crit_chance/parry_chance milestones |
| Zone Events | ✅ Реализовано | §5.3 migration 039, ZoneEventManager, loot + speed multipliers |

---

### Критические баги (нужно исправить до наполнения контентом)

| # | Проблема | Файл | Исправление |
|---|----------|------|-------------|
| B1 | `evaluateInventory` возвращает `true` для всех (item-диалоги видны всем) | `DialogueConditionEvaluator.cpp` | ✅ **Закрыт** — `buildPlayerContext` заполняет `ctx.flagsInt["item_N"]` из инвентаря |
| B2 | Item Soul tier-бонусы не влияют на effective stats | `CharacterStatsNotificationService.cpp` | ✅ **Закрыт** — применяется flat bonus по тир-порогам из `game_config` |
| B3 | Item Soul: DB write на каждое убийство | `CombatSystem.cpp` | ✅ **Закрыт** — flush только каждые N убийств или при пересечении tier-порога |
| B4 | Guard NPC: диалог открывался при отрицательной репутации с фракцией NPC | `DialogueEventHandler.cpp` | ✅ **Закрыт** — возвращает `dialogueError / BLOCKED_BY_REPUTATION` если `npc.factionSlug != "" && rep < −500` |
| B5 | Vendor discount: скидка за репутацию не применялась ни в одном торговом обработчике | `VendorEventHandler.cpp` | ✅ **Закрыт** — `getReputationDiscountPct()` применяется во всех операциях shop/buy/sell/batch |

---

### Приоритизированный план реализации

#### P0 — Критические баги (немедленно)

- [x] **B1:** Фикс `evaluateInventory` — уже был исправлён в `buildPlayerContext`
- [x] **B2:** Item Soul tier-бонусы в `CharacterStatsNotificationService`
- [x] **B3:** Debounce сохранения `kill_count`

#### P1 — Завершение Этапа 1 ✅ COMPLETED

- [x] **worldNotification пакет** — `CharacterStatsNotificationService::sendWorldNotification(characterId, type, text, data)`
- [x] **Fellowship floating text** — уведомление после каждого fellowship XP grant
- [x] **Exploration Rewards** (§2.1)
  - [x] Миграция 036: bounds + `exploration_xp_reward` к таблице `zones`
  - [x] Game Server: `GET_GAME_ZONES` событие, prepared statement, `handleGetGameZonesEvent`
  - [x] Chunk Server: `GameZoneManager`, `SET_GAME_ZONES` событие, AABB zone detection
  - [x] `CharacterEventHandler`: смена зоны → проверка флага → XP + флаг + worldNotification
  - [x] `QuestManager::getFlagBool` / `setFlagBool` — helper для in-memory + persist

#### P2 — Этап 2 (глубина мира) ✅ COMPLETED

- [x] **Pity System** (§3.1)
  - Миграция: таблица `character_pity(character_id, item_id, kill_count_without_drop)`
  - В `LootManager::generateLootOnMobDeath`: читать pity counter → модифицировать шанс → сбрасывать при дропе
  - Обновлять counter при каждом убийстве моба с редким лутом
  - При достижении `pity.hint_threshold_kills`: `worldNotification(pity_hint)` игроку
- [x] **Item→NPC Dialogue** (§3.2) — после фикса B1 только контент: диалоговые деревья + предметы
- [x] **Бестиарий** (§3.3)
  - Миграция: `character_bestiary(character_id, mob_template_id, kill_count)`
  - `QuestManager::onMobKilled`: инкремент bestiary counter
  - Новый пакет/endpoint: клиент запрашивает запись бестиария → tier-фильтрованный ответ
- [x] **Локализация name_key** (§3.4)
  - Миграция: `ALTER TABLE items ADD COLUMN name_key VARCHAR`
  - Заполнить `name_key` для существующих предметов
  - Клиент использует `name_key`, сервер хранит `name` как fallback

#### P3 — Этап 3 (экосистема мобов) ✅ COMPLETED

> Все системы Этапа 3 реализуются через единый `ChampionManager` (§4.0).
> Реализацию начинать после добавления `ChampionManager` в `GameServices`.

**Шаг 0 — Общий фундамент (выполнить один раз):**
- [x] **Migration 038** (§8.3) — `zones.champion_threshold_kills`, `timed_champion_templates`,
  `mob_templates.can_evolve/is_rare/rare_spawn_chance/rare_spawn_condition`
- [x] **DataStructs.hpp** — новые поля в `MobDataStruct` (`isChampion`, `canEvolve`, `hasEvolved`,
  `spawnEpochSec`, `lootMultiplier`) и `GameZoneStruct` (`championThresholdKills`)
- [x] **MobInstanceManager** — добавить `getAllLivingInstances()`, `updateMobInstance(mob)`
- [x] **CharacterStatsNotificationService** — добавить `sendWorldNotificationToGameZone(gameZoneId, ...)`
- [x] **SpawnZoneManager::spawnMobsInZone** — копировать `canEvolve` из шаблона, выставлять `spawnEpochSec`
- [x] **LootManager::generateLoot** — умножать шансы на `mob.lootMultiplier` (1.0 = без изменений)
- [x] **ChampionManager** — создать `include/services/ChampionManager.hpp` + `src/services/ChampionManager.cpp`
- [x] **GameServices** — добавить `getChampionManager()`

**Шаг 1 — Threshold Champion** (§4.1):
- [x] В `CombatSystem::handleMobDeath`: блок "Threshold kill counter" + блок "Champion death"
- [x] `ChampionManager::recordMobKill` + `spawnChampion` + `onChampionKilled` + `checkDespawnedChampions`
- [x] Scheduled task в `ChunkServer`: каждые 30 сек вызывать `checkDespawnedChampions()`

**Шаг 2 — Survival Champion** (§4.2):
- [x] `ChampionManager::tickSurvivalEvolution` + `evolveSurvivalMob`
- [x] Scheduled task в `ChunkServer`: каждые 5 мин вызывать `tickSurvivalEvolution()`
- [x] Контент: расставить `can_evolve = TRUE` у именованных мобов-шаблонов

**Шаг 3 — Timed Champion** (§4.4):
- [x] Game Server: SQL + handler + `SET_TIMED_CHAMPION_TEMPLATES` event + `TIMED_CHAMPION_KILLED` handler
- [x] `TimedChampionTemplate` struct + `SET_TIMED_CHAMPION_TEMPLATES` обработчик в Chunk Server
- [x] `ChampionManager::loadTimedChampions` + `tickTimedChampions`
- [x] Scheduled task в `ChunkServer`: каждые 30 сек вызывать `tickTimedChampions()`
- [x] Контент: добавить первый `timed_champion_templates` row в БД

**Шаг 4 — Rare Spawn** (§4.3): ⏳ После day/night cycle

#### P4 — Этап 4 (долгосрочный retention) ✅ COMPLETED

> Все системы Этапа 4 изолированы и не зависят друг от друга (кроме Zone Events Invasion, требующего ChampionManager).
> Реализацию начинать после добавления `ReputationManager`/`MasteryManager`/`ZoneEventManager` в `GameServices`.

**Шаг 0 — Общий фундамент (выполнить один раз):**
- [x] **Migration 039** (§8.4) — `factions`, `character_reputation`, `mastery_definitions`, `character_skill_mastery`,
  `mob_templates.faction_slug/rep_delta_per_kill`, `items.weapon_type_slug`,
  `npc_templates.faction_slug`, `entity_attributes.crit_chance`, `zone_event_templates`
- [x] **DataStructs.hpp** — добавить в `PlayerContextStruct`: `unordered_map<string,int> reputations`, `unordered_map<string,float> masteries`
- [x] **DataStructs.hpp** — добавить в `MobDataStruct`: `std::string factionSlug`, `int repDeltaPerKill`
- [x] **DataStructs.hpp** — добавить в `NPCDataStruct`: `std::string factionSlug`
- [x] **GameServices** — добавить `getReputationManager()`, `getMasteryManager()`, `getZoneEventManager()`

**Шаг 1 — Reputation System** (§5.1):
- [x] **ReputationManager** — создать `include/services/ReputationManager.hpp` + `src/services/ReputationManager.cpp`
  - [x] `loadReputations(characterId, reputations)` — заполнить in-memory map при логине
  - [x] `changeReputation(characterId, factionSlug, delta)` → обновить + queue `UPDATE_REPUTATION` на Game Server
  - [x] `fillReputationContext(characterId, ctx)` → заполнить `ctx.reputations` для dialogue evaluation
  - [x] `getTier(characterId, factionSlug)` → `"enemies" | "hostile" | "neutral" | "friendly" | "honored"`
- [x] **Game Server** — `SET_CHARACTER_REPUTATIONS` event: загружать `character_reputation` при логине + отправлять на Chunk Server
- [x] **CombatSystem::handleMobDeath** — блок reputation: `if mob.repDeltaPerKill != 0 → reputationManager.changeReputation`
- [x] **DialogueConditionEvaluator** — тип `reputation`:
  ```cpp
  bool evaluateReputation(const nlohmann::json& rule, const PlayerContextStruct& ctx)
  ```
- [x] **DialogueActionExecutor** — тип `change_reputation`:
  ```cpp
  void executeChangeReputation(const nlohmann::json& action, int characterId)
  ```
- [x] **DialogueManager::openDialogue** — guard NPC MVP: проверить тир фракции NPC перед открытием диалога
- [x] Контент: faction slug для охранников (`guards_city`) + `rep_delta_per_kill` для бандитских мобов

**Шаг 2 — Skill Mastery** (§5.2):
- [x] **MasteryManager** — создать `include/services/MasteryManager.hpp` + `src/services/MasteryManager.cpp`
  - [x] `loadMasteries(characterId, masteries)` — заполнить in-memory map при логине
  - [x] `onPlayerAttack(characterId, masterySlug, targetLevel)` — начислить XP mastery (debounce persist)
  - [x] `getMasteryValue(characterId, masterySlug)` → текущее значение
  - [x] `checkMilestones(characterId, masterySlug, oldValue, newValue)` → применить ActiveEffectStruct при пересечении порога
- [x] **Game Server** — `SET_CHARACTER_MASTERIES` event: загружать `character_skill_mastery` при логине + отправлять на Chunk Server
- [x] **CombatSystem::executeSkillUsage** — после успешной атаки: получить `weapon_type_slug` экипированного оружия → `masteryManager.onPlayerAttack`
- [x] **DialogueConditionEvaluator** — тип `mastery`:
  ```cpp
  bool evaluateMastery(const nlohmann::json& rule, const PlayerContextStruct& ctx)
  ```
- [x] **CombatCalculator** — добавить `crit_chance` в формулу расчёта крита: `effective_crit = base_crit + sum(modifiers["crit_chance"])`
- [x] Контент: `weapon_type_slug` заполнить для существующих предметов в БД; добавить `mastery_definitions` через migration 039

**Шаг 3 — Zone Events** (§5.3):
- [x] **ZoneEventManager** — создать `include/services/ZoneEventManager.hpp` + `src/services/ZoneEventManager.cpp`
  - [x] `loadTemplates(templates)` — загрузить шаблоны из `SET_ZONE_EVENT_TEMPLATES`
  - [x] `startEvent(slug)` + `endEvent(slug)` → обновить in-memory + пересоздать atomic snapshot + broadcast
  - [x] `tickEventScheduler()` — проверить scheduled/random триггеры + истечение событий
  - [x] `getLootMultiplier(gameZoneId)` / `getMobSpeedMultiplier(zoneId)` / `getSpawnRateMultiplier(gameZoneId)` — lock-free через atomic snapshot
- [x] **Game Server** — `SET_ZONE_EVENT_TEMPLATES` event: загружать `zone_event_templates` при старте + отправлять на Chunk Server
- [x] **LootManager::generateLoot** — умножать шансы на `zoneEventManager.getLootMultiplier(gameZoneId)` (поверх `mob.lootMultiplier`)
- [x] **MobMovementManager** — умножать скорость на `zoneEventManager.getMobSpeedMultiplier(spawnZoneId)`
- [x] **SpawnZoneManager** — при расчёте respawn cooldown умножать на `zoneEventManager.getSpawnRateMultiplier(gameZoneId)`
- [x] **ChunkServer** — scheduled task каждые 30 сек: `zoneEventManager_.tickEventScheduler()`
- [x] **Invasion wave** — интеграция с `ChampionManager::spawnChampion` при завершении волны (требует P3 ✅)
- [x] Контент: добавить события `wolf_hour` и `merchant_convoy_forest` через migration 039

#### Технический долг (без привязки к этапу)

- [x] **accountId в `ClientDataStruct` chunk server** — `int accountId = 0` добавлен в `ClientDataStruct`; заполняется в `EventDispatcher::handleJoinGameClient` и `ClientManager::setClientCharacterId`; `ClientManager::getClientDataByCharacterId` добавлен; `CombatSystem` fellowship block фильтрует co-attackers с одинаковым accountId.
- [x] **Crit chance как атрибут** — закрыто migration 039: `crit_chance` добавлен в `entity_attributes`. `CombatCalculator` нужно добавить формулу `effective_crit = base_crit + sum(modifiers["crit_chance"])` в Шаге 2 P4.
