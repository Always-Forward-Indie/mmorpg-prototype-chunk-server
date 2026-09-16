// Unit tests for CombatCalculator (GameConfigService == nullptr => defaults).
// RNG rolls (miss/crit/block/variance) are covered via ranges and invariants
// over many samples - no exact-value assertions on random draws.
#include "services/CombatCalculator.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace
{

CharacterAttributeStruct attr(const std::string &slug, int value)
{
    CharacterAttributeStruct a;
    a.slug = slug;
    a.value = value;
    return a;
}

MobAttributeStruct mobAttr(const std::string &slug, int value)
{
    MobAttributeStruct a;
    a.slug = slug;
    a.value = value;
    return a;
}

SkillStruct damageSkill(const std::string &school = "physical")
{
    SkillStruct s;
    s.skillSlug = "test_strike";
    s.school = school;
    s.scaleStat = "strength";
    s.flatAdd = 100.0f;
    s.coeff = 1.0f;
    return s;
}

CharacterDataStruct character(int level, std::vector<CharacterAttributeStruct> attrs)
{
    CharacterDataStruct c;
    c.characterLevel = level;
    c.attributes = std::move(attrs);
    return c;
}

ActiveEffectStruct buff(const std::string &slug, float value, int64_t expiresAt = 0,
    const std::string &type = "buff")
{
    ActiveEffectStruct e;
    e.attributeSlug = slug;
    e.value = value;
    e.expiresAt = expiresAt;
    e.effectTypeSlug = type;
    return e;
}

// Collect N non-missed samples (miss chance is always >= 5%, so loop with a
// generous deterministic cap instead of asserting on any single roll).
std::vector<DamageCalculationStruct> collectHits(CombatCalculator &calc,
    const SkillStruct &skill, const CharacterDataStruct &atk, const CharacterDataStruct &tgt, int n)
{
    std::vector<DamageCalculationStruct> out;
    for (int i = 0; i < 2000 && static_cast<int>(out.size()) < n; ++i)
    {
        DamageCalculationStruct r = calc.calculateSkillDamage(skill, atk, tgt);
        EXPECT_GE(r.totalDamage, 0);
        EXPECT_GE(r.scaledDamage, 0);
        if (!r.isMissed)
            out.push_back(r);
    }
    return out;
}

} // namespace

TEST(CombatCalculator, GetAttributeValue)
{
    CombatCalculator calc(nullptr);
    std::vector<CharacterAttributeStruct> attrs = {attr("strength", 50), attr("accuracy", 10)};
    EXPECT_EQ(calc.getAttributeValue(attrs, "strength"), 50);
    EXPECT_EQ(calc.getAttributeValue(attrs, "missing"), 0);

    std::vector<MobAttributeStruct> mattrs = {mobAttr("strength", 7)};
    EXPECT_EQ(calc.getAttributeValue(mattrs, "strength"), 7);
    EXPECT_EQ(calc.getAttributeValue(mattrs, "missing"), 0);
}

TEST(CombatCalculator, ApplyDefenseExactDefaults)
{
    CombatCalculator calc(nullptr);
    // K=7.5, cap=0.85, level clamped to >= 1.
    EXPECT_EQ(calc.applyDefense(100, 0, "physical", 10), 100);
    EXPECT_EQ(calc.applyDefense(0, 500, "physical", 10), 0);
    // reduction = 75/(75+7.5*10) = 0.5 -> 100 * 0.5 = 50
    EXPECT_EQ(calc.applyDefense(100, 75, "physical", 10), 50);
    // Huge armor clamps at 0.85: 100 * 0.15 = 15 (lround, no float truncation).
    EXPECT_EQ(calc.applyDefense(100, 100000, "physical", 10), 15);
    // Level 0 behaves as level 1 (no division pathologies)
    EXPECT_GE(calc.applyDefense(100, 75, "physical", 0), 0);
    EXPECT_LE(calc.applyDefense(100, 75, "physical", 0), 100);
}

TEST(CombatCalculator, BaseDamageStaysInVarianceBand)
{
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    // raw = 100 + 50*1 = 150, variance default 12% -> [132, 168]
    std::vector<CharacterAttributeStruct> attrs = {attr("strength", 50)};
    for (int i = 0; i < 50; ++i)
    {
        int dmg = calc.calculateBaseDamage(skill, attrs);
        EXPECT_GE(dmg, 125);
        EXPECT_LE(dmg, 175);
    }
}

TEST(CombatCalculator, BaseDamageMobOverloadSameBand)
{
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    std::vector<MobAttributeStruct> attrs = {mobAttr("strength", 50)};
    for (int i = 0; i < 50; ++i)
    {
        int dmg = calc.calculateBaseDamage(skill, attrs);
        EXPECT_GE(dmg, 125);
        EXPECT_LE(dmg, 175);
    }
}

TEST(CombatCalculator, BaseDamageFloorIsOne)
{
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    skill.flatAdd = -1000.0f;
    skill.coeff = 0.0f;
    std::vector<CharacterAttributeStruct> attrs; // no scale stat -> raw = max(1, -1000) = 1
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(calc.calculateBaseDamage(skill, attrs), 1);
}

TEST(CombatCalculator, HealAmountFloorIsOne)
{
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    skill.flatAdd = -1000.0f;
    skill.coeff = 0.0f;
    std::vector<CharacterAttributeStruct> attrs;
    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(calc.calculateHealAmount(skill, attrs), 1);
}

TEST(CombatCalculator, DamageTypeMapping)
{
    CombatCalculator calc(nullptr);
    CharacterDataStruct atk = character(10, {attr("strength", 50), attr("accuracy", 100)});
    CharacterDataStruct tgt = character(10, {});
    for (int i = 0; i < 50; ++i)
    {
        EXPECT_EQ(calc.calculateSkillDamage(damageSkill("physical"), atk, tgt).damageType, "physical");
        EXPECT_EQ(calc.calculateSkillDamage(damageSkill("fire"), atk, tgt).damageType, "magical");
    }
}

TEST(CombatCalculator, ActiveBuffRaisesDamageBand)
{
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    CharacterDataStruct tgt = character(10, {});
    // Unbuffed raw = 150 -> samples in [132, 168].
    CharacterDataStruct plain = character(10, {attr("strength", 50)});
    auto plainHits = collectHits(calc, skill, plain, tgt, 20);
    ASSERT_EQ(plainHits.size(), 20u);
    for (const auto &r : plainHits)
    {
        EXPECT_GE(r.baseDamage, 125);
        EXPECT_LE(r.baseDamage, 175);
    }
    // +50 strength buff -> raw = 200 -> samples in [176, 224].
    CharacterDataStruct buffed = character(10, {attr("strength", 50)});
    buffed.activeEffects.push_back(buff("strength", 50.0f));
    auto buffedHits = collectHits(calc, skill, buffed, tgt, 20);
    ASSERT_EQ(buffedHits.size(), 20u);
    for (const auto &r : buffedHits)
    {
        EXPECT_GE(r.baseDamage, 170);
        EXPECT_LE(r.baseDamage, 230);
    }
}

TEST(CombatCalculator, ExpiredBuffAndDotsAreIgnored)
{
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    CharacterDataStruct tgt = character(10, {});
    CharacterDataStruct c = character(10, {attr("strength", 50)});
    c.activeEffects.push_back(buff("strength", 500.0f, 1)); // expired in 1970
    c.activeEffects.push_back(buff("strength", 500.0f, 0, "dot")); // DoT is not a stat mod
    c.activeEffects.push_back(buff("strength", 500.0f, 0, "hot")); // HoT is not a stat mod
    auto hits = collectHits(calc, skill, c, tgt, 20);
    ASSERT_EQ(hits.size(), 20u);
    for (const auto &r : hits)
    {
        EXPECT_GE(r.baseDamage, 125); // unbuffed band
        EXPECT_LE(r.baseDamage, 175);
    }
}

TEST(CombatCalculator, HitChanceClampedToFloorAndCeil)
{
    // hitChance is clamped to [0.05, 0.95]: even overwhelming accuracy still
    // misses sometimes, and even overwhelming evasion still gets hit sometimes.
    // 2000 samples make the wrong-clamp outcome ~1e-45 (deterministic in practice).
    CombatCalculator calc(nullptr);
    SkillStruct skill = damageSkill();
    CharacterDataStruct tgt = character(10, {});
    {
        CharacterDataStruct god = character(10, {attr("accuracy", 10000)});
        int misses = 0, hits = 0;
        for (int i = 0; i < 2000; ++i)
        {
            if (calc.calculateSkillDamage(skill, god, tgt).isMissed)
                ++misses;
            else
                ++hits;
        }
        EXPECT_GT(misses, 0); // 5% miss floor survives godlike accuracy
        EXPECT_GT(hits, 0);
    }
    {
        CharacterDataStruct ghost = character(10, {});
        CharacterDataStruct tank = character(10, {attr("evasion", 10000)});
        int misses = 0, hits = 0;
        for (int i = 0; i < 2000; ++i)
        {
            if (calc.calculateSkillDamage(skill, ghost, tank).isMissed)
                ++misses;
            else
                ++hits;
        }
        EXPECT_GT(hits, 0); // 5% hit floor survives godlike evasion
        EXPECT_GT(misses, 0);
    }
}

TEST(CombatCalculator, GlobalInvariantsHoldOnEveryRoll)
{
    CombatCalculator calc(nullptr);
    CharacterDataStruct atk = character(10, {attr("strength", 50), attr("crit_chance", 50)});
    CharacterDataStruct tgt = character(10, {attr("physical_defense", 30), attr("block_chance", 30)});
    for (int i = 0; i < 200; ++i)
    {
        DamageCalculationStruct r = calc.calculateSkillDamage(damageSkill(), atk, tgt);
        EXPECT_GE(r.totalDamage, 0);
        EXPECT_GE(r.scaledDamage, 0);
        if (!r.isMissed)
            EXPECT_GE(r.baseDamage, 1);
    }
}
