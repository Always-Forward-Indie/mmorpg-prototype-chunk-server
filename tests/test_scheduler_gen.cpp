// Unit tests for Scheduler + Generators (chunk utils).
#include "utils/Generators.hpp"
#include "utils/Scheduler.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

TEST(Scheduler, OneShotTaskFires)
{
    Scheduler sched;
    sched.start();
    std::atomic<int> fires{0};
    Task t([&] { ++fires; }, 50, std::chrono::steady_clock::now(), 1);
    sched.scheduleTask(t);
    for (int i = 0; i < 100 && fires.load() < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    sched.stop();
    EXPECT_GE(fires.load(), 1);
}

TEST(Scheduler, RemovedTaskNeverFires)
{
    Scheduler sched;
    sched.start();
    std::atomic<int> fires{0};
    Task t([&] { ++fires; }, 30, std::chrono::steady_clock::now() + std::chrono::seconds(5), 2);
    sched.scheduleTask(t);
    sched.removeTask(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    sched.stop();
    EXPECT_EQ(fires.load(), 0);
}

TEST(Generators, MobUidUniquePositive)
{
    int a = Generators::generateUniqueMobUID();
    int b = Generators::generateUniqueMobUID();
    EXPECT_GT(a, 0);
    EXPECT_GT(b, 0);
    EXPECT_NE(a, b);
}

TEST(Generators, SimpleRandomInRange)
{
    for (int i = 0; i < 50; ++i)
    {
        int v = Generators::generateSimpleRandomNumber(5, 10);
        EXPECT_GE(v, 5);
        EXPECT_LE(v, 10);
    }
}

TEST(Generators, TimeBasedKeyIsTimeBucketed)
{
    // NOTE: not monotonic — a random suffix breaks ordering within one ms,
    // and there are currently no callers. Pinned as-is; do not rely on order.
    long long a = Generators::generateUniqueTimeBasedKey(7);
    EXPECT_GT(a, 0);
    // Same time bucket magnitude (ms % 1e9, scaled): both keys are huge.
    EXPECT_GT(a, 1000000000LL);
}
