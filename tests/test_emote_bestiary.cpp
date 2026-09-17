// Unit tests for EmoteManager (Logger-only DI) + BestiaryManager (Logger).
#include "services/BestiaryManager.hpp"
#include "services/EmoteManager.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

EmoteDefinitionStruct makeEmote(const std::string &slug, int order, bool def = false)
{
    EmoteDefinitionStruct e;
    e.slug = slug;
    e.displayName = slug;
    e.sortOrder = order;
    e.isDefault = def;
    return e;
}

} // namespace

TEST(Emotes, DefinitionsSortedAndDefault)
{
    Logger logger{"test"};
    EmoteManager mgr{logger};
    mgr.loadEmoteDefinitions({makeEmote("dance", 2), makeEmote("wave", 1), makeEmote("sit", 0, true)});
    auto all = mgr.getAllDefinitions();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].slug, "sit");
    EXPECT_EQ(all[1].slug, "wave");
    EXPECT_EQ(all[2].slug, "dance");
    EXPECT_EQ(mgr.getEmoteDefinition("wave").sortOrder, 1);
    EXPECT_EQ(mgr.getEmoteDefinition("nope").slug, "");
}

TEST(Emotes, PlayerUnlocksWithDefaults)
{
    Logger logger{"test"};
    EmoteManager mgr{logger};
    mgr.loadEmoteDefinitions({makeEmote("dance", 2), makeEmote("sit", 0, true)});
    mgr.loadPlayerEmotes(1, {"dance"});
    EXPECT_TRUE(mgr.isUnlocked(1, "dance"));
    EXPECT_TRUE(mgr.isUnlocked(1, "sit")); // default emote: always unlocked
    EXPECT_FALSE(mgr.isUnlocked(1, "wave"));
    EXPECT_TRUE(mgr.isUnlocked(2, "sit")); // defaults apply even before join
    EXPECT_EQ(mgr.getPlayerEmotes(1).size(), 2u); // dance + default sit
    mgr.unloadPlayerEmotes(1);
    EXPECT_TRUE(mgr.isUnlocked(1, "sit")); // defaults survive unload
    EXPECT_FALSE(mgr.isUnlocked(1, "dance"));
    EXPECT_EQ(mgr.getPlayerEmotes(1).size(), 1u); // only the default remains
}

TEST(Bestiary, LoadQueryTiers)
{
    Logger logger{"test"};
    BestiaryManager best(logger);
    EXPECT_EQ(best.getKillCount(1, 7), 0);
    EXPECT_TRUE(best.getKnownMobs(1).empty());
    best.loadBestiaryData(1, {{7, 12}, {8, 3}});
    EXPECT_EQ(best.getKillCount(1, 7), 12);
    EXPECT_EQ(best.getKnownMobs(1).size(), 2u);
    // Default thresholds [1,10,25,50,100,300]: 12 kills -> tiers 1,2.
    auto tiers = best.getRevealedTiers(1, 7, {1, 10, 25, 50, 100, 300});
    EXPECT_EQ(tiers.size(), 2u);
    EXPECT_TRUE(best.getRevealedTiers(1, 424242, {1, 10}).empty());
}

TEST(Bestiary, RecordKillNotifiesOnTierCross)
{
    Logger logger{"test"};
    BestiaryManager best(logger);
    best.setThresholds({1, 10, 25, 50, 100, 300}); // member thresholds gate notify
    int notifies = 0;
    int killsSeen = 0;
    best.setNotifyCallback([&](int, int, int, int kc, const std::string &) { ++notifies; killsSeen = kc; });
    best.setKillUpdateCallback([&](int, int, int) {});
    best.loadBestiaryData(1, {{7, 9}});
    best.recordKill(1, 7); // 10 -> crosses tier 2 with defaults
    EXPECT_EQ(best.getKillCount(1, 7), 10);
    EXPECT_GE(notifies, 1);
    EXPECT_EQ(killsSeen, 10);
}

TEST(Bestiary, SaveCallbackOnKill)
{
    Logger logger{"test"};
    BestiaryManager best(logger);
    std::vector<std::string> saved;
    best.setSaveCallback([&](const std::string &pkt) { saved.push_back(pkt); });
    best.recordKill(1, 7);
    EXPECT_FALSE(saved.empty());
    EXPECT_EQ(saved[0].back(), '\n');
}
