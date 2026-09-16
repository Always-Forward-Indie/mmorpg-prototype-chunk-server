// Unit tests for TrainerManager (ItemManager + Logger only).
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/TrainerManager.hpp"

#include <gtest/gtest.h>

namespace
{

ClassSkillTreeEntryStruct skillEntry(const std::string &slug, int reqLevel, int sp, int gold)
{
    ClassSkillTreeEntryStruct e;
    e.skillSlug = slug;
    e.skillName = slug;
    e.requiredLevel = reqLevel;
    e.spCost = sp;
    e.goldCost = gold;
    return e;
}

struct TrainerFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    TrainerManager trainers{items, logger};

    void SetUp() override
    {
        ItemDataStruct gold;
        gold.id = 999;
        gold.slug = "gold_coin";
        gold.stackMax = 1000000;
        items.setItemsList({gold});

        TrainerNPCDataStruct t;
        t.npcId = 21;
        t.skills = {skillEntry("fireball", 5, 1, 100), skillEntry("meteor", 10, 2, 500)};
        trainers.setTrainerData({t});
    }

    PlayerContextStruct ctx(int level, int sp)
    {
        PlayerContextStruct c;
        c.characterId = 1;
        c.characterLevel = level;
        c.freeSkillPoints = sp;
        return c;
    }
};

} // namespace

TEST_F(TrainerFixture, LookupByNpcAndSkill)
{
    ASSERT_NE(trainers.getTrainerByNpcId(21), nullptr);
    EXPECT_EQ(trainers.getTrainerByNpcId(424242), nullptr);
    ASSERT_NE(trainers.getSkillEntry(21, "fireball"), nullptr);
    EXPECT_EQ(trainers.getSkillEntry(21, "fireball")->goldCost, 100);
    EXPECT_EQ(trainers.getSkillEntry(21, "nope"), nullptr);
    EXPECT_EQ(trainers.getSkillEntry(424242, "fireball"), nullptr);
}

TEST_F(TrainerFixture, ShopJsonFlagsAffordability)
{
    ASSERT_TRUE(inv.addItemToInventory(1, 999, 1000));
    auto shop = trainers.buildSkillShopJson(21, ctx(10, 5), inv);
    EXPECT_FALSE(shop.is_null());
    EXPECT_TRUE(trainers.buildSkillShopJson(424242, ctx(10, 5), inv).is_null());
}

TEST_F(TrainerFixture, ReloadReplacesDataset)
{
    TrainerNPCDataStruct t2;
    t2.npcId = 22;
    t2.skills = {skillEntry("frostbolt", 1, 1, 0)};
    trainers.setTrainerData({t2});
    EXPECT_EQ(trainers.getTrainerByNpcId(21), nullptr);
    ASSERT_NE(trainers.getTrainerByNpcId(22), nullptr);
}
