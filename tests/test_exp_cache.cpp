// Unit tests for ExperienceCacheManager (DI: Logger only, no GameServices).
#include "services/ExperienceCacheManager.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace
{

ExperienceLevelEntry entry(int level, int xp)
{
    ExperienceLevelEntry e;
    e.level = level;
    e.experiencePoints = xp;
    return e;
}

struct CacheFixture : ::testing::Test
{
    Logger logger{"test"};
    ExperienceCacheManager cache{logger};
};

} // namespace

TEST_F(CacheFixture, EmptyByDefault)
{
    EXPECT_FALSE(cache.isTableLoaded());
    EXPECT_EQ(cache.getTableSize(), 0u);
    EXPECT_EQ(cache.getMaxLevel(), 0);
}

TEST_F(CacheFixture, SetAndQuery)
{
    cache.setExperienceTable({entry(1, 0), entry(2, 100), entry(3, 300)});
    EXPECT_TRUE(cache.isTableLoaded());
    EXPECT_EQ(cache.getTableSize(), 3u);
    EXPECT_EQ(cache.getMaxLevel(), 3);
    EXPECT_EQ(cache.getExperienceForLevel(1), 0);
    EXPECT_EQ(cache.getExperienceForLevel(2), 100);
    EXPECT_EQ(cache.getExperienceForLevel(3), 300);
}

TEST_F(CacheFixture, ClearAndReload)
{
    cache.setExperienceTable({entry(1, 0), entry(2, 100)});
    EXPECT_TRUE(cache.isTableLoaded());
    cache.clearCache();
    EXPECT_FALSE(cache.isTableLoaded());
    EXPECT_EQ(cache.getTableSize(), 0u);
    cache.setExperienceTable({entry(1, 0)});
    EXPECT_TRUE(cache.isTableLoaded());
    EXPECT_EQ(cache.getMaxLevel(), 1);
}

TEST_F(CacheFixture, ManualReloadRequestDoesNotThrow)
{
    cache.loadExperienceTableFromGameServer(); // no-op without worker by design
    cache.refreshFromGameServer();
    SUCCEED();
}
