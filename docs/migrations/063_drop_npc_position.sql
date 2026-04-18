-- Migration 063: Drop npc_position table
-- npc_position is superseded by npc_placements (added in migration 047).
-- All data has been migrated. npc_placements is the single source of truth for NPC world placement.

DROP TABLE IF EXISTS public.npc_position;
