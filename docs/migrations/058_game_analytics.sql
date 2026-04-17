-- Migration 058: Game Analytics
-- ============================================================
-- Append-only event log for playtest analytics.
-- Written exclusively by Game Server when it receives an
-- "analyticsEvent" packet from Chunk Server.
-- Read via the GM panel with plain SQL — never updated in-place.
-- See docs/analytics-system-plan.md for event_type contracts.
-- ============================================================

CREATE TABLE IF NOT EXISTS public.game_analytics (
    id           BIGSERIAL    PRIMARY KEY,
    event_type   VARCHAR(64)  NOT NULL,
    character_id BIGINT       REFERENCES public.characters(id) ON DELETE SET NULL,
    session_id   VARCHAR(128) NOT NULL DEFAULT '',
    level        SMALLINT     NOT NULL DEFAULT 0,
    zone_id      INT          NOT NULL DEFAULT 0,
    payload      JSONB        NOT NULL DEFAULT '{}'::jsonb,
    created_at   TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

COMMENT ON TABLE  public.game_analytics              IS 'Append-only event log for playtest analytics. Written by Game Server; never updated or deleted manually.';
COMMENT ON COLUMN public.game_analytics.id           IS 'Auto-increment primary key.';
COMMENT ON COLUMN public.game_analytics.event_type   IS 'Type of event: session_start, session_end, level_up, player_death, quest_accept, quest_complete, quest_abandon, mob_killed, item_acquired, gold_change, skill_used, etc.';
COMMENT ON COLUMN public.game_analytics.character_id IS 'FK → characters.id. SET NULL on character deletion so historic rows are preserved.';
COMMENT ON COLUMN public.game_analytics.session_id   IS 'Server-generated session token "sess_{characterId}_{unix_ms}". Generated once on joinGameCharacter and reused for every event in that play session.';
COMMENT ON COLUMN public.game_analytics.level        IS 'Character level at the moment the event occurred. Direct column (not in payload) for fast GROUP BY / filter queries.';
COMMENT ON COLUMN public.game_analytics.zone_id      IS 'Zone/chunk where the event occurred. 0 = unknown/global.';
COMMENT ON COLUMN public.game_analytics.payload      IS 'Event-specific JSONB. Schema varies by event_type — see analytics-system-plan.md.';
COMMENT ON COLUMN public.game_analytics.created_at   IS 'Server-side UTC timestamp when the row was inserted.';

-- ── Indexes ──────────────────────────────────────────────────────────────────
-- Covering index for per-event_type queries ordered by time (most common GM queries)
CREATE INDEX idx_ga_event_type ON public.game_analytics (event_type, created_at DESC);

-- Per-character history
CREATE INDEX idx_ga_character  ON public.game_analytics (character_id, created_at DESC);

-- Per-session grouping (session_end duration calc, funnel analysis)
CREATE INDEX idx_ga_session    ON public.game_analytics (session_id);

-- Global time range scans
CREATE INDEX idx_ga_created_at ON public.game_analytics (created_at DESC);

-- Full payload search (for ad-hoc queries on nested fields)
CREATE INDEX idx_ga_payload    ON public.game_analytics USING GIN (payload);
