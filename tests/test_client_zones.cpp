// Unit tests for chunk ClientManager (generations, ping tracking)
// + GameZoneManager (AABB/shape lookup). Logger only (+unconnected sockets).
#include "services/ClientManager.hpp"
#include "services/GameZoneManager.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

namespace
{

using boost::asio::ip::tcp;

ClientDataStruct makeClient(int id, int charId = 0)
{
    ClientDataStruct c;
    c.clientId = id;
    c.characterId = charId;
    return c;
}

struct ClientFixture : ::testing::Test
{
    Logger logger{"test"};
    ClientManager mgr{logger};
    boost::asio::io_context ioc;
};

GameZoneStruct makeRect(int id, float minX, float maxX, float minY, float maxY)
{
    GameZoneStruct z;
    z.id = id;
    z.minX = minX;
    z.maxX = maxX;
    z.minY = minY;
    z.maxY = maxY;
    return z;
}

PositionStruct pos(float x, float y)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    return p;
}

} // namespace

TEST_F(ClientFixture, LoadAndResolve)
{
    mgr.loadClientsList({makeClient(1, 101), makeClient(2, 102)});
    EXPECT_EQ(mgr.getClientData(1).characterId, 101);
    EXPECT_EQ(mgr.getClientDataByCharacterId(102).clientId, 2);
    EXPECT_EQ(mgr.getClientsList().size(), 2u);
}

TEST_F(ClientFixture, WorldReadyFlag)
{
    mgr.loadClientData(makeClient(1));
    EXPECT_FALSE(mgr.isClientWorldReady(1));
    mgr.setClientWorldReady(1, true);
    EXPECT_TRUE(mgr.isClientWorldReady(1));
}

TEST_F(ClientFixture, SocketReconnectBumpsGeneration)
{
    mgr.loadClientData(makeClient(1));
    auto s1 = std::make_shared<tcp::socket>(ioc);
    auto s2 = std::make_shared<tcp::socket>(ioc);
    uint64_t g1 = mgr.setClientSocket(1, s1);
    EXPECT_NE(g1, 0u);
    uint64_t g2 = mgr.setClientSocket(1, s2);
    EXPECT_NE(g2, g1);
    EXPECT_FALSE(mgr.isLiveRegistration(1, g1)); // stale gen dead
    EXPECT_TRUE(mgr.isLiveRegistration(1, g2));  // live gen valid
    EXPECT_EQ(mgr.getSocketGen(1), g2);
    EXPECT_EQ(mgr.getClientSocket(1), s2);
}

TEST_F(ClientFixture, StaleGenerationRemoveIsNoOp)
{
    mgr.loadClientData(makeClient(1));
    auto s1 = std::make_shared<tcp::socket>(ioc);
    auto s2 = std::make_shared<tcp::socket>(ioc);
    uint64_t g1 = mgr.setClientSocket(1, s1);
    mgr.setClientSocket(1, s2); // reconnect
    EXPECT_FALSE(mgr.removeClientData(1, g1)); // stale gen: keep live
    EXPECT_NE(mgr.getClientSocket(1), nullptr);
    EXPECT_TRUE(mgr.removeClientData(1)); // unguarded: removes
    EXPECT_EQ(mgr.getClientSocket(1), nullptr);
}

TEST_F(ClientFixture, PingTracking)
{
    mgr.loadClientData(makeClient(1));
    mgr.loadClientData(makeClient(2));
    mgr.recordPingTime(1);
    mgr.recordPingTime(2);
    // Huge timeout: nobody is inactive.
    EXPECT_TRUE(mgr.getInactiveClientIds(3600).empty());
    mgr.removePingTime(1);
    // Zero timeout: everyone with a recorded ping is older than 0s... except removed.
    auto inactive = mgr.getInactiveClientIds(0);
    EXPECT_EQ(inactive.size(), 1u);
    EXPECT_EQ(inactive[0], 2);
}

TEST(GameZones, RectLookupAndReload)
{
    Logger logger{"test"};
    GameZoneManager zones(logger);
    EXPECT_FALSE(zones.getZoneForPosition(pos(5, 5)).has_value());
    zones.loadGameZones({makeRect(1, 0, 10, 0, 10), makeRect(2, 20, 30, 20, 30)});
    auto z = zones.getZoneForPosition(pos(5, 5));
    ASSERT_TRUE(z.has_value());
    EXPECT_EQ(z->id, 1);
    EXPECT_FALSE(zones.getZoneForPosition(pos(15, 15)).has_value());
    EXPECT_EQ(zones.getAllZones().size(), 2u);
    zones.loadGameZones({makeRect(3, 0, 100, 0, 100)});
    EXPECT_EQ(zones.getZoneForPosition(pos(5, 5))->id, 3);
}
