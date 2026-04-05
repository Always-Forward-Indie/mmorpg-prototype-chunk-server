-- Migration 044: New Warrior Skills
-- Adds: Shield Bash, Whirlwind (active) + Iron Skin, Constitution Mastery (passive)
-- Updates: existing warrior class_skill_tree rows with costs and prereqs.
-- Assumes migration 043 has been applied.

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — SKILL DEFINITIONS
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skills (id, name, slug, scale_stat_id, school_id, animation_name, is_passive)
OVERRIDING SYSTEM VALUE
VALUES
    -- Active warrior skills
    (4, 'Shield Bash',           'shield_bash',           1, 1, NULL, FALSE),
    (5, 'Whirlwind',             'whirlwind',             1, 1, NULL, FALSE),
    -- Passive warrior skills
    (6, 'Iron Skin',             'iron_skin',             1, 1, NULL, TRUE),
    (7, 'Constitution Mastery',  'constitution_mastery',  1, 1, NULL, TRUE)
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — SKILL EFFECT INSTANCES
-- Every skill needs at least one instance so get_character_skills JOIN works.
-- passive skills use target_type_id=1 (self) with a passive_marker formula.
-- ═══════════════════════════════════════════════════════════════════════════

-- target_type_id: 1=single target (standard for all warrior skills)
INSERT INTO public.skill_effect_instances (id, skill_id, order_idx, target_type_id)
VALUES
    (4, 4, 1, 1),  -- shield_bash
    (5, 5, 1, 1),  -- whirlwind
    (6, 6, 1, 1),  -- iron_skin (passive)
    (7, 7, 1, 1)   -- constitution_mastery (passive)
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — SKILL EFFECTS MAPPING (level 1)
-- effect_id=1 → coeff   (damage formula)
-- effect_id=2 → flat_add (damage formula)
-- effect_id=3 → passive_marker (passive, value=0, coeff=0 returned to server)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skill_effects_mapping
    (id, effect_instance_id, effect_id, value, level, tick_ms, duration_ms, attribute_id)
VALUES
    -- Shield Bash: 1.4× phys_atk + 40 flat at lvl 1
    (7,  4, 1, 1.4, 1, 0, 0, NULL),   -- coeff
    (8,  4, 2,  40, 1, 0, 0, NULL),   -- flat_add

    -- Whirlwind: 1.2× phys_atk + 25 flat (AOE, lower per-target)
    (9,  5, 1, 1.2, 1, 0, 0, NULL),   -- coeff
    (10, 5, 2,  25, 1, 0, 0, NULL),   -- flat_add

    -- Iron Skin: passive, no damage
    (11, 6, 3, 0, 1, 0, 0, NULL),    -- passive_marker

    -- Constitution Mastery: passive, no damage
    (12, 7, 3, 0, 1, 0, 0, NULL)     -- passive_marker

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — SKILL PROPERTIES MAPPING
-- property_id: 2=cooldown_ms, 3=gcd_ms, 4=cast_ms, 5=cost_mp, 6=max_range,
--              7=area_radius, 8=swing_ms
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.skill_properties_mapping
    (id, skill_id, skill_level, property_id, property_value)
OVERRIDING SYSTEM VALUE
VALUES
    -- Shield Bash (skill_id=4, lvl 1): instant melee with 5s cooldown, 30mp
    (15, 4, 1, 2, 5000),   -- cooldown_ms = 5000
    (16, 4, 1, 3, 1000),   -- gcd_ms = 1000
    (17, 4, 1, 4,    0),   -- cast_ms = 0 (instant)
    (18, 4, 1, 5,   30),   -- cost_mp = 30
    (19, 4, 1, 6,  2.5),   -- max_range = 2.5
    (20, 4, 1, 8,  800),   -- swing_ms = 800

    -- Whirlwind (skill_id=5, lvl 1): AoE melee, 12s cooldown, 50mp, area_radius=4
    (21, 5, 1, 2, 12000),  -- cooldown_ms = 12000
    (22, 5, 1, 3,  1000),  -- gcd_ms = 1000
    (23, 5, 1, 4,     0),  -- cast_ms = 0 (instant)
    (24, 5, 1, 5,    50),  -- cost_mp = 50
    (25, 5, 1, 6,   3.0),  -- max_range = 3.0
    (26, 5, 1, 7,   4.0),  -- area_radius = 4.0
    (27, 5, 1, 8,  1000)   -- swing_ms = 1000

    -- Passives have no properties mapping (they apply stat modifiers at character load)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — PASSIVE SKILL MODIFIERS
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.passive_skill_modifiers (id, skill_id, attribute_slug, modifier_type, value)
VALUES
    -- Iron Skin: +15 flat physical_defense
    (1, 6, 'physical_defense', 'flat', 15),

    -- Constitution Mastery: +8% max_health
    (2, 7, 'max_health', 'percent', 8)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — CLASS SKILL TREE (Warrior = class_id=2)
-- Update existing rows with costs. Add new skills.
-- ═══════════════════════════════════════════════════════════════════════════

-- Update existing Power Slash entry (id=4) with costs
UPDATE public.class_skill_tree
SET skill_point_cost = 1,
    gold_cost        = 100,
    max_level        = 1
WHERE class_id = 2 AND skill_id = 2;  -- power_slash

-- Shield Bash: requires lvl 5, needs 1 SP + 150g, no prereq
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (2, 4, 5, FALSE, 1, 150, 1, FALSE, NULL, NULL)
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Whirlwind: requires lvl 10, needs 1 SP + 300g + Tome of Whirlwind (item set in migration 046)
--            prerequisite: shield_bash
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (2, 5, 10, FALSE, 1, 300, 1, TRUE, NULL, 4)
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Iron Skin (passive): requires lvl 5, needs 1 SP + 120g
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (2, 6, 5, FALSE, 1, 120, 1, FALSE, NULL, NULL)
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Constitution Mastery (passive): requires lvl 8, needs 1 SP + 200g
INSERT INTO public.class_skill_tree
    (class_id, skill_id, required_level, is_default, skill_point_cost, gold_cost, max_level, requires_book, skill_book_item_id, prerequisite_skill_id)
VALUES
    (2, 7, 8, FALSE, 1, 200, 1, FALSE, NULL, 6)
ON CONFLICT (class_id, skill_id) DO NOTHING;

-- Advance identity sequences past the last explicitly inserted IDs
SELECT setval(pg_get_serial_sequence('public.skills', 'id'), 7, true);
SELECT setval(pg_get_serial_sequence('public.skill_properties_mapping', 'id'), 27, true);
