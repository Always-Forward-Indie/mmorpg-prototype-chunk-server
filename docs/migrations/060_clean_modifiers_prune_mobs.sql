-- =============================================================================
-- Migration 060: Clean permanent modifiers, prune mobs, rebalance mob_stat
-- =============================================================================
-- 1. Truncate character_permanent_modifiers — players get stats from class formula
-- 2. Remove mobs 6 (WolfPackLeader), 7 (OldWolf), 8 (ForestGoblin) + all their FK data
-- 3. Rebalance mob_stat for SmallFox (id=1) and GreyWolf (id=2)
-- =============================================================================

BEGIN;

-- ─────────────────────────────────────────────────────────────────────────────
-- 1. Clear all character_permanent_modifiers
--    Players now rely purely on class_stat_formula for base stats.
-- ─────────────────────────────────────────────────────────────────────────────

TRUNCATE public.character_permanent_modifiers;

-- ─────────────────────────────────────────────────────────────────────────────
-- 2. Delete mobs 6, 7, 8 and all FK references
-- ─────────────────────────────────────────────────────────────────────────────

DELETE FROM public.mob_stat          WHERE mob_id IN (6, 7, 8);
DELETE FROM public.mob_skills        WHERE mob_id IN (6, 7, 8);
DELETE FROM public.mob_loot_info     WHERE mob_id IN (6, 7, 8);
DELETE FROM public.mob_position      WHERE mob_id IN (6, 7, 8);
DELETE FROM public.mob_resistances   WHERE mob_id IN (6, 7, 8);
DELETE FROM public.mob_weaknesses    WHERE mob_id IN (6, 7, 8);
DELETE FROM public.spawn_zone_mobs   WHERE mob_id IN (6, 7, 8);
DELETE FROM public.mob_active_effect WHERE mob_uid IN (
    SELECT id FROM public.mob WHERE id IN (6, 7, 8)
);
-- character_bestiary has mob_template_id FK but was empty for 6,7,8
DELETE FROM public.character_bestiary          WHERE mob_template_id IN (6, 7, 8);
DELETE FROM public.timed_champion_templates    WHERE mob_template_id IN (6, 7, 8);
DELETE FROM public.zone_event_templates        WHERE invasion_mob_template_id IN (6, 7, 8);

DELETE FROM public.mob WHERE id IN (6, 7, 8);

-- ─────────────────────────────────────────────────────────────────────────────
-- 3. Rebalance mob_stat for SmallFox (id=1) and GreyWolf (id=2)
--
-- Design goals:
--   Fox  (level 1): weakest mob. ~40 HP. Deals ~4-6 dmg/hit.
--     A level 1 Mage (88 HP, 7 mag_atk → Fireball ~35 raw) kills in 3-4 hits.
--     A level 1 Warrior (168 HP, 14 phys_atk → Basic Attack ~15 raw) kills in 5-6 hits.
--     Fox hits Mage for ~5 dmg (after 5 phys_def), Warrior for ~2-3 (after 19 phys_def).
--
--   Wolf (level 1): tougher. ~80 HP. Deals ~8-10 dmg/hit.
--     Mage: kills in 4-5 hits. Takes ~6-7 dmg per wolf hit.
--     Warrior: kills in 7-8 hits. Takes ~3-4 dmg per wolf hit.
--     More rewarding (20 XP vs 15 XP fox).
-- ─────────────────────────────────────────────────────────────────────────────

-- Wipe existing mob_stat for both mobs
DELETE FROM public.mob_stat WHERE mob_id IN (1, 2);

-- ── SmallFox (id=1, level 1) ─────────────────────────────────────────────────
-- Passive animal, low stats, easy kill target.
INSERT INTO public.mob_stat (mob_id, attribute_id, flat_value, multiplier, exponent) VALUES
    -- Core combat
    (1, 1,  40.00,  NULL, NULL),   -- max_health         (40 HP)
    (1, 2,  0.00,   NULL, NULL),   -- max_mana           (animal, no mana)
    (1, 12, 6.00,   NULL, NULL),   -- physical_attack     (light bite)
    (1, 13, 0.00,   NULL, NULL),   -- magical_attack
    (1, 6,  2.00,   NULL, NULL),   -- physical_defense    (fur)
    (1, 7,  0.00,   NULL, NULL),   -- magical_defense
    -- Hit/crit
    (1, 8,  3.00,   NULL, NULL),   -- crit_chance         (3%)
    (1, 9,  200.00, NULL, NULL),   -- crit_multiplier     (200 = x2.0)
    (1, 14, 4.00,   NULL, NULL),   -- accuracy
    (1, 15, 8.00,   NULL, NULL),   -- evasion             (fox is nimble)
    (1, 16, 0.00,   NULL, NULL),   -- block_chance
    (1, 17, 0.00,   NULL, NULL),   -- block_value
    -- Utility
    (1, 10, 0.50,   NULL, NULL),   -- hp_regen_per_s
    (1, 11, 0.00,   NULL, NULL),   -- mp_regen_per_s
    (1, 18, 6.50,   NULL, NULL),   -- move_speed          (fast small animal)
    (1, 19, 5.00,   NULL, NULL),   -- attack_speed
    (1, 20, 0.00,   NULL, NULL),   -- cast_speed
    -- Primary (for mob flavor, not used in combat calc for mobs)
    (1, 3,  3.00,   NULL, NULL),   -- strength
    (1, 4,  1.00,   NULL, NULL),   -- intelligence
    (1, 27, 3.00,   NULL, NULL),   -- constitution
    (1, 28, 1.00,   NULL, NULL),   -- wisdom
    (1, 29, 8.00,   NULL, NULL),   -- agility             (nimble fox)
    -- Elemental resistances (0 across the board — basic mob)
    (1, 30, 0.00,   NULL, NULL),   -- fire_resistance
    (1, 31, 0.00,   NULL, NULL),   -- ice_resistance
    (1, 32, 0.00,   NULL, NULL),   -- nature_resistance
    (1, 33, 0.00,   NULL, NULL),   -- arcane_resistance
    (1, 34, 0.00,   NULL, NULL),   -- holy_resistance
    (1, 35, 0.00,   NULL, NULL);   -- shadow_resistance

-- ── GreyWolf (id=2, level 1) ────────────────────────────────────────────────
-- Aggressive predator, tougher than fox, higher damage, lower evasion.
INSERT INTO public.mob_stat (mob_id, attribute_id, flat_value, multiplier, exponent) VALUES
    -- Core combat
    (2, 1,  80.00,  NULL, NULL),   -- max_health         (80 HP)
    (2, 2,  0.00,   NULL, NULL),   -- max_mana
    (2, 12, 10.00,  NULL, NULL),   -- physical_attack     (strong bite)
    (2, 13, 0.00,   NULL, NULL),   -- magical_attack
    (2, 6,  5.00,   NULL, NULL),   -- physical_defense    (thick hide)
    (2, 7,  2.00,   NULL, NULL),   -- magical_defense     (slight)
    -- Hit/crit
    (2, 8,  5.00,   NULL, NULL),   -- crit_chance         (5%)
    (2, 9,  200.00, NULL, NULL),   -- crit_multiplier     (200 = x2.0)
    (2, 14, 6.00,   NULL, NULL),   -- accuracy
    (2, 15, 4.00,   NULL, NULL),   -- evasion             (less nimble than fox)
    (2, 16, 0.00,   NULL, NULL),   -- block_chance
    (2, 17, 0.00,   NULL, NULL),   -- block_value
    -- Utility
    (2, 10, 1.00,   NULL, NULL),   -- hp_regen_per_s
    (2, 11, 0.00,   NULL, NULL),   -- mp_regen_per_s
    (2, 18, 5.00,   NULL, NULL),   -- move_speed          (normal predator)
    (2, 19, 5.00,   NULL, NULL),   -- attack_speed
    (2, 20, 0.00,   NULL, NULL),   -- cast_speed
    -- Primary
    (2, 3,  6.00,   NULL, NULL),   -- strength
    (2, 4,  2.00,   NULL, NULL),   -- intelligence
    (2, 27, 6.00,   NULL, NULL),   -- constitution
    (2, 28, 2.00,   NULL, NULL),   -- wisdom
    (2, 29, 5.00,   NULL, NULL),   -- agility
    -- Elemental resistances
    (2, 30, 0.00,   NULL, NULL),   -- fire_resistance
    (2, 31, 2.00,   NULL, NULL),   -- ice_resistance      (wolf has slight cold resist)
    (2, 32, 0.00,   NULL, NULL),   -- nature_resistance
    (2, 33, 0.00,   NULL, NULL),   -- arcane_resistance
    (2, 34, 0.00,   NULL, NULL),   -- holy_resistance
    (2, 35, 0.00,   NULL, NULL);   -- shadow_resistance

-- Update mob table spawn values to match new stats
UPDATE public.mob SET spawn_health = 40,  spawn_mana = 0 WHERE id = 1;
UPDATE public.mob SET spawn_health = 80,  spawn_mana = 0 WHERE id = 2;

COMMIT;
