-- Migration 048: Trainer NPC → Class Mapping
-- Adds: npc_trainer_class table linking trainer NPCs to the class whose skills they teach.
-- The game-server reads this table at startup and sends setTrainerData to chunk-servers.
-- Must be applied AFTER migration 047.

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — TABLE DEFINITION
-- ═══════════════════════════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS public.npc_trainer_class (
    id       SERIAL PRIMARY KEY,
    npc_id   INTEGER NOT NULL REFERENCES public.npc(id)            ON DELETE CASCADE,
    class_id INTEGER NOT NULL REFERENCES public.character_class(id) ON DELETE CASCADE,
    UNIQUE (npc_id, class_id)
);

COMMENT ON TABLE public.npc_trainer_class IS
    'Maps trainer NPC ids to the class whose skills they can teach. '
    'Used by game-server to build setTrainerData payload sent to chunk-servers at startup.';

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — SEED DATA
-- Theron (npc_id=4) → Warrior (class_id=2)
-- Sylara (npc_id=5) → Mage    (class_id=1)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_trainer_class (npc_id, class_id)
VALUES
    (4, 2),  -- Theron → Warrior
    (5, 1)   -- Sylara → Mage
ON CONFLICT DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — GAME SERVER STARTUP QUERY (reference, not executed here)
-- The game-server should run a query similar to the following to build the
-- setTrainerData payload sent to chunk-servers:
--
-- SELECT
--     ntc.npc_id,
--     s.id          AS skill_id,
--     s.slug        AS skill_slug,
--     s.name        AS skill_name,
--     s.description,
--     s.is_passive,
--     cst.required_level,
--     cst.skill_point_cost  AS sp_cost,
--     cst.gold_cost,
--     cst.requires_book,
--     COALESCE(cst.skill_book_item_id, 0) AS book_item_id,
--     COALESCE(prereq.slug, '')           AS prerequisite_skill_slug
-- FROM public.npc_trainer_class ntc
-- JOIN public.class_skill_tree cst ON cst.class_id = ntc.class_id
-- JOIN public.skills s             ON s.id = cst.skill_id
-- LEFT JOIN public.skills prereq   ON prereq.id = cst.prerequisite_skill_id
-- ORDER BY ntc.npc_id, cst.required_level, s.id;
--
-- Result is grouped by npc_id and serialised as:
-- {
--   "eventType": "setTrainerData",
--   "body": {
--     "trainers": [
--       { "npcId": 4, "skills": [{...}, ...] },
--       { "npcId": 5, "skills": [{...}, ...] }
--     ]
--   }
-- }
-- ═══════════════════════════════════════════════════════════════════════════
