// Unit tests for the DialogueActionExecutor orchestrator (Wave 4.1).
//
// Validators/builders were covered in phase 2 and Wave 3.1; this drives the
// executor itself with a real GameServices: flags, items, gold, exp, quests,
// reputation and analytics capture. Network leaves through the GameServices
// analytics seam (no sockets/DB).
#include "services/DialogueActionExecutor.hpp"
#include "services/GameServices.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

nlohmann::json actionGroup(std::initializer_list<nlohmann::json> actions)
{
    nlohmann::json g;
    g["actions"] = nlohmann::json::array();
    for (const auto &a : actions)
        g["actions"].push_back(a);
    return g;
}

ItemDataStruct makeItem(int id, const std::string &slug)
{
    ItemDataStruct it;
    it.id = id;
    it.slug = slug;
    it.stackMax = 64;
    return it;
}

struct ExecutorFixture : ::testing::Test
{
    Logger logger{"test"};
    GameServices gs{logger};
    DialogueActionExecutor *exec = nullptr;
    std::vector<std::string> analytics;

    void SetUp() override
    {
        exec = new DialogueActionExecutor(gs, logger);
        gs.setAnalyticsSender([this](const std::string &pkt) { analytics.push_back(pkt); });

        ItemDataStruct gold = makeItem(16, "gold_coin");
        gold.stackMax = 1000000;
        ItemDataStruct potion = makeItem(100, "health_potion");
        gs.getItemManager().setItemsList({gold, potion});

        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 100;
        c.characterMaxMana = 50;
        c.characterCurrentMana = 50;
        gs.getCharacterManager().addCharacter(c);

        QuestStruct q;
        q.id = 1;
        q.slug = "kill_foxes";
        q.clientQuestKey = "key_kill_foxes";
        gs.getQuestManager().setQuests({q});
    }

    void TearDown() override { delete exec; }
};

} // namespace

TEST_F(ExecutorFixture, SetFlagWritesContextAndManager)
{
    PlayerContextStruct ctx;
    auto r = exec->execute(
        actionGroup({{{"type", "set_flag"}, {"key", "met_bob"}, {"bool_value", true}},
            {{"type", "set_flag"}, {"key", "wolves"}, {"int_value", 3}}}),
        1, 77, ctx);
    EXPECT_TRUE(ctx.flagsBool["met_bob"]);
    EXPECT_EQ(ctx.flagsInt["wolves"], 3);
    EXPECT_TRUE(gs.getQuestManager().getFlagBool(1, "met_bob"));
}

TEST_F(ExecutorFixture, GiveItemAddsToInventoryAndNotifies)
{
    PlayerContextStruct ctx;
    auto r = exec->execute(
        actionGroup({{{"type", "give_item"}, {"item_id", 100}, {"quantity", 2}}}), 1, 77, ctx);
    EXPECT_EQ(gs.getInventoryManager().getItemQuantity(1, 100), 2);
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "item_received");
    // Best-effort analytics emitted (character has no sessionId => skipped).
    EXPECT_TRUE(analytics.empty());
}

TEST_F(ExecutorFixture, GiveGoldAndExpApply)
{
    PlayerContextStruct ctx;
    const int expBefore = gs.getCharacterManager().getCharacterData(1).characterExperiencePoints;
    auto r = exec->execute(
        actionGroup({{{"type", "give_gold"}, {"amount", 50}}, {{"type", "give_exp"}, {"amount", 30}}}),
        1, 77, ctx);
    EXPECT_EQ(gs.getInventoryManager().getItemQuantity(1, 16), 50);
    EXPECT_GT(gs.getCharacterManager().getCharacterData(1).characterExperiencePoints, expBefore);
    ASSERT_EQ(r.clientNotifications.size(), 2u);
}

TEST_F(ExecutorFixture, OfferQuestActivatesAndNotifies)
{
    PlayerContextStruct ctx;
    auto r = exec->execute(actionGroup({{{"type", "offer_quest"}, {"slug", "kill_foxes"}}}), 1, 77, ctx);
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "active");
    EXPECT_EQ(ctx.questStates["kill_foxes"], "active");
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "quest_offered");
}

TEST_F(ExecutorFixture, FailQuestMarksFailed)
{
    PlayerContextStruct ctx;
    exec->execute(actionGroup({{{"type", "offer_quest"}, {"slug", "kill_foxes"}}}), 1, 77, ctx);
    auto r = exec->execute(actionGroup({{{"type", "fail_quest"}, {"slug", "kill_foxes"}}}), 1, 77, ctx);
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "failed");
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "quest_failed");
}

TEST_F(ExecutorFixture, ChangeReputationAdjustsFaction)
{
    PlayerContextStruct ctx;
    auto r = exec->execute(
        actionGroup({{{"type", "change_reputation"}, {"faction", "wolves"}, {"delta", 5}}}), 1, 77, ctx);
    EXPECT_EQ(gs.getReputationManager().getReputation(1, "wolves"), 5);
    (void)r;
}

TEST_F(ExecutorFixture, UnknownActionIsIgnoredSafely)
{
    PlayerContextStruct ctx;
    auto r = exec->execute(actionGroup({{{"type", "fly_to_moon"}}}), 1, 77, ctx);
    EXPECT_TRUE(r.clientNotifications.empty());
}
