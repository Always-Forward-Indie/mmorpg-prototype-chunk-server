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
    EXPECT_TRUE(FactOutbox::isFactType("saveDurabilityChange"));
    EXPECT_TRUE(FactOutbox::isFactType("saveCurrencyTransaction"));
    EXPECT_TRUE(FactOutbox::isFactType("timedChampionKilled"));
    EXPECT_TRUE(FactOutbox::isFactType("analyticsEvent"));
    // Typed-vector / delegated / link-level traffic stays legacy.
    EXPECT_FALSE(FactOutbox::isFactType("savePositions"));
    EXPECT_FALSE(FactOutbox::isFactType("saveHpMana"));
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
    box.noteSent();
    box.dueFlush(0); // sent=2 (1 immediate + 1 retry)
    box.ack("a");    // acked=1
    auto snap = box.snapshot();
    EXPECT_EQ(snap.pending, 0u);
    EXPECT_EQ(snap.sent, 2u);
    EXPECT_EQ(snap.acked, 1u);
    EXPECT_EQ(snap.expired, 0u);
    EXPECT_GE(snap.sent, snap.acked); // invariant: sends >= acks
}

TEST(FactOutbox, SetHighWaterForwardOnly)
{
    FactOutbox box;
    box.assignKey(7, "saveReputation"); // seq=1
    box.assignKey(7, "saveReputation"); // seq=2
    EXPECT_EQ(box.nextSeq(), 3u);
    box.setHighWater(100);
    EXPECT_EQ(box.nextSeq(), 101u);
    box.setHighWater(50); // stale/low ignored
    EXPECT_EQ(box.nextSeq(), 101u);
    EXPECT_FALSE(box.bootId().empty());
}

TEST(FactOutbox, UnackedKeysBoundedRespectsLimit)
{
    FactOutbox box;
    for (int i = 0; i < 5; ++i)
        box.store("k" + std::to_string(i), "p", 0);
    auto all = box.unackedKeys(100);
    EXPECT_EQ(all.size(), 5u);
    auto capped = box.unackedKeys(3);
    EXPECT_EQ(capped.size(), 3u);
    // ~120 keys * ~50B/key ~= 6KB < 8KB game inbound cap.
    for (int i = 0; i < 200; ++i)
        box.store("x" + std::to_string(i), "p", 0);
    EXPECT_LE(box.unackedKeys(120).size(), 120u);
}

TEST(FactOutbox, PokeReschedulesOnlyKnown)
{
    FactOutbox box;
    box.store("known", "p", 0);
    box.dueFlush(0); // next due at +5000
    EXPECT_TRUE(box.dueFlush(1000).empty());
    box.poke({"known", "unknown"}, 1000); // unknown ignored
    EXPECT_EQ(box.dueFlush(1000).size(), 1u); // known resent now
}

TEST(FactOutbox, CrashSimRekeyedResendNeverCollides)
{
    // Simulates a chunk crash WITHOUT a WAL: the process dies with its
    // pending map; only the flushed payloads (captured here via dueFlush)
    // survive. A fresh instance (new bootId) re-keys them. Keys must differ
    // (no false dedup game-side = no LOSS), payloads stay identical.
    FactOutbox before;
    before.store(before.assignKey(7, "saveReputation"), "{\"delta\":3}", 0);
    before.store(before.assignKey(7, "saveReputation"), "{\"delta\":5}", 0);
    auto flushed = before.dueFlush(0);
    ASSERT_EQ(flushed.size(), 2u);
    // Crash: `before` is gone. New boot, same payloads, fresh keys.
    FactOutbox after;
    EXPECT_NE(after.bootId(), before.bootId());
    std::vector<std::string> newKeys;
    for (auto &f : flushed)
        newKeys.push_back(after.enqueue(7, "saveReputation", f.payload, 0));
    EXPECT_NE(newKeys[0], flushed[0].key);
    EXPECT_NE(newKeys[1], flushed[1].key);
    EXPECT_NE(newKeys[0], newKeys[1]);
    // And the re-keyed facts are immediately due (convergence, not sleep).
    EXPECT_EQ(after.dueFlush(0).size(), 2u);
}

TEST(FactOutbox, DueFlushOldestFirst)
{
    // Same-character absolute saves must apply in send order: unordered_map
    // iteration would last-writer-win at random. Insert out of alphabetical
    // key order to prove ordering comes from insertion, not the map.
    FactOutbox box;
    box.store("z-last", "p3", 0);
    box.store("a-first", "p1", 0);
    box.store("m-middle", "p2", 0);
    auto due = box.dueFlush(0);
    ASSERT_EQ(due.size(), 3u);
    EXPECT_EQ(due[0].key, "z-last");
    EXPECT_EQ(due[1].key, "a-first");
    EXPECT_EQ(due[2].key, "m-middle");
    // Coalesced re-store keeps its original slot (absolute SET semantics).
    box.store("a-first", "p1b", 100000);
    auto due2 = box.dueFlush(100000);
    ASSERT_EQ(due2.size(), 3u);
    EXPECT_EQ(due2[0].key, "z-last");
    EXPECT_EQ(due2[0].payload, "p3");
}
