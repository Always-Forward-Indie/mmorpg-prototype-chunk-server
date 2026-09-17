// Unit tests for NPCManager + MobManager + MobInstanceManager + SpawnZoneManager
// (Logger only; SpawnZoneManager takes MobManager&).
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/NPCManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/RandomUtils.hpp"

#include <gtest/gtest.h>

namespace
{

PositionStruct pos(float x, float y)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    return p;
}

MobDataStruct makeMob(int id, const std::string &slug, int level = 5)
{
    MobDataStruct m;
    m.id = id;
    m.slug = slug;
    m.name = slug;
    m.level = level;
    m.maxHealth = 100;
    m.currentHealth = 100;
    m.maxMana = 50;
    m.currentMana = 50;
    return m;
}

NPCDataStruct makeNpc(int id, float x, float y)
{
    NPCDataStruct n;
    n.id = id;
    n.name = "npc_" + std::to_string(id);
    n.position = pos(x, y);
    return n;
}

struct MobChainFixture : ::testing::Test
{
    Logger logger{"test"};
    MobManager mobs{logger};
    MobInstanceManager instances{logger};
    SpawnZoneManager zones{mobs, logger};

    void SetUp() override
    {
        mobs.setListOfMobs({makeMob(1, "wolf"), makeMob(2, "boar")});
    }
};

} // namespace

TEST_F(MobChainFixture, MobCatalogLookup)
{
    EXPECT_EQ(mobs.getMobById(1).slug, "wolf");
    EXPECT_EQ(mobs.getMobById(424242).id, 0); // miss sentinel
    EXPECT_EQ(mobs.getMobBySlug("boar").id, 2);
    EXPECT_EQ(mobs.getMobsAsVector().size(), 2u);
    EXPECT_FALSE(mobs.getMobs().empty());
}

TEST_F(MobChainFixture, WeaknessesResistances)
{
    mobs.setWeaknessesResistances({{1, {"fire"}}}, {{1, {"ice"}}});
    EXPECT_EQ(mobs.getWeaknessesForMob(1).size(), 1u);
    EXPECT_EQ(mobs.getWeaknessesForMob(1)[0], "fire");
    EXPECT_EQ(mobs.getResistancesForMob(1)[0], "ice");
    EXPECT_TRUE(mobs.getWeaknessesForMob(424242).empty());
}

TEST_F(MobChainFixture, InstanceRegisterDamageDeath)
{
    MobDataStruct inst = makeMob(1, "wolf");
    inst.uid = 9001;
    inst.zoneId = 7;
    inst.position = pos(10, 10);
    EXPECT_TRUE(instances.registerMobInstance(inst));
    EXPECT_EQ(instances.getMobInstance(9001).uid, 9001);
    EXPECT_TRUE(instances.isMobAlive(9001));

    auto r = instances.applyDamageToMob(9001, 30, 0);
    EXPECT_TRUE(r.success);
    EXPECT_FALSE(r.mobDied);
    EXPECT_EQ(r.newHealth, 70);
    EXPECT_TRUE(instances.isMobAlive(9001));

    auto kill = instances.applyDamageToMob(9001, 1000, 0);
    EXPECT_TRUE(kill.success);
    EXPECT_TRUE(kill.mobDied);
    EXPECT_FALSE(instances.isMobAlive(9001));

    // Damaging the dead reports wasAlreadyDead.
    auto again = instances.applyDamageToMob(9001, 10, 0);
    EXPECT_TRUE(again.wasAlreadyDead);

    instances.unregisterMobInstance(9001);
    EXPECT_EQ(instances.getMobInstance(9001).uid, 0);
}

TEST_F(MobChainFixture, InstanceHealAndMana)
{
    MobDataStruct inst = makeMob(1, "wolf");
    inst.uid = 9002;
    inst.zoneId = 7;
    EXPECT_TRUE(instances.registerMobInstance(inst));
    instances.applyDamageToMob(9002, 60, 0);
    auto heal = instances.applyHealToMob(9002, 1000);
    EXPECT_TRUE(heal.success);
    EXPECT_EQ(instances.getMobInstance(9002).currentHealth, 100); // clamped to max
    EXPECT_TRUE(instances.trySpendMana(9002, 20));
    EXPECT_EQ(instances.getMobInstance(9002).currentMana, 30);
    EXPECT_FALSE(instances.trySpendMana(9002, 1000)); // insufficient
}

TEST_F(MobChainFixture, InstancesInZoneAndRange)
{
    MobDataStruct a = makeMob(1, "wolf");
    a.uid = 9101;
    a.zoneId = 7;
    a.position = pos(0, 0);
    MobDataStruct b = makeMob(2, "boar");
    b.uid = 9102;
    b.zoneId = 7;
    b.position = pos(10000, 10000);
    EXPECT_TRUE(instances.registerMobInstance(a));
    EXPECT_TRUE(instances.registerMobInstance(b));
    EXPECT_EQ(instances.getAliveMobCountInZone(7), 2);
    EXPECT_EQ(instances.getMobsInRange(0, 0, 500).size(), 1u);
    EXPECT_EQ(instances.getMobInstancesInZone(7).size(), 2u);
}

TEST_F(MobChainFixture, SpawnZoneSpawnsFromTemplate)
{
    SpawnZoneStruct z;
    z.zoneId = 7;
    z.zoneName = "test";
    z.minX = 0;
    z.maxX = 1000;
    z.minY = 0;
    z.maxY = 1000;
    SpawnZoneMobEntry e;
    e.mobId = 1;
    e.maxCount = 3;
    z.mobEntries = {e};
    zones.loadMobSpawnZones({z});

    auto spawned = zones.spawnMobsInZone(7);
    EXPECT_EQ(spawned.size(), 3u);
    EXPECT_EQ(zones.getMobsInZone(7).size(), 3u);
    // Zone is full now: further spawns add nothing.
    EXPECT_TRUE(zones.spawnMobsInZone(7).empty());
    EXPECT_TRUE(zones.spawnMobsInZone(424242).empty()); // unknown zone
}

TEST_F(MobChainFixture, RemoveMobByUIDUnregistersAndCleansZone)
{
    zones.setMobInstanceManager(&instances);
    SpawnZoneStruct z;
    z.zoneId = 7;
    z.minX = 0;
    z.maxX = 1000;
    z.minY = 0;
    z.maxY = 1000;
    SpawnZoneMobEntry e;
    e.mobId = 1;
    e.maxCount = 1;
    z.mobEntries = {e};
    zones.loadMobSpawnZones({z});
    ASSERT_EQ(zones.spawnMobsInZone(7).size(), 1u);
    const int uid = zones.getMobsInZone(7)[0].uid;
    ASSERT_NE(uid, 0);
    ASSERT_EQ(instances.getMobInstance(uid).uid, uid); // spawn registered it

    zones.removeMobByUID(uid);
    EXPECT_EQ(instances.getMobInstance(uid).uid, 0); // unregistered ...
    EXPECT_TRUE(zones.getMobsInZone(7).empty());      // ... and zone-cleaned

    // Idempotent: second removal is a safe no-op (no crash, stays empty).
    zones.removeMobByUID(uid);
    EXPECT_TRUE(zones.getMobsInZone(7).empty());
    // NOTE: lock ordering (zone mutex released before unregister, same as
    // mobDied) is structural — it cannot deadlock-detect single-threaded;
    // the full TSan suite guards the locking. This test pins the behavior.
}

TEST_F(MobChainFixture, MobDiedCounterFloorsAtZero)
{
    // Double mobDied (cleanup + harvest both reporting) must not drive the
    // display counter negative; respawn uses the live instance count.
    zones.setMobInstanceManager(&instances);
    SpawnZoneStruct z;
    z.zoneId = 7;
    z.minX = 0;
    z.maxX = 1000;
    z.minY = 0;
    z.maxY = 1000;
    SpawnZoneMobEntry e;
    e.mobId = 1;
    e.maxCount = 1;
    z.mobEntries = {e};
    zones.loadMobSpawnZones({z});
    ASSERT_EQ(zones.spawnMobsInZone(7).size(), 1u);
    const int uid = zones.getMobsInZone(7)[0].uid;

    zones.mobDied(7, uid);
    zones.mobDied(7, uid);
    EXPECT_GE(zones.getMobSpawnZoneByID(7).spawnedMobsCount, 0);
}

TEST_F(MobChainFixture, LoadMobsRoutesIntoOwnZone)
{
    // loadMobsInSpawnZones must file each mob under its own row.zoneId —
    // previously zoneId was never copied and everything landed in zone 0.
    MobDataStruct row = makeMob(1, "wolf");
    row.uid = 9301;
    row.zoneId = 7;
    zones.loadMobsInSpawnZones({row});
    ASSERT_EQ(zones.getMobsInZone(7).size(), 1u);
    EXPECT_EQ(zones.getMobsInZone(7)[0].uid, 9301);
    EXPECT_TRUE(zones.getMobsInZone(0).empty());
}

TEST_F(MobChainFixture, MissingTemplateSkipsEntryOnly)
{
    // A missing template delays only its own entry type — later entries
    // must still spawn this tick (a bare return starved them).
    SpawnZoneStruct z;
    z.zoneId = 7;
    z.minX = 0;
    z.maxX = 1000;
    z.minY = 0;
    z.maxY = 1000;
    SpawnZoneMobEntry missing;
    missing.mobId = 424242;
    missing.maxCount = 1;
    SpawnZoneMobEntry present;
    present.mobId = 1;
    present.maxCount = 2;
    z.mobEntries = {missing, present};
    zones.loadMobSpawnZones({z});

    auto spawned = zones.spawnMobsInZone(7);
    ASSERT_EQ(spawned.size(), 2u);
    for (const auto &m : spawned)
        EXPECT_EQ(m.id, 1);
}

TEST_F(MobChainFixture, ReloadPreservesRuntimeTracking)
{
    // Static pushes re-arrive on chunk<->game reconnect while mobs are live:
    // reload must refresh config but keep runtime tracking, else the zone
    // forgets its mobs and double-spawns on the next tick.
    SpawnZoneStruct z;
    z.zoneId = 7;
    z.zoneName = "v1";
    z.minX = 0;
    z.maxX = 1000;
    z.minY = 0;
    z.maxY = 1000;
    SpawnZoneMobEntry e;
    e.mobId = 1;
    e.maxCount = 1;
    z.mobEntries = {e};
    zones.loadMobSpawnZones({z});
    ASSERT_EQ(zones.spawnMobsInZone(7).size(), 1u);

    z.zoneName = "v2-reload";
    zones.loadMobSpawnZones({z});
    EXPECT_EQ(zones.getMobSpawnZoneByID(7).zoneName, "v2-reload"); // config refreshed
    EXPECT_EQ(zones.getMobsInZone(7).size(), 1u);                  // runtime kept
    EXPECT_EQ(zones.getMobSpawnZoneByID(7).spawnedMobsCount, 1);
}

TEST_F(MobChainFixture, SpawnPositionsDeterministicUnderSeed)
{
    // Spawn sampling runs on RandomUtils: same seed + same config must give
    // the same positions (uids differ — atomic counter — compare x/y only).
    auto buildZone = [] {
        SpawnZoneStruct z;
        z.zoneId = 7;
        z.minX = 0;
        z.maxX = 1000;
        z.minY = 0;
        z.maxY = 1000;
        SpawnZoneMobEntry e;
        e.mobId = 1;
        e.maxCount = 3;
        z.mobEntries = {e};
        return z;
    };
    RandomUtils::seedForTests(777u);
    zones.loadMobSpawnZones({buildZone()});
    auto first = zones.spawnMobsInZone(7);
    ASSERT_EQ(first.size(), 3u);

    MobManager mobs2{logger};
    mobs2.setListOfMobs({makeMob(1, "wolf"), makeMob(2, "boar")});
    SpawnZoneManager zones2{mobs2, logger};
    RandomUtils::seedForTests(777u);
    zones2.loadMobSpawnZones({buildZone()});
    auto second = zones2.spawnMobsInZone(7);
    ASSERT_EQ(second.size(), 3u);

    for (size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_FLOAT_EQ(first[i].position.positionX, second[i].position.positionX);
        EXPECT_FLOAT_EQ(first[i].position.positionY, second[i].position.positionY);
    }
}

TEST(NpcCatalog, LoadLookupAreaClear)
{
    Logger logger{"test"};
    NPCManager npcs(logger);
    EXPECT_FALSE(npcs.isNPCsLoaded());
    npcs.setNPCsList({makeNpc(1, 0, 0), makeNpc(2, 5000, 5000)});
    EXPECT_TRUE(npcs.isNPCsLoaded());
    EXPECT_EQ(npcs.getNPCCount(), 2u);
    EXPECT_EQ(npcs.getNPCById(1).name, "npc_1");
    EXPECT_EQ(npcs.getNPCById(424242).id, 0);
    EXPECT_EQ(npcs.getNPCsInArea(0, 0, 100).size(), 1u);
    EXPECT_EQ(npcs.getNPCsInArea(0, 0, 100000).size(), 2u);
    npcs.clearNPCData();
    EXPECT_EQ(npcs.getNPCCount(), 0u);
}
