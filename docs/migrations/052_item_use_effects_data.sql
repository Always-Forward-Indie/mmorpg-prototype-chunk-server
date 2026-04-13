-- Migration 052: Seed item_use_effects for consumable items
-- Adds use-effect rows for Health Potion and Bread, plus the required
-- status_effects catalog entries for timed (non-instant) effects.
--
-- After this migration UseItem is fully operational:
--   Health Potion (id=3) → instant +50 HP, 30-second cooldown
--   Bread         (id=4) → HoT +5 HP every 2 s for 30 s, 60-second cooldown
-- ---------------------------------------------------------------------------

-- 1. Status-effect catalog entries (required by insert_player_active_effect
--    which does a sub-SELECT on status_effects.slug).
--    Only timed effects need a row here; instant effects are never persisted.
INSERT INTO public.status_effects (id, slug, category, duration_sec) VALUES
    (2, 'bread_hot', 'hot', 30);

-- 2. Item use-effects
--    Health Potion: one-shot instant HP restore
INSERT INTO public.item_use_effects
    (id, item_id, effect_slug, attribute_slug, value, is_instant, duration_seconds, tick_ms, cooldown_seconds)
VALUES
    (1, 3, 'hp_restore_50', 'hp', 50, true,  0,  0, 30);

--    Bread: heal-over-time (+5 HP per tick, 2 s interval, 30 s total)
INSERT INTO public.item_use_effects
    (id, item_id, effect_slug, attribute_slug, value, is_instant, duration_seconds, tick_ms, cooldown_seconds)
VALUES
    (2, 4, 'bread_hot',    'hp',  5, false, 30, 2000, 60);

-- 3. Advance sequences so subsequent INSERTs pick correct next values
SELECT setval('public.status_effects_id_seq',   2, true);
SELECT setval('public.item_use_effects_id_seq', 2, true);
