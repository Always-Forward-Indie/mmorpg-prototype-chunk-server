// Unit tests for dialogue action extracts (pure, no world):
// DialogueEnvelopeParser, LearnSkillValidator, RepairCostCalculator,
// DialogueNotificationBuilders.
#include "services/DialogueEnvelopeParser.hpp"
#include "services/DialogueNotificationBuilders.hpp"
#include "services/LearnSkillValidator.hpp"
#include "services/RepairCostCalculator.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

TEST(DialogueEnvelopeParser, NullAndEmpty)
{
    EXPECT_TRUE(parseDialogueActionGroup(nlohmann::json()).empty());
    EXPECT_TRUE(parseDialogueActionGroup(nlohmann::json::array()).empty());
    EXPECT_TRUE(parseDialogueActionGroup(nlohmann::json::object()).empty());
}

TEST(DialogueEnvelopeParser, FlatArraySkipsTypeless)
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({{"type", "give_exp"}, {"amount", 50}});
    arr.push_back({{"amount", 10}}); // no type -> skipped
    arr.push_back({{"type", "give_gold"}, {"amount", 5}});
    auto out = parseDialogueActionGroup(arr);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].type, "give_exp");
    EXPECT_EQ(out[0].action["amount"], 50);
    EXPECT_EQ(out[1].type, "give_gold");
}

TEST(DialogueEnvelopeParser, ActionsEnvelope)
{
    nlohmann::json group;
    group["actions"] = nlohmann::json::array();
    group["actions"].push_back({{"type", "set_flag"}, {"key", "met_bob"}});
    auto out = parseDialogueActionGroup(group);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, "set_flag");
}

TEST(DialogueEnvelopeParser, SingleObjectDispatchesEvenWithoutType)
{
    // 1-1: single-object form dispatches with empty type (unknown-action log downstream)
    nlohmann::json single{{"slug", "q1"}};
    auto out = parseDialogueActionGroup(single);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, "");
    nlohmann::json typed{{"type", "fail_quest"}, {"slug", "q1"}};
    auto out2 = parseDialogueActionGroup(typed);
    ASSERT_EQ(out2.size(), 1u);
    EXPECT_EQ(out2[0].type, "fail_quest");
}

namespace
{

LearnSkillState okState()
{
    LearnSkillState st;
    st.freeSkillPoints = 5;
    st.goldItemKnown = true;
    st.goldItemId = 999;
    st.totalGold = 1000;
    st.hasBook = true;
    return st;
}

LearnSkillRequest okReq()
{
    LearnSkillRequest req;
    req.skillSlug = "shield_bash";
    req.spCost = 1;
    req.goldCost = 500;
    req.requiresBook = true;
    req.bookItemId = 18;
    return req;
}

} // namespace

TEST(LearnSkillValidator, ParseRejectsEmptySlug)
{
    LearnSkillRequest req;
    EXPECT_FALSE(parseLearnSkillRequest(nlohmann::json::object(), req));
    EXPECT_FALSE(parseLearnSkillRequest({{"sp_cost", 2}}, req));
    EXPECT_TRUE(parseLearnSkillRequest({{"skill_slug", "x"}}, req));
    EXPECT_EQ(req.spCost, 1); // defaults 1-1
    EXPECT_EQ(req.goldCost, 0);
    EXPECT_FALSE(req.requiresBook);
    EXPECT_EQ(req.bookItemId, 0);
}

TEST(LearnSkillValidator, GuardChainOrder)
{
    auto req = okReq();
    EXPECT_EQ(validateLearnSkill(req, okState()), LearnSkillError::None);

    auto learned = okState();
    learned.learnedSlugs = {"shield_bash"};
    EXPECT_EQ(validateLearnSkill(req, learned), LearnSkillError::AlreadyLearned);

    auto poor_sp = okState();
    poor_sp.freeSkillPoints = 0;
    EXPECT_EQ(validateLearnSkill(req, poor_sp), LearnSkillError::InsufficientSP);

    auto poor_gold = okState();
    poor_gold.totalGold = 499;
    EXPECT_EQ(validateLearnSkill(req, poor_gold), LearnSkillError::InsufficientGold);

    auto no_book = okState();
    no_book.hasBook = false;
    EXPECT_EQ(validateLearnSkill(req, no_book), LearnSkillError::MissingBook);

    // gold template unknown -> silent (no notification reason)
    auto no_gold_item = okState();
    no_gold_item.goldItemKnown = false;
    EXPECT_EQ(validateLearnSkill(req, no_gold_item), LearnSkillError::GoldItemUnknown);
    EXPECT_EQ(learnSkillErrorReason(LearnSkillError::GoldItemUnknown), "");

    // zero-cost paths skip their guards
    LearnSkillRequest free = okReq();
    free.goldCost = 0;
    free.requiresBook = false;
    LearnSkillState broke;
    broke.freeSkillPoints = 5;
    EXPECT_EQ(validateLearnSkill(free, broke), LearnSkillError::None);

    EXPECT_EQ(learnSkillErrorReason(LearnSkillError::AlreadyLearned), "already_learned");
    EXPECT_EQ(learnSkillErrorReason(LearnSkillError::InsufficientSP), "insufficient_sp");
    EXPECT_EQ(learnSkillErrorReason(LearnSkillError::InsufficientGold), "insufficient_gold");
    EXPECT_EQ(learnSkillErrorReason(LearnSkillError::MissingBook), "missing_skill_book");
}

TEST(RepairCostCalculator, FiltersAndPrices)
{
    std::vector<RepairCostInput> items = {
        {11, 200, "iron_sword", 70, true, 100, 100},  // missing 30 -> 30
        {12, 201, "plate", 0, true, 100, 99},         // cur 0 -> fallback max -> missing 0 -> skip
        {13, 202, "shield", 100, true, 100, 50},      // full -> skip
        {14, 203, "bread", 0, false, 0, 5},           // not durable -> skip
        {15, 204, "relic", 99, true, 100, 99},        // missing 1 -> ceil(0.99) = 1
        {16, 205, "odd", 50, true, 0, 10},            // max <= 0 -> skip
    };
    auto out = computeRepairEntries(items);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].inventoryItemId, 11);
    EXPECT_EQ(out[0].itemName, "iron_sword");
    EXPECT_EQ(out[0].durabilityCurrent, 70);
    EXPECT_EQ(out[0].durabilityMax, 100);
    // NOTE: 1-1 float artifact — 100 * (30/100 in float32) = 30.000002 → ceil = 31.
    // Pinned as-is (client prices already live with it); do not "fix" without a product decision.
    EXPECT_EQ(out[0].repairCost, 31);
    EXPECT_EQ(out[1].inventoryItemId, 15);
    EXPECT_EQ(out[1].repairCost, 1);
}

TEST(DialogueNotificationBuilders, Shapes)
{
    using namespace DialogueNotificationBuilders;
    auto q = questOffered(42, "find_sword", {{"step", 1}}, true, {{"exp", 100}});
    EXPECT_EQ(q["type"], "quest_offered");
    EXPECT_EQ(q["questId"], 42);
    EXPECT_EQ(q["currentStep"]["step"], 1);
    auto q0 = questOffered(42, "find_sword", {}, false, {});
    EXPECT_FALSE(q0.contains("currentStep")); // 1-1: key absent without steps

    auto f = questFailed(7, "q");
    EXPECT_EQ(f["type"], "quest_failed");
    auto r = reputationChanged("bandits", -10);
    EXPECT_EQ(r["faction"], "bandits");
    EXPECT_EQ(r["delta"], -10);
    auto lf = learnSkillFailed("insufficient_sp", "bash");
    EXPECT_EQ(lf["type"], "learn_skill_failed");
    EXPECT_EQ(lf["reason"], "insufficient_sp");
    EXPECT_EQ(lf["skillSlug"], "bash");
    auto g = goldReceived(500);
    EXPECT_EQ(g["amount"], 500);
    auto e = expReceived(250);
    EXPECT_EQ(e["amount"], 250);
    auto it = itemReceived(5, "sword", 2);
    EXPECT_EQ(it["item_slug"], "sword");
}
