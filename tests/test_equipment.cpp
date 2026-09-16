// Unit tests for EquipmentManager (Inventory + Item + CharacterManager, Logger).
#include "services/CharacterManager.hpp"
#include "services/EquipmentManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>

namespace
{

struct EquipFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    CharacterManager chars{logger};
    EquipmentManager eq{inv, items, chars, logger};
    GameConfigService cfg{logger};

    void SetUp() override
    {
        ItemDataStruct gold;
        gold.id = 999;
        gold.slug = "gold_coin";
        gold.stackMax = 1000000;
        ItemDataStruct sword;
        sword.id = 200;
        sword.slug = "iron_sword";
        sword.stackMax = 1;
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        sword.levelRequirement = 5;
        ItemDataStruct greatsword;
        greatsword.id = 201;
        greatsword.slug = "greatsword";
        greatsword.stackMax = 1;
        greatsword.isEquippable = true;
        greatsword.equipSlotSlug = "main_hand";
        greatsword.isTwoHanded = true;
        ItemDataStruct shield;
        shield.id = 202;
        shield.slug = "shield";
        shield.stackMax = 1;
        shield.isEquippable = true;
        shield.equipSlotSlug = "off_hand";
        ItemDataStruct epic;
        epic.id = 203;
        epic.slug = "epic_blade";
        epic.stackMax = 1;
        epic.isEquippable = true;
        epic.equipSlotSlug = "main_hand";
        epic.levelRequirement = 99;
        items.setItemsList({gold, sword, greatsword, shield, epic});

        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 100;
        chars.addCharacter(c);

        eq.setGameConfigService(&cfg);
    }

    int giveSynced(int itemId, int64_t rowId)
    {
        EXPECT_TRUE(inv.addItemToInventory(1, itemId, 1));
        inv.updateInventoryItemId(1, itemId, rowId);
        return static_cast<int>(rowId);
    }
};

} // namespace

TEST_F(EquipFixture, EquipUnequipRoundtrip)
{
    int row = giveSynced(200, 11);
    auto r = eq.equipItem(1, row);
    EXPECT_EQ(r.error, EquipmentManager::EquipError::NONE);
    EXPECT_EQ(r.equipSlotSlug, "main_hand");
    EXPECT_EQ(eq.findSlotForItemId(1, 200)->first, "main_hand");
    auto state = eq.getEquipmentState(1);
    (void)state;
    auto json = eq.buildEquipmentStateJson(1);
    EXPECT_FALSE(json.is_null());

    auto u = eq.unequipItem(1, "main_hand");
    EXPECT_EQ(u.error, EquipmentManager::UnequipError::NONE);
    EXPECT_EQ(u.inventoryItemId, row);
    EXPECT_FALSE(eq.findSlotForItemId(1, 200).has_value());
    auto u2 = eq.unequipItem(1, "main_hand");
    EXPECT_EQ(u2.error, EquipmentManager::UnequipError::SLOT_EMPTY);
}

TEST_F(EquipFixture, LevelRequirementEnforced)
{
    int row = giveSynced(203, 12);
    auto r = eq.equipItem(1, row);
    EXPECT_EQ(r.error, EquipmentManager::EquipError::LEVEL_REQUIREMENT_NOT_MET);
}

TEST_F(EquipFixture, UnknownRowFails)
{
    auto r = eq.equipItem(1, 424242);
    EXPECT_EQ(r.error, EquipmentManager::EquipError::ITEM_NOT_IN_INVENTORY);
}

TEST_F(EquipFixture, TwoHandedBlocksOffHand)
{
    int gs = giveSynced(201, 13);
    auto r = eq.equipItem(1, gs);
    EXPECT_EQ(r.error, EquipmentManager::EquipError::NONE);
    int sh = giveSynced(202, 14);
    auto r2 = eq.equipItem(1, sh);
    EXPECT_EQ(r2.error, EquipmentManager::EquipError::SLOT_BLOCKED_BY_TWO_HANDED);
}

TEST_F(EquipFixture, BuildFromInventoryAndClear)
{
    int row = giveSynced(200, 15);
    inv.setItemEquipped(1, row, true);
    eq.buildFromInventory(1);
    EXPECT_TRUE(eq.findSlotForItemId(1, 200).has_value());
    eq.clearCharacter(1);
    EXPECT_FALSE(eq.findSlotForItemId(1, 200).has_value());
}

TEST_F(EquipFixture, CarryWeightLimit)
{
    cfg.setConfig({{"carry_weight.base", "100"}, {"carry_weight.per_strength", "5"}});
    CharacterAttributeStruct a;
    a.slug = "strength";
    a.value = 10;
    chars.replaceCharacterAttributes(1, {a});
    EXPECT_FLOAT_EQ(eq.getCarryWeightLimit(1), 150.0f);
}
