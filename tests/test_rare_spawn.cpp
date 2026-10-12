// Unit tests for rare-spawn replacement (migration 108).
// When a common family mob is due to respawn, the rare variant rolls
// its percent chance and spawns instead (same slot/position logic).
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"

#include <gtest/gtest.h>

namespace
{

MobDataStruct makeTemplate(int id, const std::string &slug)
{
    MobDataStruct m;
    m.id = id;
    m.slug = slug;
    m.name = slug;
    m.level = 1;
    m.maxHealth = 50;
    m.currentHealth = 50;
    m.radius = 10;
    return m;
}

SpawnZoneStruct makeZone()
{
    SpawnZoneStruct z;
    z.zoneId = 9001;
    z.zoneName = "arena";
    z.gameZoneId = 9001;
    z.shape = ZoneShape::RECT;
    z.minX = 0.0f;
    z.maxX = 200.0f;
    z.minY = 0.0f;
    z.maxY = 200.0f;
    SpawnZoneMobEntry e;
    e.mobId = 4;
    e.maxCount = 1;
    e.respawnTime = std::chrono::seconds(0);
    z.mobEntries.push_back(e);
    return z;
}

struct RareFixture : ::testing::Test
{
    Logger logger{"test"};
    MobManager mobs{logger};
    SpawnZoneManager zones{mobs, logger};

    void SetUp() override
    {
        MobDataStruct fox = makeTemplate(4, "ForestFox");
        MobDataStruct spirit = makeTemplate(8, "SpiritFox");
        spirit.isRare = true;
        spirit.rareOfMobId = 4;
        spirit.rareSpawnChance = 100.0f;
        mobs.setListOfMobs({fox, spirit});
        zones.loadMobSpawnZones({makeZone()});
    }
};

} // namespace

TEST_F(RareFixture, HundredPercentChanceSpawnsRare)
{
    auto spawned = zones.spawnMobsInZone(9001);
    ASSERT_EQ(spawned.size(), 1u);
    EXPECT_EQ(spawned[0].id, 8);
    EXPECT_EQ(spawned[0].slug, "SpiritFox");
}

TEST_F(RareFixture, ZeroChanceSpawnsCommon)
{
    MobDataStruct spirit;
    spirit.id = 8;
    spirit.slug = "SpiritFox";
    spirit.rareOfMobId = 4;
    spirit.rareSpawnChance = 0.0f;
    MobDataStruct fox = makeTemplate(4, "ForestFox");
    mobs.setListOfMobs({fox, spirit});
    zones.loadMobSpawnZones({makeZone()});
    auto spawned = zones.spawnMobsInZone(9001);
    ASSERT_EQ(spawned.size(), 1u);
    EXPECT_EQ(spawned[0].id, 4);
}

TEST_F(RareFixture, NoRareBoundSpawnsCommon)
{
    // Fresh managers: setListOfMobs upserts without clearing, so the
    // fixture's spirit (id 8) would linger here.
    MobManager freshMobs{logger};
    SpawnZoneManager freshZones{freshMobs, logger};
    freshMobs.setListOfMobs({makeTemplate(4, "ForestFox")});
    freshZones.loadMobSpawnZones({makeZone()});
    auto spawned = freshZones.spawnMobsInZone(9001);
    ASSERT_EQ(spawned.size(), 1u);
    EXPECT_EQ(spawned[0].id, 4);
}

TEST_F(RareFixture, RareLookupByFamily)
{
    auto rare = mobs.getRareVariantOf(4);
    EXPECT_EQ(rare.id, 8);
    EXPECT_EQ(mobs.getRareVariantOf(999).id, 0);
    EXPECT_EQ(mobs.getRareVariantOf(0).id, 0);
}
