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

TEST_F(ChampFixture, ChampionZoneIdMatchesTargetGameZone)
{
    // Pin for the spawnChampion zone-search: the champion must take the
    // zoneId of a spawn zone INSIDE the target game zone, not the first
    // map entry. std::map iterates by key, so the outside zone (id 1)
    // comes first and exposes the unconditional-break bug.
    GameZoneStruct other = makeZone();
    other.id = 8;
    other.minX = 20000;
    other.maxX = 30000;
    other.minY = 20000;
    other.maxY = 30000;
    zones.loadGameZones({makeZone(), other});

    SpawnZoneStruct szOutside;
    szOutside.zoneId = 1;
    szOutside.centerX = 25000.0f;
    szOutside.centerY = 25000.0f;
    SpawnZoneStruct szInside;
    szInside.zoneId = 2;
    szInside.centerX = 5000.0f;
    szInside.centerY = 5000.0f;
    spawnZones.loadMobSpawnZones({szOutside, szInside});

    const int uid = champ.spawnChampion(5, 7, "Test ", 1.0f, "test_zone");
    ASSERT_NE(uid, 0);
    EXPECT_EQ(instances.getMobInstance(uid).zoneId, 2);
}

TEST_F(ChampFixture, ChampionSpawnPointRespectsCircleGameZone)
{
    // resolveChampionSpawnPoint must use shape-aware containment: a spawn
    // zone in the AABB corner but outside the CIRCLE game zone must not be
    // picked — with no valid candidates the fallback (zone centre) applies.
    GameZoneStruct circle;
    circle.id = 9;
    circle.shape = ZoneShape::CIRCLE;
    circle.centerX = 5000.0f;
    circle.centerY = 5000.0f;
    circle.outerRadius = 1000.0f;
    circle.minX = 4000;
    circle.maxX = 6000;
    circle.minY = 4000;
    circle.maxY = 6000;
    zones.loadGameZones({circle});

    SpawnZoneStruct corner;
    corner.zoneId = 1;
    corner.centerX = 4100.0f; // dist ~1273 from centre: inside AABB, outside disc
    corner.centerY = 4100.0f;
    corner.minX = 4050;
    corner.maxX = 4150;
    corner.minY = 4050;
    corner.maxY = 4150;
    corner.minZ = 0;
    corner.maxZ = 200;
    spawnZones.loadMobSpawnZones({corner});

    const int uid = champ.spawnChampion(5, 9, "Test ", 1.0f, "test_circle");
    ASSERT_NE(uid, 0);
    const auto inst = instances.getMobInstance(uid);
    const float dx = inst.position.positionX - 5000.0f;
    const float dy = inst.position.positionY - 5000.0f;
    EXPECT_LE(dx * dx + dy * dy, 1000.0f * 1000.0f);
}

TEST_F(ChampFixture, ChanceZeroNeverSpawns)
{
    // spawn_chance_pct=0: threshold reached, roll always fails, counter
    // resets (retry next threshold — still nothing).
    config.setConfig({{"champion.spawn_chance_pct", "0"}});
    for (int i = 0; i < 10; ++i)
        champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 0u);
}

TEST_F(ChampFixture, CapBlocksSecondTemplateWhileActive)
{
    // max_active_per_zone=1: first template spawns, second template's
    // threshold is refused (counter reset, natural retry later).
    MobDataStruct tpl2 = makeTemplate();
    tpl2.id = 6;
    tpl2.slug = "boar";
    tpl2.name = "Boar";
    mobs.setListOfMobs({makeTemplate(), tpl2});
    config.setConfig({{"champion.max_active_per_zone", "1"}});
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 6);
    EXPECT_EQ(liveChampions(), 1u);
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
