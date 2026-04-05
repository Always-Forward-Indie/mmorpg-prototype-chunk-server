-- Migration 045: New Mage Skills
-- Adds: Frost Bolt, Arcane Blast, Chain Lightning (active) + Mana Shield, Elemental Mastery (passive)
-- Updates: existing mage class_skill_tree row for Fireball with costs.
-- Assumes migrations 043 and 044 have been applied.

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — SKILL DEFINITIONS
-- scale_stat_id=2 (magical_attack), school_id=2 (magical)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skills (id, name, slug, scale_stat_id, school_id, animation_name, is_passive)
OVERRIDING SYSTEM VALUE
VALUES
    (8,  'Frost Bolt',          'frost_bolt',          2, 2, NULL, FALSE),
    (9,  'Arcane Blast',        'arcane_blast',        2, 2, NULL, FALSE),
    (10, 'Chain Lightning',     'chain_lightning',     2, 2, NULL, FALSE),
    (11, 'Mana Shield',         'mana_shield',         2, 2, NULL, TRUE),
    (12, 'Elemental Mastery',   'elemental_mastery',   2, 2, NULL, TRUE)
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — SKILL EFFECT INSTANCES
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skill_effect_instances (id, skill_id, order_idx, target_type_id)
VALUES
    (8,  8,  1, 1),  -- frost_bolt
    (9,  9,  1, 1),  -- arcane_blast
    (10, 10, 1, 1),  -- chain_lightning
    (11, 11, 1, 1),  -- mana_shield (passive)
    (12, 12, 1, 1)   -- elemental_mastery (passive)
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — SKILL EFFECTS MAPPING (level 1)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skill_effects_mapping
    (id, effect_instance_id, effect_id, value, level, tick_ms, duration_ms, attribute_id)
VALUES
    -- Frost Bolt: 1.6× mag_atk + 20 flat (single target, moderate damage)
    (13, 8,  1, 1.6, 1, 0, 0, NULL),  -- coeff
    (14, 8,  2,  20, 1, 0, 0, NULL),  -- flat_add

    -- Arcane Blast: 2.0× mag_atk + 50 flat (single target, hard-hitting)
    (15, 9,  1, 2.0, 1, 0, 0, NULL),  -- coeff
    (16, 9,  2,  50, 1, 0, 0, NULL),  -- flat_add

    -- Chain Lightning: 1.5× mag_atk + 30 flat (AOE hit, lower per-target)
    (17, 10, 1, 1.5, 1, 0, 0, NULL),  -- coeff
    (18, 10, 2,  30, 1, 0, 0, NULL),  -- flat_add

    -- Mana Shield: passive
    (19, 11, 3, 0, 1, 0, 0, NULL),    -- passive_marker

    -- Elemental Mastery: passive
    (20, 12, 3, 0, 1, 0, 0, NULL)     -- passive_marker

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — SKILL PROPERTIES MAPPING
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skill_properties_mapping
    (id, skill_id, skill_level, property_id, property_value)
OVERRIDING SYSTEM VALUE
VALUES
    -- Frost Bolt (skill_id=8, lvl 1): 2s cast, 7s cooldown, 35mp, range=15
    (28, 8,  1, 2,  7000),   -- cooldown_ms = 7000
    (29, 8,  1, 3,  1000),   -- gcd_ms = 1000
    (30, 8,  1, 4,  2000),   -- cast_ms = 2000
    (31, 8,  1, 5,    35),   -- cost_mp = 35
    (32, 8,  1, 6,    15),   -- max_range = 15

    -- Arcane Blast (skill_id=9, lvl 1): 3s cast, 10s cooldown, 55mp, range=12
    (33, 9,  1, 2, 10000),   -- cooldown_ms = 10000
    (34, 9,  1, 3,  1000),   -- gcd_ms = 1000
    (35, 9,  1, 4,  3000),   -- cast_ms = 3000
    (36, 9,  1, 5,    55),   -- cost_mp = 55
    (37, 9,  1, 6,    12),   -- max_range = 12

    -- Chain Lightning (skill_id=10, lvl 1): 2.5s cast, 15s cooldown, 70mp, range=15, AOE=8
    (38, 10, 1, 2, 15000),   -- cooldown_ms = 15000
    (39, 10, 1, 3,  1000),   -- gcd_ms = 1000
    (40, 10, 1, 4,  2500),   -- cast_ms = 2500
    (41, 10, 1, 5,    70),   -- cost_mp = 70
    (42, 10, 1, 6,    15),   -- max_range = 15
    (43, 10, 1, 7,   8.0)    -- area_radius = 8.0

    -- Passives have no properties

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — PASSIVE SKILL MODIFIERS
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.passive_skill_modifiers (id, skill_id, attribute_slug, modifier_type, value)
VALUES
    -- Mana Shield: +200 flat max_mana
    (3, 11, 'max_mana', 'flat', 200),

    -- Elemental Mastery: +12% magical_attack
    (4, 12, 'magical_attack', 'percent', 12)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — CLASS SKILL TREE (Mage = class_id=1)
-- ═══════════════════════════════════════════════════════════════════════════

-- Update existing Fireball entry (id=2) with costs
UPDATE public.class_skill_tree
SET skill_point_cost = 1,
    gold_cost        = 120,
    max_level        = 1
WHERE class_id = 1 AND skill_id = 3;  -- fireball

-- Frost Bolt: requires lvl 5, needs 1 SP + 100g
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (1, 8, 5, FALSE, 1, 100, 1, FALSE, NULL, NULL)
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Arcane Blast: requires lvl 8, needs 1 SP + 200g
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (1, 9, 8, FALSE, 1, 200, 1, FALSE, NULL, 8)  -- prereq: frost_bolt
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Chain Lightning: requires lvl 12, needs 1 SP + 400g + Tome of Chain Lightning (item set in migration 046)
--                 prerequisite: arcane_blast
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (1, 10, 12, FALSE, 1, 400, 1, TRUE, NULL, 9)  -- prereq: arcane_blast, book set in 046
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Mana Shield (passive): requires lvl 5, needs 1 SP + 150g
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (1, 11, 5, FALSE, 1, 150, 1, FALSE, NULL, NULL)
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Elemental Mastery (passive): requires lvl 10, needs 1 SP + 300g
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (1, 12, 10, FALSE, 1, 300, 1, FALSE, NULL, 11)  -- prereq: mana_shield
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Advance identity sequences past the last explicitly inserted IDs
SELECT setval(pg_get_serial_sequence('public.skills', 'id'), 12, true);
SELECT setval(pg_get_serial_sequence('public.skill_properties_mapping', 'id'), 43, true);
