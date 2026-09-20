// Unit tests for ReputationManager (Logger only + callbacks).
#include "services/ReputationManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

struct RepFixture : ::testing::Test
{
    Logger logger{"test"};
    ReputationManager rep{logger};
};

} // namespace

TEST(ReputationTiers, StaticMapping)
{
    EXPECT_EQ(ReputationManager::getTier(-1000), "enemy");
    EXPECT_EQ(ReputationManager::getTier(0), "neutral");
    EXPECT_EQ(ReputationManager::getTier(1000), "ally");
    EXPECT_GE(ReputationManager::getTierOrdinal("ally"), ReputationManager::getTierOrdinal("enemy"));
    EXPECT_EQ(ReputationManager::getTierOrdinal("nope"), -1);
}

TEST_F(RepFixture, LoadQueryUnload)
{
    EXPECT_EQ(rep.getReputation(1, "wolves"), 0);
    EXPECT_TRUE(rep.getAllReputations(1).empty());
    rep.loadCharacterReputations(1, {{"wolves", 50}, {"bears", -20}});
    EXPECT_EQ(rep.getReputation(1, "wolves"), 50);
    EXPECT_EQ(rep.getReputation(1, "bears"), -20);
    EXPECT_EQ(rep.getReputation(1, "unknown"), 0);
    EXPECT_EQ(rep.getAllReputations(1).size(), 2u);
    rep.unloadCharacterReputations(1);
    EXPECT_EQ(rep.getReputation(1, "wolves"), 0);
}

TEST_F(RepFixture, ChangePersistsAndNotifies)
{
    rep.loadCharacterReputations(1, {{"wolves", 0}});
    std::vector<std::string> saved;
    rep.setSaveCallback([&](const std::string &pkt) { saved.push_back(pkt); });
    int notifiedValue = 0;
    std::string notifiedTier;
    rep.setClientNotifyCallback(
        [&](int, const std::string &, int v, const std::string &t)
        {
            notifiedValue = v;
            notifiedTier = t;
        });
    rep.changeReputation(1, "wolves", 30);
    EXPECT_EQ(rep.getReputation(1, "wolves"), 30);
    EXPECT_FALSE(saved.empty());
    EXPECT_EQ(notifiedValue, 30);
    EXPECT_FALSE(notifiedTier.empty());
}

TEST_F(RepFixture, TierChangeCallbackFires)
{
    rep.loadCharacterReputations(1, {{"wolves", 0}});
    int tierChanges = 0;
    rep.setTierChangeCallback([&](int, const std::string &, const std::string &, int) { ++tierChanges; });
    rep.changeReputation(1, "wolves", 5000); // force tier jump
    EXPECT_GE(tierChanges, 1);
}

TEST_F(RepFixture, PersistPacketCarriesDelta)
{
    // Game applies deltas atomically (add_reputation); absolutes race
    // last-writer-wins on concurrent changes (lost update).
    rep.loadCharacterReputations(1, {{"wolves", 10}});
    std::vector<std::string> saved;
    rep.setSaveCallback([&](const std::string &pkt) { saved.push_back(pkt); });
    rep.changeReputation(1, "wolves", 30);
    ASSERT_FALSE(saved.empty());
    const auto body = nlohmann::json::parse(saved.back())["body"];
    EXPECT_EQ(body["delta"], 30);
    EXPECT_EQ(body["value"], 40);
}

TEST_F(RepFixture, FillContext)
{
    rep.loadCharacterReputations(1, {{"wolves", 42}});
    PlayerContextStruct ctx;
    rep.fillReputationContext(1, ctx);
    EXPECT_EQ(ctx.reputations["wolves"], 42);
}
