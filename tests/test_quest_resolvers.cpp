// Unit tests for QuestResolvers (fake catalogs, no world).
#include "services/QuestResolvers.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

struct Catalogs
{
    std::unordered_map<int, std::string> mobs;  // id -> slug
    std::unordered_map<int, std::string> items; // id -> slug
    std::unordered_map<int, std::string> npcs;  // id -> slug

    QuestResolverLookups lookups()
    {
        QuestResolverLookups l;
        l.getMobById = [this](int id)
        {
            MobDataStruct m;
            auto it = mobs.find(id);
            if (it != mobs.end())
            {
                m.id = id;
                m.slug = it->second;
            }
            return m;
        };
        l.getItemById = [this](int id)
        {
            ItemDataStruct i;
            auto it = items.find(id);
            if (it != items.end())
            {
                i.id = id;
                i.slug = it->second;
            }
            return i;
        };
        l.getNPCById = [this](int id)
        {
            NPCDataStruct n;
            auto it = npcs.find(id);
            if (it != npcs.end())
            {
                n.id = id;
                n.slug = it->second;
            }
            return n;
        };
        return l;
    }
};

QuestStepStruct stepOf(const std::string &type, nlohmann::json params)
{
    QuestStepStruct s;
    s.stepType = type;
    s.clientStepKey = "key_" + type;
    s.params = std::move(params);
    return s;
}

QuestRewardStruct itemReward(int itemId, int qty, bool hidden = false)
{
    QuestRewardStruct r;
    r.rewardType = "item";
    r.itemId = itemId;
    r.quantity = qty;
    r.isHidden = hidden;
    return r;
}

QuestRewardStruct expReward(int64_t amount, bool hidden = false)
{
    QuestRewardStruct r;
    r.rewardType = "exp";
    r.amount = amount;
    r.isHidden = hidden;
    return r;
}

} // namespace

TEST(QuestResolvers, StepKinds)
{
    Catalogs cats;
    cats.mobs[7] = "wolf";
    cats.items[46] = "hide";
    cats.npcs[11] = "bob";
    QuestResolvers resolvers{cats.lookups()};

    auto kill = resolvers.resolveStepForClient(stepOf("kill", {{"mob_id", 7}, {"count", 2}}));
    EXPECT_EQ(kill["target_slug"], "wolf");
    EXPECT_EQ(kill["count"], 2);
    EXPECT_EQ(kill["clientStepKey"], "key_kill");

    auto collect = resolvers.resolveStepForClient(stepOf("collect", {{"item_id", 46}, {"count", 3}}));
    EXPECT_EQ(collect["target_slug"], "hide");

    auto talk = resolvers.resolveStepForClient(stepOf("talk", {{"npc_id", 11}}));
    EXPECT_EQ(talk["target_slug"], "bob");

    auto reach = resolvers.resolveStepForClient(stepOf("reach", {{"zone_slug", "hill"}, {"x", 1.5}, {"y", 2.5}}));
    EXPECT_EQ(reach["zone_slug"], "hill");
    EXPECT_FLOAT_EQ(reach["x"].get<float>(), 1.5f);

    auto custom = resolvers.resolveStepForClient(stepOf("custom", {{"foo", "bar"}}));
    EXPECT_EQ(custom["params"]["foo"], "bar");

    // unknown ids -> empty slugs (1-1: lookup miss yields default struct)
    auto missing = resolvers.resolveStepForClient(stepOf("kill", {{"mob_id", 424242}}));
    EXPECT_EQ(missing["target_slug"], "");
}

TEST(QuestResolvers, RewardsClassAndHidden)
{
    Catalogs cats;
    cats.items[5] = "sword";
    QuestResolvers resolvers{cats.lookups()};

    QuestRewardStruct cls = itemReward(5, 1);
    cls.allowedClassIds = {2};
    std::vector<QuestRewardStruct> rewards = {cls, expReward(100)};

    // class filter: warrior (1) skips the class-2 sword
    auto filtered = resolvers.resolveRewardsForClient(rewards, false, 1);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0]["rewardType"], "exp");
    EXPECT_EQ(filtered[0]["amount"], 100);

    // classId 0 = no filter
    auto unfiltered = resolvers.resolveRewardsForClient(rewards, false, 0);
    EXPECT_EQ(unfiltered.size(), 2u);

    // hidden without reveal -> type + isHidden only
    auto hidden = resolvers.resolveRewardsForClient({itemReward(5, 2, true)}, false, 0);
    ASSERT_EQ(hidden.size(), 1u);
    EXPECT_EQ(hidden[0]["isHidden"], true);
    EXPECT_FALSE(hidden[0].contains("item_slug"));

    // reveal -> full disclosure
    auto revealed = resolvers.resolveRewardsForClient({itemReward(5, 2, true)}, true, 0);
    EXPECT_EQ(revealed[0]["isHidden"], false);
    EXPECT_EQ(revealed[0]["item_slug"], "sword");
    EXPECT_EQ(revealed[0]["quantity"], 2);
}
