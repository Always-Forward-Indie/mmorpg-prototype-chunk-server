-- =============================================================================
-- Migration 063: Fix vendor shops for all NPCs
-- =============================================================================
-- Исправляет торговые окна (open_vendor_shop) для всех NPC деревни.
--
-- • Milaya (npc_id=2): новый vendor_npc (зелья: Health + Mana Elixir + Bread)
-- • Edrik (npc_id=3): новый vendor_npc (книги воина) + опция в диалоге
-- • Theron (npc_id=4): чиним vendor_inventory (оружие/броня вместо книг)
-- • Sylara (npc_id=5): добавляем опцию «Покажи тома» (книги мага)
-- • Varan (npc_id=1): меняем ассортимент на ресурсы
-- • Фикс C++ SQL: JOIN vendor_npc вместо surrogate ID (уже сделан в Database.cpp)
-- =============================================================================

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — THERON (npc_id=4): удаляем книги воина, добавляем оружие/броню
-- vendor_npc_id для Theron = 2 (существующий)
-- ═══════════════════════════════════════════════════════════════════════════

DELETE FROM public.vendor_inventory WHERE vendor_npc_id = 2;

INSERT INTO public.vendor_inventory
    (vendor_npc_id, item_id, stock_count, stock_max, restock_amount, restock_interval_sec)
VALUES
    (2, 1,  -1, -1, 0, 3600),   -- Iron Sword
    (2, 2,  -1, -1, 0, 3600),   -- Wooden Shield
    (2, 15, -1, -1, 0, 3600);   -- Wooden Staff

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — VARAN (npc_id=1): меняем оружие/броню на ресурсы
-- vendor_npc_id для Varan = 1 (существующий)
-- ═══════════════════════════════════════════════════════════════════════════

DELETE FROM public.vendor_inventory WHERE vendor_npc_id = 1;

INSERT INTO public.vendor_inventory
    (vendor_npc_id, item_id, stock_count, stock_max, restock_amount, restock_interval_sec)
VALUES
    (1, 6,  -1, -1, 0, 3600),   -- Iron Ore
    (1, 7,  -1, -1, 0, 3600),   -- Small Animal Bone
    (1, 9,  -1, -1, 0, 3600),   -- Small Animal Skin
    (1, 10, -1, -1, 0, 3600),   -- Animal Fat
    (1, 11, -1, -1, 0, 3600),   -- Animal Blood
    (1, 12, -1, -1, 0, 3600),   -- Animal Meat
    (1, 13, -1, -1, 0, 3600),   -- Animal Fang
    (1, 14, -1, -1, 0, 3600);   -- Animal Eye

-- ═══════════════════════════════════════════════════════════════════════════
-- PART C — MILAYA (npc_id=2): новый vendor_npc + зелья
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.vendor_npc (id, npc_id, markup_pct)
OVERRIDING SYSTEM VALUE
VALUES (4, 2, 5)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.vendor_inventory
    (vendor_npc_id, item_id, stock_count, stock_max, restock_amount, restock_interval_sec)
VALUES
    (4, 3,  -1, -1, 0, 3600),   -- Health Potion
    (4, 27, -1, -1, 0, 3600),   -- Small Mana Elixir
    (4, 4,  -1, -1, 0, 3600);   -- Bread

-- ═══════════════════════════════════════════════════════════════════════════
-- PART D — EDRIK (npc_id=3): новый vendor_npc + книги воина + опция в диалоге
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.vendor_npc (id, npc_id, markup_pct)
OVERRIDING SYSTEM VALUE
VALUES (5, 3, 0)
ON CONFLICT (id) DO NOTHING;

INSERT INTO public.vendor_inventory
    (vendor_npc_id, item_id, stock_count, stock_max, restock_amount, restock_interval_sec)
VALUES
    (5, 18, -1, -1, 0, 3600),   -- Tome of Shield Bash
    (5, 20, -1, -1, 0, 3600),   -- Tome of Iron Skin
    (5, 21, -1, -1, 0, 3600),   -- Tome of Constitution Mastery
    (5, 19,  1,  1, 1, 86400);  -- Tome of Whirlwind (редкая, 1 шт в сутки)

-- Добавляем в диалог Edrik опцию «Покажи учебники» (vendor shop с книгами)
-- Меняем edge skill_shop (order=0) и добавляем book_shop (order=1), farewell (order=2)

DELETE FROM public.dialogue_edge WHERE from_node_id = 302 AND to_node_id = 399;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    -- «Я хочу изучить новые боевые навыки.» → открывает окно изучения навыков
    (302, 399, 0, 'edrik.choice.skills', NULL, '{"actions": [{"type": "open_skill_shop"}]}'::jsonb, FALSE),
    -- «Покажи учебники.» → открывает окно торговли (книги воина)
    (302, 399, 1, 'edrik.choice.books',  NULL, '{"actions": [{"type": "open_vendor_shop"}]}'::jsonb, FALSE),
    -- «Я вернусь позже.» → выход
    (302, 399, 2, 'edrik.choice.farewell', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART E — SYLARA (npc_id=5): добавляем опцию «Покажи тома» (vendor shop)
-- vendor_npc_id для Sylara = 3 (существующий, уже торгует книгами)
-- ═══════════════════════════════════════════════════════════════════════════

-- Меняем edge skill_shop (order=0), добавляем book_shop (order=1), farewell (order=2)

DELETE FROM public.dialogue_edge WHERE from_node_id = 502 AND to_node_id = 599;

INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key, condition_group, action_group, hide_if_locked)
VALUES
    -- «Я хочу изучить новые заклинания.» → открывает окно изучения навыков
    (502, 599, 0, 'sylara.choice.skills',  NULL, '{"actions": [{"type": "open_skill_shop"}]}'::jsonb, FALSE),
    -- «Покажи тома.» → открывает окно торговли (книги мага)
    (502, 599, 1, 'sylara.choice.books',   NULL, '{"actions": [{"type": "open_vendor_shop"}]}'::jsonb, FALSE),
    -- «Я вернусь позже.» → выход
    (502, 599, 2, 'sylara.choice.farewell', NULL, NULL, FALSE);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART F — FIX TRAINER MAPPINGS
-- Theron (npc_id=4) — кузнец, НЕ тренер (только trade + repair)
-- Edrik  (npc_id=3) — тренер воинов (skill shop + книги)
-- ═══════════════════════════════════════════════════════════════════════════

DELETE FROM public.npc_trainer_class WHERE npc_id = 4;
INSERT INTO public.npc_trainer_class (npc_id, class_id) VALUES (3, 2)
ON CONFLICT DO NOTHING;

-- ═══════════════════════════════════════════════════════════════════════════
-- PART G — SEQUENCE UPDATES
-- ═══════════════════════════════════════════════════════════════════════════

SELECT setval('public.vendor_npc_id_seq',      (SELECT MAX(id) FROM public.vendor_npc),      true);
SELECT setval('public.vendor_inventory_id_seq', (SELECT MAX(id) FROM public.vendor_inventory), true);
SELECT setval('public.dialogue_edge_id_seq',    (SELECT MAX(id) FROM public.dialogue_edge),    true);

-- =============================================================================
-- NEW LOCALISATION KEYS
-- =============================================================================
--   edrik.choice.books       «Покажи учебники.»
--   sylara.choice.books      «Покажи тома.»
-- =============================================================================
