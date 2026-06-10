-- =============================================================================
-- Migration 062: Rewrite NPC dialogues + Varan fox quest
-- =============================================================================
-- Переписывает диалоги всех деревеньских NPC и добавляет новый квест у Варана.
--
-- Включает:
--   • Новый предмет: Fox Skin (id=28) — «Лисья шкура» + дроп с SmallFox
--   • Новый квест: varan_fox_menace (id=2) — «Рыжая напасть» (2 части)
--       Part 1: убить 8 лис (kill, step 0, auto)
--               → report to Varan (custom, step 1, manual)
--       Part 2: собрать 8 шкур (collect, step 2, auto)
--   • Удаление старого квеста wolf_hunt_intro и прогресса игроков
--   • Dialogue 1 — Milaya: простой диалог торговки (shop + farewell)
--   • Dialogue 2 — Varan: торговец + квестовый хаб
--   • Dialogue 3 — Edrik: тренер воинов (skill shop + farewell)
--   • Dialogue 4 — Theron: кузнец (trade shop + repair + farewell)
--   • Dialogue 5 — Sylara: тренер магов (skill shop + farewell)
--
-- !!! DESTRUCTIVE MIGRATION — удаляет старые диалоги и квест !!!
-- =============================================================================

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — NEW ITEM: Fox Skin (id=28)
-- quest_item=true, not tradable, resource type, max_stack=10
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.items (id, name, slug, description, is_quest_item, item_type, weight,
    rarity_id, stack_max, is_container, is_durable, is_tradable, durability_max,
    vendor_price_buy, vendor_price_sell, equip_slot, level_requirement,
    is_equippable, is_harvest, is_usable, is_two_handed, mastery_slug)
OVERRIDING SYSTEM VALUE
VALUES (28, 'Fox Skin', 'fox_skin',
    'A usable fox skin suitable for trade.', TRUE, 6, 0.5, 1, 10, FALSE, FALSE, FALSE, 100,
    5, 2, NULL, 0, FALSE, FALSE, FALSE, FALSE, NULL)
ON CONFLICT (id) DO NOTHING;

-- Add Fox Skin to SmallFox (mob_id=1) loot — 40% direct drop (не harvest-only)
INSERT INTO public.mob_loot_info
    (mob_id, item_id, drop_chance, is_harvest_only, min_quantity, max_quantity, loot_tier)
VALUES
    (1, 28, 0.40, FALSE, 1, 1, 'common')
ON CONFLICT DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — NEW QUEST: varan_fox_menace (id=2) — «Рыжая напасть»
-- giver: Varan (npc_id=1), turnin: Varan (npc_id=1)
-- Steps: kill(0,auto) → custom(1,manual) → collect(2,auto)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.quest (id, slug, min_level, repeatable, cooldown_sec,
    giver_npc_id, turnin_npc_id, client_quest_key,
    reputation_faction_slug, reputation_on_complete, reputation_on_fail)
OVERRIDING SYSTEM VALUE
VALUES (2, 'varan_fox_menace', 1, FALSE, 0,
    1, 1, 'quest.varan_fox_menace',
    NULL, 0, 0)
ON CONFLICT (id) DO NOTHING;

-- Quest steps
INSERT INTO public.quest_step (id, quest_id, step_index, step_type, params, client_step_key, completion_mode)
OVERRIDING SYSTEM VALUE
VALUES
    (5, 2, 0, 'kill',    '{"count": 8, "mob_id": 1}',      'quest.varan_fox_menace.kill_foxes',        'auto'),
    (6, 2, 1, 'custom',  '{}',                               'quest.varan_fox_menace.report_to_varan',   'manual'),
    (7, 2, 2, 'collect', '{"count": 8, "item_id": 28}',      'quest.varan_fox_menace.collect_fox_skins',  'auto')
ON CONFLICT (id) DO NOTHING;

-- Quest rewards (выдаются при turn_in)
INSERT INTO public.quest_reward (id, quest_id, reward_type, item_id, quantity, amount, is_hidden)
OVERRIDING SYSTEM VALUE
VALUES
    (3, 2, 'item', 3, 3, 3, FALSE)    -- 3 Health Potions
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — DELETE OLD WOLF_HUNT QUEST AND PLAYER PROGRESS
-- ═══════════════════════════════════════════════════════════════════════════

DELETE FROM public.player_quest WHERE quest_id = 1;
DELETE FROM public.quest_step  WHERE quest_id = 1;
DELETE FROM public.quest_reward WHERE quest_id = 1;
DELETE FROM public.quest       WHERE id       = 1;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — CLEANUP: удаляем старые ноды/рёбра диалогов 1-5
-- FK dialogue_edge → ON DELETE CASCADE
-- ═══════════════════════════════════════════════════════════════════════════

DELETE FROM public.dialogue_node WHERE dialogue_id IN (1, 2, 3, 4, 5);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — DIALOGUE 1: Milaya (торговка зельями)
--
-- Граф:
--   1   line   «*Женщина аккуратно расставляет флаконы...*»
--               «Не трогай зелёные флаконы без спроса...»
--   2   choice_hub  «Покажи товары.» / «Я зайду позже.»
--   99  end
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (1,   1, 'line',       2, 'milaya.dialogue.greeting', NULL, NULL, NULL),
    (2,   1, 'choice_hub', 2, 'milaya.dialogue.main_hub', NULL, NULL, NULL),
    (99,  1, 'end',        2, NULL,                       NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    (1,  2,  0, 'milaya.choice.continue', NULL, NULL, FALSE),
    (2, 99, 0, 'milaya.choice.shop',      NULL, '{"actions": [{"type": "open_vendor_shop"}]}'::jsonb, FALSE),
    (2, 99, 1, 'milaya.choice.farewell',  NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — DIALOGUE 2: Varan (торговец + квест «Рыжая напасть»)
--
-- Граф (упрощённо):
--   200 line        «Подходи, не стесняйся...»
--   201 choice_hub  Main hub: «Покажи товары.» / «Нужна какая-то помощь?» / «Я зайду позже.»
--   202 choice_hub  Quest hub (условные рёбра по состоянию квеста)
--   204 line        «Хорошо! Отправляйся к реке...» (quest accepted)
--   205 line        «Лес нынче неспокойный...» (lore ответ)
--   206 line        «Помоги расправиться с лисами...» (reminder, step 0)
--   208 line        «Спасибо!... Есть ещё одна просьба.»
--   209 line        «8 лисьих шкур...» (reminder, step 2)
--   211 line        «Спасибо тебе!... Награда небольшая...»
--   212 line        «Что ж… они твои по праву...» (отказ от сдачи шкур)
--   213 choice_hub  «Какую?» / «Нет, с меня хватит!»
--   214 line        «Понимаешь, тут такое дело... шкуры нужны...» (объяснение part 2)
--   215 choice_hub  «Хорошо, я помогу...» / «Нет, с меня хватит!»
--   216 line        «Понимаю... Береги себя!» (отказ)
--   217 choice_hub  «Прощай!» (после отказа)
--   218 line        «Отлично! Принеси 8 шкур...» (принятие part 2)
--   219 choice_hub  «Принять награду» / «Прощай!» (после turn_in)
--   220 choice_hub  «Прощай!» (после отказа сдавать шкуры)
--   299 end
-- ═══════════════════════════════════════════════════════════════════════════

-- F.1 — Update dialogue 2 start_node
UPDATE public.dialogue SET start_node_id = 200 WHERE id = 2;

-- F.2 — Nodes
INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (200, 2, 'line',       1, 'varan.dialogue.greeting',             NULL, NULL, NULL),
    (201, 2, 'choice_hub', 1, 'varan.dialogue.main_hub',             NULL, NULL, NULL),
    (202, 2, 'choice_hub', 1, 'varan.dialogue.quest_hub',            NULL, NULL, NULL),
    (204, 2, 'line',       1, 'varan.dialogue.quest_accepted',       NULL, NULL, NULL),
    (205, 2, 'line',       1, 'varan.dialogue.why_foxes',            NULL, NULL, NULL),
    (206, 2, 'line',       1, 'varan.dialogue.reminder_step0',       NULL, NULL, NULL),
    (208, 2, 'line',       1, 'varan.dialogue.thanks_need_more',     NULL, NULL, NULL),
    (209, 2, 'line',       1, 'varan.dialogue.reminder_step2',       NULL, NULL, NULL),
    (211, 2, 'line',       1, 'varan.dialogue.turnin_thanks',        NULL, NULL, NULL),
    (212, 2, 'line',       1, 'varan.dialogue.refuse_turnin',        NULL, NULL, NULL),
    (213, 2, 'choice_hub', 1, 'varan.dialogue.what_help',            NULL, NULL, NULL),
    (214, 2, 'line',       1, 'varan.dialogue.explain_skins',        NULL, NULL, NULL),
    (215, 2, 'choice_hub', 1, 'varan.dialogue.accept_skins_hub',     NULL, NULL, NULL),
    (216, 2, 'line',       1, 'varan.dialogue.decline_part2',        NULL, NULL, NULL),
    (217, 2, 'choice_hub', 1, 'varan.dialogue.farewell_hub',         NULL, NULL, NULL),
    (218, 2, 'line',       1, 'varan.dialogue.accepted_skins',       NULL, NULL, NULL),
    (219, 2, 'choice_hub', 1, 'varan.dialogue.accept_reward_hub',    NULL, NULL, NULL),
    (220, 2, 'choice_hub', 1, 'varan.dialogue.farewell_hub',         NULL, NULL, NULL),
    (299, 2, 'end',        1, NULL,                                  NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

-- F.3 — Edges
INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    -- 200 → 201: «Продолжить»
    (200, 201, 0, 'varan.choice.continue', NULL, NULL, FALSE),

    -- 201 (Main hub): «Покажи товары.» / «Нужна какая-то помощь?» / «Я зайду позже.»
    (201, 299, 0, 'varan.choice.shop',         NULL, '{"actions": [{"type": "open_vendor_shop"}]}'::jsonb, FALSE),
    (201, 202, 1, 'varan.choice.need_help',    NULL, NULL, FALSE),
    (201, 299, 2, 'varan.choice.farewell',     NULL, NULL, FALSE),

    -- 202 (Quest hub): condition-based edges
    -- [quest NOT started OR failed]: accept + lore
    -- accept edge has offer_quest action directly
    (202, 204, 0, 'varan.choice.accept_quest', '[
        {"any": [
            {"type": "quest", "state": "not_started", "slug": "varan_fox_menace"},
            {"type": "quest", "state": "failed",      "slug": "varan_fox_menace"}
        ]}
    ]'::jsonb, '[
        {"type": "offer_quest", "slug": "varan_fox_menace"}
    ]'::jsonb, TRUE),
    (202, 205, 1, 'varan.choice.why_foxes', '[
        {"any": [
            {"type": "quest", "state": "not_started", "slug": "varan_fox_menace"},
            {"type": "quest", "state": "failed",      "slug": "varan_fox_menace"}
        ]}
    ]'::jsonb, NULL, TRUE),

    -- [quest active, step 0 (kill)]: reminder
    (202, 206, 0, 'varan.choice.remind', '[
        {"all": [
            {"type": "quest",      "state": "active", "slug": "varan_fox_menace"},
            {"type": "quest_step", "step":  0,        "slug": "varan_fox_menace"}
        ]}
    ]'::jsonb, NULL, TRUE),

    -- [quest active, step 1 (report)]: «Я избавился от лис!» + advance + rewards
    (202, 208, 0, 'varan.choice.report_foxes', '[
        {"all": [
            {"type": "quest",      "state": "active", "slug": "varan_fox_menace"},
            {"type": "quest_step", "step":  1,        "slug": "varan_fox_menace"}
        ]}
    ]'::jsonb, '[
        {"type": "advance_quest_step", "slug": "varan_fox_menace"},
        {"type": "change_reputation",  "faction": "merchants", "delta": 10},
        {"type": "give_gold",          "amount":  5}
    ]'::jsonb, TRUE),

    -- [quest active, step 2 (collect)]: reminder
    (202, 209, 0, 'varan.choice.remind_skins', '[
        {"all": [
            {"type": "quest",      "state": "active", "slug": "varan_fox_menace"},
            {"type": "quest_step", "step":  2,        "slug": "varan_fox_menace"}
        ]}
    ]'::jsonb, NULL, TRUE),

    -- [quest completed]: «Вот держи шкуры!» (turn_in on edge) / «Я не отдам тебе шкуры!»
    (202, 211, 0, 'varan.choice.turnin_foxes', '[
        {"type": "quest", "state": "completed", "slug": "varan_fox_menace"}
    ]'::jsonb, '[
        {"type": "turn_in_quest", "slug": "varan_fox_menace"}
    ]'::jsonb, TRUE),
    (202, 212, 1, 'varan.choice.refuse_turnin_foxes', '[
        {"type": "quest", "state": "completed", "slug": "varan_fox_menace"}
    ]'::jsonb, NULL, TRUE),

    -- [quest turned_in — награда ещё не получена]: показываем спасибо снова
    (202, 211, 0, 'varan.choice.claim_reward', '[
        {"type": "quest", "state": "turned_in", "slug": "varan_fox_menace"},
        {"type": "flag",  "key": "varan_fox_menace.reward_claimed", "eq": false}
    ]'::jsonb, NULL, TRUE),

    -- [quest turned_in + reward claimed — nothing more to do]
    -- handled via default «Прощай!» edge below

    -- Default: «Прощай!» (всегда видно)
    (202, 299, 99, 'varan.choice.farewell', NULL, NULL, FALSE),

    -- 204 (quest accepted) → continue back to hub
    (204, 202, 0, 'varan.choice.back', NULL, NULL, FALSE),

    -- 205 (lore) → back to quest hub
    (205, 202, 0, 'varan.choice.back', NULL, NULL, FALSE),

    -- 206 (reminder step 0) → back to quest hub
    (206, 202, 0, 'varan.choice.back', NULL, NULL, FALSE),

    -- 208 (thanks + "Есть ещё одна просьба") → 213
    (208, 213, 0, 'varan.choice.continue', NULL, NULL, FALSE),

    -- 213: «Какую?» / «Нет, с меня хватит!»
    (213, 214, 0, 'varan.choice.what_request', NULL, NULL, FALSE),
    (213, 216, 1, 'varan.choice.no_more',      NULL, NULL, FALSE),

    -- 214 (explain skins) → 215
    (214, 215, 0, 'varan.choice.continue', NULL, NULL, FALSE),

    -- 215: «Хорошо, я помогу...» / «Нет, с меня хватит!»
    (215, 218, 0, 'varan.choice.help_skins', NULL, NULL, FALSE),
    (215, 216, 1, 'varan.choice.no_more',    NULL, NULL, FALSE),

    -- 218 (accepted skins task) → back to quest hub
    (218, 202, 0, 'varan.choice.back', NULL, NULL, FALSE),

    -- 216 (decline: «Понимаю... Береги себя!») → 217
    (216, 217, 0, 'varan.choice.continue', NULL, NULL, FALSE),

    -- 217: «Прощай!»
    (217, 299, 0, 'varan.choice.farewell', NULL, NULL, FALSE),

    -- 209 (reminder step 2) → back to quest hub
    (209, 202, 0, 'varan.choice.back', NULL, NULL, FALSE),

    -- 211 (turnin thanks) → 219
    (211, 219, 0, 'varan.choice.continue', NULL, NULL, FALSE),

    -- 219: «Принять награду» / «Прощай!»
    (219, 299, 0, 'varan.choice.accept_reward', NULL, '[
        {"type": "change_reputation", "faction": "merchants", "delta": 15},
        {"type": "give_gold",         "amount":  15},
        {"type": "set_flag",          "key": "varan_fox_menace.reward_claimed", "bool_value": true}
    ]'::jsonb, FALSE),
    (219, 299, 1, 'varan.choice.farewell', NULL, NULL, FALSE),

    -- 212 (refuse turnin text) → 220
    (212, 220, 0, 'varan.choice.continue', NULL, NULL, FALSE),

    -- 220: «Прощай!»
    (220, 299, 0, 'varan.choice.farewell', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART G — DIALOGUE 3: Edrik (тренер воинов)
--
-- Граф:
--   301 line        «*Перед тобой стоит крепкий мужчина...*»
--                    «Хочешь научиться драться?...»
--   302 choice_hub  «Я хочу изучить новые боевые навыки.» / «Я вернусь позже.»
--   399 end
-- ═══════════════════════════════════════════════════════════════════════════

UPDATE public.dialogue SET start_node_id = 301 WHERE id = 3;

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (301, 3, 'line',       3, 'edrik.dialogue.greeting', NULL, NULL, NULL),
    (302, 3, 'choice_hub', 3, 'edrik.dialogue.main_hub', NULL, NULL, NULL),
    (399, 3, 'end',        3, NULL,                      NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    (301, 302, 0, 'edrik.choice.continue', NULL, NULL, FALSE),
    (302, 399, 0, 'edrik.choice.skills',   NULL, '{"actions": [{"type": "open_skill_shop"}]}'::jsonb, FALSE),
    (302, 399, 1, 'edrik.choice.farewell', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART H — DIALOGUE 4: Theron (кузнец)
--
-- Граф:
--   401 line        «*Кузнец отрывается от наковальни...*»
--                    «Если пришёл поглазеть - стой подальше от горна...»
--   402 choice_hub  «Мне нужно оружие.» / «Можешь починить моё снаряжение?» / «Я зайду позже.»
--   499 end
-- ═══════════════════════════════════════════════════════════════════════════

UPDATE public.dialogue SET start_node_id = 401 WHERE id = 4;

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (401, 4, 'line',       4, 'theron.dialogue.greeting', NULL, NULL, NULL),
    (402, 4, 'choice_hub', 4, 'theron.dialogue.main_hub', NULL, NULL, NULL),
    (499, 4, 'end',        4, NULL,                       NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    (401, 402, 0, 'theron.choice.continue', NULL, NULL, FALSE),
    (402, 499, 0, 'theron.choice.shop',     NULL, '{"actions": [{"type": "open_vendor_shop"}]}'::jsonb,  FALSE),
    (402, 499, 1, 'theron.choice.repair',   NULL, '{"actions": [{"type": "open_repair_shop"}]}'::jsonb,  FALSE),
    (402, 499, 2, 'theron.choice.farewell', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART I — DIALOGUE 5: Sylara (тренер магов)
--
-- Граф:
--   501 line        «*Перед тобой стоит довольно странный человек...*»
--                    «Магия - это не сила в руках...»
--   502 choice_hub  «Я хочу изучить новые заклинания.» / «Я вернусь позже.»
--   599 end
-- ═══════════════════════════════════════════════════════════════════════════

UPDATE public.dialogue SET start_node_id = 501 WHERE id = 5;

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (501, 5, 'line',       5, 'sylara.dialogue.greeting', NULL, NULL, NULL),
    (502, 5, 'choice_hub', 5, 'sylara.dialogue.main_hub', NULL, NULL, NULL),
    (599, 5, 'end',        5, NULL,                       NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    (501, 502, 0, 'sylara.choice.continue', NULL, NULL, FALSE),
    (502, 599, 0, 'sylara.choice.skills',   NULL, '{"actions": [{"type": "open_skill_shop"}]}'::jsonb, FALSE),
    (502, 599, 1, 'sylara.choice.farewell', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART J — SEQUENCE UPDATES
-- ═══════════════════════════════════════════════════════════════════════════

SELECT setval('public.items_id_seq',             28, true);
SELECT setval('public.mob_loot_info_id_seq', (SELECT MAX(id) FROM public.mob_loot_info), true);
SELECT setval('public.quest_id_seq',              2, true);
SELECT setval('public.quest_step_id_seq',         7, true);
SELECT setval('public.quest_reward_id_seq',       3, true);
SELECT setval('public.dialogue_node_id_seq', (SELECT MAX(id) FROM public.dialogue_node), true);
SELECT setval('public.dialogue_edge_id_seq', (SELECT MAX(id) FROM public.dialogue_edge), true);

-- =============================================================================
-- LOCALISATION KEYS (for client-side implementation)
-- =============================================================================
--
-- === Milaya (Dialogue 1) ===
-- NPC lines:
--   milaya.dialogue.greeting   «Не трогай зелёные флаконы без спроса...»
-- Choices:
--   milaya.choice.shop         «Покажи товары.»
--   milaya.choice.farewell     «Я зайду позже.»
--
-- === Varan (Dialogue 2) ===
-- NPC lines:
--   varan.dialogue.greeting            «Подходи, не стесняйся...»
--   varan.dialogue.quest_accepted      Quest accepted text
--   varan.dialogue.why_foxes           «Лес нынче неспокойный...»
--   varan.dialogue.reminder_step0      «Помоги расправиться с лисами...»
--   varan.dialogue.thanks_need_more    «Спасибо!... Есть ещё одна просьба.»
--   varan.dialogue.reminder_step2      «8 лисьих шкур любого качества...»
--   varan.dialogue.turnin_thanks       «Спасибо тебе!... Награда небольшая...»
--   varan.dialogue.refuse_turnin       «Что ж… они твои по праву...»
--   varan.dialogue.explain_skins       «Понимаешь, тут такое дело...»
--   varan.dialogue.decline_part2       «Понимаю... Береги себя!»
--   varan.dialogue.accepted_skins      «Отлично! Принеси 8 шкур...»
-- Choices:
--   varan.choice.shop                  «Покажи товары.»
--   varan.choice.need_help             «Нужна какая-то помощь?»
--   varan.choice.farewell              «Я зайду позже.»
--   varan.choice.accept_quest          «Я разберусь с ними!»
--   varan.choice.why_foxes             «Почему лисы так близко подходят к деревне?»
--   varan.choice.remind                «Напомни что нужно сделать?»
--   varan.choice.report_foxes          «Я избавился от лис как ты и просил!»
--   varan.choice.remind_skins          «Напомни что нужно?»
--   varan.choice.turnin_foxes          «Вот держи шкуры!»
--   varan.choice.refuse_turnin_foxes   «Я не отдам тебе шкуры, они мои!»
--   varan.choice.claim_reward          (переход к награде после turn_in)
--   varan.choice.what_request          «Какую?»
--   varan.choice.no_more               «Нет, с меня хватит!»
--   varan.choice.help_skins            «Хорошо, я помогу...»
--   varan.choice.accept_reward         «Принять награду»
--
-- === Edrik (Dialogue 3) ===
-- NPC lines:
--   edrik.dialogue.greeting   «Хочешь научиться драться?...»
-- Choices:
--   edrik.choice.skills       «Я хочу изучить новые боевые навыки.»
--   edrik.choice.farewell     «Я вернусь позже.»
--
-- === Theron (Dialogue 4) ===
-- NPC lines:
--   theron.dialogue.greeting  «Если пришёл поглазеть - стой подальше...»
-- Choices:
--   theron.choice.shop        «Мне нужно оружие.»
--   theron.choice.repair      «Можешь починить моё снаряжение?»
--   theron.choice.farewell    «Я зайду позже.»
--
-- === Sylara (Dialogue 5) ===
-- NPC lines:
--   sylara.dialogue.greeting  «Магия - это не сила в руках...»
-- Choices:
--   sylara.choice.skills      «Я хочу изучить новые заклинания.»
--   sylara.choice.farewell    «Я вернусь позже.»
--
-- === Quest (varan_fox_menace) ===
-- Quest keys:
--   quest.varan_fox_menace              Quest title/desc
--   quest.varan_fox_menace.kill_foxes   «Убить обезумевших лис у реки: 0/8»
--   quest.varan_fox_menace.report_to_varan  «Вернуться к Варану»
--   quest.varan_fox_menace.collect_fox_skins «Принести пригодные лисьи шкуры: 0/8»
-- =============================================================================
