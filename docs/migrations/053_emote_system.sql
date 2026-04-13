-- Migration 053: Emote (Emotion/Animation) System
-- Introduces a player-facing emote catalog and per-character unlock tracking.
--
-- Tables created:
--   emote_definitions  — static catalog of all available emotes
--   character_emotes   — per-character unlocked emotes (default emotes seeded for all)
--
-- Emote unlock workflow:
--   • DEFAULT emotes (is_default = TRUE) are auto-granted to every character.
--   • Non-default emotes can be awarded via quests, vendors, drops, GM tools, etc.
--   • The chunk-server receives all definitions on startup (getEmoteDefinitionsData).
--   • Per-character unlocks are fetched on joinGameCharacter (getPlayerEmotesData).
--   • The client sends `useEmote` to play an animation; chunk validates unlock.
-- ---------------------------------------------------------------------------

-- 1. Emote definitions catalog
CREATE TABLE public.emote_definitions (
    id             SERIAL       PRIMARY KEY,
    slug           VARCHAR(64)  UNIQUE NOT NULL,
    display_name   VARCHAR(128) NOT NULL,
    animation_name VARCHAR(128) NOT NULL,
    category       VARCHAR(64)  NOT NULL DEFAULT 'general',
    is_default     BOOLEAN      NOT NULL DEFAULT FALSE,
    sort_order     INT          NOT NULL DEFAULT 0,
    created_at     TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE  public.emote_definitions                 IS 'Static catalog of player emote / animation definitions';
COMMENT ON COLUMN public.emote_definitions.slug            IS 'Unique snake_case key used in packets, e.g. dance_silly';
COMMENT ON COLUMN public.emote_definitions.animation_name  IS 'Name of the client-side animation clip to play';
COMMENT ON COLUMN public.emote_definitions.category        IS 'UI grouping: basic | social | dance | sit | ...';
COMMENT ON COLUMN public.emote_definitions.is_default      IS 'TRUE = all characters own this emote automatically';

-- 2. Per-character unlock table
CREATE TABLE public.character_emotes (
    id           SERIAL      PRIMARY KEY,
    character_id INT         NOT NULL REFERENCES public.characters(id) ON DELETE CASCADE,
    emote_slug   VARCHAR(64) NOT NULL REFERENCES public.emote_definitions(slug) ON DELETE CASCADE,
    unlocked_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT uq_character_emote UNIQUE (character_id, emote_slug)
);

COMMENT ON TABLE public.character_emotes IS 'Per-character unlocked emotes; default emotes are seeded here too';

CREATE INDEX idx_character_emotes_character_id ON public.character_emotes(character_id);

-- 3. Seed default emote definitions
INSERT INTO public.emote_definitions (slug, display_name, animation_name, category, is_default, sort_order)
VALUES
    -- Basic / default
    ('sit',          'Сесть',              'emote_sit',          'basic',  TRUE,   1),
    ('wave',         'Помахать рукой',     'emote_wave',         'basic',  TRUE,   2),
    ('bow',          'Поклониться',        'emote_bow',          'basic',  TRUE,   3),
    ('laugh',        'Смеяться',           'emote_laugh',        'social', TRUE,   4),
    ('cry',          'Плакать',            'emote_cry',          'social', TRUE,   5),
    ('point',        'Указать',            'emote_point',        'basic',  TRUE,   6),
    -- Social (unlockable)
    ('salute',       'Козырять',           'emote_salute',       'social', FALSE,  10),
    ('clap',         'Аплодировать',       'emote_clap',         'social', FALSE,  11),
    ('shrug',        'Пожать плечами',     'emote_shrug',        'social', FALSE,  12),
    ('taunt',        'Дразниться',         'emote_taunt',        'social', FALSE,  13),
    -- Dance (unlockable)
    ('dance_basic',  'Танцевать',          'emote_dance_basic',  'dance',  FALSE,  20),
    ('dance_wild',   'Дикий танец',        'emote_dance_wild',   'dance',  FALSE,  21),
    ('dance_slow',   'Медленный танец',    'emote_dance_slow',   'dance',  FALSE,  22);

-- 4. Auto-grant every default emote to all existing characters
INSERT INTO public.character_emotes (character_id, emote_slug)
SELECT c.id, ed.slug
FROM   public.characters     c
CROSS  JOIN public.emote_definitions ed
WHERE  ed.is_default = TRUE
ON CONFLICT DO NOTHING;

-- 5. Advance sequences to safe values
SELECT setval('public.emote_definitions_id_seq', (SELECT MAX(id) FROM public.emote_definitions), TRUE);
