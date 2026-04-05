-- Migration 043: Skill Learning System — schema foundation
-- Adds:
--   1. skill_damage_types id=2 "passive" for passive skill classification
--   2. skill_damage_formulas id=3 "passive_marker" for passive skill effect rows
--   3. item_types id=10 "Skill Book"
--   4. class_skill_tree new columns: prerequisite_skill_id, skill_point_cost, gold_cost,
--      max_level, requires_book, skill_book_item_id
-- Note: npc_type 'trainer' (id=6) was already added in migration 042.

-- ─── 1. Passive skill type ───────────────────────────────────────────────────
INSERT INTO public.skill_damage_types (id, slug) VALUES (2, 'passive')
    ON CONFLICT (id) DO NOTHING;

-- ─── 2. Passive marker formula ───────────────────────────────────────────────
-- Used only as a join anchor so passive skills appear in get_character_skills.
-- coeff=0 / flat_add=0 for passive skills; real modifiers live in passive_skill_modifiers.
INSERT INTO public.skill_damage_formulas (id, slug, effect_type_id) VALUES (3, 'passive_marker', 2)
    ON CONFLICT (id) DO NOTHING;

-- ─── 3. Skill Book item type ─────────────────────────────────────────────────
INSERT INTO public.item_types (id, name, slug)
OVERRIDING SYSTEM VALUE
VALUES (10, 'Skill Book', 'skill_book')
    ON CONFLICT (id) DO NOTHING;
SELECT setval(pg_get_serial_sequence('public.item_types', 'id'), 10, true);

-- ─── 4. Extend class_skill_tree ──────────────────────────────────────────────
ALTER TABLE public.class_skill_tree
    ADD COLUMN IF NOT EXISTS prerequisite_skill_id INTEGER
        REFERENCES public.skills(id) ON DELETE SET NULL,
    ADD COLUMN IF NOT EXISTS skill_point_cost      SMALLINT NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS gold_cost             INTEGER  NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS max_level             SMALLINT NOT NULL DEFAULT 1,
    ADD COLUMN IF NOT EXISTS requires_book         BOOLEAN  NOT NULL DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS skill_book_item_id    INTEGER
        REFERENCES public.items(id) ON DELETE SET NULL;

-- ─── 5. Update default skills (is_default rows need 0 cost — already 0 by default) ──
-- Default basic_attack rows (class_skill_tree id=1, id=3) require no SP or gold.
-- Non-default rows get costs in migration 044/045 when the skills themselves are added.

COMMENT ON COLUMN public.class_skill_tree.skill_point_cost IS
    'Skill points consumed when this skill is learned. 0 = free (default skills).';
COMMENT ON COLUMN public.class_skill_tree.gold_cost IS
    'Gold (gold_coin quantity) required to learn this skill. 0 = no gold cost.';
COMMENT ON COLUMN public.class_skill_tree.requires_book IS
    'If TRUE the player must have the skill_book_item_id item in inventory to learn.';
COMMENT ON COLUMN public.class_skill_tree.skill_book_item_id IS
    'The skill book item that is consumed when learning this skill (if requires_book=TRUE).';
COMMENT ON COLUMN public.class_skill_tree.prerequisite_skill_id IS
    'Skill that must already be learned before this one becomes available. NULL = no prereq.';
COMMENT ON COLUMN public.class_skill_tree.max_level IS
    'Maximum level to which this skill can be upgraded. 1 = cannot be upgraded.';
