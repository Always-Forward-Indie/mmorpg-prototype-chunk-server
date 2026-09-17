// Unit tests for the QuestManager orchestrator (Wave 4.1).
//
// QuestStore (transitions) and QuestResolvers were covered in phase 2; this
// drives the orchestrator itself with a real GameServices: offer gates,
// turn-in flow, fail flow, flags, triggers and the QUEST_UPDATE sender seam.
// Network leaves through seams (no sockets/DB): an unconnected
// tcp::socket stands in for a client connection.
#include "services/GameServices.hpp"
#include "services/QuestManager.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

QuestStepStruct killStep(int mobId, int count)
{
    QuestStepStruct s;
    s.stepType = "kill";
    s.completionMode = "auto";
    s.clientStepKey = "kill_key";
    s.params = {{"mob_id", mobId}, {"count", count}};
    return s;
}

QuestStruct makeQuest(int id, const std::string &slug, int minLevel = 1)
{
    QuestStruct q;
    q.id = id;
    q.slug = slug;
    q.minLevel = minLevel;
    q.clientQuestKey = "key_" + slug;
    q.steps = {killStep(7, 2)};
    return q;
}

struct OrchestratorFixture : ::testing::Test
{
    Logger logger{"test"};
    GameServices gs{logger};
    boost::asio::io_context ioc;

    struct SentUpdate
    {
        int characterId = 0;
        nlohmann::json packet;
    };
    std::vector<SentUpdate> updates;
    std::vector<std::string> persisted;

    void SetUp() override
    {
        gs.getQuestManager().setQuests({makeQuest(1, "kill_foxes"), makeQuest(2, "hard", 50)});
        gs.getQuestManager().setQuestUpdateSender(
            [this](std::shared_ptr<boost::asio::ip::tcp::socket>, const nlohmann::json &packet)
            {
                // characterId is not in the packet envelope; recover from body.
                SentUpdate u;
                u.packet = packet;
                updates.push_back(std::move(u));
            });
        gs.getQuestManager().setSendToGameServerCallback(
            [this](const std::string &packet) { persisted.push_back(packet); });

        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 100;
        c.characterMaxMana = 50;
        c.characterCurrentMana = 50;
        gs.getCharacterManager().addCharacter(c);
    }
};

} // namespace

TEST_F(OrchestratorFixture, OfferAcceptsAndTracksState)
{
    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "active");
    // No socket bound: no QUEST_UPDATE pushed, no crash.
    EXPECT_TRUE(updates.empty());
}

TEST_F(OrchestratorFixture, OfferRejectsUnknownAndLowLevel)
{
    EXPECT_FALSE(gs.getQuestManager().offerQuest(1, "nope"));
    EXPECT_FALSE(gs.getQuestManager().offerQuest(1, "hard")); // minLevel 50
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "hard"), "");
    // Offering twice keeps single active state.
    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
    EXPECT_FALSE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
}

TEST_F(OrchestratorFixture, QuestUpdateSenderReceivesPacket)
{
    // Bind an (unconnected) socket so the update path proceeds to the seam.
    auto sock = std::make_shared<boost::asio::ip::tcp::socket>(ioc);
    gs.getClientManager().setClientSocket(77, sock);
    CharacterDataStruct c = gs.getCharacterManager().getCharacterData(1);
    c.clientId = 77;
    gs.getCharacterManager().loadCharacterData(c);

    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
    ASSERT_EQ(updates.size(), 1u);
    EXPECT_EQ(updates[0].packet["body"]["questSlug"].get<std::string>(), "kill_foxes");
    EXPECT_EQ(updates[0].packet["body"]["state"].get<std::string>(), "active");
}

TEST_F(OrchestratorFixture, FailQuestTerminal)
{
    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
    EXPECT_TRUE(gs.getQuestManager().failQuest(1, "kill_foxes"));
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "failed");
    EXPECT_FALSE(gs.getQuestManager().failQuest(1, "kill_foxes")); // terminal already
}

TEST_F(OrchestratorFixture, FlagsRoundtrip)
{
    gs.getQuestManager().setFlagBool(1, "met_bob", true);
    EXPECT_TRUE(gs.getQuestManager().getFlagBool(1, "met_bob"));
    EXPECT_FALSE(gs.getQuestManager().getFlagBool(1, "unknown_flag"));
    gs.getQuestManager().setFlagInt(1, "wolves_slain", 3);
    // Int flags live on the character record (quest-progress flags are a
    // separate store surfaced via fillQuestContext — not conflated here).
    bool found = false;
    for (const auto &f : gs.getCharacterManager().getCharacterData(1).flags)
    {
        if (f.flagKey == "wolves_slain" && f.intValue == 3)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(OrchestratorFixture, TriggersAreSafeNoOpsWithoutQuests)
{
    // No matching progress: must not throw, must not create progress rows.
    gs.getQuestManager().onMobKilled(1, 7);
    gs.getQuestManager().onItemObtained(1, 100, 1);
    gs.getQuestManager().onNPCTalked(1, 5);
    gs.getQuestManager().onPositionReached(1, 10.0f, 20.0f);
    gs.getQuestManager().onWorldObjectInteracted(1, 9);
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "");
}

TEST_F(OrchestratorFixture, TurnInCompletedQuestNotifies)
{
    PlayerQuestProgressStruct pq;
    pq.characterId = 1;
    pq.questId = 1;
    pq.questSlug = "kill_foxes";
    pq.state = "completed";
    gs.getQuestManager().loadPlayerQuests(1, {pq});
    auto notes = gs.getQuestManager().turnInQuest(1, "kill_foxes", 77);
    EXPECT_FALSE(notes.empty());
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "turned_in");
}

// ── A3: thin-delegate wiring (transitions live in QuestStore, pinned in
// phase 2; here we pin that the orchestrator actually delegates) ─────────

namespace
{

QuestStruct makeManualQuest()
{
    QuestStruct q;
    q.id = 3;
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

} // namespace

TEST_F(OrchestratorFixture, AdvanceQuestStepBySlugWiring)
{
    gs.getQuestManager().setQuests({makeQuest(1, "kill_foxes"), makeManualQuest()});
    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "manual_escort"));
    gs.getQuestManager().advanceQuestStepBySlug(1, "manual_escort");
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "manual_escort"), "completed");
    // Unknown slug: safe no-op, nothing created.
    gs.getQuestManager().advanceQuestStepBySlug(1, "nope");
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "nope"), "");
}

TEST_F(OrchestratorFixture, LoadUnloadLifecycle)
{
    PlayerQuestProgressStruct pq;
    pq.characterId = 1;
    pq.questId = 1;
    pq.questSlug = "kill_foxes";
    pq.state = "active";
    gs.getQuestManager().loadPlayerQuests(1, {pq});
    ASSERT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "active");
    gs.getQuestManager().unloadPlayerQuests(1);
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "");
}

TEST_F(OrchestratorFixture, FillQuestContextWiring)
{
    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
    PlayerContextStruct ctx;
    gs.getQuestManager().fillQuestContext(1, ctx);
    EXPECT_EQ(ctx.questStates["kill_foxes"], "active");
}

TEST_F(OrchestratorFixture, ResolveDelegatesReturnClientJson)
{
    QuestStepStruct step = killStep(7, 2);
    const nlohmann::json stepJson = gs.getQuestManager().resolveStepForClient(step);
    EXPECT_TRUE(stepJson.is_object());
    QuestRewardStruct rw;
    rw.rewardType = "gold";
    rw.amount = 50;
    const nlohmann::json rwJson = gs.getQuestManager().resolveRewardsForClient({rw});
    EXPECT_TRUE(rwJson.is_array());
    ASSERT_EQ(rwJson.size(), 1u);
}

TEST_F(OrchestratorFixture, FlushPathsReachGameServerSeam)
{
    EXPECT_TRUE(gs.getQuestManager().offerQuest(1, "kill_foxes"));
    gs.getQuestManager().setFlagBool(1, "met_bob", true);
    gs.getQuestManager().flushDirtyProgress();
    gs.getQuestManager().flushPendingFlags();
    EXPECT_FALSE(persisted.empty()); // offer + flag update persisted
    gs.getQuestManager().flushAllProgress(1);
    // Disconnect flush then unload: no crash, state gone.
    gs.getQuestManager().unloadPlayerQuests(1);
    EXPECT_EQ(gs.getQuestManager().getQuestStateBySlug(1, "kill_foxes"), "");
}

TEST_F(OrchestratorFixture, GetByIdSlugIsLoaded)
{
    EXPECT_TRUE(gs.getQuestManager().isLoaded());
    ASSERT_NE(gs.getQuestManager().getQuestBySlug("kill_foxes"), nullptr);
    EXPECT_EQ(gs.getQuestManager().getQuestBySlug("kill_foxes")->id, 1);
    ASSERT_NE(gs.getQuestManager().getQuestById(2), nullptr);
    EXPECT_EQ(gs.getQuestManager().getQuestById(2)->slug, "hard");
    EXPECT_EQ(gs.getQuestManager().getQuestBySlug("nope"), nullptr);
    EXPECT_EQ(gs.getQuestManager().getQuestById(424242), nullptr);

    QuestManager fresh(&gs, logger);
    EXPECT_FALSE(fresh.isLoaded());
    EXPECT_EQ(fresh.getQuestBySlug("kill_foxes"), nullptr);
}
