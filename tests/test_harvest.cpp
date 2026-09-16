// Unit tests for HarvestManager corpse registry + validation
// (ItemManager + Logger only; loot rolls covered by LootManager tests).
#include "services/HarvestManager.hpp"
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>

namespace
{

PositionStruct pos(float x, float y, float z = 0.0f)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    p.positionZ = z;
    return p;
}

struct HarvestFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    HarvestManager harvest{items, logger};

    void SetUp() override
    {
        ItemDataStruct hide;
        hide.id = 300;
        hide.slug = "wolf_hide";
        items.setItemsList({hide});
        harvest.registerCorpse(1001, 7, pos(500, 500));
    }
};

} // namespace

TEST_F(HarvestFixture, CorpseRegisteredAndFound)
{
    auto c = harvest.getCorpseByUID(1001);
    EXPECT_EQ(c.mobUID, 1001);
    EXPECT_EQ(c.mobId, 7);
    EXPECT_EQ(harvest.getCorpseByUID(424242).mobUID, 0); // unknown -> empty
}

TEST_F(HarvestFixture, NearbySearchRespectsRadius)
{
    auto near = harvest.getHarvestableCorpsesNearPosition(pos(500, 500), 300.0f);
    EXPECT_EQ(near.size(), 1u);
    auto far = harvest.getHarvestableCorpsesNearPosition(pos(50000, 50000), 300.0f);
    EXPECT_TRUE(far.empty());
}

TEST_F(HarvestFixture, ValidateHarvestDistance)
{
    auto ok = harvest.validateHarvest(1, 1001, pos(500, 500));
    EXPECT_TRUE(ok.isValid);
    auto tooFar = harvest.validateHarvest(1, 1001, pos(50000, 50000));
    EXPECT_FALSE(tooFar.isValid);
    EXPECT_FALSE(tooFar.failureReason.empty());
    auto unknown = harvest.validateHarvest(1, 424242, pos(500, 500));
    EXPECT_FALSE(unknown.isValid);
}

TEST_F(HarvestFixture, StartCancelHarvestState)
{
    EXPECT_FALSE(harvest.isCharacterHarvesting(1));
    EXPECT_TRUE(harvest.startHarvest(1, 1001, pos(500, 500)));
    EXPECT_TRUE(harvest.isCharacterHarvesting(1));
    auto prog = harvest.getHarvestProgress(1);
    EXPECT_EQ(prog.corpseUID, 1001);
    EXPECT_TRUE(prog.isActive);
    harvest.cancelHarvest(1, "moved");
    EXPECT_FALSE(harvest.isCharacterHarvesting(1));
    harvest.cancelHarvest(1, "idempotent"); // must not crash
}

TEST_F(HarvestFixture, StartHarvestFailsWhenInvalid)
{
    EXPECT_FALSE(harvest.startHarvest(1, 424242, pos(500, 500))); // no corpse
    EXPECT_FALSE(harvest.startHarvest(1, 1001, pos(50000, 50000))); // too far
    EXPECT_FALSE(harvest.isCharacterHarvesting(1));
}

TEST_F(HarvestFixture, CompleteBeforeDurationYieldsNothing)
{
    // Harvest takes ~3s: completing immediately yields nothing and the
    // session stays active (duration gate, not an error).
    EXPECT_TRUE(harvest.startHarvest(1, 1001, pos(500, 500)));
    auto loot = harvest.completeHarvestAndGenerateLoot(1);
    EXPECT_TRUE(loot.empty());
    EXPECT_TRUE(harvest.isCharacterHarvesting(1));
    harvest.cancelHarvest(1, "test done");
    EXPECT_FALSE(harvest.isCharacterHarvesting(1));
}

TEST_F(HarvestFixture, CleanupOldCorpses)
{
    harvest.registerCorpse(1002, 8, pos(600, 600));
    EXPECT_EQ(harvest.getCorpseByUID(1002).mobUID, 1002);
    harvest.cleanupOldCorpses(0); // everything older than 0s goes
    EXPECT_EQ(harvest.getCorpseByUID(1001).mobUID, 0);
    EXPECT_EQ(harvest.getCorpseByUID(1002).mobUID, 0);
}

TEST_F(HarvestFixture, CorpseLootEmptyByDefault)
{
    EXPECT_TRUE(harvest.getCorpseLoot(1001).empty());
    EXPECT_FALSE(harvest.corpseHasLoot(1001));
}

TEST_F(HarvestFixture, CleanupSkipsActivelyHarvestedCorpse)
{
    // Regression (L3 test_corpse_return_within_ttl): cleanup deleted a corpse
    // 1s after harvestStart (TTL expired mid-harvest) -> completion failed with
    // "Corpse not found" and the client waited with no signal.
    EXPECT_TRUE(harvest.startHarvest(1, 1001, pos(500, 500)));
    harvest.cleanupOldCorpses(0); // everything expired, but harvest is active
    EXPECT_EQ(harvest.getCorpseByUID(1001).mobUID, 1001); // survives
    EXPECT_TRUE(harvest.isCharacterHarvesting(1));
    harvest.cancelHarvest(1, "test done");
    harvest.cleanupOldCorpses(0); // no active harvest anymore -> gone
    EXPECT_EQ(harvest.getCorpseByUID(1001).mobUID, 0);
}
