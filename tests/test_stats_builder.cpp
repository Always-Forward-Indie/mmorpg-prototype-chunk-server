// Unit tests for StatsPacketBuilder (pure merge attrs/equip/effects/soul → JSON).
#include "services/StatsPacketBuilder.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

CharacterAttributeStruct attr(const std::string &slug, const std::string &name, int value)
{
    CharacterAttributeStruct a;
    a.slug = slug;
    a.name = name;
    a.value = value;
    return a;
}

ActiveEffectStruct effect(const std::string &attrSlug,
    float value,
    int64_t expiresAt = 0,
    int tickMs = 0,
    const std::string &type = "passive")
{
    ActiveEffectStruct e;
    e.effectSlug = attrSlug + "_buff";
    e.effectTypeSlug = type;
    e.attributeSlug = attrSlug;
    e.value = value;
    e.expiresAt = expiresAt;
    e.tickMs = tickMs;
    return e;
}

StatsPacketBuilderInput baseInput()
{
    StatsPacketBuilderInput in;
    in.character.characterId = 7;
    in.character.characterLevel = 5;
    in.character.characterExperiencePoints = 500;
    in.character.expForNextLevel = 800;
    in.character.experienceDebt = 10;
    in.character.freeSkillPoints = 3;
    in.character.characterCurrentHealth = 80;
    in.character.characterMaxHealth = 100;
    in.character.characterCurrentMana = 20;
    in.character.characterMaxMana = 50;
    in.character.attributes = {
        attr("max_health", "Maximum Health", 100),
        attr("max_mana", "Maximum Mana", 50),
        attr("strength", "Strength", 10),
    };
    in.levelStart = 400;
    in.currentWeight = 12.5f;
    in.weightLimit = 100.0f;
    in.nowSec = 1000000;
    return in;
}

const nlohmann::json *findAttr(const nlohmann::json &attrs, const std::string &slug)
{
    for (const auto &a : attrs)
        if (a["slug"] == slug)
            return &a;
    return nullptr;
}

} // namespace

TEST(StatsPacketBuilder, EnvelopeAndScalars)
{
    auto pkt = StatsPacketBuilder::build(baseInput());
    EXPECT_EQ(pkt["header"]["eventType"], "stats_update");
    EXPECT_EQ(pkt["header"]["requestId"], "stats_update_7");
    EXPECT_EQ(pkt["body"]["characterId"], 7);
    EXPECT_EQ(pkt["body"]["level"], 5);
    EXPECT_EQ(pkt["body"]["freeSkillPoints"], 3);
    EXPECT_EQ(pkt["body"]["experience"]["current"], 500);
    EXPECT_EQ(pkt["body"]["experience"]["levelStart"], 400);
    EXPECT_EQ(pkt["body"]["experience"]["nextLevel"], 800);
    EXPECT_EQ(pkt["body"]["experience"]["debt"], 10);
    EXPECT_EQ(pkt["body"]["health"]["current"], 80);
    EXPECT_EQ(pkt["body"]["health"]["max"], 100);
    EXPECT_EQ(pkt["body"]["weight"]["current"], 12.5f);
    EXPECT_EQ(pkt["body"]["activeEffects"].size(), 0u);
}

TEST(StatsPacketBuilder, EffectiveMaxFallbackToStruct)
{
    auto in = baseInput();
    in.character.attributes = {attr("strength", "Strength", 10)}; // no max_* attrs
    auto pkt = StatsPacketBuilder::build(in);
    EXPECT_EQ(pkt["body"]["health"]["max"], 100); // struct fallback
    EXPECT_EQ(pkt["body"]["mana"]["max"], 50);
}

TEST(StatsPacketBuilder, EquipBonusMerge)
{
    auto in = baseInput();
    in.equipBonuses = {{"strength", 5}, {"crit_chance", 2}};
    in.equipNames = {{"crit_chance", "Crit Chance"}};
    auto pkt = StatsPacketBuilder::build(in);
    const auto *str = findAttr(pkt["body"]["attributes"], "strength");
    ASSERT_NE(str, nullptr);
    EXPECT_EQ((*str)["base"], 10);
    EXPECT_EQ((*str)["effective"], 15);
    const auto *crit = findAttr(pkt["body"]["attributes"], "crit_chance");
    ASSERT_NE(crit, nullptr);
    EXPECT_EQ((*crit)["base"], 0); // equip-only slug
    EXPECT_EQ((*crit)["effective"], 2);
    EXPECT_EQ((*crit)["name"], "Crit Chance");
}

TEST(StatsPacketBuilder, EffectsMergeAndDisplay)
{
    auto in = baseInput();
    in.character.activeEffects = {
        effect("strength", 3.0f),                       // permanent buff -> merge
        effect("max_health", 20.0f, 1),                 // expired -> skip everywhere
        effect("hp_regen_per_s", 100.0f, 0, 1000, "dot"), // dot -> display only
    };
    auto pkt = StatsPacketBuilder::build(in);
    const auto *str = findAttr(pkt["body"]["attributes"], "strength");
    ASSERT_NE(str, nullptr);
    EXPECT_EQ((*str)["effective"], 13);
    // expired max_health ignored -> max stays 100
    EXPECT_EQ(pkt["body"]["health"]["max"], 100);
    // display list: permanent + dot, expired excluded
    EXPECT_EQ(pkt["body"]["activeEffects"].size(), 2u);
}

TEST(StatsPacketBuilder, EmptyAttributeSlugSkippedInMerge)
{
    auto in = baseInput();
    ActiveEffectStruct e;
    e.effectSlug = "mystery";
    e.effectTypeSlug = "passive";
    e.attributeSlug = "";
    e.value = 999.0f;
    e.expiresAt = 0;
    in.character.activeEffects = {e};
    auto pkt = StatsPacketBuilder::build(in);
    EXPECT_EQ(findAttr(pkt["body"]["attributes"], ""), nullptr);
    EXPECT_EQ(pkt["body"]["activeEffects"].size(), 1u); // still displayed (1-1)
}

TEST(StatsPacketBuilder, SoulBonus)
{
    auto in = baseInput();
    in.soulAttrSlug = "physical_attack";
    in.soulAttrName = "Physical Attack";
    in.soulBonusFlat = 3;
    in.character.attributes.push_back(attr("physical_attack", "Physical Attack", 20));
    auto pkt = StatsPacketBuilder::build(in);
    const auto *pa = findAttr(pkt["body"]["attributes"], "physical_attack");
    ASSERT_NE(pa, nullptr);
    EXPECT_EQ((*pa)["base"], 20);
    EXPECT_EQ((*pa)["effective"], 23);
}

TEST(StatsPacketBuilder, EffectiveMaxRounding)
{
    auto in = baseInput();
    in.character.activeEffects = {effect("max_mana", 0.5f)};
    auto pkt = StatsPacketBuilder::build(in);
    // 50 + 0.5 = 50.5 -> round half away from zero -> 51
    EXPECT_EQ(pkt["body"]["mana"]["max"], 51);
}

TEST(StatsPacketBuilder, EffectOnlySlugNameFallback)
{
    auto in = baseInput();
    // effect introduces a slug with no base attr and no equip name:
    // display name falls back to the slug itself.
    in.character.activeEffects = {effect("lucky_aura", 4.0f)};
    auto pkt = StatsPacketBuilder::build(in);
    const auto *aura = findAttr(pkt["body"]["attributes"], "lucky_aura");
    ASSERT_NE(aura, nullptr);
    EXPECT_EQ((*aura)["base"], 0);
    EXPECT_EQ((*aura)["effective"], 4);
    EXPECT_EQ((*aura)["name"], "lucky_aura");
}

TEST(StatsPacketBuilder, SoulNameFallbackToSlug)
{
    auto in = baseInput();
    in.soulAttrSlug = "mystic_power";
    in.soulAttrName = ""; // unknown display name -> slug fallback
    in.soulBonusFlat = 2;
    auto pkt = StatsPacketBuilder::build(in);
    const auto *mp = findAttr(pkt["body"]["attributes"], "mystic_power");
    ASSERT_NE(mp, nullptr);
    EXPECT_EQ((*mp)["effective"], 2);
    EXPECT_EQ((*mp)["name"], "mystic_power");
}
