## [ПЛАН] Авторегенерация HP/MP и система пассивных скилов

**Статус реализации:**
- ✅ Фаза 1 (Авторегенерация HP/MP) — реализована: `RegenManager.cpp`, `ChunkServer.cpp:1100`, атрибуты `hp_regen_per_s`/`mp_regen_per_s` в `entity_attributes`, параметры `regen.*` в `game_config`
- ✅ Фаза 2 (Пассивные скилы — always-on модификаторы) — реализована: `isPassive` в `SkillStructs`, игнорируется в `EventHandler.cpp`, `passive_skill_modifiers` в game server
- ❌ Фаза 3 (Тригерные proc-пассивки) — **НЕ реализована**: `PassiveSkillTriggerManager` отсутствует, поле `triggerEvent` не существует

---

### Фаза 1 — Базовый авторегенерация HP/MP (stat-driven)

**Цель**: Персонаж автоматически восстанавливает HP и MP вне зависимости от скилов.

#### 1.1. База данных
- [x] Добавить атрибуты персонажа `hp_regen_per_s` и `mp_regen_per_s` в таблицу `entity_attributes` ✅
      (базовые значения: напр. 2 HP/tick, 1 MP/tick на 1-м уровне)
- [x] Добавить формулы масштабирования: `hp_regen` растёт от `CON`, `mp_regen` от `WIS/INT` ✅
      через atribute_modifiers или отдельную таблицу `regen_formulas`

#### 1.2. Сервер — ChunkServer (главный цикл)
- [x] Добавить отдельный таймер регена в `ChunkServer.cpp` (раз в 3-5 сек, вне боя) ✅ (строка 1094)
      Отдельный таймер, а не через CombatSystem — реген должен работать и без боя
- [x] Реализовать `RegenManager` (новый сервис) с методом `tickRegen()` ✅:
      - Перебирает всех активных персонажей из `CharacterManager`
      - Рассчитывает `hp_regen = base_hp_regen + (CON_value * coeff)` из атрибутов
      - Рассчитывает `mp_regen = base_mp_regen + (WIS_value * coeff)` из атрибутов
      - Применяет только если `currentHp < maxHp` (или mp)
      - Не применяет реген если персонаж мёртв

#### 1.3. Сервер — Уведомление клиента
- [x] После тика регена отправить `stats_update` пакет клиенту ✅
      (переиспользовать существующий `StatsNotificationService`)
- [x] Добавить пакет `regen_tick_result` (или включить в существующий stats_update) ✅:
      `{ type: "regen_tick", hp_gained, mp_gained, new_hp, new_mp }`
      Клиент плавно анимирует цифры поверх персонажа

#### 1.4. Настройка в `config.json`
- [x] Добавить параметры ✅отправить `stats_update` пакет клиенту ✅
      (переиспользовать существующий `StatsNotificationService`)
- [x] Добавить пакет `regen_tick_result` (или включить в существующий stats_update) ✅:
      `{ type: "regen_tick", hp_gained, mp_gained, new_hp, new_mp }`
      Клиент плавно анимирует цифры поверх персонажа

#### 1.4. Настройка в `config.json`
- [x] Добавить параметры ✅:
      `"regen": { "tickIntervalMs": 4000, "baseHpRegen": 2, "baseMpRegen": 1,
                  "hpRegenConCoeff": 0.3, "mpRegenWisCoeff": 0.5,
                  "disableInCombatMs": 8000 }`
      `disableInCombatMs` — сколько мс после последнего удара реген неактивен

---
x] Добавить в таблицу `skills` поле `is_passive BOOLEAN DEFAULT FALSE` ✅
- [x] Добавить таблицу `passive_skill_modifiers` ✅:
      ```
      id, skill_id, attribute_slug, modifier_type (flat|percent),
      value, condition (NULL | "out_of_combat" | "in_combat")
      ```
      Один пассивный скил может давать несколько модификаторов

#### 2.2. SkillStruct расширение (`include/data/SkillStructs.hpp`)
- [x] Добавить поля ✅:
      ```cpp
      bool isPassive = false;
      // Для пассивных скилов список модификаторов:
      // хранятся через тот же ActiveEffectStruct с expiresAt=0, sourceType="skill"
      ```
      Не нужен отдельный новый struct — ActiveEffectStruct уже подходит

#### 2.3. CharacterManager — применение пассивных скилов
- [x] При загрузке персонажа (`onCharacterLoaded`): пройтись по скилам персонажа, ✅
      для каждого `isPassive == true` — создать `ActiveEffectStruct` ( expiresAt=0,
      tickMs=0, sourceType="skill_passive") и добавить в `character.activeEffects`
- [ ] При снятии скила (если будет reset skills) — убрать соответствующие эффекты

#### 2.4. CombatCalculator — учёт пассивных модификаторов
- [x] `CombatCalculator` корректно суммирует пассивные `ActiveEffect` ✅
      с `tickMs=0, expiresAt=0` при расчёте статовжа, ✅
      для каждого `isPassive == true` — создать `ActiveEffectStruct` ( expiresAt=0,
      tickMs=0, sourceType="skill_passive") и добавить в `character.activeEffects`
- [ ] При снятии скила (если будет reset skills) — убрать соответствующие эффекты
ерные)

> **❌ НЕ РЕАЛИЗОВАНО** — `PassiveSkillTriggerManager` отсутствует, поле `triggerEvent` не добавлено в `SkillStructs`.
#### 2.4. CombatCalculator — учёт пассивных модификаторов
- [x] `CombatCalculator` корректно суммирует пассивные `ActiveEffect` ✅
      с `tickMs=0, expiresAt=0` при расчёте статов

---

### Фаза 3 — Продвинутые пассивные скилы (тригерные)

> **❌ НЕ РЕАЛИЗОВАНО** — `PassiveSkillTriggerManager` отсутствует, поле `triggerEvent` не добавлено в `SkillStructs`.

**Цель**: Пассивные скилы с условием срабатывания — proc, on_hit, on_damage_taken.
Это уже полноценная passive skill система уровня L2/WoW.

#### 3.1. SkillStruct — triggerEvent
- [ ] Добавить поле `std::string triggerEvent = ""`:
      Значения: `""` (always, уже покрыто Фазой 2), `"on_hit"`, `"on_damage_taken"`,
      `"on_kill"`, `"on_low_hp"` (< 20% HP), `"in_combat"`

#### 3.2. PassiveSkillTriggerManager (новый сервис)
- [ ] Создать `PassiveSkillTriggerManager` с методами:
      - `onHit(casterId, targetId)` — срабатывет при нанесении удара
      - `onDamageTaken(characterId, damage)` — при получении урона
      - `onKill(characterId, targetId)` — при убийстве
      - `onLowHp(characterId)` — при падении HP ниже порога
- [ ] Каждый метод: перебирает скилы персонажа с нужным `triggerEvent`,
      проверяет `procChance` (вероятность%), применяет эффект через `ActiveEffectStruct`
      с коротким `expiresAt` (временный buff от proc)

#### 3.3. Примеры пассивных скилов (для базы данных)
- [ ] `improved_regen` — `always`, увеличивает `hp_regen` на 20%
- [ ] `constitution_mastery` — `always`, +flat к `CON`
- [ ] `battle_hardening` — `on_damage_taken`, temporary +armor на 3 сек
- [ ] `executioner` — `on_kill`, restore 10% maxHp

---

### Заметки по дизайну
- Реген вне боя vs в бою: разные коэффициенты. В бою реген замедляется или отключается.
  Это создаёт тактику (выход из боя для лечения vs использование HP-зелий).
- Пассивные скилы не занимают слоты в hotbar — отображаются отдельным вкладкой UI.
- Пассивные скилы из Фазы 2 (flat/percent модификаторы) ПЕРЕРАСЧИТЫВАЮТСЯ при:
  экипировке/снятии шмота, смене скилов, уровнем вверх — поэтому нужен метод
  `recalculatePassiveBonuses(characterId)`.

---