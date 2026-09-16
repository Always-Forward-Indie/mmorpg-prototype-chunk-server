// Unit tests for InterestManager (spatial subscriptions, interest v2).
// Migrated from the hand-rolled CHECK-macro style to gtest; same 7 cases.
#include "services/InterestManager.hpp"

#include <gtest/gtest.h>

namespace
{

bool hasCell(const std::vector<InterestManager::CellKey> &v, int cx, int cy)
{
    for (const auto &c : v)
        if (c.cx == cx && c.cy == cy)
            return true;
    return false;
}

struct InterestFixture : ::testing::Test
{
    Logger logger{"test"};
    InterestManager im{logger};

    void SetUp() override
    {
        im.configure(1500.0f, 1, 0.15f, true);
    }
};

} // namespace

// Sections 1-6 share one manager: the sequence is order-dependent
// (first sighting -> jitter -> rehome -> teleport -> watch -> refcount).
TEST_F(InterestFixture, SubscriptionLifecycle)
{
    // 1. First sighting subscribes 3x3.
    {
        auto r = im.onPlayerMoved(5, 10, 0.0f, 0.0f);
        EXPECT_EQ(r.entered.size(), 9u);
        EXPECT_TRUE(r.left.empty());
        auto snap = im.snapshot();
        EXPECT_EQ(snap.tracked.count(5), 1u);
        EXPECT_EQ(snap.members.size(), 9u);
        auto cell = InterestManager::cellFor(-434.0f, -973.0f, 1500.0f);
        EXPECT_EQ(cell.cx, -1);
        EXPECT_EQ(cell.cy, -1);
        EXPECT_NE(snap.members.find(cell), snap.members.end());
    }

    // 2. Hysteresis: small step across the edge does NOT rehome.
    {
        // Anchor cell is (0,0): [0,1500)x[0,1500), margin 225.
        auto r = im.onPlayerMoved(5, 10, 1600.0f, 100.0f); // 100 past edge < margin
        EXPECT_TRUE(r.entered.empty());
        EXPECT_TRUE(r.left.empty());
        r = im.onPlayerMoved(5, 10, 2000.0f, 100.0f); // 500 past edge > margin
        EXPECT_EQ(r.entered.size(), 3u);
        EXPECT_EQ(r.left.size(), 3u);
        auto cells = im.getClientCells(5);
        EXPECT_TRUE(hasCell(cells, 2, -1));
        EXPECT_FALSE(hasCell(cells, -1, -1));
    }

    // 3. Teleport resubscribes fully.
    {
        auto r = im.onPlayerMoved(5, 10, 20000.0f, 20000.0f);
        EXPECT_EQ(r.entered.size(), 9u);
        EXPECT_EQ(r.left.size(), 9u);
    }

    // 4. Watchlist with TTL semantics.
    {
        im.watch(5, 1000007);
        im.watch(5, 0);   // invalid uid ignored
        im.watch(0, 100); // invalid client ignored
        auto wi = im.watchIndex();
        ASSERT_EQ(wi.size(), 1u);
        EXPECT_EQ(wi[1000007].size(), 1u);
        im.unwatch(5, 1000007);
        EXPECT_TRUE(im.watchIndex().empty());
    }

    // 5. Second client shares cells (refcounted members).
    {
        im.onPlayerMoved(6, 11, 20100.0f, 20100.0f);
        auto snap = im.snapshot();
        auto cell = InterestManager::cellFor(20000.0f, 20000.0f, 1500.0f);
        auto it = snap.members.find(cell);
        ASSERT_NE(it, snap.members.end());
        EXPECT_EQ(it->second.size(), 2u);
        im.removeClient(5);
        snap = im.snapshot();
        it = snap.members.find(cell);
        ASSERT_NE(it, snap.members.end());
        EXPECT_EQ(it->second.size(), 1u);
        EXPECT_EQ(im.snapshot().tracked.count(5), 0u);
        im.removeClient(5); // idempotent
    }

    // 6. Disabled => empty snapshot, no tracking.
    {
        im.configure(1500.0f, 1, 0.15f, false);
        EXPECT_TRUE(im.snapshot().members.empty());
        auto r = im.onPlayerMoved(7, 12, 0.0f, 0.0f);
        EXPECT_TRUE(r.entered.empty());
        im.configure(1500.0f, 1, 0.15f, true);
    }
}

TEST_F(InterestFixture, RecipientsFailOpen)
{
    // 7. recipientsFor: subscribers + fail-open (unready/untracked), exclude.
    InterestManager im2(logger);
    im2.configure(1500.0f, 1, 0.15f, true);
    im2.onPlayerMoved(1, 1, 0.0f, 0.0f); // subscribes around origin
    std::vector<std::pair<int, bool>> viewers = {{1, true}, {2, true}, {3, false}};
    auto ids = im2.recipientsFor(100.0f, 100.0f, viewers, -1);
    bool has1 = false, has2 = false, has3 = false;
    for (int id : ids)
    {
        has1 = has1 || id == 1;
        has2 = has2 || id == 2;
        has3 = has3 || id == 3;
    }
    EXPECT_TRUE(has1); // subscriber included
    EXPECT_TRUE(has2); // untracked fail-open included
    EXPECT_TRUE(has3); // not-ready fail-open included
    auto ids2 = im2.recipientsFor(100.0f, 100.0f, viewers, 1);
    for (int id : ids2)
        EXPECT_NE(id, 1);
}
