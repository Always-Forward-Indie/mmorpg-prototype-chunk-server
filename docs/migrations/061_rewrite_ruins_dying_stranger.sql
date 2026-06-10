-- =============================================================================
-- Migration 061: Rewrite ruins_dying_stranger dialogue
-- =============================================================================
-- Полностью переписывает диалог «Умирающий незнакомец» (ruins_dying_stranger):
-- ветвящийся разговор с выдачей классовых предметов и replay-состояниями.
--
-- Включает:
--   • Новый предмет: Small Mana Elixir (id=27) — «Малый эликсир маны» + use_effect
--   • Dialogue 6 (ruins_dying_stranger_main): полное ветвящееся дерево
--   • Dialogue 7 (ruins_dying_stranger_replay): короткий replay (предметы уже взяты)
--   • Dialogue 8 (ruins_dying_stranger_items_shortcut): перевыдача предметов (вернулся без предметов)
--   • Три NPC→dialogue mapping с condition-based выбором:
--       priority 2: dialogue_started=true AND received_gift=true → replay (id=7)
--       priority 1: dialogue_started=true AND received_gift=false → shortcut (id=8)
--       priority 0: catch-all → main (id=6)
--   • Флаги: ruins_dying_stranger.dialogue_started, ruins_dying_stranger.received_gift
--   • Классовые награды:
--       Warrior (class_id=2): Iron Sword (id=1) + Health Potion ×2 (id=3) + Mana Elixir ×1 (id=27)
--       Mage    (class_id=1): Wooden Staff (id=15) + Health Potion ×2 (id=3) + Mana Elixir ×1 (id=27)
--
-- !!! DESTRUCTIVE MIGRATION — нельзя запускать повторно без последствий !!!
-- =============================================================================

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — NEW ITEM: Small Mana Elixir (id=27)
-- item_type=3 (potion), rarity=1 (common), usable=true, instant mp restore
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.items (id, name, slug, description, is_quest_item, item_type, weight,
    rarity_id, stack_max, is_container, is_durable, is_tradable, durability_max,
    vendor_price_buy, vendor_price_sell, equip_slot, level_requirement,
    is_equippable, is_harvest, is_usable, is_two_handed, mastery_slug)
OVERRIDING SYSTEM VALUE
VALUES (27, 'Small Mana Elixir', 'small_mana_elixir',
    'Restores 50 mana points.', FALSE, 3, 0.5, 1, 64, FALSE, FALSE, TRUE, 100,
    10, 4, NULL, 0, FALSE, FALSE, TRUE, FALSE, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.item_use_effects (id, item_id, effect_slug, attribute_slug, value,
    is_instant, duration_seconds, tick_ms, cooldown_seconds)
OVERRIDING SYSTEM VALUE
VALUES (3, 27, 'mp_restore_50', 'mp', 50, TRUE, 0, 0, 30)
ON CONFLICT (id) DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — CLEANUP: удаляем старые ноды/рёбра диалога 6 и npc_dialogue
-- FK dialogue_edge_{from,to}_node_id → ON DELETE CASCADE — рёбра удалятся сами
-- npc_dialogue удаляем вручную (чтобы добавить новые с conditions)
-- ═══════════════════════════════════════════════════════════════════════════

DELETE FROM public.dialogue_node WHERE dialogue_id = 6;
DELETE FROM public.npc_dialogue   WHERE npc_id      = 6;

-- Очищаем старый флаг received_gift (мог остаться от предыдущей версии диалога)
DELETE FROM public.player_flag WHERE flag_key = 'ruins_dying_stranger.received_gift';

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — DIALOGUE 6: ruins_dying_stranger_main (полное дерево)
--
-- Граф (числа = node_id; → = edge):
--
--   600 line   «Ты… ты всё-таки жив!...»
--     → [continue] (set_flag dialogue_started=true)
--   601 choice_hub (4 варианта)
--     ├→ 602  «Где я?»
--     ├→ 604  «Что случилось с тобой?»
--     ├→ 608  «У тебя есть что-нибудь полезное?»
--     └→ 610  «Мне пора идти.»
--
--   602 line   «Это очень древние руины...»
--     → [continue]
--   603 choice_hub (3 варианта, без «Где я?»)
--     ├→ 604  «Что случилось с тобой?»
--     ├→ 608  «У тебя есть что-нибудь полезное?»
--     └→ 610  «Мне пора идти.»
--
--   604 line   «Нас было трое...»
--     → [continue]
--   605 choice_hub (3 варианта)
--     ├→ 606  «Ты пробовал выбраться?»
--     ├→ 608  «У тебя есть что-нибудь полезное?»
--     └→ 610  «Мне пора идти.»
--
--   606 line   «Судя по всему - дальше тупик...»
--     → [continue]
--   607 choice_hub (2 варианта)
--     ├→ 608  «У тебя есть что-нибудь полезное?»
--     └→ 610  «Мне пора идти.»
--
--   608 line   «*Он с трудом протягивает...*»
--     → [continue]
--   609 choice_hub («Принять» / «Мне пора идти»)
--     ├→ 610  «Принять» (Warrior, condition: flag + class_id=2, action: items + set_flag)
--     ├→ 610  «Принять» (Mage,    condition: flag + class_id=1, action: items + set_flag)
--     └→ 610  «Мне пора идти.» (без actions)
--
--   610 line   «*Раненый искатель закрывает глаза...*»
--     → [continue]
--   611 line   «Надеюсь тебе повезёт больше чем нам...»
--     → [continue]
--   699 end
-- ═══════════════════════════════════════════════════════════════════════════

-- C.1 — Dialogue record (update existing)
UPDATE public.dialogue SET start_node_id = 600 WHERE id = 6;

-- C.2 — Nodes
INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (600, 6, 'line',       6, 'npc.ruins_dying_stranger.greeting',           NULL, NULL, NULL),
    (601, 6, 'choice_hub', 6, 'npc.ruins_dying_stranger.first_hub',           NULL, NULL, NULL),
    (602, 6, 'line',       6, 'npc.ruins_dying_stranger.where_am_i',          NULL, NULL, NULL),
    (603, 6, 'choice_hub', 6, 'npc.ruins_dying_stranger.from_where',          NULL, NULL, NULL),
    (604, 6, 'line',       6, 'npc.ruins_dying_stranger.what_happened',       NULL, NULL, NULL),
    (605, 6, 'choice_hub', 6, 'npc.ruins_dying_stranger.from_what_happened',  NULL, NULL, NULL),
    (606, 6, 'line',       6, 'npc.ruins_dying_stranger.tried_to_escape',     NULL, NULL, NULL),
    (607, 6, 'choice_hub', 6, 'npc.ruins_dying_stranger.from_escape',         NULL, NULL, NULL),
    (608, 6, 'line',       6, 'npc.ruins_dying_stranger.offer_item_text',     NULL, NULL, NULL),
    (609, 6, 'choice_hub', 6, 'npc.ruins_dying_stranger.accept_hub',          NULL, NULL, NULL),
    (610, 6, 'line',       6, 'npc.ruins_dying_stranger.farewell_narrative',  NULL, NULL, NULL),
    (611, 6, 'line',       6, 'npc.ruins_dying_stranger.farewell',            NULL, NULL, NULL),
    (699, 6, 'end',        6, NULL,                                           NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

-- C.3 — Edges
INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    -- 600 → 601: «Продолжить» (устанавливает флаг первого взаимодействия)
    (600, 601, 0, 'ruins_dying_stranger.choice.continue', NULL,
        '[{"type": "set_flag", "key": "ruins_dying_stranger.dialogue_started", "bool_value": true}]'::jsonb,
        FALSE),

    -- 601 (choice_hub): 4 варианта
    (601, 602, 0, 'ruins_dying_stranger.choice.where_am_i',      NULL, NULL, FALSE),
    (601, 604, 1, 'ruins_dying_stranger.choice.what_happened',    NULL, NULL, FALSE),
    (601, 608, 2, 'ruins_dying_stranger.choice.useful_items',     NULL, NULL, FALSE),
    (601, 610, 3, 'ruins_dying_stranger.choice.leave',            NULL, NULL, FALSE),

    -- 602 → 603: «Продолжить»
    (602, 603, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 603 (choice_hub): 3 варианта (без «Где я?»)
    (603, 604, 0, 'ruins_dying_stranger.choice.what_happened', NULL, NULL, FALSE),
    (603, 608, 1, 'ruins_dying_stranger.choice.useful_items',  NULL, NULL, FALSE),
    (603, 610, 2, 'ruins_dying_stranger.choice.leave',         NULL, NULL, FALSE),

    -- 604 → 605: «Продолжить»
    (604, 605, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 605 (choice_hub): 3 варианта
    (605, 606, 0, 'ruins_dying_stranger.choice.tried_to_escape', NULL, NULL, FALSE),
    (605, 608, 1, 'ruins_dying_stranger.choice.useful_items',    NULL, NULL, FALSE),
    (605, 610, 2, 'ruins_dying_stranger.choice.leave',           NULL, NULL, FALSE),

    -- 606 → 607: «Продолжить»
    (606, 607, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 607 (choice_hub): 2 варианта
    (607, 608, 0, 'ruins_dying_stranger.choice.useful_items', NULL, NULL, FALSE),
    (607, 610, 1, 'ruins_dying_stranger.choice.leave',         NULL, NULL, FALSE),

    -- 608 → 609: «Продолжить»
    (608, 609, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 609 (choice_hub): «Принять» / «Мне пора идти»
    -- Warrior accept: Iron Sword + 2 HP potions + 1 Mana Elixir + set_flag
    (609, 610, 0, 'ruins_dying_stranger.choice.accept',
        '[
            {"type": "flag",  "key": "ruins_dying_stranger.received_gift", "eq": false},
            {"type": "class", "class_id": 2}
        ]'::jsonb,
        '[
            {"type": "give_item", "item_id": 1,  "quantity": 1},
            {"type": "give_item", "item_id": 3,  "quantity": 2},
            {"type": "give_item", "item_id": 27, "quantity": 1},
            {"type": "set_flag",  "key": "ruins_dying_stranger.received_gift", "bool_value": true}
        ]'::jsonb,
        TRUE),
    -- Mage accept: Wooden Staff + 2 HP potions + 1 Mana Elixir + set_flag
    (609, 610, 1, 'ruins_dying_stranger.choice.accept',
        '[
            {"type": "flag",  "key": "ruins_dying_stranger.received_gift", "eq": false},
            {"type": "class", "class_id": 1}
        ]'::jsonb,
        '[
            {"type": "give_item", "item_id": 15, "quantity": 1},
            {"type": "give_item", "item_id": 3,  "quantity": 2},
            {"type": "give_item", "item_id": 27, "quantity": 1},
            {"type": "set_flag",  "key": "ruins_dying_stranger.received_gift", "bool_value": true}
        ]'::jsonb,
        TRUE),
    -- «Мне пора идти» — просто прощание без предметов
    (609, 610, 2, 'ruins_dying_stranger.choice.leave', NULL, NULL, FALSE),

    -- 610 → 611: «Продолжить»
    (610, 611, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 611 → 699: «Продолжить» (завершение)
    (611, 699, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — DIALOGUE 7: ruins_dying_stranger_replay (предметы уже взяты)
--
-- Граф:
--   650 line   «*Раненый искатель закрывает глаза...* Надеюсь тебе повезёт...»
--     → [continue]
--   651 choice_hub (1 вариант: «Держись!»)
--     → [держись]
--   652 end
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue (id, slug, version, start_node_id)
VALUES (7, 'ruins_dying_stranger_replay', 1, 650)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (650, 7, 'line',       6, 'npc.ruins_dying_stranger.replay_farewell', NULL, NULL, NULL),
    (651, 7, 'choice_hub', 6, 'npc.ruins_dying_stranger.replay_hub',      NULL, NULL, NULL),
    (652, 7, 'end',        6, NULL,                                       NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    (650, 651, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),
    (651, 652, 0, 'ruins_dying_stranger.choice.hold_on',  NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — DIALOGUE 8: ruins_dying_stranger_items_shortcut (вернулся без предметов)
--
-- Граф:
--   660 line   «*Он с трудом протягивает...* Вот, держи, это всё что у меня есть...»
--     → [continue]
--   661 choice_hub («Принять» / «Мне пора идти»)
--     ├→ 662  «Принять» (Warrior, condition + action: items + set_flag)
--     ├→ 662  «Принять» (Mage,    condition + action: items + set_flag)
--     └→ 662  «Мне пора идти.» (без actions)
--   662 line   «*Раненый искатель закрывает глаза...*»
--     → [continue]
--   663 line   «Надеюсь тебе повезёт больше чем нам...»
--     → [continue]
--   669 end
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.dialogue (id, slug, version, start_node_id)
VALUES (8, 'ruins_dying_stranger_items_shortcut', 1, 660)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_node
    (id, dialogue_id, type, speaker_npc_id, client_node_key, condition_group, action_group, jump_target_node_id)
OVERRIDING SYSTEM VALUE
VALUES
    (660, 8, 'line',       6, 'npc.ruins_dying_stranger.offer_item_shortcut', NULL, NULL, NULL),
    (661, 8, 'choice_hub', 6, 'npc.ruins_dying_stranger.accept_hub',          NULL, NULL, NULL),
    (662, 8, 'line',       6, 'npc.ruins_dying_stranger.farewell_narrative',  NULL, NULL, NULL),
    (663, 8, 'line',       6, 'npc.ruins_dying_stranger.farewell',            NULL, NULL, NULL),
    (669, 8, 'end',        6, NULL,                                           NULL, NULL, NULL)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    -- 660 → 661: «Продолжить»
    (660, 661, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 661 (choice_hub): «Принять» / «Мне пора идти»
    -- Warrior accept
    (661, 662, 0, 'ruins_dying_stranger.choice.accept',
        '[
            {"type": "flag",  "key": "ruins_dying_stranger.received_gift", "eq": false},
            {"type": "class", "class_id": 2}
        ]'::jsonb,
        '[
            {"type": "give_item", "item_id": 1,  "quantity": 1},
            {"type": "give_item", "item_id": 3,  "quantity": 2},
            {"type": "give_item", "item_id": 27, "quantity": 1},
            {"type": "set_flag",  "key": "ruins_dying_stranger.received_gift", "bool_value": true}
        ]'::jsonb,
        TRUE),
    -- Mage accept
    (661, 662, 1, 'ruins_dying_stranger.choice.accept',
        '[
            {"type": "flag",  "key": "ruins_dying_stranger.received_gift", "eq": false},
            {"type": "class", "class_id": 1}
        ]'::jsonb,
        '[
            {"type": "give_item", "item_id": 15, "quantity": 1},
            {"type": "give_item", "item_id": 3,  "quantity": 2},
            {"type": "give_item", "item_id": 27, "quantity": 1},
            {"type": "set_flag",  "key": "ruins_dying_stranger.received_gift", "bool_value": true}
        ]'::jsonb,
        TRUE),
    -- «Мне пора идти»
    (661, 662, 2, 'ruins_dying_stranger.choice.leave', NULL, NULL, FALSE),

    -- 662 → 663: «Продолжить»
    (662, 663, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE),

    -- 663 → 669: «Продолжить» (завершение)
    (663, 669, 0, 'ruins_dying_stranger.choice.continue', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — NPC → DIALOGUE MAPPINGS (condition-based selection)
--
--   priority 2: dialogue_started=true AND received_gift=true → replay (id=7)
--   priority 1: dialogue_started=true AND received_gift=false → shortcut (id=8)
--   priority 0: catch-all (нет условий) → main (id=6)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.npc_dialogue (npc_id, dialogue_id, priority, condition_group)
VALUES
    (6, 7, 2, '[
        {"type": "flag", "key": "ruins_dying_stranger.dialogue_started", "eq": true},
        {"type": "flag", "key": "ruins_dying_stranger.received_gift",    "eq": true}
    ]'::jsonb),
    (6, 8, 1, '[
        {"type": "flag", "key": "ruins_dying_stranger.dialogue_started", "eq": true},
        {"type": "flag", "key": "ruins_dying_stranger.received_gift",    "eq": false}
    ]'::jsonb),
    (6, 6, 0, NULL)
ON CONFLICT DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART G — SEQUENCE UPDATES
-- ═══════════════════════════════════════════════════════════════════════════

SELECT setval('public.items_id_seq',             27, true);
SELECT setval('public.item_use_effects_id_seq',   3, true);
SELECT setval('public.dialogue_id_seq',           8, true);
SELECT setval('public.dialogue_node_id_seq',    699, true);
SELECT setval('public.dialogue_edge_id_seq', (SELECT MAX(id) FROM public.dialogue_edge), true);

-- =============================================================================
-- LOCALISATION KEYS (for client-side implementation)
-- =============================================================================
--
-- NPC lines (client_node_key):
--   npc.ruins_dying_stranger.greeting            «Ты… ты всё-таки жив! Я видел, как этот камень
--                                                  выплюнул тебя из света. Не знаю, кто ты… но
--                                                  руины уже заметили тебя.»
--   npc.ruins_dying_stranger.where_am_i          «Это очень древние руины... Местные зовут это
--                                                  место Кладбище великих. Сюда не приходят...
--                                                  Сюда пропадают...»
--   npc.ruins_dying_stranger.what_happened       «Нас было трое. Мы обычные авантюристы. Чёрт нас
--                                                  поволок в эти руины в поисках сокровищ... Потом
--                                                  из темноты начали звать наши имена...»
--   npc.ruins_dying_stranger.tried_to_escape     «Судя по всему - дальше тупик... У меня не хватает
--                                                  сил подняться... Но у тебя ещё есть шанс...»
--   npc.ruins_dying_stranger.offer_item_text     «*Он с трудом протягивает тебе старое оружие
--                                                  и два небольших флакона.*»
--                                                  (NPC text: «Вот, держи, это всё что у меня есть...»)
--   npc.ruins_dying_stranger.offer_item_shortcut  «*Он с трудом протягивает тебе старое оружие
--                                                    и два небольших флакона.* Вот, держи, это всё
--                                                    что у меня есть...» (для shortcut-диалога)
--   npc.ruins_dying_stranger.farewell_narrative  «*Раненый искатель закрывает глаза. Его голос
--                                                  становится почти шёпотом.*»
--   npc.ruins_dying_stranger.farewell            «Надеюсь тебе повезёт больше чем нам...»
--   npc.ruins_dying_stranger.replay_farewell     «*Раненый искатель закрывает глаза. Его голос
--                                                  становится почти шёпотом.* Надеюсь тебе повезёт
--                                                  больше чем нам...» (для replay-диалога)
--
-- Choice hubs (client_node_key — UI-заголовки, опционально):
--   npc.ruins_dying_stranger.first_hub           4 варианта: «Где я?», «Что случилось?» и т.д.
--   npc.ruins_dying_stranger.from_where          3 варианта (без «Где я?»)
--   npc.ruins_dying_stranger.from_what_happened  3 варианта (с «Ты пробовал выбраться?»)
--   npc.ruins_dying_stranger.from_escape         2 варианта
--   npc.ruins_dying_stranger.accept_hub          «Принять» / «Мне пора идти»
--   npc.ruins_dying_stranger.replay_hub          «Держись!»
--
-- Choice buttons (client_choice_key — текст кнопок):
--   ruins_dying_stranger.choice.continue          «Продолжить»
--   ruins_dying_stranger.choice.where_am_i        «Где я?»
--   ruins_dying_stranger.choice.what_happened     «Что случилось с тобой?»
--   ruins_dying_stranger.choice.useful_items      «У тебя есть что-нибудь полезное?»
--   ruins_dying_stranger.choice.leave             «Мне пора идти.»
--   ruins_dying_stranger.choice.tried_to_escape   «Ты пробовал выбраться?»
--   ruins_dying_stranger.choice.accept            «Принять»
--   ruins_dying_stranger.choice.hold_on           «Держись!»
-- =============================================================================
