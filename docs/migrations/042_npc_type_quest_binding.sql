-- Migration 042: Expand npc_type entries and assign correct types to existing NPCs
-- The npc_type table previously contained only 'general'.
-- This migration adds vendor, quest_giver, blacksmith, guard, and trainer types,
-- then assigns the correct type to each NPC based on game data.
-- Quest-to-NPC binding already exists via quest.giver_npc_id / quest.turnin_npc_id;
-- no additional join table is needed.

-- ─── 1. Add new NPC type entries ─────────────────────────────────────────────
INSERT INTO public.npc_type (name, slug) VALUES
    ('Vendor',      'vendor'),
    ('Quest Giver', 'quest_giver'),
    ('Blacksmith',  'blacksmith'),
    ('Guard',       'guard'),
    ('Trainer',     'trainer');

-- ─── 2. Assign types to existing NPCs ────────────────────────────────────────

-- Milaya (id=2): quest giver and turn-in target for wolf_hunt_intro
UPDATE public.npc
SET npc_type = (SELECT id FROM public.npc_type WHERE slug = 'quest_giver')
WHERE id = 2;

-- Varan (id=1) and Edrik (id=3) remain 'general' (already set to type id=1)
