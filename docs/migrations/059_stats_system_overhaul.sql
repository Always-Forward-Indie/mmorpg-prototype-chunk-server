-- =============================================================================
-- Migration 059: Stats System Overhaul
-- =============================================================================
-- Цели:
--   1. Добавить primary stats: constitution, wisdom, agility
--   2. Добавить elemental resistances: fire/ice/nature/arcane/holy/shadow
--   3. Удалить мёртвые/некорректные записи: luck, heal_on_use, hunger_restore,
--      parry_chance, physical_resistance, magical_resistance
--   4. Пересчитать class_stat_formula для обоих классов по новой системе
--   5. Пересчитать character_permanent_modifiers для тестовых персонажей
--   6. Пересчитать mob_stat для мобов (удалить ссылки на удалённые атрибуты)
--   7. Унифицировать crit_multiplier: хранить как % (200 = x2.0)
--   8. Добавить stat caps в game_config
-- =============================================================================

BEGIN;

-- ─────────────────────────────────────────────────────────────────────────────
-- 1. Add new entity_attributes
-- ─────────────────────────────────────────────────────────────────────────────

INSERT INTO public.entity_attributes (id, name, slug) OVERRIDING SYSTEM VALUE VALUES
    (27, 'Constitution',        'constitution'),
    (28, 'Wisdom',              'wisdom'),
    (29, 'Agility',             'agility'),
    (30, 'Fire Resistance',     'fire_resistance'),
    (31, 'Ice Resistance',      'ice_resistance'),
    (32, 'Nature Resistance',   'nature_resistance'),
    (33, 'Arcane Resistance',   'arcane_resistance'),
    (34, 'Holy Resistance',     'holy_resistance'),
    (35, 'Shadow Resistance',   'shadow_resistance')
ON CONFLICT DO NOTHING;

-- ─────────────────────────────────────────────────────────────────────────────
-- 2. Remove dead attributes from entity_attributes
--    (cascade into FKs: mob_stat, item_attributes_mapping, class_stat_formula,
--     character_permanent_modifiers, etc.)
-- ─────────────────────────────────────────────────────────────────────────────

-- First clean up all foreign-key references to the attrs we are about to delete.
-- id=5 (luck), id=21 (heal_on_use), id=22 (hunger_restore),
-- id=23 (physical_resistance), id=24 (magical_resistance), id=26 (parry_chance)

DELETE FROM public.class_stat_formula       WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
DELETE FROM public.character_permanent_modifiers WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
DELETE FROM public.mob_stat                 WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
DELETE FROM public.item_attributes_mapping  WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
-- item_set_bonuses — таблица пуста, но на всякий случай:
DELETE FROM public.item_set_bonuses         WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
-- status_effect_modifiers — attribute_id references entity_attributes
DELETE FROM public.status_effect_modifiers  WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
-- player/mob active effects and NPC attributes
DELETE FROM public.player_active_effect     WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
DELETE FROM public.mob_active_effect        WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
DELETE FROM public.npc_attributes           WHERE attribute_id IN (5, 21, 22, 23, 24, 26);
DELETE FROM public.skill_effects_mapping    WHERE attribute_id IN (5, 21, 22, 23, 24, 26);

-- Now safe to delete the attributes themselves
DELETE FROM public.entity_attributes WHERE id IN (5, 21, 22, 23, 24, 26);

-- ─────────────────────────────────────────────────────────────────────────────
-- 3. Rebuild class_stat_formula for both classes with new stat system
--    Formula: stat = base_value + multiplier * level^exponent
-- ─────────────────────────────────────────────────────────────────────────────

-- Wipe existing formulas for both classes and re-insert cleanly
DELETE FROM public.class_stat_formula WHERE class_id IN (1, 2);

-- ── CLASS 1: MAGE ────────────────────────────────────────────────────────────
-- Primary focus: intelligence, wisdom, mana. Weak: HP, physical, block.
INSERT INTO public.class_stat_formula (class_id, attribute_id, base_value, multiplier, exponent) VALUES
    -- Primary stats
    (1, 3,  4.00,  0.6000, 1.0000),  -- strength        (low)
    (1, 4,  15.00, 2.5000, 1.0500),  -- intelligence     (main stat)
    (1, 27, 6.00,  0.8000, 1.0000),  -- constitution     (low-med)
    (1, 28, 10.00, 2.0000, 1.0500),  -- wisdom           (high)
    (1, 29, 5.00,  0.5000, 1.0000),  -- agility          (low)
    -- Derived combat
    (1, 1,  80.00, 8.0000, 1.1000),  -- max_health       (low base)
    (1, 2,  200.00,25.0000,1.1200),  -- max_mana         (high base)
    (1, 12, 2.00,  0.5000, 1.0000),  -- physical_attack  (minimal)
    (1, 13, 5.00,  1.8000, 1.0800),  -- magical_attack   (main DPS)
    (1, 6,  4.00,  1.0000, 1.0500),  -- physical_defense (low)
    (1, 7,  8.00,  2.0000, 1.0800),  -- magical_defense  (high)
    (1, 8,  5.00,  0.3000, 1.0000),  -- crit_chance      (moderate %)
    (1, 9,  200.00,0.0000, 1.0000),  -- crit_multiplier  (% — 200 = x2.0, flat)
    (1, 14, 5.00,  0.5000, 1.0000),  -- accuracy
    (1, 15, 5.00,  0.5000, 1.0000),  -- evasion
    (1, 16, 0.00,  0.0000, 1.0000),  -- block_chance     (mage — нет блока)
    (1, 17, 0.00,  0.0000, 1.0000),  -- block_value
    -- Utility
    (1, 10, 1.00,  0.2000, 1.0000),  -- hp_regen_per_s
    (1, 11, 2.00,  0.8000, 1.0500),  -- mp_regen_per_s   (mage high mana regen)
    (1, 18, 5.00,  0.0000, 1.0000),  -- move_speed       (flat 5)
    (1, 19, 5.00,  0.2000, 1.0000),  -- attack_speed
    (1, 20, 7.00,  0.4000, 1.0000),  -- cast_speed       (mage fast cast)
    -- Elemental resistances (mage has innate magic affinity)
    (1, 30, 0.00,  0.3000, 1.0000),  -- fire_resistance
    (1, 31, 0.00,  0.3000, 1.0000),  -- ice_resistance
    (1, 32, 0.00,  0.3000, 1.0000),  -- nature_resistance
    (1, 33, 0.00,  0.3000, 1.0000),  -- arcane_resistance
    (1, 34, 0.00,  0.3000, 1.0000),  -- holy_resistance
    (1, 35, 0.00,  0.3000, 1.0000);  -- shadow_resistance

-- ── CLASS 2: WARRIOR ─────────────────────────────────────────────────────────
-- Primary focus: strength, constitution, HP. Block-based defense. Weak magic.
INSERT INTO public.class_stat_formula (class_id, attribute_id, base_value, multiplier, exponent) VALUES
    -- Primary stats
    (2, 3,  12.00, 2.0000, 1.0800),  -- strength      (main stat)
    (2, 4,  5.00,  0.5000, 1.0000),  -- intelligence   (minimal)
    (2, 27, 10.00, 2.0000, 1.0800),  -- constitution   (high — tanky)
    (2, 28, 4.00,  0.5000, 1.0000),  -- wisdom         (low)
    (2, 29, 6.00,  0.8000, 1.0000),  -- agility        (moderate)
    -- Derived combat
    (2, 1,  150.00,18.0000,1.1500),  -- max_health     (high base)
    (2, 2,  50.00, 5.0000, 1.0500),  -- max_mana       (just enough for skills)
    (2, 12, 10.00, 3.5000, 1.1000),  -- physical_attack (main DPS)
    (2, 13, 2.00,  0.5000, 1.0000),  -- magical_attack  (minimal)
    (2, 6,  15.00, 4.0000, 1.1200),  -- physical_defense (high, plate armor)
    (2, 7,  5.00,  1.5000, 1.0500),  -- magical_defense  (low)
    (2, 8,  5.00,  0.3000, 1.0000),  -- crit_chance
    (2, 9,  200.00,0.0000, 1.0000),  -- crit_multiplier  (% — 200 = x2.0, flat)
    (2, 14, 5.00,  0.5000, 1.0000),  -- accuracy
    (2, 15, 3.00,  0.3000, 1.0000),  -- evasion         (warrior не уклоняется, блокирует)
    (2, 16, 10.00, 0.5000, 1.0000),  -- block_chance     (warrior-only mechanic)
    (2, 17, 3.00,  1.0000, 1.0500),  -- block_value
    -- Utility
    (2, 10, 1.00,  0.4000, 1.0000),  -- hp_regen_per_s   (high sturdy regen)
    (2, 11, 0.50,  0.2000, 1.0000),  -- mp_regen_per_s   (low)
    (2, 18, 5.00,  0.0000, 1.0000),  -- move_speed        (flat 5)
    (2, 19, 5.00,  0.3000, 1.0000),  -- attack_speed
    (2, 20, 3.00,  0.1000, 1.0000),  -- cast_speed        (warrior has slow casts)
    -- Elemental resistances (warrior has lower magic resist)
    (2, 30, 0.00,  0.2000, 1.0000),  -- fire_resistance
    (2, 31, 0.00,  0.2000, 1.0000),  -- ice_resistance
    (2, 32, 0.00,  0.2000, 1.0000),  -- nature_resistance
    (2, 33, 0.00,  0.2000, 1.0000),  -- arcane_resistance
    (2, 34, 0.00,  0.2000, 1.0000),  -- holy_resistance
    (2, 35, 0.00,  0.2000, 1.0000);  -- shadow_resistance

-- ─────────────────────────────────────────────────────────────────────────────
-- 4. Rebuild character_permanent_modifiers for test characters
--    These are the GM-set "base" attributes. After migration the game-server
--    should recalculate them via class_stat_formula on next login.
--    For now we set sensible level-appropriate values manually.
-- ─────────────────────────────────────────────────────────────────────────────

-- Wipe existing GM modifiers for all 3 test chars
DELETE FROM public.character_permanent_modifiers WHERE source_type = 'gm' AND character_id IN (1, 2, 3);

-- Helper: reset the sequence so new IDs don't collide
SELECT setval('public.character_attributes_id_seq', 100, false);

-- ── Character 1: TetsMage1Player (Mage, level 2) ─────────────────────────────
-- stat = base_value + multiplier * 2^exponent  (from class 1 formulas above)
INSERT INTO public.character_permanent_modifiers (character_id, attribute_id, value, source_type) VALUES
    (1, 3,  5,   'gm'),   -- strength       = 4 + 0.6*2 ≈ 5
    (1, 4,  20,  'gm'),   -- intelligence   = 15 + 2.5*2.1 ≈ 20
    (1, 27, 8,   'gm'),   -- constitution   = 6 + 0.8*2 = 8
    (1, 28, 14,  'gm'),   -- wisdom         = 10 + 2.0*2.1 ≈ 14
    (1, 29, 6,   'gm'),   -- agility        = 5 + 0.5*2 = 6
    (1, 1,  97,  'gm'),   -- max_health     = 80 + 8*2.14 ≈ 97
    (1, 2,  254, 'gm'),   -- max_mana       = 200 + 25*2.17 ≈ 254
    (1, 12, 3,   'gm'),   -- physical_attack = 2 + 0.5*2 = 3
    (1, 13, 9,   'gm'),   -- magical_attack  = 5 + 1.8*2.11 ≈ 9
    (1, 6,  6,   'gm'),   -- physical_defense= 4 + 1.0*2.07 ≈ 6
    (1, 7,  12,  'gm'),   -- magical_defense = 8 + 2.0*2.11 ≈ 12
    (1, 8,  6,   'gm'),   -- crit_chance     = 5 + 0.3*2 = 6 (%)
    (1, 9,  200, 'gm'),   -- crit_multiplier = 200 (= x2.0)
    (1, 14, 6,   'gm'),   -- accuracy        = 5 + 0.5*2 = 6
    (1, 15, 6,   'gm'),   -- evasion         = 5 + 0.5*2 = 6
    (1, 16, 0,   'gm'),   -- block_chance    = 0 (mage)
    (1, 17, 0,   'gm'),   -- block_value     = 0
    (1, 10, 1,   'gm'),   -- hp_regen_per_s  = 1 + 0.2*2 = 1.4 → 1
    (1, 11, 4,   'gm'),   -- mp_regen_per_s  = 2 + 0.8*2.07 ≈ 4
    (1, 18, 5,   'gm'),   -- move_speed
    (1, 19, 5,   'gm'),   -- attack_speed    = 5 + 0.2*2 = 5
    (1, 20, 8,   'gm'),   -- cast_speed      = 7 + 0.4*2 = 8
    (1, 30, 1,   'gm'),   -- fire_resistance
    (1, 31, 1,   'gm'),   -- ice_resistance
    (1, 32, 1,   'gm'),   -- nature_resistance
    (1, 33, 1,   'gm'),   -- arcane_resistance
    (1, 34, 1,   'gm'),   -- holy_resistance
    (1, 35, 1,   'gm');   -- shadow_resistance

-- ── Character 2: TetsMage2Player (Mage, level 3) ─────────────────────────────
INSERT INTO public.character_permanent_modifiers (character_id, attribute_id, value, source_type) VALUES
    (2, 3,  6,   'gm'),   -- strength
    (2, 4,  23,  'gm'),   -- intelligence
    (2, 27, 8,   'gm'),   -- constitution
    (2, 28, 16,  'gm'),   -- wisdom
    (2, 29, 7,   'gm'),   -- agility
    (2, 1,  106, 'gm'),   -- max_health
    (2, 2,  280, 'gm'),   -- max_mana
    (2, 12, 4,   'gm'),   -- physical_attack
    (2, 13, 11,  'gm'),   -- magical_attack
    (2, 6,  7,   'gm'),   -- physical_defense
    (2, 7,  14,  'gm'),   -- magical_defense
    (2, 8,  6,   'gm'),   -- crit_chance (%)
    (2, 9,  200, 'gm'),   -- crit_multiplier (= x2.0)
    (2, 14, 7,   'gm'),   -- accuracy
    (2, 15, 7,   'gm'),   -- evasion
    (2, 16, 0,   'gm'),   -- block_chance
    (2, 17, 0,   'gm'),   -- block_value
    (2, 10, 2,   'gm'),   -- hp_regen_per_s
    (2, 11, 4,   'gm'),   -- mp_regen_per_s
    (2, 18, 5,   'gm'),   -- move_speed
    (2, 19, 6,   'gm'),   -- attack_speed
    (2, 20, 8,   'gm'),   -- cast_speed
    (2, 30, 1,   'gm'),   -- fire_resistance
    (2, 31, 1,   'gm'),   -- ice_resistance
    (2, 32, 1,   'gm'),   -- nature_resistance
    (2, 33, 1,   'gm'),   -- arcane_resistance
    (2, 34, 1,   'gm'),   -- holy_resistance
    (2, 35, 1,   'gm');   -- shadow_resistance

-- ── Character 3: TetsWarrior1Player (Warrior, level 5) ───────────────────────
INSERT INTO public.character_permanent_modifiers (character_id, attribute_id, value, source_type) VALUES
    (3, 3,  22,  'gm'),   -- strength       = 12 + 2.0*5^1.08 ≈ 22
    (3, 4,  8,   'gm'),   -- intelligence   = 5 + 0.5*5 = 8
    (3, 27, 20,  'gm'),   -- constitution   = 10 + 2.0*5^1.08 ≈ 20
    (3, 28, 7,   'gm'),   -- wisdom         = 4 + 0.5*5 = 7
    (3, 29, 10,  'gm'),   -- agility        = 6 + 0.8*5 = 10
    (3, 1,  252, 'gm'),   -- max_health     = 150 + 18*5^1.15 ≈ 252
    (3, 2,  76,  'gm'),   -- max_mana       = 50 + 5*5.18 ≈ 76
    (3, 12, 30,  'gm'),   -- physical_attack = 10 + 3.5*5^1.1 ≈ 30
    (3, 13, 5,   'gm'),   -- magical_attack  = 2 + 0.5*5 = 5
    (3, 6,  39,  'gm'),   -- physical_defense= 15 + 4.0*5^1.12 ≈ 39
    (3, 7,  13,  'gm'),   -- magical_defense = 5 + 1.5*5.18 ≈ 13
    (3, 8,  7,   'gm'),   -- crit_chance     = 5 + 0.3*5 = 7 (%)
    (3, 9,  200, 'gm'),   -- crit_multiplier (= x2.0)
    (3, 14, 8,   'gm'),   -- accuracy
    (3, 15, 5,   'gm'),   -- evasion         = 3 + 0.3*5 = 5
    (3, 16, 13,  'gm'),   -- block_chance    = 10 + 0.5*5 = 13 (%)
    (3, 17, 8,   'gm'),   -- block_value     = 3 + 1.0*5.18 ≈ 8
    (3, 10, 3,   'gm'),   -- hp_regen_per_s  = 1 + 0.4*5 = 3
    (3, 11, 2,   'gm'),   -- mp_regen_per_s
    (3, 18, 5,   'gm'),   -- move_speed
    (3, 19, 7,   'gm'),   -- attack_speed    = 5 + 0.3*5 = 7
    (3, 20, 4,   'gm'),   -- cast_speed      = 3 + 0.1*5 = 4
    (3, 30, 1,   'gm'),   -- fire_resistance
    (3, 31, 1,   'gm'),   -- ice_resistance
    (3, 32, 1,   'gm'),   -- nature_resistance
    (3, 33, 1,   'gm'),   -- arcane_resistance
    (3, 34, 1,   'gm'),   -- holy_resistance
    (3, 35, 1,   'gm');   -- shadow_resistance

-- ─────────────────────────────────────────────────────────────────────────────
-- 5. Clean up mob_stat: remove references to deleted attributes,
--    add constitution/wisdom/agility/elemental resist for mobs that had old stats
-- ─────────────────────────────────────────────────────────────────────────────

-- Already deleted luck(5), heal_on_use(21), hunger_restore(22),
-- physical_resistance(23), magical_resistance(24), parry_chance(26) above.

-- Mobs don't need constitution/wisdom/agility as primary stats (those are derived
-- for players only). Mob stat blocks remain as-is: direct combat stats.
-- But we add elemental resistances to give mobs flavour:

-- Wolf Pack Leader (id=6): fire_resistance = 0 (animal), ice_resistance = 5 (cold-adapted)
INSERT INTO public.mob_stat (mob_id, attribute_id, flat_value, multiplier, exponent) VALUES
    (6, 30, 0.00, NULL, NULL),    -- fire_resistance
    (6, 31, 5.00, NULL, NULL),    -- ice_resistance (cold wolf)
    (6, 32, 0.00, NULL, NULL),    -- nature_resistance
    (6, 33, 0.00, NULL, NULL),    -- arcane_resistance
    (6, 34, 0.00, NULL, NULL),    -- holy_resistance
    (6, 35, 0.00, NULL, NULL);    -- shadow_resistance

-- OldWolf (id=7): ice_resistance = 3 (partial)
INSERT INTO public.mob_stat (mob_id, attribute_id, flat_value, multiplier, exponent) VALUES
    (7, 30, 0.00, NULL, NULL),
    (7, 31, 3.00, NULL, NULL),
    (7, 32, 0.00, NULL, NULL),
    (7, 33, 0.00, NULL, NULL),
    (7, 34, 0.00, NULL, NULL),
    (7, 35, 0.00, NULL, NULL);

-- Forest Goblin (id=8): nature_resistance = 10, fire weakness (already 0)
INSERT INTO public.mob_stat (mob_id, attribute_id, flat_value, multiplier, exponent) VALUES
    (8, 30, 0.00, NULL, NULL),
    (8, 31, 0.00, NULL, NULL),
    (8, 32, 10.00, NULL, NULL),   -- nature_resistance (forest creature)
    (8, 33, 5.00, NULL, NULL),    -- arcane_resistance
    (8, 34, 0.00, NULL, NULL),
    (8, 35, 0.00, NULL, NULL);

-- Small Fox (id=1) + Grey Wolf (id=2): resistance = 0 everywhere (common mobs)
INSERT INTO public.mob_stat (mob_id, attribute_id, flat_value, multiplier, exponent) VALUES
    (1, 30, 0.00, NULL, NULL), (1, 31, 0.00, NULL, NULL), (1, 32, 0.00, NULL, NULL),
    (1, 33, 0.00, NULL, NULL), (1, 34, 0.00, NULL, NULL), (1, 35, 0.00, NULL, NULL),
    (2, 30, 0.00, NULL, NULL), (2, 31, 0.00, NULL, NULL), (2, 32, 0.00, NULL, NULL),
    (2, 33, 0.00, NULL, NULL), (2, 34, 0.00, NULL, NULL), (2, 35, 0.00, NULL, NULL);

-- ─────────────────────────────────────────────────────────────────────────────
-- 6. Fix item_attributes_mapping: item 3 (Health Potion) and item 4 (Bread)
--    currently reference heal_on_use (id=21). That attribute is deleted.
--    These items use item_use_effects for their effects (already populated).
--    Remove the item_attributes_mapping rows since they are item-use, not equip.
-- ─────────────────────────────────────────────────────────────────────────────

DELETE FROM public.item_attributes_mapping WHERE item_id IN (3, 4) AND attribute_id = 21;

-- Add meaningful equipment bonuses for Iron Sword (id=1) and Wooden Staff (id=15)
-- Iron Sword already has physical_attack=10. Add strength bonus.
INSERT INTO public.item_attributes_mapping (item_id, attribute_id, value, apply_on) VALUES
    (1, 3, 3, 'equip');    -- Iron Sword: +3 strength

-- Wooden Staff already has magical_attack=8 (item_id=15, attribute_id=13).
-- Add intelligence bonus.
INSERT INTO public.item_attributes_mapping (item_id, attribute_id, value, apply_on) VALUES
    (15, 4, 3, 'equip');   -- Wooden Staff: +3 intelligence

-- Wooden Shield (id=2): already has physical_defense=5. Add block_value.
INSERT INTO public.item_attributes_mapping (item_id, attribute_id, value, apply_on) VALUES
    (2, 17, 5, 'equip');   -- Wooden Shield: +5 block_value

-- ─────────────────────────────────────────────────────────────────────────────
-- 7. Add stat caps and new config entries to game_config
-- ─────────────────────────────────────────────────────────────────────────────

INSERT INTO public.game_config (key, value, value_type, description) VALUES
    ('combat.crit_chance_cap',        '75',   'float', 'Maximum crit_chance (%). Prevents 100% crit builds.'),
    ('combat.block_chance_cap',       '75',   'float', 'Maximum block_chance (%). Prevents unkillable tank builds.'),
    ('combat.evasion_cap',            '75',   'float', 'Maximum evasion effectiveness (%). Prevents un-hittable builds.'),
    ('combat.elemental_resistance_cap','75',   'float', 'Maximum elemental resistance per school (%). Same as max_resistance_cap default.'),
    ('combat.attack_speed_base_divisor','100', 'float', 'attack_speed divisor. effectiveSwingMs = baseSwingMs / (1 + attack_speed/divisor).'),
    ('combat.cast_speed_base_divisor','100',   'float', 'cast_speed divisor. effectiveCastMs = baseCastMs / (1 + cast_speed/divisor).')
ON CONFLICT (key) DO NOTHING;

-- ─────────────────────────────────────────────────────────────────────────────
-- 8. Fix title_definitions: fix "magic_attack" → "magical_attack" in archmage bonus
-- ─────────────────────────────────────────────────────────────────────────────

UPDATE public.title_definitions
SET bonuses = '[{"value": 5.0, "attributeSlug": "magical_attack"}]'::jsonb
WHERE slug = 'archmage';

-- ─────────────────────────────────────────────────────────────────────────────
-- 9. Clean up stale player_active_effect rows (all expired old death debuffs)
-- ─────────────────────────────────────────────────────────────────────────────

DELETE FROM public.player_active_effect
WHERE source_type = 'death'
  AND expires_at < NOW();

-- ─────────────────────────────────────────────────────────────────────────────
-- 11. Expand skill_school with elemental schools
--     Physical attacks → school "physical" → no elemental resistance (returns 0)
--     Elemental attacks → school "fire"/"ice"/etc. → fire_resistance/ice_resistance
--     Generic magical → school "magical" → no elemental resistance (same behavior)
-- ─────────────────────────────────────────────────────────────────────────────

INSERT INTO public.skill_school (id, name, slug) OVERRIDING SYSTEM VALUE VALUES
    (3, 'Fire',    'fire'),
    (4, 'Ice',     'ice'),
    (5, 'Nature',  'nature'),
    (6, 'Arcane',  'arcane'),
    (7, 'Holy',    'holy'),
    (8, 'Shadow',  'shadow')
ON CONFLICT DO NOTHING;

-- Update existing skills to use specific elemental schools:
-- Fireball (id=3): magical → fire
UPDATE public.skills SET school_id = 3 WHERE id = 3;
-- Frost Bolt (id=8): magical → ice
UPDATE public.skills SET school_id = 4 WHERE id = 8;
-- Arcane Blast (id=9): magical → arcane
UPDATE public.skills SET school_id = 6 WHERE id = 9;
-- Chain Lightning (id=10): magical → arcane (lightning = arcane school)
UPDATE public.skills SET school_id = 6 WHERE id = 10;
-- Mana Shield (id=11): stays magical (passive, non-damage)
-- Elemental Mastery (id=12): stays magical (passive)

-- ─────────────────────────────────────────────────────────────────────────────
-- 12. Fix damage_elements: add 'ice', keep 'water' for now
-- ─────────────────────────────────────────────────────────────────────────────

INSERT INTO public.damage_elements (slug) VALUES ('ice') ON CONFLICT DO NOTHING;

-- ─────────────────────────────────────────────────────────────────────────────
-- 13. Update game_config: default_crit_multiplier from raw (2.0) to pct (200)
--     Code will now do: critMultiplier = attribute_value / 100.0
--     200 → x2.0, 250 → x2.5, etc.
-- ─────────────────────────────────────────────────────────────────────────────

UPDATE public.game_config
SET value = '200', description = 'Default crit multiplier as percentage (200 = x2.0). Used when attacker has no crit_multiplier attribute.'
WHERE key = 'combat.default_crit_multiplier';

-- ─────────────────────────────────────────────────────────────────────────────
-- 10. Add is_percentage column to entity_attributes for UI documentation
-- ─────────────────────────────────────────────────────────────────────────────

-- Add column if not exists
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_schema = 'public'
          AND table_name = 'entity_attributes'
          AND column_name = 'is_percentage'
    ) THEN
        ALTER TABLE public.entity_attributes ADD COLUMN is_percentage boolean NOT NULL DEFAULT false;
    END IF;
END;
$$;

-- Mark percentage-based attributes
UPDATE public.entity_attributes SET is_percentage = true WHERE slug IN (
    'crit_chance', 'crit_multiplier', 'block_chance',
    'fire_resistance', 'ice_resistance', 'nature_resistance',
    'arcane_resistance', 'holy_resistance', 'shadow_resistance'
);

COMMIT;
