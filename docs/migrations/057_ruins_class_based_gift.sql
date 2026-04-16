-- =============================================================================
-- Migration 057: Ruins Dying Stranger — class-based gift
-- =============================================================================
-- Разбивает единое ребро "accept" диалога ruins_dying_stranger_main (edge 137)
-- на два классовых:
--
--   Warrior (class_id=2) → Iron Sword (id=1) + Health Potion × 2 (id=3)
--   Mage    (class_id=1) → Wooden Staff (id=15) + Health Potion × 2 (id=3)
--
-- Оба ребра имеют:
--   • condition_group: [flag received_gift=false, class = <id>]
--   • action_group: give_item × 2 + set_flag received_gift=true
--   • hide_if_locked=TRUE  (исчезают из диалога после первого получения)
--
-- Ребро decline (id=138) сдвигается на order_index=2.
--
-- После применения граф 603 выглядит так:
--   order 0: accept (warrior) — Iron Sword + 2 Potions
--   order 1: accept (mage)    — Wooden Staff + 2 Potions
--   order 2: decline
-- =============================================================================

-- ─────────────────────────────────────────────────────────────────────────────
-- STEP 1 — обновить ребро 137 (было: все классы → теперь: только Warrior)
-- ─────────────────────────────────────────────────────────────────────────────
UPDATE public.dialogue_edge
SET condition_group = '[
    {"type": "flag",  "key": "ruins_dying_stranger.received_gift", "eq": false},
    {"type": "class", "class_id": 2}
]'::jsonb
WHERE id = 137;

-- ─────────────────────────────────────────────────────────────────────────────
-- STEP 2 — сдвинуть ребро decline (id=138) на order_index=2
--          để освободить место для нового мажьего ребра
-- ─────────────────────────────────────────────────────────────────────────────
UPDATE public.dialogue_edge
SET order_index = 2
WHERE id = 138;

-- ─────────────────────────────────────────────────────────────────────────────
-- STEP 3 — вставить ребро для Mage (order_index=1)
--          Wooden Staff (id=15) + Health Potion × 2 (id=3)
-- ─────────────────────────────────────────────────────────────────────────────
INSERT INTO public.dialogue_edge
    (from_node_id, to_node_id, order_index, client_choice_key,
     condition_group, action_group, hide_if_locked)
VALUES
    (603, 605, 1, 'ruins_dying_stranger.choice.accept',
        '[
            {"type": "flag",  "key": "ruins_dying_stranger.received_gift", "eq": false},
            {"type": "class", "class_id": 1}
        ]'::jsonb,
        '[
            {"type": "give_item", "item_id": 15, "quantity": 1},
            {"type": "give_item", "item_id": 3,  "quantity": 2},
            {"type": "set_flag",  "key": "ruins_dying_stranger.received_gift", "bool_value": true}
        ]'::jsonb,
        TRUE);

-- Sync sequence
SELECT setval('dialogue_edge_id_seq', (SELECT MAX(id) FROM public.dialogue_edge), true);
