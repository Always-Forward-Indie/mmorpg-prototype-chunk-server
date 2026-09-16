// Unit tests for TitleManager (nullptr GameServices: data paths only).
#include "services/TitleManager.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

TitleDefinitionStruct makeTitle(const std::string &slug, const std::string &cond = "admin_grant")
{
    TitleDefinitionStruct d;
    d.id = 1;
    d.slug = slug;
    d.displayName = slug;
    d.earnCondition = cond;
    return d;
}

struct TitleFixture : ::testing::Test
{
    TitleManager titles{nullptr};

    void SetUp() override
    {
        titles.loadTitleDefinitions({makeTitle("hero"), makeTitle("veteran")});
        PlayerTitleStateStruct st;
        st.characterId = 1;
        st.earnedSlugs = {"hero"};
        titles.loadPlayerTitles(1, st);
    }
};

} // namespace

TEST_F(TitleFixture, DefinitionLookup)
{
    EXPECT_EQ(titles.getTitleDefinition("hero").id, 1);
    EXPECT_EQ(titles.getTitleDefinition("nope").id, 0); // miss sentinel
    EXPECT_EQ(titles.getAllDefinitions().size(), 2u);
}

TEST_F(TitleFixture, HasAndGrant)
{
    EXPECT_TRUE(titles.hasTitle(1, "hero"));
    EXPECT_FALSE(titles.hasTitle(1, "veteran"));
    EXPECT_FALSE(titles.hasTitle(2, "hero"));
    titles.grantTitle(1, "veteran");
    EXPECT_TRUE(titles.hasTitle(1, "veteran"));
}

TEST_F(TitleFixture, EquipUnequip)
{
    EXPECT_TRUE(titles.equipTitle(1, "hero"));
    EXPECT_EQ(titles.getPlayerTitles(1).equippedSlug, "hero");
    EXPECT_TRUE(titles.equipTitle(1, "")); // unequip always valid
    EXPECT_EQ(titles.getPlayerTitles(1).equippedSlug, "");
    EXPECT_FALSE(titles.equipTitle(1, "veteran")); // not earned
    EXPECT_FALSE(titles.equipTitle(2, "hero"));    // not loaded
}

TEST_F(TitleFixture, CheckAndGrantByCondition)
{
    nlohmann::json ev;
    ev["questSlug"] = "q1";
    TitleDefinitionStruct q = makeTitle("quester", "quest");
    q.conditionParams["questSlug"] = "q1";
    titles.loadTitleDefinitions({makeTitle("hero"), makeTitle("veteran"), q});
    titles.checkAndGrantTitles(1, "quest", ev);
    EXPECT_TRUE(titles.hasTitle(1, "quester"));
}

TEST_F(TitleFixture, UnloadClears)
{
    titles.unloadPlayerTitles(1);
    EXPECT_FALSE(titles.hasTitle(1, "hero"));
}
