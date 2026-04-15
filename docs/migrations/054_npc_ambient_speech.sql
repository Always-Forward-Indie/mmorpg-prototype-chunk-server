-- =============================================================================
-- Migration 054: NPC Ambient Speech System
-- =============================================================================
-- Adds two tables for the NPC ambient speech system:
--   npc_ambient_speech_configs  — per-NPC timing settings
--   npc_ambient_speech_lines    — individual speech lines with triggers and conditions

CREATE TABLE public.npc_ambient_speech_configs (
    id               SERIAL       PRIMARY KEY,
    npc_id           INT          NOT NULL REFERENCES public.npc(id) ON DELETE CASCADE,
    min_interval_sec INT          NOT NULL DEFAULT 20,
    max_interval_sec INT          NOT NULL DEFAULT 60,
    CONSTRAINT uq_npc_ambient_config UNIQUE (npc_id)
);

COMMENT ON TABLE  public.npc_ambient_speech_configs              IS 'Per-NPC ambient speech timing configuration.';
COMMENT ON COLUMN public.npc_ambient_speech_configs.npc_id       IS 'FK to npcs.id. One config per NPC.';
COMMENT ON COLUMN public.npc_ambient_speech_configs.min_interval_sec IS 'Minimum interval (seconds) between periodic lines on client.';
COMMENT ON COLUMN public.npc_ambient_speech_configs.max_interval_sec IS 'Maximum interval (seconds) between periodic lines on client.';

-- ---------------------------------------------------------------------------

CREATE TABLE public.npc_ambient_speech_lines (
    id              SERIAL       PRIMARY KEY,
    npc_id          INT          NOT NULL REFERENCES public.npc(id) ON DELETE CASCADE,
    line_key        VARCHAR(128) NOT NULL,
    trigger_type    VARCHAR(16)  NOT NULL DEFAULT 'periodic',
    trigger_radius  INT          NOT NULL DEFAULT 400,
    priority        INT          NOT NULL DEFAULT 0,
    weight          INT          NOT NULL DEFAULT 10,
    cooldown_sec    INT          NOT NULL DEFAULT 60,
    condition_group JSONB                 DEFAULT NULL
);

CREATE INDEX idx_ambient_lines_npc_id ON public.npc_ambient_speech_lines(npc_id);

COMMENT ON TABLE  public.npc_ambient_speech_lines                IS 'Individual ambient speech lines for NPCs.';
COMMENT ON COLUMN public.npc_ambient_speech_lines.line_key       IS 'Localisation key sent to client, e.g. npc.blacksmith.idle_1';
COMMENT ON COLUMN public.npc_ambient_speech_lines.trigger_type   IS '"periodic" = fired by client timer; "proximity" = fired once on player approach.';
COMMENT ON COLUMN public.npc_ambient_speech_lines.trigger_radius IS 'Trigger / display radius in world units (used for proximity trigger and UI culling).';
COMMENT ON COLUMN public.npc_ambient_speech_lines.priority       IS 'Highest-priority non-empty pool is used. Within a pool, lines are weighted-random.';
COMMENT ON COLUMN public.npc_ambient_speech_lines.weight         IS 'Relative weight for weighted-random selection within same priority group.';
COMMENT ON COLUMN public.npc_ambient_speech_lines.cooldown_sec   IS 'Per-client cooldown (seconds) before this specific line may show again.';
COMMENT ON COLUMN public.npc_ambient_speech_lines.condition_group IS 'Optional JSONB condition tree compatible with DialogueConditionEvaluator. NULL = always show.';
