// Unit tests for StatsResolver (P0-1 single source of truth).
//
// Pins: stored attributes pass through untouched (equip/set/durability live
// in DB values — never re-added); effects apply with mergeEffects rules
// (skip empty/expired/dot/hot, round, append unknowns); conversions are off
// by default and exact when enabled.
#include "services/StatsResolver.hpp"

#include <gtest/gtest.h>

namespace
{

CharacterAttributeStruct attr(const std::string &slug, int value)
{
    CharacterAttributeStruct a;
    a.slug = slug;
    a.value = value;
    return a;
}

ActiveEffectStruct eff(const std::string &slug, float value,
    const std::string &type = "buff", int64_t expires = 0)
{
    ActiveEffectStruct e;
    e.attributeSlug = slug;
    e.value = value;
    e.effectTypeSlug = type;
    e.expiresAt = expires;
    return e;
}

} // namespace

TEST(StatsResolver, StoredPassThroughNoDoubleCount)
{
    // Warrior with sword already inside stored (+3 phys from DB).
    // Resolver must NOT add anything by itself: 17 stays 17.
    const std::vector<CharacterAttributeStruct> stored = {attr("physical_attack", 17)};
    const auto out = StatsResolver::effectiveMap(stored, {}, 1000);
    EXPECT_FLOAT_EQ(out.at("physical_attack"), 17.0f);
}

TEST(StatsResolver, EffectsApplyWithMergeRules)
{
    const std::vector<CharacterAttributeStruct> stored = {attr("physical_attack", 17)};
    const std::vector<ActiveEffectStruct> effects = {
        eff("physical_attack", 2.6f), // rounds to +3
        eff("", 99.0f), // empty slug skipped
        eff("evasion", 5.0f, "buff", 500), // expired (now=1000) skipped
        eff("physical_attack", 4.0f, "dot"), // dot skipped
        eff("evasion", 5.0f), // unknown slug appended
    };
    const auto out = StatsResolver::effectiveMap(stored, effects, 1000);
    // Map form keeps float precision (no rounding — same as the old packet
    // builder); the vector form rounds (same as old mergeEffects).
    EXPECT_FLOAT_EQ(out.at("physical_attack"), 19.6f);
    EXPECT_FLOAT_EQ(out.at("evasion"), 5.0f);
}

TEST(StatsResolver, MergedVectorMatchesOldMergeEffects)
{
    // Order + rounding 1-1 with the old CombatCalculator::mergeEffects loop.
    const std::vector<CharacterAttributeStruct> stored = {attr("a", 1), attr("b", 2)};
    const std::vector<ActiveEffectStruct> effects = {eff("b", 1.4f), eff("c", 3.0f)};
    const auto out = StatsResolver::mergedVector(stored, effects, 1000);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].slug, "a");
    EXPECT_EQ(out[0].value, 1);
    EXPECT_EQ(out[1].slug, "b");
    EXPECT_EQ(out[1].value, 3); // 2 + round(1.4)
    EXPECT_EQ(out[2].slug, "c");
    EXPECT_EQ(out[2].value, 3);
}

TEST(StatsResolver, ConversionsOffByDefaultExactWhenOn)
{
    const std::vector<CharacterAttributeStruct> stored = {
        attr("agility", 10), attr("evasion", 5), attr("strength", 8)};
    const auto off = StatsResolver::effectiveMap(stored, {}, 1000);
    EXPECT_FLOAT_EQ(off.at("evasion"), 5.0f);
    StatsResolverOptions opts;
    opts.agilityToEvasion = 0.3f;
    opts.strengthToPhysAttack = 0.5f;
    const auto on = StatsResolver::effectiveMap(stored,
        std::vector<ActiveEffectStruct>{}, 1000, opts);
    EXPECT_FLOAT_EQ(on.at("evasion"), 8.0f); // 5 + 10*0.3
    EXPECT_FLOAT_EQ(on.at("physical_attack"), 4.0f); // 0 + 8*0.5
}

TEST(StatsResolver, MergedVectorConversionsAppendUnknown)
{
    const std::vector<CharacterAttributeStruct> stored = {attr("strength", 8)};
    StatsResolverOptions opts;
    opts.strengthToPhysAttack = 0.5f;
    const auto out = StatsResolver::mergedVector(stored, {}, 1000, opts);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[1].slug, "physical_attack");
    EXPECT_EQ(out[1].value, 4);
}

namespace
{
ActiveEffectStruct pctEff(const std::string &slug, float pct)
{
    ActiveEffectStruct e;
    e.attributeSlug = slug;
    e.value = pct;
    e.modifierType = "percent";
    return e;
}
} // namespace

TEST(StatsResolver, PercentIsOfStoredBaseNotRunningTotal)
{
    // constitution_mastery: max_health +8% on base 100, plus a +20 flat.
    // Percent applies to STORED base regardless of effect order.
    const std::vector<CharacterAttributeStruct> stored = {attr("max_health", 100)};
    const std::vector<ActiveEffectStruct> effects = {
        pctEff("max_health", 8.0f), eff("max_health", 20.0f)};
    const auto map = StatsResolver::effectiveMap(stored, effects, 1000);
    EXPECT_FLOAT_EQ(map.at("max_health"), 128.0f); // 100 + 20 + 8
    const auto vec = StatsResolver::mergedVector(stored, effects, 1000);
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0].value, 128);
}

TEST(StatsResolver, PercentWithoutBaseAddsNothing)
{
    const std::vector<CharacterAttributeStruct> stored = {attr("strength", 5)};
    const auto out = StatsResolver::effectiveMap(
        stored, {pctEff("max_health", 8.0f)}, 1000);
    EXPECT_FLOAT_EQ(out.at("max_health"), 0.0f);
}
