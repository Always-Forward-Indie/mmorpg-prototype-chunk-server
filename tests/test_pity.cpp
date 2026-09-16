// Unit tests for PityManager (Logger only, no DB/sockets).
#include "services/PityManager.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

struct PityFixture : ::testing::Test
{
    Logger logger{"test"};
    PityManager pity{logger};
};

} // namespace

TEST_F(PityFixture, UnknownCounterIsZero)
{
    EXPECT_EQ(pity.getKillCount(1, 100), 0);
    EXPECT_EQ(pity.getExtraDropChance(1, 100, 10, 0.5f), 0.0f);
    EXPECT_FALSE(pity.isHardPity(1, 100, 50));
}

TEST_F(PityFixture, LoadAndIncrementAndReset)
{
    pity.loadPityData(1, {{100, 7}, {200, 3}});
    EXPECT_EQ(pity.getKillCount(1, 100), 7);
    EXPECT_EQ(pity.getKillCount(1, 200), 3);
    EXPECT_EQ(pity.getKillCount(2, 100), 0); // other character unaffected

    pity.incrementCounter(1, 100, 0);
    EXPECT_EQ(pity.getKillCount(1, 100), 8);

    pity.resetCounter(1, 100);
    EXPECT_EQ(pity.getKillCount(1, 100), 0);
    EXPECT_EQ(pity.getKillCount(1, 200), 3); // other item unaffected
}

TEST_F(PityFixture, SoftBonusMath)
{
    pity.loadPityData(1, {{100, 12}});
    EXPECT_FLOAT_EQ(pity.getExtraDropChance(1, 100, 10, 0.5f), 1.0f); // (12-10)*0.5
    EXPECT_FLOAT_EQ(pity.getExtraDropChance(1, 100, 12, 0.5f), 0.0f); // at threshold: nothing
    EXPECT_FLOAT_EQ(pity.getExtraDropChance(1, 100, 20, 0.5f), 0.0f); // below threshold
}

TEST_F(PityFixture, HardPityThreshold)
{
    pity.loadPityData(1, {{100, 49}});
    EXPECT_FALSE(pity.isHardPity(1, 100, 50));
    pity.incrementCounter(1, 100, 0);
    EXPECT_TRUE(pity.isHardPity(1, 100, 50));
    pity.resetCounter(1, 100);
    EXPECT_FALSE(pity.isHardPity(1, 100, 50));
}

TEST_F(PityFixture, HintFiresOnceOnCrossing)
{
    int calls = 0;
    auto hint = [&] { ++calls; };
    for (int i = 0; i < 4; ++i)
        pity.incrementCounter(1, 100, 3, hint);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(pity.getKillCount(1, 100), 4);
}

TEST_F(PityFixture, SaveCallbackFiresOnChange)
{
    std::vector<std::string> saved;
    pity.setSaveCallback([&](const std::string &pkt) { saved.push_back(pkt); });
    for (int i = 0; i < 10; ++i)
        pity.incrementCounter(1, 100, 0); // persist fires every 10th increment
    pity.resetCounter(1, 100);            // reset always persists
    ASSERT_EQ(saved.size(), 2u);
    for (const auto &pkt : saved)
    {
        EXPECT_FALSE(pkt.empty());
        EXPECT_EQ(pkt.back(), '\n'); // newline-terminated JSON
    }
    // Without callback: no crash.
    PityManager bare(logger);
    bare.incrementCounter(1, 100, 0);
    bare.resetCounter(1, 100);
}
