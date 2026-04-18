# Stats System Design Document

> Версия: 3.0 (after migration 060)

## Обзор

Система состоит из **28 атрибутов** (entity_attributes), сгруппированных в 4 категории:

1. **Primary Stats** — определяют архетип класса, растут по формуле
2. **Derived Combat** — боевые характеристики, вычисляемые из primary + формулы класса
3. **Utility** — регенерация, скорость
4. **Elemental Resistances** — снижение магического урона по школам

---

## Полный список атрибутов

| ID | Slug | Название | Категория | Единица | Описание |
|----|------|----------|-----------|---------|----------|
| 3 | `strength` | Strength | Primary | flat | Физическая сила. Скейлит physical_attack, carry weight |
| 4 | `intelligence` | Intelligence | Primary | flat | Магическая сила. Скейлит magical_attack |
| 27 | `constitution` | Constitution | Primary | flat | Выносливость. Бонус к HP regen через `regen.hpRegenConCoeff` |
| 28 | `wisdom` | Wisdom | Primary | flat | Мудрость. Бонус к MP regen через `regen.mpRegenWisCoeff` |
| 29 | `agility` | Agility | Primary | flat | Ловкость. Влияет на evasion, attack_speed |
| 1 | `max_health` | Maximum Health | Derived | flat | Максимальное HP |
| 2 | `max_mana` | Maximum Mana | Derived | flat | Максимальное MP |
| 12 | `physical_attack` | Physical Attack | Derived | flat | Базовый физический урон |
| 13 | `magical_attack` | Magical Attack | Derived | flat | Базовый магический урон |
| 6 | `physical_defense` | Physical Defense | Derived | flat | Снижение физического урона (DR формула) |
| 7 | `magical_defense` | Magical Defense | Derived | flat | Снижение магического урона (DR формула) |
| 8 | `crit_chance` | Crit Chance | Derived | % | Шанс крита (0–75%) |
| 9 | `crit_multiplier` | Crit Multiplier | Derived | % | Множитель крита (200 = x2.0) |
| 14 | `accuracy` | Accuracy | Derived | flat | Точность (влияет на hit chance) |
| 15 | `evasion` | Evasion | Derived | flat | Уклонение (снижает hit chance противника) |
| 16 | `block_chance` | Block Chance | Derived | % | Шанс блока (0–75%, Warrior only) |
| 17 | `block_value` | Block Value | Derived | flat | Уменьшение урона при блоке |
| 10 | `hp_regen_per_s` | HP Regen /s | Utility | flat | HP регенерация в секунду |
| 11 | `mp_regen_per_s` | MP Regen /s | Utility | flat | MP регенерация в секунду |
| 18 | `move_speed` | Move Speed | Utility | flat | Скорость передвижения |
| 19 | `attack_speed` | Attack Speed | Utility | flat | Скорость атаки. `effectiveSwingMs = baseSwingMs / (1 + attack_speed/100)` |
| 20 | `cast_speed` | Cast Speed | Utility | flat | Скорость каста. `effectiveCastMs = baseCastMs / (1 + cast_speed/100)` |
| 30 | `fire_resistance` | Fire Resistance | Elemental | % | Сопротивление огню (0–75%) |
| 31 | `ice_resistance` | Ice Resistance | Elemental | % | Сопротивление льду (0–75%) |
| 32 | `nature_resistance` | Nature Resistance | Elemental | % | Сопротивление природе (0–75%) |
| 33 | `arcane_resistance` | Arcane Resistance | Elemental | % | Сопротивление тайной магии (0–75%) |
| 34 | `holy_resistance` | Holy Resistance | Elemental | % | Сопротивление свету (0–75%) |
| 35 | `shadow_resistance` | Shadow Resistance | Elemental | % | Сопротивление тьме (0–75%) |

### Удалённые атрибуты (migration 059)
| Бывший ID | Slug | Причина |
|-----------|------|---------|
| 5 | `luck` | Нигде не использовался в коде/формулах |
| 21 | `heal_on_use` | Заменён на `item_use_effects` таблицу |
| 22 | `hunger_restore` | Заменён на `item_use_effects` таблицу |
| 23 | `physical_resistance` | Дублировал physical_defense (double dip) |
| 24 | `magical_resistance` | Дублировал magical_defense (double dip). Заменён elemental resistances |
| 26 | `parry_chance` | Нигде не использовался в коде |

---

## Формула роста стата по уровню

```
stat_at_level = base_value + multiplier × level^exponent
```

Параметры задаются в таблице `class_stat_formula`:
- `base_value` — значение на уровне 0 / стартовое
- `multiplier` — коэффициент роста
- `exponent` — степень роста (1.0 = линейно, >1.0 = ускоренно)

### Пример (Warrior, strength):
```
base_value = 12, multiplier = 2.0, exponent = 1.08
Level  1: 12 + 2.0 × 1^1.08 =  14.0
Level  5: 12 + 2.0 × 5^1.08 =  22.6
Level 10: 12 + 2.0 × 10^1.08 = 36.0
Level 20: 12 + 2.0 × 20^1.08 = 64.0
Level 50: 12 + 2.0 × 50^1.08 = 152.0
```

---

## Pipeline расчёта effective стата

```
effective = base_from_formula
          + Σ equipment_bonuses (apply_on = 'equip')
          + Σ active_effects (non-dot, non-hot, non-expired)
          + item_soul_tier_bonus
```

Все бонусы — **аддитивные flat**. Нет percentage bonuses в runtime pipeline (процентные бонусы конвертируются во flat при применении).

### Порядок слоёв:
1. `character_permanent_modifiers` → base attributes
2. `character_equipment` → `item_attributes_mapping` (apply_on='equip')
3. `player_active_effect` → status effects, buffs, debuffs (non-dot/hot)
4. Item Soul tier bonus → weapon primary attribute

---

## Damage Pipeline

```
1. HIT CHECK
   hitChance = base_hit_chance + (accuracy - evasion) × 0.01 + levelDiffHitMod
   hitChance = clamp(hitChance, hit_chance_min, hit_chance_max)    // 5%–95%
   if (rand > hitChance) → MISS

2. BASE DAMAGE
   rawDmg = skill.flatAdd + getAttr(scaleStat) × skill.coeff
   rawDmg = rawDmg × (1 ± damage_variance)    // ±12% по умолчанию

3. CRIT  ← CAPPED by combat.crit_chance_cap (75%)
   effectiveCritChance = min(crit_chance, crit_chance_cap)
   if (rand < effectiveCritChance / 100)
       damage = baseDmg × (crit_multiplier / 100)    // 200 = x2.0

4. BLOCK (target only, not mobs)  ← CAPPED by combat.block_chance_cap (75%)
   effectiveBlockChance = min(block_chance, block_chance_cap)
   if (rand < effectiveBlockChance / 100)
       damage = max(0, damage - block_value)

5. LEVEL DIFF MODIFIER
   levelDiff = clamp(attackerLevel - targetLevel, -cap, cap)
   damage = damage × (1 + levelDiff × level_diff_damage_per_level)

6. DEFENSE (diminishing returns)
   reduction = defense / (defense + K × targetLevel)
   reduction = clamp(reduction, 0, defense_cap)         // cap = 85%
   damage = damage × (1 - reduction)

   defense = physical_defense for physical school
   defense = magical_defense  for ALL magical schools (fire/ice/arcane/...)

7. ELEMENTAL RESISTANCE
   resistSlug = skill.school + "_resistance"             // e.g. "fire_resistance"
   resistPct = min(resistValue, max_resistance_cap) / 100
   damage = damage × (1 - resistPct)

   NOTE: physical school → "physical_resistance" → not in DB → 0 → no reduction
         Elemental schools (fire, ice, nature, arcane, holy, shadow) → имеют свой resist
```

---

## Школы магии (skill_school)

| ID | Slug | Используется в | Resist attr |
|----|------|----------------|-------------|
| 1 | `physical` | Basic Attack, Power Slash, Shield Bash, Whirlwind | — (нет resist) |
| 2 | `magical` | Generic magical (passives, buffs) | — |
| 3 | `fire` | Fireball | `fire_resistance` |
| 4 | `ice` | Frost Bolt | `ice_resistance` |
| 5 | `nature` | (future) | `nature_resistance` |
| 6 | `arcane` | Arcane Blast, Chain Lightning | `arcane_resistance` |
| 7 | `holy` | (future) | `holy_resistance` |
| 8 | `shadow` | (future) | `shadow_resistance` |

`damageType` (для выбора defense):
- `school == "physical"` → `"physical"` → `physical_defense`
- Всё остальное → `"magical"` → `magical_defense`

---

## Attack Speed / Cast Speed Pipeline

Применяется в `CombatSystem::initiateSkillUsage()` при создании `CombatActionStruct`:

```
// Для заклинаний (castMs > 0):
speedFactor = 1 / (1 + cast_speed / combat.cast_speed_base_divisor)    // divisor = 100
effectiveCastMs = castMs × speedFactor
effectiveSwingMs = swingMs × speedFactor

// Для физических скиллов (castMs == 0):
speedFactor = 1 / (1 + attack_speed / combat.attack_speed_base_divisor)   // divisor = 100
effectiveSwingMs = swingMs × speedFactor
```

Пример: attack_speed = 50 → speedFactor = 1/(1+50/100) = 0.667 → swing на 33% быстрее.

---

## Regen Pipeline

```
tickRegen() вызывается каждые regen.tickIntervalMs (default 4000 ms)

Пропуск: dead (HP==0) ИЛИ inCombat (< regen.disableInCombatMs после удара)

hpFromStats = regen.baseHpRegen + max(0, constitution × regen.hpRegenConCoeff)
mpFromStats = regen.baseMpRegen + max(0, wisdom × regen.mpRegenWisCoeff)

hpRegenBase = base_attr(hp_regen_per_s) + equipment + active_effects
mpRegenBase = base_attr(mp_regen_per_s) + equipment + active_effects

hpGain = max(hpFromStats, hpRegenBase × tickSec)
mpGain = max(mpFromStats, mpRegenBase × tickSec)

current = min(current + gain, effectiveMax)
```

---

## Классы

### Mage (class_id=1)
- **Роль**: Ranged DPS / caster
- **Сильные стороны**: intelligence, wisdom, max_mana, magical_attack, magical_defense, cast_speed
- **Слабые стороны**: max_health, strength, physical_defense, physical_attack, block (нет блока)
- **Базовые elemental resist**: 0.3 × level (все школы, маг = arcane affinity)

### Warrior (class_id=2)
- **Роль**: Melee DPS / tank
- **Сильные стороны**: strength, constitution, max_health, physical_attack, physical_defense, block
- **Слабые стороны**: intelligence, wisdom, max_mana, magical_defense, cast_speed
- **Базовые elemental resist**: 0.2 × level (все школы, ниже чем у мага)

---

## Stat Caps (game_config)

| Config key | Значение | Описание |
|------------|----------|----------|
| `combat.defense_cap` | 0.85 | Max DR% от defense формулы |
| `combat.max_resistance_cap` | 75 | Max elemental resistance % |
| `combat.elemental_resistance_cap` | 75 | Same, explicit |
| `combat.crit_chance_cap` | 75 | Max crit chance % |
| `combat.block_chance_cap` | 75 | Max block chance % |
| `combat.evasion_cap` | 75 | Max evasion effectiveness % |
| `combat.hit_chance_min` | 0.05 | Min hit chance (5%) |
| `combat.hit_chance_max` | 0.95 | Max hit chance (95%) |

---

## Balance Tables (Level Progression)

### Warrior at key levels

| Level | STR | CON | HP | Phys.Atk | Phys.Def | Block% |
|-------|-----|-----|----|----------|----------|--------|
| 1 | 14 | 12 | 168 | 14 | 19 | 11 |
| 5 | 23 | 21 | 253 | 30 | 39 | 13 |
| 10 | 36 | 34 | 393 | 54 | 68 | 15 |
| 20 | 64 | 62 | 736 | 106 | 136 | 20 |

### Mage at key levels

| Level | INT | WIS | Mana | Mag.Atk | Mag.Def | Cast Spd |
|-------|-----|-----|------|---------|---------|----------|
| 1 | 18 | 12 | 227 | 7 | 10 | 7 |
| 5 | 28 | 21 | 336 | 15 | 20 | 9 |
| 10 | 42 | 32 | 500 | 26 | 32 | 11 |
| 20 | 72 | 56 | 893 | 48 | 60 | 15 |

---

## Item Attribute Examples

| Item | Slot | Bonuses |
|------|------|---------|
| Iron Sword | main_hand | physical_attack +10, strength +3 |
| Wooden Staff | main_hand | magical_attack +8, intelligence +3 |
| Wooden Shield | off_hand | physical_defense +5, block_value +5 |

---

## Mob Combat Stats (after migration 060)

### Small Fox (id=1, level 1)
| Stat | Value | | Stat | Value |
|------|-------|-|------|-------|
| max_health | 40 | | agility | 8 |
| strength | 3 | | evasion | 8 |
| constitution | 3 | | accuracy | 4 |
| intelligence | 1 | | crit_chance | 3% |
| wisdom | 1 | | crit_multiplier | 200 (x2.0) |
| physical_attack | 6 | | attack_speed | 5 |
| physical_defense | 2 | | move_speed | 6.5 |
| hp_regen_per_s | 0.5 | | | |

### Grey Wolf (id=2, level 1)
| Stat | Value | | Stat | Value |
|------|-------|-|------|-------|
| max_health | 80 | | agility | 5 |
| strength | 6 | | evasion | 4 |
| constitution | 6 | | accuracy | 6 |
| intelligence | 2 | | crit_chance | 5% |
| wisdom | 2 | | crit_multiplier | 200 (x2.0) |
| physical_attack | 10 | | attack_speed | 5 |
| physical_defense | 5 | | move_speed | 5.0 |
| magical_defense | 2 | | ice_resistance | 2% |
| hp_regen_per_s | 1.0 | | | |

## Mob Elemental Resistances

| Mob | fire | ice | nature | arcane | holy | shadow |
|-----|------|-----|--------|--------|------|--------|
| Small Fox | 0 | 0 | 0 | 0 | 0 | 0 |
| Grey Wolf | 0 | 2 | 0 | 0 | 0 | 0 |

> Мобы Wolf Pack Leader, Old Wolf, Forest Goblin удалены (migration 060).

---

## Migration 060 — Clean Modifiers & Prune Mobs

- `character_permanent_modifiers` — TRUNCATED (все персонажи используют только class_stat_formula)
- Удалены мобы: Wolf Pack Leader (id=6), Old Wolf (id=7), Forest Goblin (id=8) + все FK: mob_stat, mob_skills, mob_loot_info, mob_position, spawn_zone_mobs
- Оставлены только: Small Fox (id=1), Grey Wolf (id=2)
- Ребаланс mob_stat: fox HP=40, wolf HP=80
- C++: attack_speed/cast_speed подключены в CombatSystem
- C++: stat caps (crit_chance_cap, block_chance_cap) добавлены в CombatCalculator

---

## Stat Caps (enforced in C++)

| Stat | Cap | Config key | Enforced in |
|------|-----|------------|-------------|
| crit_chance | 75% | `combat.crit_chance_cap` | `CombatCalculator::rollCriticalHit()` |
| block_chance | 75% | `combat.block_chance_cap` | `CombatCalculator::rollBlock()` |
| evasion | ~95% | `combat.hit_chance_min` / `hit_chance_max` | `CombatSystem::rollMiss()` (implicit) |
| defense reduction | 85% | `combat.defense_cap` | `CombatCalculator::applyDefense()` |
| elemental resistance | 75% | `combat.max_resistance_cap` | `CombatCalculator::applyElementalResistance()` |

---

## Files Reference

### Database tables
- `entity_attributes` — master list of all stat slugs
- `class_stat_formula` — level scaling per class per attribute
- `character_permanent_modifiers` — base stat values per character (currently empty)
- `item_attributes_mapping` — item stat bonuses
- `mob_stat` — mob flat stat values
- `skill_school` — damage schools (physical, fire, ice, etc.)
- `damage_elements` — element slugs
- `game_config` — tunable constants (combat.*, regen.*)
- `item_set_bonuses` — set piece thresholds/bonuses (exists but unused in code)

### C++ files
- `CombatCalculator.cpp/hpp` — damage/heal pipeline, mergeEffects, stat caps
- `CombatSystem.cpp/hpp` — skill usage, attack_speed/cast_speed modifiers
- `RegenManager.cpp/hpp` — HP/MP regen tick
- `CharacterStatsNotificationService.cpp/hpp` — stats_update packet builder
- `DataStructs.hpp` — CharacterAttributeStruct, MobAttributeStruct
- `SkillStructs.hpp` — SkillStruct (school field → element)
