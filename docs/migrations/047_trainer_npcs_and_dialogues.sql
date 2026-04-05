-- Migration 047: Trainer NPCs, Vendor Shops, and Dialogue Trees
-- Adds: Theron (warrior trainer, npc_id=4), Sylara (mage trainer, npc_id=5)
-- Adds: vendor shops for buyable skill books
-- Adds: complete skill-learning dialogue trees for both trainers
-- Must be applied AFTER migration 046.

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — NPC DEFINITIONS
-- npc_type: 6 = Trainer (added in migration 042)
-- race_id=1 = Human (same as existing NPCs)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc (id, name, race_id, level, current_health, current_mana, is_dead, slug, radius, is_interactable, npc_type, faction_slug)
OVERRIDING SYSTEM VALUE
VALUES
    (4, 'Theron',  1, 5, 500, 100, FALSE, 'theron',  100, TRUE, 6, NULL),
    (5, 'Sylara',  1, 5, 400, 250, FALSE, 'sylara',  100, TRUE, 6, NULL)
ON CONFLICT (id) DO NOTHING;
SELECT setval(pg_get_serial_sequence('public.npc', 'id'), 5, true);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — NPC ATTRIBUTES (attribute_id: 1=max_health, 2=max_mana)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_attributes (npc_id, attribute_id, value)
OVERRIDING SYSTEM VALUE
VALUES
    (4, 1, 500),  -- Theron max_health
    (4, 2, 100),  -- Theron max_mana
    (5, 1, 400),  -- Sylara max_health
    (5, 2, 250)   -- Sylara max_mana
ON CONFLICT DO NOTHING;
SELECT setval(pg_get_serial_sequence('public.npc_attributes', 'id'), 11, true);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — NPC POSITIONS (both tables, matching existing NPC pattern)
-- ═══════════════════════════════════════════════════════════════════════════

-- npc_placements — primary spatial placement table
INSERT INTO public.npc_placements (npc_id, zone_id, x, y, z, rot_z)
VALUES
    (4, 1,  1200.0, -2800.0, 200.0,  90.0),  -- Theron, near village warrior area
    (5, 1,  -400.0,  1600.0, 200.0, -90.0)   -- Sylara, near village mage area
ON CONFLICT DO NOTHING;

-- npc_position — secondary position table (kept in sync with npc_placements)
INSERT INTO public.npc_position (npc_id, x, y, z, rot_z, zone_id)
OVERRIDING SYSTEM VALUE
VALUES
    (4,  1200.0, -2800.0, 200.0,  90.0, NULL),
    (5,  -400.0,  1600.0, 200.0, -90.0, NULL)
ON CONFLICT DO NOTHING;
SELECT setval(pg_get_serial_sequence('public.npc_position', 'id'), 5, true);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — VENDOR NPC SETUP (trainers also sell buyable skill books)
-- vendor_npc ids: 2 = Theron, 3 = Sylara
-- markup_pct=0 — books are sold at base vendor_price_buy
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.vendor_npc (id, npc_id, markup_pct)
VALUES
    (2, 4, 0),  -- Theron
    (3, 5, 0)   -- Sylara
ON CONFLICT (id) DO NOTHING;

-- ─── Theron's Shop ── Warrior skill books (buyable ones only) ─────────────
-- stock_count=-1 = unlimited, restock_amount=0 = no restock needed
INSERT INTO public.vendor_inventory
    (vendor_npc_id, item_id, stock_count, price_override, restock_amount, stock_max, restock_interval_sec)
VALUES
    (2, 18, -1, NULL, 0, -1, 0),  -- Tome of Shield Bash  (500g)
    (2, 20, -1, NULL, 0, -1, 0),  -- Tome of Iron Skin    (400g)
    (2, 21, -1, NULL, 0, -1, 0)   -- Tome of Constitution Mastery (800g)
ON CONFLICT (vendor_npc_id, item_id) DO NOTHING;
-- NOTE: Tome of Whirlwind (id=19) is drop-only — NOT added to vendor inventory.

-- ─── Sylara's Shop ── Mage skill books (buyable ones only) ───────────────
INSERT INTO public.vendor_inventory
    (vendor_npc_id, item_id, stock_count, price_override, restock_amount, stock_max, restock_interval_sec)
VALUES
    (3, 22, -1, NULL, 0, -1, 0),  -- Tome of Frost Bolt    (500g)
    (3, 23, -1, NULL, 0, -1, 0),  -- Tome of Arcane Blast  (900g)
    (3, 25, -1, NULL, 0, -1, 0),  -- Tome of Mana Shield   (600g)
    (3, 26, -1, NULL, 0, -1, 0)   -- Tome of Elemental Mastery (1200g)
ON CONFLICT (vendor_npc_id, item_id) DO NOTHING;
-- NOTE: Tome of Chain Lightning (id=24) is drop/quest only — NOT sold.

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — DIALOGUE DEFINITIONS
-- Theron = dialogue id=4, nodes 400–499
-- Sylara = dialogue id=5, nodes 500–599
-- Node start_node_id set after inserts resolve circular dependency.
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue (id, slug, version, start_node_id)
VALUES
    (4, 'theron_main', 1, 400),
    (5, 'sylara_main', 1, 500)
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — DIALOGUE NODES: THERON (Warrior Trainer)
-- Flow overview:
--   400(line greeting) → 401(choice_hub "what to learn?")
--     → 402(confirm power_slash) ──yes→ 403(action learn) → 404(line success) → 401
--     → 410(confirm shield_bash) ──yes→ 411(action learn) → 412(line success) → 401
--     → 420(confirm whirlwind)   ──yes→ 421(action learn) → 422(line success) → 401
--     → 430(confirm iron_skin)   ──yes→ 431(action learn) → 432(line success) → 401
--     → 440(confirm con_mastery) ──yes→ 441(action learn) → 442(line success) → 401
--   499(end)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
VALUES
    -- ── Greeting & Hub ───────────────────────────────────────────────
    (400, 4, 'line',       4, 'theron.dialogue.greeting',    NULL, NULL, NULL),
    (401, 4, 'choice_hub', 4, 'theron.dialogue.skill_hub',   NULL, NULL, NULL),

    -- ── Power Slash ──────────────────────────────────────────────────
    (402, 4, 'line',   4, 'theron.dialogue.confirm_power_slash', NULL, NULL, NULL),
    (403, 4, 'action', 4, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"power_slash","sp_cost":1,"gold_cost":100,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (404, 4, 'line',   4, 'theron.dialogue.learned_power_slash', NULL, NULL, NULL),

    -- ── Shield Bash ──────────────────────────────────────────────────
    (410, 4, 'line',   4, 'theron.dialogue.confirm_shield_bash', NULL, NULL, NULL),
    (411, 4, 'action', 4, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"shield_bash","sp_cost":1,"gold_cost":150,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (412, 4, 'line',   4, 'theron.dialogue.learned_shield_bash', NULL, NULL, NULL),

    -- ── Whirlwind ────────────────────────────────────────────────────
    (420, 4, 'line',   4, 'theron.dialogue.confirm_whirlwind', NULL, NULL, NULL),
    (421, 4, 'action', 4, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"whirlwind","sp_cost":1,"gold_cost":300,"requires_book":true,"book_item_id":19}]}',
        NULL),
    (422, 4, 'line',   4, 'theron.dialogue.learned_whirlwind', NULL, NULL, NULL),

    -- ── Iron Skin (passive) ───────────────────────────────────────────
    (430, 4, 'line',   4, 'theron.dialogue.confirm_iron_skin', NULL, NULL, NULL),
    (431, 4, 'action', 4, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"iron_skin","sp_cost":1,"gold_cost":120,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (432, 4, 'line',   4, 'theron.dialogue.learned_iron_skin', NULL, NULL, NULL),

    -- ── Constitution Mastery (passive) ───────────────────────────────
    (440, 4, 'line',   4, 'theron.dialogue.confirm_constitution_mastery', NULL, NULL, NULL),
    (441, 4, 'action', 4, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"constitution_mastery","sp_cost":1,"gold_cost":200,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (442, 4, 'line',   4, 'theron.dialogue.learned_constitution_mastery', NULL, NULL, NULL),

    (499, 4, 'end',    4, NULL, NULL, NULL, NULL)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART G — DIALOGUE NODES: SYLARA (Mage Trainer)
-- Mirrors Theron's structure for fireball,frost_bolt,arcane_blast,chain_lightning,
-- mana_shield,elemental_mastery
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
VALUES
    -- ── Greeting & Hub ───────────────────────────────────────────────
    (500, 5, 'line',       5, 'sylara.dialogue.greeting',    NULL, NULL, NULL),
    (501, 5, 'choice_hub', 5, 'sylara.dialogue.skill_hub',   NULL, NULL, NULL),

    -- ── Fireball ─────────────────────────────────────────────────────
    (502, 5, 'line',   5, 'sylara.dialogue.confirm_fireball', NULL, NULL, NULL),
    (503, 5, 'action', 5, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"fireball","sp_cost":1,"gold_cost":120,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (504, 5, 'line',   5, 'sylara.dialogue.learned_fireball', NULL, NULL, NULL),

    -- ── Frost Bolt ───────────────────────────────────────────────────
    (510, 5, 'line',   5, 'sylara.dialogue.confirm_frost_bolt', NULL, NULL, NULL),
    (511, 5, 'action', 5, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"frost_bolt","sp_cost":1,"gold_cost":100,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (512, 5, 'line',   5, 'sylara.dialogue.learned_frost_bolt', NULL, NULL, NULL),

    -- ── Arcane Blast ─────────────────────────────────────────────────
    (520, 5, 'line',   5, 'sylara.dialogue.confirm_arcane_blast', NULL, NULL, NULL),
    (521, 5, 'action', 5, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"arcane_blast","sp_cost":1,"gold_cost":200,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (522, 5, 'line',   5, 'sylara.dialogue.learned_arcane_blast', NULL, NULL, NULL),

    -- ── Chain Lightning ──────────────────────────────────────────────
    (530, 5, 'line',   5, 'sylara.dialogue.confirm_chain_lightning', NULL, NULL, NULL),
    (531, 5, 'action', 5, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"chain_lightning","sp_cost":1,"gold_cost":400,"requires_book":true,"book_item_id":24}]}',
        NULL),
    (532, 5, 'line',   5, 'sylara.dialogue.learned_chain_lightning', NULL, NULL, NULL),

    -- ── Mana Shield (passive) ─────────────────────────────────────────
    (540, 5, 'line',   5, 'sylara.dialogue.confirm_mana_shield', NULL, NULL, NULL),
    (541, 5, 'action', 5, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"mana_shield","sp_cost":1,"gold_cost":150,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (542, 5, 'line',   5, 'sylara.dialogue.learned_mana_shield', NULL, NULL, NULL),

    -- ── Elemental Mastery (passive) ───────────────────────────────────
    (550, 5, 'line',   5, 'sylara.dialogue.confirm_elemental_mastery', NULL, NULL, NULL),
    (551, 5, 'action', 5, NULL, NULL,
        '{"actions":[{"type":"learn_skill","skill_slug":"elemental_mastery","sp_cost":1,"gold_cost":300,"requires_book":false,"book_item_id":0}]}',
        NULL),
    (552, 5, 'line',   5, 'sylara.dialogue.learned_elemental_mastery', NULL, NULL, NULL),

    (599, 5, 'end',    5, NULL, NULL, NULL, NULL)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART H — DIALOGUE EDGES: THERON
-- Condition JSON keys:
--   {"all":[ {"type":"level","gte":N}, {"type":"has_skill_points","gte":1},
--            {"type":"skill_not_learned","slug":"X"} ]}
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue_edge
    (id, from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES

    -- 400 (greeting) → 401 (hub)
    (70,  400, 401, 0, 'theron.choice.continue',           NULL, NULL, FALSE),

    -- 401 (hub) → skill confirmations (hidden if condition fails)
    (71,  401, 402, 0, 'theron.choice.learn_power_slash',
        '{"all":[{"type":"level","gte":5},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"power_slash"}]}',
        NULL, TRUE),

    (72,  401, 410, 1, 'theron.choice.learn_shield_bash',
        '{"all":[{"type":"level","gte":5},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"shield_bash"}]}',
        NULL, TRUE),

    (73,  401, 420, 2, 'theron.choice.learn_whirlwind',
        '{"all":[{"type":"level","gte":10},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"whirlwind"},{"type":"skill_learned","slug":"shield_bash"},{"type":"item","item_id":19,"gte":1}]}',
        NULL, TRUE),

    (74,  401, 430, 3, 'theron.choice.learn_iron_skin',
        '{"all":[{"type":"level","gte":5},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"iron_skin"}]}',
        NULL, TRUE),

    (75,  401, 440, 4, 'theron.choice.learn_constitution_mastery',
        '{"all":[{"type":"level","gte":8},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"constitution_mastery"},{"type":"skill_learned","slug":"iron_skin"}]}',
        NULL, TRUE),

    (76,  401, 499, 5, 'theron.choice.open_shop',          NULL,
        '{"actions":[{"mode":"buy","type":"open_vendor_shop"}]}',
        FALSE),

    (77,  401, 499, 6, 'theron.choice.farewell',           NULL, NULL, FALSE),

    -- Confirm nodes: "Yes" → action, "No" → back to hub
    (78,  402, 403, 0, 'theron.choice.yes',                NULL, NULL, FALSE),
    (79,  402, 401, 1, 'theron.choice.no',                 NULL, NULL, FALSE),
    (80,  403, 404, 0, 'theron.choice.continue',           NULL, NULL, FALSE),
    (81,  404, 401, 0, 'theron.choice.continue',           NULL, NULL, FALSE),

    (82,  410, 411, 0, 'theron.choice.yes',                NULL, NULL, FALSE),
    (83,  410, 401, 1, 'theron.choice.no',                 NULL, NULL, FALSE),
    (84,  411, 412, 0, 'theron.choice.continue',           NULL, NULL, FALSE),
    (85,  412, 401, 0, 'theron.choice.continue',           NULL, NULL, FALSE),

    (86,  420, 421, 0, 'theron.choice.yes',                NULL, NULL, FALSE),
    (87,  420, 401, 1, 'theron.choice.no',                 NULL, NULL, FALSE),
    (88,  421, 422, 0, 'theron.choice.continue',           NULL, NULL, FALSE),
    (89,  422, 401, 0, 'theron.choice.continue',           NULL, NULL, FALSE),

    (90,  430, 431, 0, 'theron.choice.yes',                NULL, NULL, FALSE),
    (91,  430, 401, 1, 'theron.choice.no',                 NULL, NULL, FALSE),
    (92,  431, 432, 0, 'theron.choice.continue',           NULL, NULL, FALSE),
    (93,  432, 401, 0, 'theron.choice.continue',           NULL, NULL, FALSE),

    (94,  440, 441, 0, 'theron.choice.yes',                NULL, NULL, FALSE),
    (95,  440, 401, 1, 'theron.choice.no',                 NULL, NULL, FALSE),
    (96,  441, 442, 0, 'theron.choice.continue',           NULL, NULL, FALSE),
    (97,  442, 401, 0, 'theron.choice.continue',           NULL, NULL, FALSE)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART I — DIALOGUE EDGES: SYLARA
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue_edge
    (id, from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES

    -- 500 (greeting) → 501 (hub)
    (100, 500, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),

    -- 501 (hub) → skill confirmations
    (101, 501, 502, 0, 'sylara.choice.learn_fireball',
        '{"all":[{"type":"level","gte":5},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"fireball"}]}',
        NULL, TRUE),

    (102, 501, 510, 1, 'sylara.choice.learn_frost_bolt',
        '{"all":[{"type":"level","gte":5},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"frost_bolt"}]}',
        NULL, TRUE),

    (103, 501, 520, 2, 'sylara.choice.learn_arcane_blast',
        '{"all":[{"type":"level","gte":8},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"arcane_blast"},{"type":"skill_learned","slug":"frost_bolt"}]}',
        NULL, TRUE),

    (104, 501, 530, 3, 'sylara.choice.learn_chain_lightning',
        '{"all":[{"type":"level","gte":12},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"chain_lightning"},{"type":"skill_learned","slug":"arcane_blast"},{"type":"item","item_id":24,"gte":1}]}',
        NULL, TRUE),

    (105, 501, 540, 4, 'sylara.choice.learn_mana_shield',
        '{"all":[{"type":"level","gte":5},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"mana_shield"}]}',
        NULL, TRUE),

    (106, 501, 550, 5, 'sylara.choice.learn_elemental_mastery',
        '{"all":[{"type":"level","gte":10},{"type":"has_skill_points","gte":1},{"type":"skill_not_learned","slug":"elemental_mastery"},{"type":"skill_learned","slug":"mana_shield"}]}',
        NULL, TRUE),

    (107, 501, 599, 6, 'sylara.choice.open_shop',          NULL,
        '{"actions":[{"mode":"buy","type":"open_vendor_shop"}]}',
        FALSE),

    (108, 501, 599, 7, 'sylara.choice.farewell',           NULL, NULL, FALSE),

    -- Confirm nodes
    (110, 502, 503, 0, 'sylara.choice.yes',                NULL, NULL, FALSE),
    (111, 502, 501, 1, 'sylara.choice.no',                 NULL, NULL, FALSE),
    (112, 503, 504, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),
    (113, 504, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),

    (114, 510, 511, 0, 'sylara.choice.yes',                NULL, NULL, FALSE),
    (115, 510, 501, 1, 'sylara.choice.no',                 NULL, NULL, FALSE),
    (116, 511, 512, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),
    (117, 512, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),

    (118, 520, 521, 0, 'sylara.choice.yes',                NULL, NULL, FALSE),
    (119, 520, 501, 1, 'sylara.choice.no',                 NULL, NULL, FALSE),
    (120, 521, 522, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),
    (121, 522, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),

    (122, 530, 531, 0, 'sylara.choice.yes',                NULL, NULL, FALSE),
    (123, 530, 501, 1, 'sylara.choice.no',                 NULL, NULL, FALSE),
    (124, 531, 532, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),
    (125, 532, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),

    (126, 540, 541, 0, 'sylara.choice.yes',                NULL, NULL, FALSE),
    (127, 540, 501, 1, 'sylara.choice.no',                 NULL, NULL, FALSE),
    (128, 541, 542, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),
    (129, 542, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),

    (130, 550, 551, 0, 'sylara.choice.yes',                NULL, NULL, FALSE),
    (131, 550, 501, 1, 'sylara.choice.no',                 NULL, NULL, FALSE),
    (132, 551, 552, 0, 'sylara.choice.continue',           NULL, NULL, FALSE),
    (133, 552, 501, 0, 'sylara.choice.continue',           NULL, NULL, FALSE)

ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART J — BIND DIALOGUES TO NPCS
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_dialogue (npc_id, dialogue_id, priority, condition_group)
VALUES
    (4, 4, 0, NULL),  -- Theron → theron_main dialogue
    (5, 5, 0, NULL)   -- Sylara → sylara_main dialogue
ON CONFLICT DO NOTHING;
