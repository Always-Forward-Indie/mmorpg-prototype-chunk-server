// Unit tests for CooldownService (Logger only; the shared per-server table
// extracted from SkillSystem in Increment 8).
#include "services/CooldownService.hpp"

#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct CooldownFixture : ::testing::Test
{
    Logger logger{"test"};
    CooldownService cd{logger};
};

} // namespace

TEST_F(CooldownFixture, SetAndExpiry)
{
    EXPECT_FALSE(cd.isOnCooldown(1, "fireball"));
    EXPECT_TRUE(cd.isSkillAvailable(1, "fireball"));
    cd.setCooldown(1, "fireball", 60000);
    EXPECT_TRUE(cd.isOnCooldown(1, "fireball"));
    EXPECT_FALSE(cd.isSkillAvailable(1, "fireball"));
    // Other casters / skills unaffected
    EXPECT_FALSE(cd.isOnCooldown(2, "fireball"));
    EXPECT_FALSE(cd.isOnCooldown(1, "frostbolt"));
}

TEST_F(CooldownFixture, TrySetIsAtomicCheckAndSet)
{
    EXPECT_TRUE(cd.trySetCooldown(1, "fireball", 60000));
    EXPECT_FALSE(cd.trySetCooldown(1, "fireball", 60000)); // second claim fails
    bool onGCD = true;
    EXPECT_FALSE(cd.trySetCooldown(1, "fireball", 60000, 0, &onGCD));
    EXPECT_FALSE(onGCD); // per-skill reject, not GCD
}

TEST_F(CooldownFixture, GCDClaimedAtomicallyWithSkill)
{
    bool onGCD = false;
    EXPECT_TRUE(cd.trySetCooldown(1, "fireball", 60000, 1500, &onGCD));
    EXPECT_FALSE(onGCD);
    EXPECT_TRUE(cd.isGCDActive(1));
    // Another skill is rejected via GCD even though it has no cooldown itself
    EXPECT_FALSE(cd.trySetCooldown(1, "frostbolt", 60000, 1500, &onGCD));
    EXPECT_TRUE(onGCD);
    // GCD read-only probe does not consume
    EXPECT_TRUE(cd.isGCDActive(1));
    EXPECT_FALSE(cd.isGCDActive(2));
}

TEST_F(CooldownFixture, ConcurrentClaimsSingleWinner)
{
    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::vector<int> wins(kThreads, 0);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&, i] {
            if (cd.trySetCooldown(7, "fireball", 60000))
                wins[i] = 1;
        });
    }
    for (auto &t : threads)
        t.join();
    int total = 0;
    for (int w : wins)
        total += w;
    EXPECT_EQ(total, 1); // HIGH-1: no TOCTOU double-claim
}

TEST_F(CooldownFixture, RestoreSemantics)
{
    cd.restoreCooldown(1, "fireball", 0); // no-op
    cd.restoreCooldown(1, "fireball", -5); // no-op
    EXPECT_FALSE(cd.isOnCooldown(1, "fireball"));
    cd.restoreCooldown(1, "fireball", 60000);
    EXPECT_TRUE(cd.isOnCooldown(1, "fireball"));
    // Restore overwrites unconditionally (login-time remaining wins)
    cd.restoreCooldown(1, "fireball", 60000);
    EXPECT_TRUE(cd.isOnCooldown(1, "fireball"));
}

TEST_F(CooldownFixture, UpdateCooldownsCollectsExpired)
{
    cd.setCooldown(1, "instant", 0); // already expired
    cd.setCooldown(1, "long", 60000);
    cd.updateCooldowns();
    EXPECT_FALSE(cd.isOnCooldown(1, "instant")); // erased
    EXPECT_TRUE(cd.isOnCooldown(1, "long"));     // kept
}
