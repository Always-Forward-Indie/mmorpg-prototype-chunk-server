// Unit tests for DialogueConditionEvaluator (static) + DialogueManager +
// DialogueSessionManager (Logger only).
#include "services/DialogueConditionEvaluator.hpp"
#include "services/DialogueManager.hpp"
#include "services/DialogueSessionManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace
{

PlayerContextStruct makeCtx()
{
    PlayerContextStruct c;
    c.characterId = 1;
    c.characterLevel = 10;
    c.classId = 2;
    c.flagsBool = {{"met_king", true}};
    c.flagsInt = {{"kills", 7}};
    c.questStates = {{"q1", "active"}};
    c.questCurrentStep = {{"q1", 2}};
    c.reputations = {{"wolves", 250}};
    c.masteries = {{"sword", 60.0f}};
    c.freeSkillPoints = 3;
    c.learnedSkillSlugs = {"fireball"};
    return c;
}

nlohmann::json rule(const std::string &type)
{
    return nlohmann::json{{"type", type}};
}

} // namespace

TEST(Conditions, NullEmptyAndUnknownArePermissive)
{
    PlayerContextStruct ctx = makeCtx();
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "zzz"}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"no_type", 1}}, ctx));
}

TEST(Conditions, LogicGroups)
{
    PlayerContextStruct ctx = makeCtx();
    auto lvl10 = nlohmann::json{{"type", "level"}, {"gte", 10}};
    auto lvl99 = nlohmann::json{{"type", "level"}, {"gte", 99}};
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"all", {lvl10, lvl10}}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"all", {lvl10, lvl99}}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"any", {lvl99, lvl10}}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"any", {lvl99, lvl99}}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"not", lvl99}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"not", lvl10}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json::array({lvl10}), ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json::array({lvl99}), ctx));
}

TEST(Conditions, FlagQuestLevelClass)
{
    PlayerContextStruct ctx = makeCtx();
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "flag"}, {"key", "met_king"}, {"eq", true}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "flag"}, {"key", "met_king"}, {"eq", false}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "flag"}, {"key", "kills"}, {"gte", 5}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "flag"}, {"key", "kills"}, {"gt", 7}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest"}, {"slug", "q1"}, {"state", "active"}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest"}, {"slug", "q9"}, {"state", "not_started"}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest"}, {"slug", "q1"}, {"state", "not_started"}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"step", 2}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"step", 3}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "level"}, {"eq", 10}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "class"}, {"class_id", 2}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "class"}, {"class_id", 3}}, ctx));
}

TEST(Conditions, ReputationMasterySkills)
{
    PlayerContextStruct ctx = makeCtx();
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "reputation"}, {"faction", "wolves"}, {"gte", 200}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "reputation"}, {"faction", "wolves"}, {"gte", 251}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "reputation"}, {"faction", "wolves"}, {"tier", "friendly"}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "mastery"}, {"slug", "sword"}, {"gte", 50}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "mastery"}, {"slug", "sword"}, {"gte", 61}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "has_skill_points"}, {"gte", 1}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "has_skill_points"}, {"gte", 4}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "skill_learned"}, {"slug", "fireball"}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "skill_not_learned"}, {"slug", "fireball"}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "skill_not_learned"}, {"slug", "meteor"}}, ctx));
}

TEST(Conditions, ItemQuestStepClassLevelVariants)
{
    PlayerContextStruct ctx = makeCtx();
    // item_N quantities arrive via flagsInt (buildPlayerContext, B1).
    ctx.flagsInt["item_46"] = 6;
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "item"}, {"item_id", 46}, {"gte", 6}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "item"}, {"item_id", 46}, {"gte", 7}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "item"}, {"item_id", 47}, {"gte", 0}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "item"}, {"item_id", 47}, {"gte", 1}}, ctx));
    // quest_step comparisons (fixture: q1 at step 2).
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"gte", 2}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"gte", 3}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"lte", 2}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"gt", 1}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "quest_step"}, {"slug", "q1"}, {"lt", 2}}, ctx));
    // class_ids[] (fixture classId 2).
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "class"}, {"class_ids", {1, 2}}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "class"}, {"class_ids", {1, 3}}}, ctx));
    // level operator variants (fixture level 10).
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "level"}, {"lte", 10}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "level"}, {"gt", 9}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(nlohmann::json{{"type", "level"}, {"lt", 10}}, ctx));
}

TEST(Conditions, ObjectStateFlagEncoding)
{
    // Mirrors the executor's wio_state_<id> encoding (0=active, 1=depleted,
    // 2=disabled) written by set_object_state — evaluator and executor must
    // agree on the codes; pinned on both sides.
    PlayerContextStruct ctx = makeCtx();
    // No flag data: permissive.
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 9}, {"state", "depleted"}}, ctx));
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 0}, {"state", "depleted"}}, ctx));

    ctx.flagsInt["wio_state_9"] = 1; // depleted, as the executor writes it
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 9}, {"state", "depleted"}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 9}, {"state", "active"}}, ctx));
    EXPECT_FALSE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 9}, {"state", "disabled"}}, ctx));

    ctx.flagsInt["wio_state_9"] = 2; // disabled
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 9}, {"state", "disabled"}}, ctx));

    // Unknown state string maps to code 0 (active), same as the executor.
    ctx.flagsInt["wio_state_9"] = 0;
    EXPECT_TRUE(DialogueConditionEvaluator::evaluate(
        nlohmann::json{{"type", "object_state"}, {"object_id", 9}, {"state", "weird"}}, ctx));
}

TEST(DialogueManager, LoadLookupSelect)
{
    Logger logger{"test"};
    DialogueManager mgr(logger);
    EXPECT_FALSE(mgr.isLoaded());
    EXPECT_EQ(mgr.getDialogueById(1), nullptr);

    DialogueGraphStruct g;
    g.id = 1;
    g.slug = "intro";
    DialogueNodeStruct n;
    n.id = 10;
    n.dialogueId = 1;
    g.nodes = {{10, n}};
    mgr.setDialogues({g});
    EXPECT_TRUE(mgr.isLoaded());
    ASSERT_NE(mgr.getDialogueById(1), nullptr);
    ASSERT_NE(mgr.getDialogueBySlug("intro"), nullptr);
    EXPECT_EQ(mgr.getDialogueBySlug("nope"), nullptr);

    NPCDialogueMappingStruct m;
    m.npcId = 5;
    m.dialogueId = 1;
    m.priority = 1;
    mgr.setNPCDialogueMappings({m});
    EXPECT_EQ(mgr.selectDialogueForNPC(5, makeCtx()), 1);
    EXPECT_EQ(mgr.selectDialogueForNPC(424242, makeCtx()), -1);
}

TEST(DialogueSessions, Lifecycle)
{
    Logger logger{"test"};
    DialogueSessionManager mgr(logger);
    DialogueSessionStruct &s = mgr.createSession(1, 101, 5, 1, 10);
    const std::string sid = s.sessionId; // copy: refs invalidate on rehash
    EXPECT_FALSE(sid.empty());
    EXPECT_EQ(s.currentNodeId, 10);
    ASSERT_NE(mgr.getSession(sid), nullptr);
    ASSERT_NE(mgr.getSessionByCharacter(101), nullptr);
    EXPECT_EQ(mgr.getSession("nope"), nullptr);
    mgr.closeSession(sid);
    EXPECT_EQ(mgr.getSession(sid), nullptr);
    EXPECT_EQ(mgr.getSessionByCharacter(101), nullptr);
    mgr.closeSession("nope"); // safe no-op
    mgr.closeSessionByCharacter(999);
}

TEST(DialogueSessions, FreshSurvivesCleanup)
{
    Logger logger{"test"};
    DialogueSessionManager mgr(logger);
    const std::string sid = mgr.createSession(1, 101, 5, 1, 10).sessionId;
    mgr.cleanupExpiredSessions(); // TTL 300s: fresh session stays
    EXPECT_NE(mgr.getSession(sid), nullptr);
}
