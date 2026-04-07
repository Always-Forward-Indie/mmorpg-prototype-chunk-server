-- Migration 049: Title System
-- Creates title_definitions (global catalog) and character_titles (per-character state)

-- ── title_definitions ──────────────────────────────────────────────────────
-- Global list of all possible titles. Edited by admins/content designers.
-- bonuses are stored as JSON: [{"attributeSlug": "physical_attack", "value": 2.0}, ...]
CREATE TABLE IF NOT EXISTS public.title_definitions (
    id                  SERIAL PRIMARY KEY,
    slug                VARCHAR(80)  NOT NULL UNIQUE,
    display_name        VARCHAR(120) NOT NULL,
    description         TEXT         NOT NULL DEFAULT '',
    earn_condition      VARCHAR(80)  NOT NULL DEFAULT '',  -- hint for game logic (e.g. "kill_wolves_100")
    bonuses             JSONB        NOT NULL DEFAULT '[]'
);

-- ── character_titles ───────────────────────────────────────────────────────
-- Per-character: which titles have been earned, and which is currently equipped.
CREATE TABLE IF NOT EXISTS public.character_titles (
    character_id        INTEGER      NOT NULL REFERENCES public.characters(id) ON DELETE CASCADE,
    title_slug          VARCHAR(80)  NOT NULL REFERENCES public.title_definitions(slug) ON DELETE CASCADE,
    equipped            BOOLEAN      NOT NULL DEFAULT FALSE,
    earned_at           TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, title_slug)
);

-- Index for fast per-character lookup
CREATE INDEX IF NOT EXISTS idx_char_titles_char ON public.character_titles USING btree (character_id);

-- ── Seed data ──────────────────────────────────────────────────────────────
-- Example titles. Add more as gameplay content is created.
INSERT INTO public.title_definitions (slug, display_name, description, earn_condition, bonuses)
VALUES
    ('wolf_slayer',    'Wolf Slayer',    'Slain 100 wolves',      'kill_wolves_100',      '[{"attributeSlug":"physical_attack","value":2.0},{"attributeSlug":"move_speed","value":1.0}]'),
    ('first_blood',    'First Blood',    'First PvP kill',        'pvp_kill_first',       '[{"attributeSlug":"crit_chance","value":0.5}]'),
    ('dungeon_delver', 'Dungeon Delver', 'Completed a dungeon',   'dungeon_complete_first','[{"attributeSlug":"physical_defense","value":3.0}]'),
    ('merchant',       'Merchant',       'Bought 50 items',       'buy_items_50',         '[{"attributeSlug":"strength","value":1.0}]')
ON CONFLICT (slug) DO NOTHING;
