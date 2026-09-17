// Unit tests for ZoneEventManager (explicit DI: nullable notifier + Logger).
#include "services/IStatsNotifier.hpp"
#include "services/ZoneEventManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
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

struct ZoneNotifier : IStatsNotifier
{
    struct Record
    {
        int zone;
        std::string type;
    };
    std::vector<Record> zoneNotes;
    void sendStatsUpdate(int) override
    {
    }
    void sendStatsUpdate(int, const std::string &) override
    {
    }
    void sendWorldNotification(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
    void sendWorldNotificationToGameZone(int zone,
        const std::string &type,
        const nlohmann::json &,
        const std::string &,
        const std::string &) override
    {
        zoneNotes.push_back({zone, type});
    }
};

struct ZoneEventFixture : ::testing::Test
{
    Logger logger{"test"};
    ZoneNotifier notifier;
    ZoneEventManager mgr{&notifier, logger};
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

TEST_F(ZoneEventFixture, StartEndBroadcastsToZone)
{
    mgr.loadTemplates({makeEvent("feast", 7, 2.0f, 1.5f, 1.25f)});
    mgr.startEvent("feast");
    ASSERT_EQ(notifier.zoneNotes.size(), 1u);
    EXPECT_EQ(notifier.zoneNotes[0].zone, 7);
    EXPECT_EQ(notifier.zoneNotes[0].type, "zone_event_start");
    mgr.endEvent("feast");
    ASSERT_EQ(notifier.zoneNotes.size(), 2u);
    EXPECT_EQ(notifier.zoneNotes[1].type, "zone_event_end");
}

TEST_F(ZoneEventFixture, NullNotifierSkipsBroadcast)
{
    ZoneEventManager silent{nullptr, logger};
    silent.loadTemplates({makeEvent("feast", 7, 2.0f, 1.5f, 1.25f)});
    silent.startEvent("feast"); // must not crash
    EXPECT_TRUE(silent.hasActiveEvent(7));
    silent.endEvent("feast");
    EXPECT_FALSE(silent.hasActiveEvent(7));
}
