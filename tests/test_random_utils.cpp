// Unit tests for RandomUtils — the single source of truth for server-side RNG.
// Rules (tests/README.md): assert ranges/invariants over many samples, never
// exact values. Determinism is checked by re-seeding, not by golden numbers.
#include "utils/RandomUtils.hpp"

#include <atomic>
#include <cmath>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

TEST(RandomUtils, Uniform01StaysInRange)
{
    for (int i = 0; i < 2000; ++i)
    {
        float v = RandomUtils::uniform01();
        EXPECT_GE(v, 0.0f);
        EXPECT_LT(v, 1.0f);
    }
}

TEST(RandomUtils, RangeAndRangeIntRespectBounds)
{
    for (int i = 0; i < 2000; ++i)
    {
        float f = RandomUtils::range(-5.0f, 10.0f);
        EXPECT_GE(f, -5.0f);
        EXPECT_LT(f, 10.0f);

        int n = RandomUtils::rangeInt(0, 4);
        EXPECT_GE(n, 0);
        EXPECT_LE(n, 4);

        float a = RandomUtils::angle();
        EXPECT_GE(a, 0.0f);
        EXPECT_LT(a, 2.0f * 3.14159265358979323846f);
    }
}

TEST(RandomUtils, RangeIntHitsEveryBucket)
{
    // 6000 draws over 6 buckets: a stuck/biased engine fails in practice.
    bool seen[6] = {false, false, false, false, false, false};
    for (int i = 0; i < 6000; ++i)
        seen[RandomUtils::rangeInt(0, 5)] = true;
    for (bool s : seen)
        EXPECT_TRUE(s);
}

TEST(RandomUtils, SeedForTestsReproducesSequence)
{
    // Same seed on one thread => same sequence (deterministic tests).
    RandomUtils::seedForTests(12345u);
    std::vector<float> first;
    for (int i = 0; i < 50; ++i)
        first.push_back(RandomUtils::uniform01());

    RandomUtils::seedForTests(12345u);
    for (int i = 0; i < 50; ++i)
        EXPECT_FLOAT_EQ(RandomUtils::uniform01(), first[static_cast<size_t>(i)]);

    // Different seed => (practically certainly) different sequence.
    RandomUtils::seedForTests(999u);
    bool differs = false;
    for (int i = 0; i < 50; ++i)
    {
        if (RandomUtils::uniform01() != first[static_cast<size_t>(i)])
        {
            differs = true;
            break;
        }
    }
    EXPECT_TRUE(differs);
}

TEST(RandomUtils, ConcurrentUseIsRaceFree)
{
    // Regression shape (mirrors CombatCalculator.ConcurrentRollsAreRaceFree):
    // shared distributions used to race across ThreadPool workers. Hammer the
    // helper from 8 threads and assert invariants (TSan fails loudly on races).
    constexpr int kThreads = 8;
    constexpr int kIters = 500;
    std::vector<std::thread> threads;
    std::atomic<int> bad{0};
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&]
            {
                for (int j = 0; j < kIters; ++j)
                {
                    float u = RandomUtils::uniform01();
                    int n = RandomUtils::rangeInt(-3, 7);
                    float a = RandomUtils::angle();
                    if (u < 0.0f || u >= 1.0f || n < -3 || n > 7 || a < 0.0f ||
                        !std::isfinite(u) || !std::isfinite(a))
                        ++bad;
                }
            });
    }
    for (auto &t : threads)
        t.join();
    EXPECT_EQ(bad.load(), 0);
}
