// Unit tests for the pure parts of ExperienceManager.
// calculateMobExperience / getExperienceForLevel / calculateDeathPenalty are
// static (no world needed).
#include "services/ExperienceManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/ExperienceCacheManager.hpp"
#include "services/IStatsNotifier.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

struct FakeNotifier : IStatsNotifier
{
    int calls = 0;
    void sendStatsUpdate(int /*characterId*/) override
    {
        ++calls;
    }
    void sendStatsUpdate(int /*characterId*/, const std::string & /*source*/) override
    {
        ++calls;
    }
};

ExperienceLevelEntry entry(int level, int xp)
{
    ExperienceLevelEntry e;
    e.level = level;
    e.experiencePoints = xp;
    return e;
}

struct ExpFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    ExperienceCacheManager cache{logger};
    FakeNotifier notifier;
    // titles=nullptr: level-up title grants skipped (covered by test_title.cpp)
    ExperienceManager exp{chars, cache, nullptr, &notifier, logger};
    std::vector<nlohmann::json> expPackets;

    void SetUp() override
    {
        exp.setExperiencePacketCallback([this](const nlohmann::json &p) { expPackets.push_back(p); });
    }

    CharacterDataStruct baseChar(int id)
    {
        CharacterDataStruct c;
        c.characterId = id;
        c.characterLevel = 1;
        c.characterExperiencePoints = 0;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 100;
        c.characterMaxMana = 50;
        c.characterCurrentMana = 50;
        return c;
    }
};

} // namespace

TEST(ExperienceFormulas, MobExperienceBands)
{
    // base 100, same level -> x1.0
    EXPECT_EQ(ExperienceManager::calculateMobExperience(10, 10, 100), 100);
    // far below (-10) -> x0.1
    EXPECT_EQ(ExperienceManager::calculateMobExperience(1, 11, 100), 10);
    // slightly below (-3) -> x0.5
    EXPECT_EQ(ExperienceManager::calculateMobExperience(7, 10, 100), 50);
    // slightly above (+3) -> x1.5
    EXPECT_EQ(ExperienceManager::calculateMobExperience(13, 10, 100), 150);
    // far above (+10) -> x2.0
    EXPECT_EQ(ExperienceManager::calculateMobExperience(20, 10, 100), 200);
    // boundary diffs: -5 -> x0.5, -2 -> x1.0, +2 -> x1.0, +5 -> x1.5
    EXPECT_EQ(ExperienceManager::calculateMobExperience(5, 10, 100), 50);
    EXPECT_EQ(ExperienceManager::calculateMobExperience(8, 10, 100), 100);
    EXPECT_EQ(ExperienceManager::calculateMobExperience(12, 10, 100), 100);
    EXPECT_EQ(ExperienceManager::calculateMobExperience(15, 10, 100), 150);
}

TEST(ExperienceFormulas, MobExperienceBaseFallback)
{
    // base <= 0 -> mobLevel * 10, then band modifier
    EXPECT_EQ(ExperienceManager::calculateMobExperience(10, 10, 0), 100);
    EXPECT_EQ(ExperienceManager::calculateMobExperience(10, 10, -5), 100);
    EXPECT_EQ(ExperienceManager::calculateMobExperience(5, 5, 0), 50);
}

TEST(ExperienceFormulas, ExpForLevelCurve)
{
    EXPECT_EQ(ExperienceManager::getExperienceForLevel(1), 0);
    EXPECT_EQ(ExperienceManager::getExperienceForLevel(0), 0);
    EXPECT_EQ(ExperienceManager::getExperienceForLevel(-3), 0);
    EXPECT_EQ(ExperienceManager::getExperienceForLevel(2), 100);          // 100 * 1.2^0
    EXPECT_EQ(ExperienceManager::getExperienceForLevel(3), 220);          // 100 + 120
    EXPECT_EQ(ExperienceManager::getExperienceForLevel(4), 364);          // + 144
    // Monotonic growth
    int prev = 0;
    for (int lvl = 1; lvl <= 20; ++lvl)
    {
        int cur = ExperienceManager::getExperienceForLevel(lvl);
        EXPECT_GE(cur, prev);
        prev = cur;
    }
}

TEST(ExperienceFormulas, DeathPenaltyFloor)
{
    // 10% of current, floored at current-level start (pure: floor injected)
    EXPECT_EQ(ExperienceManager::calculateDeathPenalty(5, 500, 364), 50);
    EXPECT_EQ(ExperienceManager::calculateDeathPenalty(2, 370, 364), 6); // capped by floor
    EXPECT_EQ(ExperienceManager::calculateDeathPenalty(1, 50, 0), 5);
    EXPECT_EQ(ExperienceManager::calculateDeathPenalty(3, 220, 220), 0); // at floor -> no penalty
}

TEST_F(ExpFixture, GrantNoLevelUp)
{
    chars.addCharacter(baseChar(1));
    auto r = exp.grantExperience(1, 50, "mob_kill", 7);
    EXPECT_TRUE(r.success);
    EXPECT_FALSE(r.levelUp);
    EXPECT_EQ(r.experienceEvent.newExperience, 50);
    EXPECT_EQ(r.experienceEvent.newLevel, 1);
    EXPECT_EQ(chars.getCharacterData(1).characterExperiencePoints, 50);
    EXPECT_EQ(chars.getCharacterData(1).characterLevel, 1);
    EXPECT_EQ(notifier.calls, 0); // no level-up -> no stats_update
    ASSERT_EQ(expPackets.size(), 1u);
    EXPECT_EQ(expPackets[0]["body"]["newExperience"], 50);
    EXPECT_EQ(expPackets[0]["body"]["reason"], "mob_kill");
}

TEST_F(ExpFixture, GrantLevelUpChain)
{
    chars.addCharacter(baseChar(1));
    auto r = exp.grantExperience(1, 150, "mob_kill");
    EXPECT_TRUE(r.success);
    EXPECT_TRUE(r.levelUp);
    EXPECT_EQ(r.experienceEvent.oldLevel, 1);
    EXPECT_EQ(r.experienceEvent.newLevel, 2); // 100 <= 150 < 220
    auto got = chars.getCharacterData(1);
    EXPECT_EQ(got.characterLevel, 2);
    EXPECT_EQ(got.characterMaxHealth, 110); // +10 per level
    EXPECT_EQ(got.characterMaxMana, 55);    // +5 per level
    EXPECT_EQ(got.characterCurrentHealth, 110); // full restore capped to effective max
    EXPECT_EQ(notifier.calls, 1);
    EXPECT_TRUE(r.newAbilities.empty()); // no %5 level crossed
}

TEST_F(ExpFixture, GrantMilestoneAbility)
{
    chars.addCharacter(baseChar(1));
    auto r = exp.grantExperience(1, 600, "quest");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.experienceEvent.newLevel, 5); // 537 <= 600
    ASSERT_EQ(r.newAbilities.size(), 1u);
    EXPECT_EQ(r.newAbilities[0], "ability_level_5");
}

TEST_F(ExpFixture, GrantUsesCacheTableWhenLoaded)
{
    cache.setExperienceTable({entry(1, 0), entry(2, 100), entry(3, 300)});
    chars.addCharacter(baseChar(1));
    auto r = exp.grantExperience(1, 150, "mob_kill");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.experienceEvent.newLevel, 2);
    EXPECT_EQ(r.experienceEvent.expForCurrentLevel, 100);
    EXPECT_EQ(r.experienceEvent.expForNextLevel, 300);
}

TEST_F(ExpFixture, DebtPaidFirst)
{
    auto c = baseChar(1);
    chars.addCharacter(c);
    chars.addExperienceDebt(1, 100);
    auto r = exp.grantExperience(1, 150, "mob_kill");
    EXPECT_TRUE(r.success);
    auto got = chars.getCharacterData(1);
    EXPECT_EQ(got.experienceDebt, 0);
    EXPECT_EQ(got.characterExperiencePoints, 50); // remainder after debt
    // saveProgressCallback_ is null in tests -> guarded, no crash
}

TEST_F(ExpFixture, RemoveClampsAtZero)
{
    auto c = baseChar(1);
    c.characterExperiencePoints = 50;
    chars.addCharacter(c);
    auto r = exp.removeExperience(1, 100, "death");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(chars.getCharacterData(1).characterExperiencePoints, 0);
    EXPECT_EQ(r.experienceEvent.experienceChange, -50);
}

TEST_F(ExpFixture, MaxLevelCap)
{
    // Cache table with 101 levels forces the MAX_LEVEL cap branch
    // (the local exponential curve overflows int before level 100).
    std::vector<ExperienceLevelEntry> table;
    for (int lvl = 1; lvl <= 101; ++lvl)
        table.push_back(entry(lvl, (lvl - 1) * 1000));
    cache.setExperienceTable(table);
    chars.addCharacter(baseChar(1));
    auto r = exp.grantExperience(1, 200000, "admin");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.experienceEvent.newLevel, 100);
    EXPECT_EQ(chars.getCharacterData(1).characterLevel, 100);
}
