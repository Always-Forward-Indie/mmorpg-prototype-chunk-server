// Unit tests for PlayerDeathPipeline (real Character/Experience/Durability/
// Config/Zone managers + Logger; stats/analytics captured via seams).
#include "services/PlayerDeathPipeline.hpp"
#include "services/CharacterManager.hpp"
#include "services/DurabilityService.hpp"
#include "services/ExperienceCacheManager.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

struct DeathFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    ExperienceCacheManager expCache{logger};
    ExperienceManager experience{chars, expCache, nullptr, nullptr, logger};
    ItemManager items{logger};
    InventoryManager inventory{items, logger};
    GameConfigService config{logger};
    DurabilityService durability{inventory, items, config, logger};
    GameZoneManager gameZones{logger};
    PlayerDeathPipeline deaths{chars, experience, durability, gameZones, logger};

    std::vector<int> statsUpdated;
    std::vector<std::string> analytics;

    void SetUp() override
    {
        config.setConfig({
            {"durability.death_penalty_pct", "0.05"},
            {"durability.tier1_threshold_pct", "0.75"},
            {"durability.tier2_threshold_pct", "0.50"},
            {"durability.tier3_threshold_pct", "0.25"},
        });
        deaths.setStatsUpdateCallback([this](int cid) { statsUpdated.push_back(cid); });
        deaths.setAnalyticsCallback([this](const std::string &pkt) { analytics.push_back(pkt); });

        ItemDataStruct sword;
        sword.id = 200;
        sword.slug = "iron_sword";
        sword.stackMax = 1;
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        sword.isDurable = true;
        sword.durabilityMax = 100;
        items.setItemsList({sword});
    }

    CharacterDataStruct deadChar(int id, int level, int xp)
    {
        CharacterDataStruct c;
        c.characterId = id;
        c.characterLevel = level;
        c.characterExperiencePoints = xp;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 0;
        c.characterMaxMana = 50;
        c.characterCurrentMana = 0;
        c.isDead = true;
        c.sessionId = "sess_1";
        return c;
    }

    void giveSword(int charId, int rowId)
    {
        EXPECT_TRUE(inventory.addItemToInventory(charId, 200, 1));
        inventory.updateInventoryItemId(charId, 200, rowId);
        inventory.setItemEquipped(charId, rowId, true);
    }
};

} // namespace

TEST_F(DeathFixture, AliveGuardSkips)
{
    CharacterDataStruct c = deadChar(1, 5, 500);
    c.isDead = false;
    c.characterCurrentHealth = 50;
    chars.addCharacter(c);
    giveSword(1, 11);
    EXPECT_FALSE(deaths.execute(1));
    EXPECT_EQ(chars.getCharacterData(1).experienceDebt, 0);
    EXPECT_TRUE(statsUpdated.empty());
    EXPECT_TRUE(analytics.empty());
}

TEST_F(DeathFixture, DebtDurabilitySnapshotAnalytics)
{
    chars.addCharacter(deadChar(1, 5, 500));
    giveSword(1, 11);
    EXPECT_TRUE(deaths.execute(1));
    // penalty = min(round(500*0.1), 500-364) = 50
    EXPECT_EQ(chars.getCharacterData(1).experienceDebt, 50);
    // death durability: ceil(100*0.05) = 5
    int dur = -1;
    for (const auto &s : inventory.getEquippedItems(1))
        if (s.id == 11)
            dur = s.durabilityCurrent;
    EXPECT_EQ(dur, 95);
    ASSERT_EQ(statsUpdated.size(), 1u);
    EXPECT_EQ(statsUpdated[0], 1);
    ASSERT_EQ(analytics.size(), 1u);
    auto pkt = nlohmann::json::parse(analytics[0]);
    EXPECT_EQ(pkt["body"]["analyticsType"], "player_death");
    EXPECT_EQ(pkt["body"]["characterId"], 1);
    EXPECT_EQ(pkt["body"]["level"], 5);
    EXPECT_EQ(pkt["body"]["sessionId"], "sess_1");
}

TEST_F(DeathFixture, ZeroPenaltyAtFloor)
{
    chars.addCharacter(deadChar(1, 5, 364)); // exactly at level floor
    EXPECT_TRUE(deaths.execute(1));
    EXPECT_EQ(chars.getCharacterData(1).experienceDebt, 0);
    // durability + snapshot + analytics still run
    ASSERT_EQ(statsUpdated.size(), 1u);
    ASSERT_EQ(analytics.size(), 1u);
}

TEST_F(DeathFixture, NoCallbacksNoCrash)
{
    PlayerDeathPipeline silent{chars, experience, durability, gameZones, logger};
    chars.addCharacter(deadChar(1, 5, 500));
    EXPECT_TRUE(silent.execute(1));
    EXPECT_EQ(chars.getCharacterData(1).experienceDebt, 50);
}
