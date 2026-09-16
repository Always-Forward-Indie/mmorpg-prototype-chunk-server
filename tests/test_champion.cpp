// Unit tests for ChampionManager (DI: explicit managers + Logger;
// statsNotify=nullptr, sendToGameServer callback recorded).
#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

GameZoneStruct makeZone()
{
    GameZoneStruct z;
    z.id = 7;
    z.minX = 0;
    z.maxX = 10000;
    z.minY = 0;
    z.maxY = 10000;
    z.championThresholdKills = 3;
    return z;
}

MobDataStruct makeTemplate()
{
    MobDataStruct m;
    m.id = 5;
    m.slug = "wolf";
    m.name = "Wolf";
    m.level = 5;
    m.maxHealth = 100;
    m.currentHealth = 100;
    return m;
}

struct ChampFixture : ::testing::Test
{
    Logger logger{"test"};
    GameZoneManager zones{logger};
    GameConfigService config{logger};
    MobInstanceManager instances{logger};
    CharacterManager chars{logger};
    MobManager mobs{logger};
    SpawnZoneManager spawnZones{mobs, logger};
    ChampionManager champ{zones, config, instances, chars, mobs, spawnZones, nullptr, logger};

    void SetUp() override
    {
        zones.loadGameZones({makeZone()});
        mobs.setListOfMobs({makeTemplate()});
    }

    size_t liveChampions()
    {
        return instances.getAllLivingInstances().size();
    }
};

} // namespace

TEST_F(ChampFixture, ThresholdSpawnAndSuppression)
{
    EXPECT_EQ(liveChampions(), 0u);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 0u); // below threshold
    champ.recordMobKill(7, 5);      // 3rd kill -> spawn
    EXPECT_EQ(liveChampions(), 1u);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 1u); // suppressed while active
}

TEST_F(ChampFixture, KillClearsAndCycleRepeats)
{
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);
    int uid = instances.getAllLivingInstances()[0].uid;
    EXPECT_NE(uid, 0);

    std::vector<std::string> sent;
    champ.setSendToGameServerCallback([&](const std::string &pkt) { sent.push_back(pkt); });
    // Production order: the instance dies in combat first, then the kill is
    // reported. Kill it through the instance manager.
    instances.applyDamageToMob(uid, 1000000);
    champ.onChampionKilled(uid, 1);
    EXPECT_EQ(liveChampions(), 0u);

    // Counter reset (or halved): a fresh threshold run spawns again.
    for (int i = 0; i < 6; ++i)
        champ.recordMobKill(7, 5);
    EXPECT_GE(liveChampions(), 1u);
}

TEST_F(ChampFixture, UnknownTemplateNeverSpawns)
{
    for (int i = 0; i < 10; ++i)
        champ.recordMobKill(7, 424242);
    EXPECT_EQ(liveChampions(), 0u);
}

TEST_F(ChampFixture, TimedLoadAndTick)
{
    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = 0; // uninitialised -> skipped
    champ.loadTimedChampions({t});
    champ.tickTimedChampions(); // must not crash, nothing due
    champ.tickSurvivalEvolution();
    SUCCEED();
}
