// Unit tests for ZoneEventManager (nullptr GameServices: template paths).
#include "services/ZoneEventManager.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

ZoneEventManager::ZoneEventTemplate makeEvent(
    const std::string &slug, int zone, float loot, float spawn, float speed)
{
    ZoneEventManager::ZoneEventTemplate t;
    t.slug = slug;
    t.gameZoneId = zone;
    t.triggerType = "manual";
    t.durationSec = 1200;
    t.lootMultiplier = loot;
    t.spawnRateMultiplier = spawn;
    t.mobSpeedMultiplier = speed;
    return t;
}

struct ZoneEventFixture : ::testing::Test
{
    ZoneEventManager mgr{nullptr};
};

} // namespace

TEST_F(ZoneEventFixture, DefaultsWithNoEvent)
{
    EXPECT_FLOAT_EQ(mgr.getLootMultiplier(7), 1.0f);
    EXPECT_FLOAT_EQ(mgr.getMobSpeedMultiplier(7), 1.0f);
    EXPECT_FLOAT_EQ(mgr.getSpawnRateMultiplier(7), 1.0f);
    EXPECT_FALSE(mgr.hasActiveEvent(7));
}

TEST_F(ZoneEventFixture, StartEndLifecycle)
{
    mgr.loadTemplates({makeEvent("feast", 7, 2.0f, 1.5f, 1.25f)});
    mgr.startEvent("feast");
    EXPECT_TRUE(mgr.hasActiveEvent(7));
    EXPECT_FLOAT_EQ(mgr.getLootMultiplier(7), 2.0f);
    EXPECT_FLOAT_EQ(mgr.getSpawnRateMultiplier(7), 1.5f);
    EXPECT_FLOAT_EQ(mgr.getMobSpeedMultiplier(7), 1.25f);
    EXPECT_FALSE(mgr.hasActiveEvent(8)); // other zone unaffected
    mgr.endEvent("feast");
    EXPECT_FALSE(mgr.hasActiveEvent(7));
    EXPECT_FLOAT_EQ(mgr.getLootMultiplier(7), 1.0f);
}

TEST_F(ZoneEventFixture, UnknownEventIsNoOp)
{
    mgr.loadTemplates({makeEvent("feast", 7, 2.0f, 1.5f, 1.25f)});
    mgr.startEvent("nope");
    EXPECT_FALSE(mgr.hasActiveEvent(7));
    mgr.endEvent("nope"); // must not crash
}
