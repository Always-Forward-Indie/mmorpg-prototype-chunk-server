-- Migration 046: Skill Book Items
-- Adds 9 skill-book items (item_type=10) and updates class_skill_tree FKs.
-- Must be applied AFTER migrations 043-045.

-- ═══════════════════════════════════════════════════════════════════════════
-- PART A — INSERT SKILL BOOKS (OVERRIDING SYSTEM VALUE because IDs are
--           referenced by class_skill_tree FKs in the same migration)
-- Rarity: 2=Uncommon, 3=Rare, 4=Epic
-- is_usable=TRUE  — books are "used" to learn skills (server validates conditions)
-- is_tradable=TRUE so they can be picked from loot and sold to other players
-- vendor_price_buy=0 means NOT sold by any vendor (drop/quest only)
-- ═══════════════════════════════════════════════════════════════════════════

INSERT INTO public.items
    (id, name, slug, description, is_quest_item, item_type, weight,
     rarity_id, stack_max, is_container, is_durable, is_tradable,
     durability_max, vendor_price_buy, vendor_price_sell,
     equip_slot, level_requirement, is_equippable, is_harvest, is_usable)
OVERRIDING SYSTEM VALUE
VALUES
    -- ── Warrior Books ────────────────────────────────────────────────────────

    -- 18: Tome of Shield Bash — sold by trainer, uncommon
    (18, 'Tome of Shield Bash',           'tome_shield_bash',
     'A worn training manual describing the technique of Shield Bash.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 500, 100,    NULL, 5, FALSE, FALSE, TRUE),

    -- 19: Tome of Whirlwind — drop-only, rare (NOT sold by vendor)
    (19, 'Tome of Whirlwind',             'tome_whirlwind',
     'An ancient scroll detailing the devastating Whirlwind technique. Rare.',
     FALSE, 10, 0.3, 3 /*rare*/,     1, FALSE, FALSE, TRUE,
     100,   0,  50, NULL, 10, FALSE, FALSE, TRUE),

    -- 20: Tome of Iron Skin — sold by trainer, uncommon
    (20, 'Tome of Iron Skin',             'tome_iron_skin',
     'Teachings of hardening the body through relentless training.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 400,  80, NULL,  5, FALSE, FALSE, TRUE),

    -- 21: Tome of Constitution Mastery — sold by trainer, uncommon
    (21, 'Tome of Constitution Mastery',  'tome_constitution_mastery',
     'A guide to unlocking the body''s true enduring potential.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 800, 160, NULL,  8, FALSE, FALSE, TRUE),

    -- ── Mage Books ───────────────────────────────────────────────────────────

    -- 22: Tome of Frost Bolt — sold by trainer, uncommon
    (22, 'Tome of Frost Bolt',            'tome_frost_bolt',
     'Basic arcane theory behind channelling cold into a bolt of frost.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 500, 100, NULL,  5, FALSE, FALSE, TRUE),

    -- 23: Tome of Arcane Blast — sold by trainer, uncommon
    (23, 'Tome of Arcane Blast',          'tome_arcane_blast',
     'Concentrated arcane theory on focusing raw magical energy into a blast.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 900, 180, NULL,  8, FALSE, FALSE, TRUE),

    -- 24: Tome of Chain Lightning — drop/quest only, epic
    (24, 'Tome of Chain Lightning',       'tome_chain_lightning',
     'A legendary scroll describing storm magic that arcs between enemies. Extremely rare.',
     FALSE, 10, 0.3, 4 /*epic*/,     1, FALSE, FALSE, TRUE,
     100,   0, 250, NULL, 12, FALSE, FALSE, TRUE),

    -- 25: Tome of Mana Shield — sold by trainer, uncommon
    (25, 'Tome of Mana Shield',           'tome_mana_shield',
     'Teachings on weaving mana into a protective aura.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 600, 120, NULL,  5, FALSE, FALSE, TRUE),

    -- 26: Tome of Elemental Mastery — sold by trainer, uncommon
    (26, 'Tome of Elemental Mastery',     'tome_elemental_mastery',
     'A comprehensive guide to attaining mastery over elemental forces.',
     FALSE, 10, 0.3, 2 /*uncommon*/, 1, FALSE, FALSE, TRUE,
     100, 1200, 240, NULL, 10, FALSE, FALSE, TRUE)

ON CONFLICT (id) DO NOTHING;

-- Advance the identity sequence past id=26 so next auto-insert gets id=27
SELECT setval(pg_get_serial_sequence('public.items', 'id'), 26, true);

-- ═══════════════════════════════════════════════════════════════════════════
-- PART B — BIND BOOKS TO CLASS SKILL TREE
-- (Previously set to NULL to avoid FK violation before items existed)
-- ═══════════════════════════════════════════════════════════════════════════

-- Warrior: Whirlwind requires Tome of Whirlwind (id=19)
UPDATE public.class_skill_tree
SET skill_book_item_id = 19
WHERE class_id = 2 AND skill_id = 5;  -- whirlwind

-- Mage: Chain Lightning requires Tome of Chain Lightning (id=24)
UPDATE public.class_skill_tree
SET skill_book_item_id = 24
WHERE class_id = 1 AND skill_id = 10;  -- chain_lightning
