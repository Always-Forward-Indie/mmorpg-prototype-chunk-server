-- =============================================================================
-- Migration 056: Ruins — Dying Stranger NPC
-- =============================================================================
-- Adds "Умирающий незнакомец" (ruins_dying_stranger) — раненый NPC в руинах.
--
-- Включает:
--   • NPC (npc_id=6, general, human, level 1, current_health=1)
--   • Атрибуты: max_health=50, max_mana=0
--   • Позиция: x=18870.0, y=-15430.0, z=1490.0, rot_z=180.0 (руины, без зоны)
--   • Ambient speech:
--       "...ещё один… значит, это правда…"
--       "они продолжают приходить…"
--       "подойди, мне есть что сказать"
--   • Диалог ruins_dying_stranger_main (id=6, ноды 600–609):
--       600 (line) → 601 (line) → 602 (line) → 603 (choice_hub)
--                                                ├─[accept]→ 605 (line: farewell, action_group: give items)
--                                                └─[decline]→ 605 (line: farewell)
--       действия на ребре accept выдают: Iron Sword (id=1, ×1) + Health Potion (id=3, ×2)
--       + устанавливают флаг ruins_dying_stranger.received_gift=true (одноразово, hide_if_locked)
-- =============================================================================

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — NPC DEFINITION
-- npc_type=1 (general) | race_id=1 (human) | current_health=1 (раненый)
-- radius=300 — слышно реплики с большого расстояния
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc (id, name, race_id, level, current_health, current_mana,
                        is_dead, slug, radius, is_interactable, npc_type, faction_slug)
OVERRIDING SYSTEM VALUE
VALUES (6, 'Умирающий незнакомец', 1, 1, 1, 0, FALSE, 'ruins_dying_stranger', 300, TRUE, 1, NULL)
ON CONFLICT (id) DO NOTHING;

SELECT setval(pg_get_serial_sequence('public.npc', 'id'), 6, true);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — NPC ATTRIBUTES
-- attribute_id: 1=max_health, 2=max_mana
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_attributes (npc_id, attribute_id, value)
OVERRIDING SYSTEM VALUE
VALUES
    (6, 1, 50),  -- max_health (низкое — он умирает)
    (6, 2,  0)   -- max_mana
ON CONFLICT DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — NPC POSITIONS
-- Руины: x=18870.0, y=-15430.0, z=1490.0, rot_z=180.0
-- zone_id=NULL — зона руин ещё не добавлена в таблицу zones
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_placements (npc_id, zone_id, x, y, z, rot_z)
VALUES (6, NULL, 18870.0, -15430.0, 1490.0, 180.0)
ON CONFLICT DO NOTHING;

ALTER TABLE public.npc_position ALTER COLUMN id RESTART WITH 8;
INSERT INTO public.npc_position (npc_id, x, y, z, rot_z, zone_id)
VALUES (6, 18870.0, -15430.0, 1490.0, 180.0, NULL)
ON CONFLICT DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — AMBIENT SPEECH
-- Три периодические реплики (priority=0, все видны без условий).
-- "come_closer" имеет чуть больший вес — вызов к взаимодействию
--   приоритетнее idle-реплик.
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_ambient_speech_configs (npc_id, min_interval_sec, max_interval_sec)
VALUES (6, 25, 80)
ON CONFLICT (npc_id) DO NOTHING;

INSERT INTO public.npc_ambient_speech_lines
    (npc_id, line_key, trigger_type, trigger_radius, priority, weight, cooldown_sec, condition_group)
VALUES
    (6, 'npc.ruins_dying_stranger.ambient.another_one', 'periodic', 400, 0, 10, 120, NULL),
    (6, 'npc.ruins_dying_stranger.ambient.keep_coming',  'periodic', 400, 0, 10, 120, NULL),
    (6, 'npc.ruins_dying_stranger.ambient.come_closer',  'periodic', 400, 0, 15,  90, NULL);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — DIALOGUE TREE: ruins_dying_stranger_main (id=6)
--
-- Граф:
--   600 line  "это место… не отпускает никого…"
--    ↓ [continue]
--   601 line  "если хочешь выжить — иди к выходу…"
--    ↓ [continue]
--   602 line  "я уже не выберусь… но ты ещё можешь"
--    ↓ [continue]
--   603 choice_hub
--    ├─ [accept]  → 605  (ребро несёт action_group: give sword + 2 potions)
--    └─ [decline] → 605
--   605 line  farewell
--    ↓ [farewell]
--   609 end
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue (id, slug, version, start_node_id)
VALUES (6, 'ruins_dying_stranger_main', 1, 600)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
VALUES
    (600, 6, 'line',       6, 'npc.ruins_dying_stranger.place_wont_let_go',      NULL, NULL, NULL),
    (601, 6, 'line',       6, 'npc.ruins_dying_stranger.if_survive_go_exit',      NULL, NULL, NULL),
    (602, 6, 'line',       6, 'npc.ruins_dying_stranger.wont_escape_but_you_can', NULL, NULL, NULL),
    (603, 6, 'choice_hub', 6, NULL,                                               NULL, NULL, NULL),
    (605, 6, 'line',       6, 'npc.ruins_dying_stranger.farewell',                NULL, NULL, NULL),
    (609, 6, 'end',        6, NULL,                                               NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

SELECT setval('dialogue_id_seq',       6,   true);
SELECT setval('dialogue_node_id_seq', 609,  true);
-- Обеспечиваем, что sequence dialogue_edge начнёт с MAX(id)+1
SELECT setval('dialogue_edge_id_seq', (SELECT MAX(id) FROM public.dialogue_edge), true);

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    -- 600 → 601: "Продолжить"
    (600, 601, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),
    -- 601 → 602: "Продолжить"
    (601, 602, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),
    -- 602 → 603: "Продолжить" (переход к выбору)
    (602, 603, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),
    -- 603 → 605: принять дар — action_group содержит give_item + set_flag (одноразово)
    --             condition_group: флаг ещё не установлен; hide_if_locked=true скрывает выбор после получения
    (603, 605, 0, 'ruins_dying_stranger.choice.accept',
        '[{"type": "flag", "key": "ruins_dying_stranger.received_gift", "eq": false}]'::jsonb,
        '[{"type": "give_item", "item_id": 1, "quantity": 1}, {"type": "give_item", "item_id": 3, "quantity": 2}, {"type": "set_flag", "key": "ruins_dying_stranger.received_gift", "bool_value": true}]'::jsonb,
        TRUE),
    -- 603 → 605: отказаться (без action_group)
    (603, 605, 1, 'ruins_dying_stranger.choice.decline',  NULL, NULL, FALSE),
    -- 605 → 609: "Прощай"
    (605, 609, 0, 'ruins_dying_stranger.choice.farewell', NULL, NULL, FALSE);

SELECT setval('dialogue_edge_id_seq', (SELECT MAX(id) FROM public.dialogue_edge), true);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — NPC → DIALOGUE MAPPING
-- Один диалог без условий, приоритет 0
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_dialogue (npc_id, dialogue_id, priority, condition_group)
VALUES (6, 6, 0, NULL)
ON CONFLICT DO NOTHING;
