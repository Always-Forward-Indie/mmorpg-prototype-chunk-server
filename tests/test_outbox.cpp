// Unit tests for FactOutbox (header-only, no asio/DB).
// Covers: key uniqueness, flush/backoff timing, ack matching, expiry sweep,
// counters snapshot, fact-type allowlist.
#include "services/FactOutbox.hpp"

#include <gtest/gtest.h>

TEST(FactOutbox, KeysUniqueAndMonotonic)
{
    FactOutbox box;
    const std::string k1 = box.assignKey(7, "saveReputation");
    const std::string k2 = box.assignKey(7, "saveReputation");
    EXPECT_NE(k1, k2);
    // "{characterId}:{factType}:{bootId}:{seq}": same prefix, seq grows.
    EXPECT_EQ(k1.rfind("7:saveReputation:", 0), 0u);
    EXPECT_EQ(k2.rfind("7:saveReputation:", 0), 0u);
    EXPECT_NE(k1.substr(17), k2.substr(17));
}

TEST(FactOutbox, BootIdsDifferAcrossInstances)
{
    // Restart safety: post-restart keys must not collide with pre-restart
    // ones (else fact_keys_applied falsely dedups them = LOSS).
    FactOutbox a, b;
    EXPECT_NE(a.assignKey(7, "saveReputation"), b.assignKey(7, "saveReputation"));
}

TEST(FactOutbox, IsFactTypeAllowlist)
{
    // Only game handlers wired with claim/ack may be listed (see header).
    EXPECT_TRUE(FactOutbox::isFactType("saveReputation"));
    EXPECT_TRUE(FactOutbox::isFactType("saveLearnedSkill"));
    EXPECT_TRUE(FactOutbox::isFactType("saveInventoryChange"));
    EXPECT_FALSE(FactOutbox::isFactType("savePositions"));
    EXPECT_FALSE(FactOutbox::isFactType("chunkServerConnection"));
    EXPECT_FALSE(FactOutbox::isFactType("pingClient"));
    EXPECT_FALSE(FactOutbox::isFactType(""));
}

TEST(FactOutbox, DueFlushBackoff)
{
    FactOutbox box;
    box.store("7:saveReputation:1", "p1", 0);
    // Due immediately on first flush.
    auto d0 = box.dueFlush(0);
    ASSERT_EQ(d0.size(), 1u);
    EXPECT_EQ(d0[0].key, "7:saveReputation:1");
    // Backoff: next due at +5000, not before.
    EXPECT_TRUE(box.dueFlush(4999).empty());
    auto d1 = box.dueFlush(5000);
    ASSERT_EQ(d1.size(), 1u);
    // Second backoff doubles: +10000 from last flush.
    EXPECT_TRUE(box.dueFlush(14999).empty());
    EXPECT_EQ(box.dueFlush(15000).size(), 1u);
}

TEST(FactOutbox, AckDropsPending)
{
    FactOutbox box;
    box.store("7:saveReputation:1", "p1", 0);
    EXPECT_TRUE(box.ack("7:saveReputation:1"));
    EXPECT_EQ(box.size(), 0u);
    EXPECT_FALSE(box.ack("7:saveReputation:1")); // duplicate ack
    EXPECT_FALSE(box.ack("nope"));               // unknown key
    auto snap = box.snapshot();
    EXPECT_EQ(snap.acked, 1u);
}

TEST(FactOutbox, SweepExpiredReportsOldest)
{
    FactOutbox box;
    box.store("old", "p", 0);
    box.store("fresh", "p", 190000);
    auto out = box.sweepExpired(200000, 60000);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].key, "old");
    EXPECT_EQ(box.size(), 1u);
    EXPECT_EQ(box.snapshot().expired, 1u);
}

TEST(FactOutbox, CountersSnapshot)
{
    FactOutbox box;
    box.store("a", "p", 0);
    box.dueFlush(0); // sent=1
    box.ack("a");    // acked=1
    auto snap = box.snapshot();
    EXPECT_EQ(snap.pending, 0u);
    EXPECT_EQ(snap.sent, 1u);
    EXPECT_EQ(snap.acked, 1u);
    EXPECT_EQ(snap.expired, 0u);
}
