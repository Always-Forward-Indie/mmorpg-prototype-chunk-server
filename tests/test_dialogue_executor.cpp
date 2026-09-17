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

// ── A2: previously uncovered action types ────────────────────────────────

namespace
{

QuestStruct makeManualQuest()
{
    QuestStruct q;
    q.id = 2;
    q.slug = "manual_escort";
    q.minLevel = 1;
    q.clientQuestKey = "key_manual_escort";
    QuestStepStruct step;
    step.stepType = "talk";
    step.completionMode = "manual";
    step.clientStepKey = "talk_key";
    step.params = {{"npc_id", 11}};
    q.steps = {step};
    return q;
}

NPCDataStruct makeNpc(int id, const std::string &slug)
{
    NPCDataStruct n;
    n.id = id;
    n.slug = slug;
    n.name = slug;
    return n;
}

} // namespace

TEST_F(ExecutorFixture, AdvanceManualStepCompletesQuest)
{
    gs.getQuestManager().setQuests({makeManualQuest()});
    PlayerContextStruct ctx;
    exec->execute(actionGroup({{{"type", "offer_quest"}, {"slug", "manual_escort"}}}), 1, 77, ctx);
    ASSERT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "manual_escort"), "active");

    exec->execute(actionGroup({{{"type", "advance_quest_step"}, {"slug", "manual_escort"}}}), 1, 77, ctx);
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "manual_escort"), "completed");
}

TEST_F(ExecutorFixture, TurnInActiveQuestSendsNothing)
{
    // BeginTurnIn contract: only completed quests turn in.
    PlayerContextStruct ctx;
    exec->execute(actionGroup({{{"type", "offer_quest"}, {"slug", "kill_foxes"}}}), 1, 77, ctx);
    auto r = exec->execute(actionGroup({{{"type", "turn_in_quest"}, {"slug", "kill_foxes"}}}), 1, 77, ctx);
    EXPECT_TRUE(r.clientNotifications.empty());
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "active");
}

TEST_F(ExecutorFixture, TurnInCompletedQuestViaDialogue)
{
    PlayerQuestProgressStruct pq;
    pq.characterId = 1;
    pq.questId = 1;
    pq.questSlug = "kill_foxes";
    pq.state = "completed";
    gs.getQuestManager().loadPlayerQuests(1, {pq});

    PlayerContextStruct ctx;
    auto r = exec->execute(actionGroup({{{"type", "turn_in_quest"}, {"slug", "kill_foxes"}}}), 1, 77, ctx);
    EXPECT_FALSE(r.clientNotifications.empty());
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "turned_in");
}

TEST_F(ExecutorFixture, OpenVendorShopNeedsSessionAndData)
{
    PlayerContextStruct ctx;
    // No dialogue session: silent no-op.
    auto r0 = exec->execute(actionGroup({{{"type", "open_vendor_shop"}}}), 1, 77, ctx);
    EXPECT_TRUE(r0.clientNotifications.empty());

    gs.getNPCManager().setNPCsList({makeNpc(11, "milaya")});
    VendorNPCDataStruct v;
    v.npcId = 11;
    VendorInventoryItemStruct potion;
    potion.itemId = 100;
    potion.stockCurrent = 5;
    potion.stockMax = 5;
    v.items = {potion};
    gs.getVendorManager().setVendorData({v});
    gs.getDialogueSessionManager().createSession(77, 1, 11, 1, 1);

    auto r = exec->execute(actionGroup({{{"type", "open_vendor_shop"}}}), 1, 77, ctx);
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "openVendorShop");
    EXPECT_EQ(r.clientNotifications[0]["npcId"].get<int>(), 11);
    EXPECT_EQ(r.clientNotifications[0]["npcSlug"].get<std::string>(), "milaya");
}

TEST_F(ExecutorFixture, OpenRepairShopListsDamagedDurables)
{
    PlayerContextStruct ctx;
    auto r0 = exec->execute(actionGroup({{{"type", "open_repair_shop"}}}), 1, 77, ctx);
    EXPECT_TRUE(r0.clientNotifications.empty());

    ItemDataStruct sword = makeItem(200, "iron_sword");
    sword.isDurable = true;
    sword.durabilityMax = 100;
    sword.vendorPriceBuy = 100;
    ItemDataStruct gold = makeItem(16, "gold_coin");
    gold.stackMax = 1000000;
    gs.getItemManager().setItemsList({gold, sword});

    ASSERT_TRUE(gs.getInventoryManager().addItemToInventory(1, 200, 1));
    // In-memory rows start with id == 0; mirror the DB upsert sync, then damage.
    gs.getInventoryManager().updateInventoryItemId(1, 200, 777);
    int rowId = 0;
    for (const auto &row : gs.getInventoryManager().getPlayerInventory(1))
        if (row.itemId == 200)
            rowId = row.id;
    ASSERT_EQ(rowId, 777);
    gs.getInventoryManager().updateDurability(1, rowId, 50);

    gs.getNPCManager().setNPCsList({makeNpc(11, "edrik")});
    gs.getDialogueSessionManager().createSession(77, 1, 11, 1, 1);

    auto r = exec->execute(actionGroup({{{"type", "open_repair_shop"}}}), 1, 77, ctx);
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "openRepairShop");
    ASSERT_EQ(r.clientNotifications[0]["items"].size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["items"][0]["itemId"].get<int>(), 200);
    // ceil(100 * 50 / 100) = 50.
    EXPECT_EQ(r.clientNotifications[0]["items"][0]["repairCost"].get<int>(), 50);
}

TEST_F(ExecutorFixture, OpenSkillShopNeedsTrainer)
{
    PlayerContextStruct ctx;
    gs.getNPCManager().setNPCsList({makeNpc(11, "edrik"), makeNpc(12, "bystander")});
    // Bystander has a session but no trainer data: silent no-op.
    gs.getDialogueSessionManager().createSession(77, 1, 12, 1, 1);
    auto r0 = exec->execute(actionGroup({{{"type", "open_skill_shop"}}}), 1, 77, ctx);
    EXPECT_TRUE(r0.clientNotifications.empty());

    TrainerNPCDataStruct t;
    t.npcId = 11;
    ClassSkillTreeEntryStruct entry;
    entry.skillSlug = "shield_bash";
    entry.skillName = "Shield Bash";
    entry.spCost = 1;
    t.skills = {entry};
    gs.getTrainerManager().setTrainerData({t});
    gs.getDialogueSessionManager().createSession(77, 1, 11, 1, 1);

    auto r = exec->execute(actionGroup({{{"type", "open_skill_shop"}}}), 1, 77, ctx);
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "openSkillShop");
    EXPECT_EQ(r.clientNotifications[0]["npcId"].get<int>(), 11);
    EXPECT_FALSE(r.clientNotifications[0]["skills"].is_null());
}

TEST_F(ExecutorFixture, LearnSkillValidationFailsLoudly)
{
    // Already learned: learn_skill_failed, nothing consumed, nothing queued.
    gs.getInventoryManager().addItemToInventory(1, 16, 500);
    PlayerContextStruct ctx;
    ctx.freeSkillPoints = 5;
    ctx.learnedSkillSlugs = {"shield_bash"};
    auto r = exec->execute(
        actionGroup({{{"type", "learn_skill"}, {"skill_slug", "shield_bash"}, {"sp_cost", 1}}}),
        1, 77, ctx);
    ASSERT_EQ(r.clientNotifications.size(), 1u);
    EXPECT_EQ(r.clientNotifications[0]["type"].get<std::string>(), "learn_skill_failed");
    EXPECT_EQ(r.clientNotifications[0]["reason"].get<std::string>(), "already_learned");
    EXPECT_TRUE(r.pendingGameServerPackets.empty());
    EXPECT_EQ(gs.getInventoryManager().getGoldAmount(1), 500);

    // Missing skill_slug: silent (no notification, 1-1 with the validator).
    PlayerContextStruct ctx2;
    auto r2 = exec->execute(actionGroup({{{"type", "learn_skill"}}}), 1, 77, ctx2);
    EXPECT_TRUE(r2.clientNotifications.empty());
    EXPECT_TRUE(r2.pendingGameServerPackets.empty());
}

TEST_F(ExecutorFixture, LearnSkillSuccessConsumesAndQueuesPersist)
{
    ItemDataStruct book = makeItem(18, "tome_of_bash");
    ItemDataStruct gold = makeItem(16, "gold_coin");
    gold.stackMax = 1000000;
    gs.getItemManager().setItemsList({gold, book});
    ASSERT_TRUE(gs.getInventoryManager().addItemToInventory(1, 16, 500));
    ASSERT_TRUE(gs.getInventoryManager().addItemToInventory(1, 18, 1));
    gs.getCharacterManager().modifyFreeSkillPoints(1, 5);

    PlayerContextStruct ctx;
    ctx.freeSkillPoints = 5;
    auto r = exec->execute(
        actionGroup({{{"type", "learn_skill"},
            {"skill_slug", "shield_bash"},
            {"sp_cost", 2},
            {"gold_cost", 100},
            {"requires_book", true},
            {"book_item_id", 18}}}),
        1, 77, ctx);
    // Success pushes no client notification — persistence goes to the game
    // server; pin the consume + queue contract.
    EXPECT_TRUE(r.clientNotifications.empty());
    ASSERT_EQ(r.pendingGameServerPackets.size(), 1u);
    const auto pkt = nlohmann::json::parse(r.pendingGameServerPackets[0]);
    EXPECT_EQ(pkt["body"]["skillSlug"].get<std::string>(), "shield_bash");
    EXPECT_EQ(pkt["header"]["eventType"].get<std::string>(), "saveLearnedSkill");
    EXPECT_TRUE(ctx.learnedSkillSlugs.count("shield_bash") > 0);
    EXPECT_EQ(ctx.freeSkillPoints, 3);
    EXPECT_EQ(gs.getCharacterManager().getCharacterFreeSkillPoints(1), 3);
    EXPECT_EQ(gs.getInventoryManager().getGoldAmount(1), 400);
    EXPECT_EQ(gs.getInventoryManager().getItemQuantity(1, 18), 0);
}

TEST_F(ExecutorFixture, SetObjectStateQueuesBroadcastAndFlags)
{
    PlayerContextStruct ctx;
    auto r = exec->execute(
        actionGroup({{{"type", "set_object_state"}, {"object_id", 9}, {"state", "depleted"}}}),
        1, 77, ctx);
    ASSERT_EQ(r.pendingObjectStateBroadcasts.size(), 1u);
    EXPECT_EQ(r.pendingObjectStateBroadcasts[0].objectId, 9);
    EXPECT_EQ(r.pendingObjectStateBroadcasts[0].state, "depleted");
    // Encoded for subsequent object_state conditions in the same dialogue.
    EXPECT_EQ(ctx.flagsInt["wio_state_9"], 1);

    // Unknown state string maps to code 0, still queued.
    PlayerContextStruct ctx2;
    auto r2 = exec->execute(
        actionGroup({{{"type", "set_object_state"}, {"object_id", 9}, {"state", "weird"}}}),
        1, 77, ctx2);
    ASSERT_EQ(r2.pendingObjectStateBroadcasts.size(), 1u);
    EXPECT_EQ(ctx2.flagsInt["wio_state_9"], 0);

    // Missing object_id: silent no-op.
    PlayerContextStruct ctx3;
    auto r3 = exec->execute(actionGroup({{{"type", "set_object_state"}}}), 1, 77, ctx3);
    EXPECT_TRUE(r3.pendingObjectStateBroadcasts.empty());
}
