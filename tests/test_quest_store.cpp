// Unit tests for QuestStore transitions (fake seams, no world).
#include "services/QuestStore.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

QuestStepStruct killStep(int mobId, int count, const std::string &mode = "auto")
{
    QuestStepStruct s;
    s.stepType = "kill";
    s.completionMode = mode;
    s.clientStepKey = "kill_key";
    s.params = {{"mob_id", mobId}, {"count", count}};
    return s;
}

QuestStepStruct collectStep(int itemId, int count)
{
    QuestStepStruct s;
    s.stepType = "collect";
    s.clientStepKey = "collect_key";
    s.params = {{"item_id", itemId}, {"count", count}};
    return s;
}

QuestStepStruct talkStep(int npcId)
{
    QuestStepStruct s;
    s.stepType = "talk";
    s.clientStepKey = "talk_key";
    s.params = {{"npc_id", npcId}};
    return s;
}

QuestStepStruct reachStep(float x, float y, float radius = 200.0f)
{
    QuestStepStruct s;
    s.stepType = "reach";
    s.clientStepKey = "reach_key";
    s.params = {{"x", x}, {"y", y}, {"radius", radius}};
    return s;
}

QuestStepStruct interactStep(int objectId, int count = 1)
{
    QuestStepStruct s;
    s.stepType = "interact";
    s.clientStepKey = "interact_key";
    s.params = {{"object_id", objectId}, {"count", count}};
    return s;
}

QuestStruct makeQuest(int id, const std::string &slug, std::vector<QuestStepStruct> steps, int minLevel = 1)
{
    QuestStruct q;
    q.id = id;
    q.slug = slug;
    q.minLevel = minLevel;
    q.clientQuestKey = "key_" + slug;
    q.steps = std::move(steps);
    return q;
}

struct StoreFixture : ::testing::Test
{
    Logger logger{"test"};
    std::unordered_map<int, int> levels; // charId -> level (absent = 0/unknown)
    std::unordered_map<int, std::vector<PlayerInventoryItemStruct>> bags;
    struct SentUpdate
    {
        int cid;
        PlayerQuestProgressStruct pq;
        QuestStruct quest;
    };
    std::vector<SentUpdate> sent;
    struct RepChange
    {
        int cid;
        std::string faction;
        int delta;
    };
    std::vector<RepChange> reps;
    QuestStoreSeams seams;
    std::unique_ptr<QuestStore> store;

    StoreFixture()
    {
        seams.sendQuestUpdate = [this](int cid, const PlayerQuestProgressStruct &pq, const QuestStruct &q)
        {
            sent.push_back({cid, pq, q});
        };
        seams.getCharacterLevel = [this](int cid)
        {
            auto it = levels.find(cid);
            return it != levels.end() ? it->second : 0;
        };
        seams.getPlayerInventory = [this](int cid)
        {
            auto it = bags.find(cid);
            return it != bags.end() ? it->second : std::vector<PlayerInventoryItemStruct>{};
        };
        seams.changeReputation = [this](int cid, const std::string &faction, int delta)
        {
            reps.push_back({cid, faction, delta});
        };
        store = std::make_unique<QuestStore>(logger, seams);
    }

    void SetUp() override
    {
        levels[1] = 10;
        QuestStruct manual;
        manual.id = 6;
        manual.slug = "manual_talk";
        manual.minLevel = 1;
        manual.clientQuestKey = "key_manual_talk";
        QuestStepStruct ms = talkStep(11);
        ms.completionMode = "manual";
        manual.steps = {ms};
        store->setQuests({
            makeQuest(1, "kill_foxes", {killStep(7, 2)}),
            makeQuest(2, "collect_hides", {collectStep(46, 3)}),
            makeQuest(3, "talk_bob", {talkStep(11)}),
            makeQuest(4, "reach_hill", {reachStep(100.0f, 200.0f)}),
            makeQuest(5, "chain", {killStep(7, 1), collectStep(46, 2)}),
            manual,
        });
    }

    std::string stateOf(int cid, const std::string &slug)
    {
        return store->getQuestStateBySlug(cid, slug);
    }
};

} // namespace

TEST_F(StoreFixture, OfferGates)
{
    EXPECT_FALSE(store->offerQuest(1, "nope"));        // unknown slug
    EXPECT_EQ(stateOf(1, "nope"), "");
    EXPECT_TRUE(store->offerQuest(1, "kill_foxes"));  // active
    EXPECT_EQ(stateOf(1, "kill_foxes"), "active");
    EXPECT_FALSE(store->offerQuest(1, "kill_foxes")); // duplicate
    levels[2] = 1;
    QuestStruct hard = makeQuest(7, "hard", {killStep(7, 1)}, /*minLevel=*/50);
    store->setQuests({hard}); // reload keeps progress? (setQuests clears static only)
    // NOTE: setQuests replaces static data; progress map is untouched.
    EXPECT_EQ(stateOf(1, "kill_foxes"), ""); // static gone -> state lookup misses
    EXPECT_FALSE(store->offerQuest(2, "hard")); // level 1 < 50
}

TEST_F(StoreFixture, KillTriggerCapsAndCompletes)
{
    ASSERT_TRUE(store->offerQuest(1, "kill_foxes"));
    store->onMobKilled(1, 8); // wrong mob -> ignored
    EXPECT_EQ(stateOf(1, "kill_foxes"), "active");
    store->onMobKilled(1, 7);
    EXPECT_EQ(stateOf(1, "kill_foxes"), "active"); // 1/2
    store->onMobKilled(1, 7);
    EXPECT_EQ(stateOf(1, "kill_foxes"), "completed"); // 2/2 auto-advance (last step)
    store->onMobKilled(1, 7); // completed -> trigger ignores (not active)
    EXPECT_EQ(stateOf(1, "kill_foxes"), "completed");
    EXPECT_FALSE(sent.empty()); // updates pushed (offer + 2 kills + complete)
}

TEST_F(StoreFixture, CollectSeedingAndCap)
{
    PlayerInventoryItemStruct slot;
    slot.itemId = 46;
    slot.quantity = 5;
    bags[1] = {slot};
    ASSERT_TRUE(store->offerQuest(1, "collect_hides")); // have=min(5,3)=3 -> completes instantly
    EXPECT_EQ(stateOf(1, "collect_hides"), "completed");
    // onItemObtained path with fresh quest
    QuestStruct q = makeQuest(9, "collect_more", {collectStep(46, 2)});
    store->setQuests({q});
    ASSERT_TRUE(store->offerQuest(1, "collect_more"));
    bags[1].clear();
    store->onItemObtained(1, 46, 5); // capped at required 2
    EXPECT_EQ(stateOf(1, "collect_more"), "completed");
}

TEST_F(StoreFixture, TalkReachInteract)
{
    ASSERT_TRUE(store->offerQuest(1, "talk_bob"));
    store->onNPCTalked(1, 99); // wrong npc
    EXPECT_EQ(stateOf(1, "talk_bob"), "active");
    store->onNPCTalked(1, 11);
    EXPECT_EQ(stateOf(1, "talk_bob"), "completed");

    ASSERT_TRUE(store->offerQuest(1, "reach_hill"));
    store->onPositionReached(1, 0.0f, 0.0f); // far
    EXPECT_EQ(stateOf(1, "reach_hill"), "active");
    store->onPositionReached(1, 150.0f, 200.0f); // dist 50 <= 200
    EXPECT_EQ(stateOf(1, "reach_hill"), "completed");
}

TEST_F(StoreFixture, ManualStepNeedsAdvance)
{
    ASSERT_TRUE(store->offerQuest(1, "manual_talk"));
    store->onNPCTalked(1, 11); // done=true but manual -> stays active
    EXPECT_EQ(stateOf(1, "manual_talk"), "active");
    store->advanceQuestStepBySlug(1, "manual_talk"); // last step -> complete
    EXPECT_EQ(stateOf(1, "manual_talk"), "completed");
}

TEST_F(StoreFixture, ChainAdvanceSeedsNext)
{
    ASSERT_TRUE(store->offerQuest(1, "chain"));
    store->onMobKilled(1, 7); // step 0 done -> advance to collect (have=0)
    EXPECT_EQ(stateOf(1, "chain"), "active");
    store->onItemObtained(1, 46, 1);
    EXPECT_EQ(stateOf(1, "chain"), "active"); // 1/2
    store->onItemObtained(1, 46, 1);
    EXPECT_EQ(stateOf(1, "chain"), "completed");
}

TEST_F(StoreFixture, BeginTurnInContract)
{
    PlayerQuestProgressStruct pq;
    QuestStruct qs;
    EXPECT_FALSE(store->beginTurnIn(1, "kill_foxes", pq, qs)); // no progress
    ASSERT_TRUE(store->offerQuest(1, "kill_foxes"));
    EXPECT_FALSE(store->beginTurnIn(1, "kill_foxes", pq, qs)); // active, not completed
    store->onMobKilled(1, 7);
    store->onMobKilled(1, 7);
    EXPECT_TRUE(store->beginTurnIn(1, "kill_foxes", pq, qs)); // completed -> turned_in
    EXPECT_EQ(pq.state, "turned_in");
    EXPECT_EQ(qs.slug, "kill_foxes");
    EXPECT_EQ(stateOf(1, "kill_foxes"), "turned_in");
    EXPECT_FALSE(store->beginTurnIn(1, "kill_foxes", pq, qs)); // already turned_in
}

TEST_F(StoreFixture, FailQuestTerminalAndRep)
{
    QuestStruct q = makeQuest(10, "doomed", {killStep(7, 1)});
    q.reputationFactionSlug = "bandits";
    q.reputationOnFail = -5;
    store->setQuests({q});
    EXPECT_FALSE(store->failQuest(1, "doomed")); // no progress
    ASSERT_TRUE(store->offerQuest(1, "doomed"));
    EXPECT_TRUE(store->failQuest(1, "doomed"));
    EXPECT_EQ(stateOf(1, "doomed"), "failed");
    ASSERT_EQ(reps.size(), 1u);
    EXPECT_EQ(reps[0].faction, "bandits");
    EXPECT_EQ(reps[0].delta, -5);
    EXPECT_FALSE(store->failQuest(1, "doomed")); // terminal
}

TEST_F(StoreFixture, InteractTriggerNoAutoComplete)
{
    QuestStruct q = makeQuest(11, "touch", {interactStep(9, 2)});
    store->setQuests({q});
    ASSERT_TRUE(store->offerQuest(1, "touch"));
    store->onWorldObjectInteracted(1, 9);
    store->onWorldObjectInteracted(1, 9);
    // interact steps have no auto-complete branch (1-1): still active
    EXPECT_EQ(stateOf(1, "touch"), "active");
    store->advanceQuestStepBySlug(1, "touch"); // manual advance completes (last step)
    EXPECT_EQ(stateOf(1, "touch"), "completed");
}

TEST_F(StoreFixture, FillContextAndFlagsLoaded)
{
    ASSERT_TRUE(store->offerQuest(1, "kill_foxes"));
    PlayerContextStruct ctx;
    store->fillQuestContext(1, ctx);
    EXPECT_EQ(ctx.questStates["kill_foxes"], "active");
    EXPECT_EQ(ctx.questCurrentStep["kill_foxes"], 0);
    EXPECT_FALSE(store->areFlagsLoaded(1));
    store->markFlagsLoaded(1);
    EXPECT_TRUE(store->areFlagsLoaded(1));
    store->clearFlagsLoaded(1);
    EXPECT_FALSE(store->areFlagsLoaded(1));
}
