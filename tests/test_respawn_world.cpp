// Unit tests for RespawnZoneManager + WorldObjectManager + AmbientSpeechManager
// (Logger only / default ctor).
#include "services/AmbientSpeechManager.hpp"
#include "services/RespawnZoneManager.hpp"
#include "services/WorldObjectManager.hpp"

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

RespawnZoneStruct makeRespawn(int id, float minX, float maxX, float minY, float maxY, bool def = false)
{
    RespawnZoneStruct z;
    z.id = id;
    z.minX = minX;
    z.maxX = maxX;
    z.minY = minY;
    z.maxY = maxY;
    z.isDefault = def;
    // findNearest measures to the spawn point, not the AABB: place it central.
    z.position.positionX = (minX + maxX) / 2.0f;
    z.position.positionY = (minY + maxY) / 2.0f;
    return z;
}

WorldObjectDataStruct makeObject(int id, int zone)
{
    WorldObjectDataStruct o;
    o.id = id;
    o.slug = "obj_" + std::to_string(id);
    o.zoneId = zone;
    o.scope = "global";
    o.respawnSec = 60;
    o.channelTimeSec = 0;
    return o;
}

} // namespace

TEST(RespawnZones, NearestAndRandomPoint)
{
    Logger logger{"test"};
    RespawnZoneManager zones(logger);
    zones.loadRespawnZones({makeRespawn(1, 0, 100, 0, 100), makeRespawn(2, 1000, 1100, 1000, 1100, true)});
    EXPECT_EQ(zones.getAllZones().size(), 2u);
    EXPECT_EQ(zones.findNearest(pos(10, 10)).id, 1);
    EXPECT_EQ(zones.findNearest(pos(1050, 1050)).id, 2);
    PositionStruct p = zones.getRandomPointInZone(zones.findNearest(pos(10, 10)));
    EXPECT_GE(p.positionX, 0.0f);
    EXPECT_LE(p.positionX, 100.0f);
    EXPECT_GE(p.positionY, 0.0f);
    EXPECT_LE(p.positionY, 100.0f);
}

TEST(RespawnZones, EmptyGivesEmpty)
{
    Logger logger{"test"};
    RespawnZoneManager zones(logger);
    EXPECT_EQ(zones.findNearest(pos(0, 0)).id, 0);
    EXPECT_TRUE(zones.getAllZones().empty());
}

TEST(WorldObjects, LoadQueryGlobalState)
{
    Logger logger{"test"};
    WorldObjectManager objs(logger);
    EXPECT_FALSE(objs.isLoaded());
    objs.setWorldObjects({makeObject(1, 7), makeObject(2, 7), makeObject(3, 9)});
    EXPECT_TRUE(objs.isLoaded());
    EXPECT_EQ(objs.getObjectById(1).id, 1);
    EXPECT_EQ(objs.getObjectById(424242).id, 0);
    EXPECT_EQ(objs.getObjectsInZone(7).size(), 2u);
    EXPECT_EQ(objs.getAllObjects().size(), 3u);

    objs.setGlobalState(1, "depleted");
    EXPECT_EQ(objs.getGlobalState(1), "depleted");
    objs.depleteGlobalObject(2);
    EXPECT_NE(objs.getGlobalState(2), "active");
}

TEST(WorldObjects, ChannelSessionImmediateComplete)
{
    Logger logger{"test"};
    WorldObjectManager objs(logger);
    objs.setWorldObjects({makeObject(1, 7)});
    objs.startChannelSession(101, 1, 0); // 0s channel -> completes immediately
    EXPECT_EQ(objs.getActiveChannelObjectId(101), 1);
    auto done = objs.pollCompletedChannels();
    ASSERT_EQ(done.size(), 1u);
    EXPECT_EQ(done[0].first, 101);
    EXPECT_EQ(done[0].second, 1);
    EXPECT_TRUE(objs.pollCompletedChannels().empty());
    EXPECT_TRUE(objs.removeChannelSession(101) == false); // already polled
}

TEST(AmbientSpeech, FilteredPool)
{
    AmbientSpeechManager mgr;
    EXPECT_FALSE(mgr.isLoaded());
    NPCAmbientSpeechConfigStruct cfg;
    cfg.npcId = 5;
    NPCAmbientLineStruct line;
    line.id = 1;
    line.npcId = 5;
    line.lineKey = "npc.idle_1";
    cfg.lines = {line};
    mgr.setAmbientSpeechData({cfg});
    EXPECT_TRUE(mgr.isLoaded());
    PlayerContextStruct ctx;
    auto pool = mgr.buildFilteredPoolForPlayer(5, ctx);
    EXPECT_FALSE(pool.is_null());
    EXPECT_TRUE(mgr.buildFilteredPoolForPlayer(424242, ctx).is_null());
}
