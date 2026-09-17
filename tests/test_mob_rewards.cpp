// Unit tests for MobKillRewardPipeline (real Logger-only/DI managers;
// quest/notify/stats/analytics/persist captured via seams).
#include "services/MobKillRewardPipeline.hpp"
#include "services/BestiaryManager.hpp"
#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/ClientManager.hpp"
#include "services/ExperienceCacheManager.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/ItemSoulTiers.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"
#include "services/ReputationManager.hpp"
#include "services/SpawnZoneManager.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

struct RewardFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    MobManager mobs{logger};
    MobInstanceManager instances{logger};
    MobMovementManager movement{chars, instances, mobs, logger};
    GameZoneManager gameZones{logger};
    ExperienceCacheManager expCache{logger};
    // titles/notify null: level-up title/stats hooks skipped (covered elsewhere)
    ExperienceManager experience{chars, expCache, nullptr, nullptr, logger};
    ItemManager items{logger};
    InventoryManager inventory{items, logger};
    GameConfigService config{logger};
    ClientManager clients{logger};
    BestiaryManager bestiary{logger};
    SpawnZoneManager spawnZones{mobs, logger};
    ChampionManager champions{
        gameZones, config, instances, chars, mobs, spawnZones, nullptr, logger};
    ReputationManager reputation{logger};
    MobKillRewardPipeline rewards{chars,
        instances,
        movement,
        gameZones,
        experience,
        inventory,
        items,
        config,
        clients,
        bestiary,
        champions,
        reputation,
        logger};

    std::vector<std::pair<int, int>> questHooks;
    struct NotifyRecord
    {
        int cid;
        std::string type;
        nlohmann::json data;
    };
    std::vector<NotifyRecord> notified;
    std::vector<int> statsUpdated;
    std::vector<std::string> analytics;
    struct SaveRecord
    {
        int cid;
        int invId;
        int kills;
    };
    std::vector<SaveRecord> savedKills;

    void SetUp() override
    {
        rewards.setQuestHook(
            [this](int killerId, int mobTemplateId) { questHooks.emplace_back(killerId, mobTemplateId); });
        rewards.setNotifyCallback([this](int cid, const std::string &type, const nlohmann::json &data,
                                        const std::string &, const std::string &)
            { notified.push_back({cid, type, data}); });
        rewards.setStatsUpdateCallback([this](int cid) { statsUpdated.push_back(cid); });
        rewards.setAnalyticsCallback([this](const std::string &pkt) { analytics.push_back(pkt); });
        rewards.setSaveKillCountCallback(
            [this](int cid, int invId, int kills) { savedKills.push_back({cid, invId, kills}); });

        CharacterDataStruct c1;
        c1.characterId = 1;
        c1.characterLevel = 10;
        c1.characterMaxHealth = 100;
        c1.characterCurrentHealth = 100;
        c1.characterMaxMana = 50;
        c1.characterCurrentMana = 50;
        chars.addCharacter(c1);
        CharacterDataStruct c2 = c1;
        c2.characterId = 2;
        chars.addCharacter(c2);
    }

    MobDataStruct makeMob(int uid)
    {
        MobDataStruct m;
        m.uid = uid;
        m.id = 1;
        m.slug = "wolf";
        m.level = 5;
        m.maxHealth = 100;
        m.currentHealth = 0;
        m.baseExperience = 100;
        m.rankMult = 1.0f;
        return m;
    }

    void setThreat(int uid, std::unordered_map<int, int> threat)
    {
        auto md = movement.getMobMovementData(uid);
        md.threatTable = std::move(threat);
        movement.updateMobMovementData(uid, md);
    }

    void setTimestampsNow(int uid, std::vector<int> charIds)
    {
        auto md = movement.getMobMovementData(uid);
        for (int cid : charIds)
            md.attackerTimestamps[cid] = std::chrono::steady_clock::now();
        movement.updateMobMovementData(uid, md);
    }

    void giveSword(int charId, int rowId)
    {
        ItemDataStruct sword;
        sword.id = 200;
        sword.slug = "iron_sword";
        sword.stackMax = 1;
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        items.setItemsList({sword});
        EXPECT_TRUE(inventory.addItemToInventory(charId, 200, 1));
        inventory.updateInventoryItemId(charId, 200, rowId);
        inventory.setItemEquipped(charId, rowId, true);
    }
};

} // namespace

// lvl10 vs lvl5 mob, base 100: diff -5 -> x0.5 -> personal 50.
// threat {1:100, 2:300}: p1 share .25 -> 13, p2 (killer) .75 -> 38*1.1 -> 42.
TEST_F(RewardFixture, ProportionalSplitWithKillerBonus)
{
    MobDataStruct m = makeMob(9001);
    ASSERT_TRUE(instances.registerMobInstance(m));
    setThreat(9001, {{1, 100}, {2, 300}});
    rewards.execute(9001, 2);
    EXPECT_EQ(chars.getCharacterData(1).characterExperiencePoints, 13);
    EXPECT_EQ(chars.getCharacterData(2).characterExperiencePoints, 42);
    ASSERT_EQ(questHooks.size(), 1u);
    EXPECT_EQ(questHooks[0].first, 2);
    EXPECT_EQ(questHooks[0].second, 1);
}

TEST_F(RewardFixture, BelowThresholdSkipped)
{
    MobDataStruct m = makeMob(9001);
    ASSERT_TRUE(instances.registerMobInstance(m));
    setThreat(9001, {{1, 100}, {2, 3}}); // p2 share ~3% < 5%
    rewards.execute(9001, 1);
    EXPECT_GT(chars.getCharacterData(1).characterExperiencePoints, 0);
    EXPECT_EQ(chars.getCharacterData(2).characterExperiencePoints, 0);
}

TEST_F(RewardFixture, KillerOnlyFallback)
{
    MobDataStruct m = makeMob(9001);
    ASSERT_TRUE(instances.registerMobInstance(m));
    // no movement data -> empty threat table -> full award to killer
    rewards.execute(9001, 1);
    EXPECT_EQ(chars.getCharacterData(1).characterExperiencePoints, 50);
    EXPECT_EQ(chars.getCharacterData(2).characterExperiencePoints, 0);
}

TEST_F(RewardFixture, FellowshipBonusBothSides)
{
    MobDataStruct m = makeMob(9001);
    ASSERT_TRUE(instances.registerMobInstance(m));
    setThreat(9001, {{1, 100}, {2, 100}});
    setTimestampsNow(9001, {1, 2});
    rewards.execute(9001, 1);
    // NOTE (1-1): the XP phase de-levels the synthetic lvl10/0xp chars to 1,
    // so fellowship re-reads level 1: personal 150 * .5 share * .07 = 5.25 -> 5.
    // In prod xp/level stay consistent, so the re-read is a no-op there.
    int fellowNotes = 0;
    for (const auto &n : notified)
        if (n.type == "fellowship_bonus" && n.data["xpBonus"] == 5)
            ++fellowNotes;
    EXPECT_EQ(fellowNotes, 2);
}

TEST_F(RewardFixture, ItemSoulIncrementAndTierFlush)
{
    MobDataStruct m = makeMob(9001);
    ASSERT_TRUE(instances.registerMobInstance(m));
    giveSword(2, 21);
    rewards.execute(9001, 2);
    // killCount 0 -> 1: tooltip notify yes, DB flush no, stats update no
    ASSERT_EQ(notified.size(), 1u);
    EXPECT_EQ(notified[0].type, "weapon_kill_count_update");
    EXPECT_EQ(notified[0].data["killCount"], 1);
    EXPECT_TRUE(savedKills.empty());
    EXPECT_TRUE(statsUpdated.empty());

    // push to tier1 boundary (50): flush + stats update
    inventory.updateItemKillCount(2, 21, 49);
    notified.clear();
    rewards.execute(9001, 2);
    ASSERT_EQ(savedKills.size(), 1u);
    EXPECT_EQ(savedKills[0].kills, 50);
    ASSERT_EQ(statsUpdated.size(), 1u);
    EXPECT_EQ(statsUpdated[0], 2);
}

TEST_F(RewardFixture, LevelUpAndKillAnalytics)
{
    CharacterDataStruct c = chars.getCharacterData(1);
    c.characterLevel = 1;
    c.sessionId = "sess_1";
    chars.loadCharacterData(c);
    MobDataStruct m = makeMob(9001);
    ASSERT_TRUE(instances.registerMobInstance(m));
    // lvl1 vs lvl5: diff +4 -> x1.5 -> 150 -> level 2
    rewards.execute(9001, 1);
    EXPECT_EQ(chars.getCharacterData(1).characterLevel, 2);
    int levelUps = 0, kills = 0;
    for (const auto &a : analytics)
    {
        auto pkt = nlohmann::json::parse(a);
        if (pkt["body"]["analyticsType"] == "level_up")
        {
            ++levelUps;
            EXPECT_EQ(pkt["body"]["characterId"], 1);
        }
        if (pkt["body"]["analyticsType"] == "mob_killed")
        {
            ++kills;
            EXPECT_EQ(pkt["body"]["payload"]["mobSlug"], "wolf");
        }
    }
    EXPECT_EQ(levelUps, 1);
    EXPECT_EQ(kills, 1);
}

TEST_F(RewardFixture, ReputationHook)
{
    MobDataStruct m = makeMob(9001);
    m.factionSlug = "wolves";
    m.repDeltaPerKill = 5;
    ASSERT_TRUE(instances.registerMobInstance(m));
    rewards.execute(9001, 1);
    EXPECT_EQ(reputation.getReputation(1, "wolves"), 5);
}

TEST(ItemSoulTiers, DefaultsMatchLegacyValues)
{
    // Wave 2.4 pin: the table both consumers previously hand-synced.
    EXPECT_EQ(ItemSoulTiers::kDefaultTier1Kills, 50);
    EXPECT_EQ(ItemSoulTiers::kDefaultTier2Kills, 200);
    EXPECT_EQ(ItemSoulTiers::kDefaultTier3Kills, 500);
    EXPECT_EQ(ItemSoulTiers::kDefaultTier1BonusFlat, 1);
    EXPECT_EQ(ItemSoulTiers::kDefaultTier2BonusFlat, 2);
    EXPECT_EQ(ItemSoulTiers::kDefaultTier3BonusFlat, 3);
}

TEST(ItemSoulTiers, BonusMappingAndBoundaries)
{
    Logger logger{"test"};
    GameConfigService cfg{logger}; // empty: all defaults
    const auto t = ItemSoulTiers::Table::load(cfg);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 0), 0);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 49), 0);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 50), 1);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 199), 1);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 200), 2);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 499), 2);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 500), 3);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 100000), 3);
    EXPECT_TRUE(ItemSoulTiers::isTierBoundary(t, 50));
    EXPECT_TRUE(ItemSoulTiers::isTierBoundary(t, 200));
    EXPECT_TRUE(ItemSoulTiers::isTierBoundary(t, 500));
    EXPECT_FALSE(ItemSoulTiers::isTierBoundary(t, 51));
    EXPECT_FALSE(ItemSoulTiers::isTierBoundary(t, 0));
}

TEST(ItemSoulTiers, CustomConfigRespected)
{
    // A live-tuned table moves both consumers together (the old desync mode).
    Logger logger{"test"};
    GameConfigService cfg{logger};
    cfg.setConfig({{"item_soul.tier1_kills", "10"},
        {"item_soul.tier2_kills", "20"},
        {"item_soul.tier3_kills", "30"},
        {"item_soul.tier1_bonus_flat", "5"},
        {"item_soul.tier2_bonus_flat", "6"},
        {"item_soul.tier3_bonus_flat", "7"}});
    const auto t = ItemSoulTiers::Table::load(cfg);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 9), 0);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 10), 5);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 25), 6);
    EXPECT_EQ(ItemSoulTiers::bonusForKills(t, 30), 7);
    EXPECT_TRUE(ItemSoulTiers::isTierBoundary(t, 10));
    EXPECT_FALSE(ItemSoulTiers::isTierBoundary(t, 50));
}
