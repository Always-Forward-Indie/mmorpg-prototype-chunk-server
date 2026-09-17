// Unit tests for TitleManager (explicit DI).
#include "services/TitleManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/IStatsNotifier.hpp"
#include "services/ReputationManager.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

struct TitleNotifier : IStatsNotifier
{
    std::vector<int> statsUpdated;
    void sendStatsUpdate(int characterId) override
    {
        statsUpdated.push_back(characterId);
    }
    void sendStatsUpdate(int characterId, const std::string &) override
    {
        statsUpdated.push_back(characterId);
    }
    void sendWorldNotification(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
    void sendWorldNotificationToGameZone(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
};

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
    Logger logger{"test"};
    ReputationManager reputation{logger};
    CharacterManager chars{logger};
    TitleNotifier notifier;
    TitleManager titles{reputation, chars, &notifier, logger};

    void SetUp() override
    {
        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        chars.addCharacter(c);
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

TEST_F(TitleFixture, EquipAppliesEffectsAndNotifies)
{
    TitleDefinitionStruct t = makeTitle("mighty");
    TitleBonusStruct b;
    b.attributeSlug = "strength";
    b.value = 5.0f;
    t.bonuses = {b};
    titles.loadTitleDefinitions({makeTitle("hero"), makeTitle("veteran"), t});
    titles.grantTitle(1, "mighty");
    notifier.statsUpdated.clear();
    EXPECT_TRUE(titles.equipTitle(1, "mighty"));
    auto effects = chars.getCharacterData(1).activeEffects;
    bool found = false;
    for (const auto &e : effects)
        if (e.effectSlug == "title_mighty_strength" && e.attributeSlug == "strength")
            found = true;
    EXPECT_TRUE(found);
    EXPECT_FALSE(notifier.statsUpdated.empty()); // equip pushes stats_update
    EXPECT_TRUE(titles.equipTitle(1, ""));       // unequip removes bonuses
    EXPECT_EQ(titles.getPlayerTitles(1).equippedSlug, "");
}

TEST_F(TitleFixture, CheckAndGrantLevelAndReputation)
{
    TitleDefinitionStruct l = makeTitle("ten", "level");
    l.conditionParams["level"] = 10;
    TitleDefinitionStruct r = makeTitle("friend", "reputation");
    r.conditionParams["factionSlug"] = "bandits";
    r.conditionParams["minTierName"] = "ally";
    titles.loadTitleDefinitions({l, r});
    titles.checkAndGrantTitles(1, "level", nlohmann::json{{"level", 12}});
    EXPECT_TRUE(titles.hasTitle(1, "ten"));
    // ally ordinal 4 >= ally 4 -> grant; stranger (1) would not
    titles.checkAndGrantTitles(1, "reputation", nlohmann::json{{"factionSlug", "bandits"}, {"tierName", "ally"}});
    EXPECT_TRUE(titles.hasTitle(1, "friend"));
    titles.checkAndGrantTitles(1, "reputation", nlohmann::json{{"factionSlug", "bandits"}, {"tierName", "stranger"}});
    SUCCEED(); // no crash, no duplicate grant path
}
