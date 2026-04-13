-- Migration 051: Character Skill Bar
-- Adds per-character hotbar slot assignments.
-- Each row maps one slot index to one skill slug.
-- Absent rows = empty slot (no NULL slugs stored).

CREATE TABLE IF NOT EXISTS public.character_skill_bar (
    character_id  INTEGER      NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot_index    SMALLINT     NOT NULL CHECK (slot_index >= 0 AND slot_index < 12),
    skill_slug    VARCHAR(64)  NOT NULL,
    PRIMARY KEY (character_id, slot_index)
);

CREATE INDEX IF NOT EXISTS idx_skill_bar_character ON public.character_skill_bar(character_id);

COMMENT ON TABLE public.character_skill_bar IS
    'Stores which skill slug is assigned to each hotbar slot per character. Absent rows = empty slot.';
COMMENT ON COLUMN public.character_skill_bar.slot_index IS
    'Zero-based hotbar slot index (0–11). Max 12 slots enforced by CHECK constraint.';
COMMENT ON COLUMN public.character_skill_bar.skill_slug IS
    'Slug of the skill assigned to this slot. Must be a known skill slug.';
