// Unit tests for MasteryManager (explicit DI).
#include "services/MasteryManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/IStatsNotifier.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

struct MasteryNotifier : IStatsNotifier
{
    int statsUpdates = 0;
    int worldNotes = 0;
    void sendStatsUpdate(int) override
    {
        ++statsUpdates;
    }
    void sendStatsUpdate(int, const std::string &) override
    {
        ++statsUpdates;
    }
    void sendWorldNotification(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
        ++worldNotes;
    }
    void sendWorldNotificationToGameZone(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
};

struct MasteryFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    GameConfigService config{logger};
    MasteryNotifier notifier;
    // titles=nullptr: milestone title hook skipped (covered by TitleManager tests)
    MasteryManager mastery{chars, config, nullptr, &notifier, logger};

    void SetUp() override
    {
        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        chars.addCharacter(c);
    }
};

} // namespace

TEST_F(MasteryFixture, LoadQueryUnload)
{
    EXPECT_EQ(mastery.getMasteryValue(1, "sword"), 0.0f);
    EXPECT_TRUE(mastery.getAllMasteries(1).empty());
    mastery.loadCharacterMasteries(1, {{"sword", 30.5f}, {"axe", 10.0f}});
    EXPECT_FLOAT_EQ(mastery.getMasteryValue(1, "sword"), 30.5f);
    EXPECT_EQ(mastery.getAllMasteries(1).size(), 2u);
    EXPECT_EQ(mastery.getMasteryValue(2, "sword"), 0.0f);
    mastery.unloadCharacterMasteries(1);
    EXPECT_EQ(mastery.getMasteryValue(1, "sword"), 0.0f);
}

TEST_F(MasteryFixture, UnloadFlushesPendingValues)
{
    // Periodic persist runs only every N hits: without an unload flush the
    // last <N hits are lost on every logout. Unload must save quietly
    // (no client notify — session is going away).
    std::vector<std::string> saved;
    mastery.setSaveCallback([&](const std::string &pkt) { saved.push_back(pkt); });
    int notified = 0;
    mastery.setClientNotifyCallback([&](int, const std::string &, float, const std::string &) { ++notified; });
    mastery.loadCharacterMasteries(1, {{"sword", 30.5f}, {"axe", 10.0f}});
    mastery.unloadCharacterMasteries(1);
    EXPECT_EQ(saved.size(), 2u);
    EXPECT_EQ(notified, 0);
}

TEST_F(MasteryFixture, AttackProgressesValueWithDefaults)
{
    // Defaults: base 0.5, same level -> x1.0 -> +0.5 per hit, cap 100.
    mastery.loadCharacterMasteries(1, {});
    for (int i = 0; i < 10; ++i)
        mastery.onPlayerAttack(1, "sword", 10, 10);
    EXPECT_FLOAT_EQ(mastery.getMasteryValue(1, "sword"), 5.0f);
    mastery.onPlayerAttack(1, "", 10, 10);
    EXPECT_EQ(mastery.getAllMasteries(1).size(), 1u); // no entry for empty slug
}

TEST_F(MasteryFixture, HigherTargetLevelProgressesFaster)
{
    mastery.loadCharacterMasteries(1, {});
    mastery.loadCharacterMasteries(2, {});
    for (int i = 0; i < 10; ++i)
    {
        mastery.onPlayerAttack(1, "sword", 10, 10); // diff 0 -> x1.0
        mastery.onPlayerAttack(2, "sword", 10, 15); // diff +5 -> x2.0
    }
    EXPECT_GT(mastery.getMasteryValue(2, "sword"), mastery.getMasteryValue(1, "sword"));
}

TEST_F(MasteryFixture, SaveCallbackOnFlush)
{
    std::vector<std::string> saved;
    mastery.setSaveCallback([&](const std::string &pkt) { saved.push_back(pkt); });
    mastery.loadCharacterMasteries(1, {});
    for (int i = 0; i < 10; ++i) // flushEvery default 10
        mastery.onPlayerAttack(1, "sword", 10, 10);
    EXPECT_FALSE(saved.empty());
}

TEST_F(MasteryFixture, FillContext)
{
    mastery.loadCharacterMasteries(1, {{"sword", 42.0f}});
    PlayerContextStruct ctx;
    mastery.fillMasteryContext(1, ctx);
    EXPECT_FLOAT_EQ(ctx.masteries["sword"], 42.0f);
}

TEST_F(MasteryFixture, DefinitionsLoad)
{
    MasteryDefinitionStruct d;
    d.slug = "sword_mastery";
    d.maxValue = 100.0;
    mastery.loadMasteryDefinitions({d});
    // No crash; definitions gate milestone effects server-side.
    SUCCEED();
}

TEST_F(MasteryFixture, MilestoneCrossingAppliesEffectAndNotifies)
{
    mastery.loadCharacterMasteries(1, {});
    for (int i = 0; i < 40; ++i) // 40 x 0.5 = 20 -> crosses t1
        mastery.onPlayerAttack(1, "sword", 10, 10);
    EXPECT_FLOAT_EQ(mastery.getMasteryValue(1, "sword"), 20.0f);
    bool found = false;
    for (const auto &e : chars.getCharacterData(1).activeEffects)
        if (e.effectSlug == "sword_t1_damage")
            found = true;
    EXPECT_TRUE(found);
    EXPECT_GT(notifier.statsUpdates, 0);
    EXPECT_GT(notifier.worldNotes, 0);
}

TEST_F(MasteryFixture, ReapplyMilestoneEffects)
{
    mastery.loadCharacterMasteries(1, {{"sword", 55.0f}});
    mastery.reapplyMilestoneEffects(1);
    int count = 0;
    for (const auto &e : chars.getCharacterData(1).activeEffects)
        if (e.sourceType == "mastery")
            ++count;
    EXPECT_EQ(count, 2); // t1 + t2 buffs
    mastery.reapplyMilestoneEffects(99); // unknown char -> no-op
    SUCCEED();
}
