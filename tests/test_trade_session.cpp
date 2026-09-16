// Unit tests for TradeSessionManager: lifecycle, invites, TTL behavior.
// Pure in-memory logic (Logger only) - no DB, no sockets.
#include "services/TradeSessionManager.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

struct TradeFixture : ::testing::Test
{
    Logger logger{"test"};
    TradeSessionManager mgr{logger};
};

} // namespace

TEST_F(TradeFixture, CreateAndResolveByIdAndCharacter)
{
    TradeSessionStruct &s = mgr.createSession(1, 101, 2, 102);
    const std::string sid = s.sessionId; // copy: reference may invalidate on later inserts
    EXPECT_FALSE(sid.empty());
    EXPECT_EQ(s.charAId, 101);
    EXPECT_EQ(s.charBId, 102);
    EXPECT_EQ(s.clientAId, 1);
    EXPECT_EQ(s.clientBId, 2);

    TradeSessionStruct *byId = mgr.getSession(sid);
    ASSERT_NE(byId, nullptr);
    EXPECT_EQ(byId->sessionId, sid);

    EXPECT_EQ(mgr.getSessionByCharacter(101)->sessionId, sid);
    EXPECT_EQ(mgr.getSessionByCharacter(102)->sessionId, sid);
}

TEST_F(TradeFixture, UnknownLookupsAreSafeNoOps)
{
    EXPECT_EQ(mgr.getSession("nope"), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(999), nullptr);
    mgr.closeSession("nope"); // must not crash
    mgr.closeSessionByCharacter(999);
}

TEST_F(TradeFixture, RecreateForSameCharacterClosesOldSession)
{
    const std::string sid1 = mgr.createSession(1, 101, 2, 102).sessionId;
    const std::string sid2 = mgr.createSession(1, 101, 3, 103).sessionId;
    EXPECT_NE(sid1, sid2);
    EXPECT_EQ(mgr.getSession(sid1), nullptr);              // old session gone
    EXPECT_EQ(mgr.getSessionByCharacter(102), nullptr);    // old peer evicted
    EXPECT_EQ(mgr.getSessionByCharacter(101)->sessionId, sid2);
    EXPECT_EQ(mgr.getSessionByCharacter(103)->sessionId, sid2);
}

TEST_F(TradeFixture, CloseSessionRemovesBothMappings)
{
    const std::string sid = mgr.createSession(1, 101, 2, 102).sessionId;
    mgr.closeSession(sid);
    EXPECT_EQ(mgr.getSession(sid), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(101), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(102), nullptr);
}

TEST_F(TradeFixture, CloseSessionByCharacterRemovesWholeSession)
{
    const std::string sid = mgr.createSession(1, 101, 2, 102).sessionId;
    mgr.closeSessionByCharacter(102);
    EXPECT_EQ(mgr.getSession(sid), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(101), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(102), nullptr);
}

TEST_F(TradeFixture, InviteAcceptConsumesOnce)
{
    mgr.addInvite(101, 102);
    EXPECT_TRUE(mgr.consumeInvite(101, 102));
    EXPECT_FALSE(mgr.consumeInvite(101, 102)); // already consumed
}

TEST_F(TradeFixture, InviteAcceptRequiresMatchingPair)
{
    mgr.addInvite(101, 102);
    EXPECT_FALSE(mgr.consumeInvite(103, 102)); // wrong sender
    EXPECT_FALSE(mgr.consumeInvite(101, 999)); // wrong recipient
    EXPECT_TRUE(mgr.consumeInvite(101, 102));  // original still valid
}

TEST_F(TradeFixture, RemoveInvitesForClearsBothDirections)
{
    mgr.addInvite(201, 202);
    mgr.addInvite(201, 203);
    mgr.addInvite(204, 201);
    mgr.removeInvitesFor(201);
    EXPECT_FALSE(mgr.consumeInvite(201, 202));
    EXPECT_FALSE(mgr.consumeInvite(201, 203));
    EXPECT_FALSE(mgr.consumeInvite(204, 201));
}

TEST_F(TradeFixture, CleanupKeepsFreshSessions)
{
    // TTL is 60s: a just-created session must survive cleanup.
    const std::string sid = mgr.createSession(1, 101, 2, 102).sessionId;
    mgr.cleanupExpiredSessions();
    EXPECT_NE(mgr.getSession(sid), nullptr);
    EXPECT_NE(mgr.getSessionByCharacter(101), nullptr);
}

TEST_F(TradeFixture, RecreateWithinSameMillisecondKeepsSingleSession)
{
    // sessionId embeds ms time: two back-to-back creates for one pair must
    // still leave exactly one live session (the second replaces the first).
    mgr.createSession(1, 101, 2, 102);
    const std::string sid2 = mgr.createSession(1, 101, 2, 102).sessionId;
    EXPECT_NE(mgr.getSession(sid2), nullptr);
    EXPECT_NE(mgr.getSessionByCharacter(101), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(101)->sessionId, sid2);
    EXPECT_EQ(mgr.getSessionByCharacter(102)->sessionId, sid2);
}
