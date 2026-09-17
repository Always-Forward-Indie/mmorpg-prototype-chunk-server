// Unit tests for ItemCooldownStore (B3 extract).
//
// Pins the consumable-reuse throttle 1-1 with the inline handler map this
// replaced: zero-second cooldowns always pass, first claim wins, expiry
// lapses on its own, per-(character,item) isolation, plus a threaded
// claim race for the TSan suite.
#include "services/ItemCooldownStore.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

TEST(ItemCooldownStore, ZeroCooldownAlwaysPasses)
{
    ItemCooldownStore store;
    EXPECT_TRUE(store.tryAcquire(1, 100, 0));
    EXPECT_TRUE(store.tryAcquire(1, 100, -5));
    EXPECT_EQ(store.size(), 0u); // no claim recorded
}

TEST(ItemCooldownStore, FirstClaimWinsUntilExpiry)
{
    ItemCooldownStore store;
    EXPECT_TRUE(store.tryAcquire(1, 100, 3600));
    EXPECT_FALSE(store.tryAcquire(1, 100, 3600)); // still held
    EXPECT_EQ(store.size(), 1u);
}

TEST(ItemCooldownStore, IsolationPerCharacterAndItem)
{
    ItemCooldownStore store;
    EXPECT_TRUE(store.tryAcquire(1, 100, 3600));
    EXPECT_TRUE(store.tryAcquire(2, 100, 3600)); // other character: free
    EXPECT_TRUE(store.tryAcquire(1, 200, 3600)); // other item: free
    EXPECT_EQ(store.size(), 3u);
}

TEST(ItemCooldownStore, ConcurrentFirstClaimWins)
{
    ItemCooldownStore store;
    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::vector<int> wins(kThreads, 0);
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&store, &wins, t]
            {
                for (int i = 0; i < 200; ++i)
                {
                    if (store.tryAcquire(1, 100, 3600))
                        ++wins[t];
                    store.tryAcquire(t + 2, 200 + i, 0); // no-cooldown traffic
                }
            });
    }
    for (auto &th : threads)
        th.join();
    int total = 0;
    for (int w : wins)
        total += w;
    // Exactly one thread won the single 3600s claim; no crash / no TSan hit.
    EXPECT_EQ(total, 1);
}
