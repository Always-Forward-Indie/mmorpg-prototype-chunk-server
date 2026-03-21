-- Migration 041: populate move_speed stat for all mobs
--
-- move_speed (entity_attributes id=18) already exists in the schema but mobs have no
-- rows in mob_stat for it.  The server reads this stat from mob.attributes and converts
-- with MOVE_SPEED_SCALE=40 (same formula used for player speed validation):
--
--   chase speed (u/s) = move_speed_stat * 40
--
-- Reference values (adjust for game balance):
--   melee  mob  move_speed=12  →  480 u/s  (slightly faster than a typical player at 200 u/s)
--   ranged mob  move_speed=9   →  360 u/s  (kites less aggressively)
--   caster mob  move_speed=10  →  400 u/s
--   support mob move_speed=8   →  320 u/s
--
-- If a mob has NO move_speed stat the server falls back to MobAIConfig::chaseSpeedUnitsPerSec=450.
-- Insert only — skip mobs that already have a move_speed row (safe to re-run).

INSERT INTO mob_stat (mob_id, attribute_id, flat_value)
SELECT m.id, 18, CASE m.ai_archetype
    WHEN 'melee'   THEN 12.0
    WHEN 'ranged'  THEN 9.0
    WHEN 'caster'  THEN 10.0
    WHEN 'support' THEN 8.0
    ELSE                12.0  -- default for unknown archetypes
  END
FROM mob m
WHERE NOT EXISTS (
    SELECT 1 FROM mob_stat ms WHERE ms.mob_id = m.id AND ms.attribute_id = 18
);
