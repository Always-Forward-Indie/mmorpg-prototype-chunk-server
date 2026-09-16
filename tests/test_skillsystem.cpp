// Unit tests for SkillSystem resolver (explicit DI + shared CooldownService).
// SkillSystem.cpp was never linked into unit_tests before (only SkillManager
// was); this closes the gap. Cooldown math itself lives in test_cooldown.cpp.
#include "services/SkillSystem.hpp"
#include "services/CharacterManager.hpp"
#include "services/CooldownService.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

SkillStruct makeSkill(const std::string &slug, int costMp, int cooldownMs, float maxRange)
{
    SkillStruct s;
    s.skillSlug = slug;
    s.skillName = slug;
    s.costMp = costMp;
    s.cooldownMs = cooldownMs;
    s.gcdMs = 0;
    s.maxRange = maxRange;
    s.skillEffectType = "damage";
    s.coeff = 1.0f;
    return s;
}

struct SystemFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    MobManager mobs{logger};
    MobInstanceManager instances{logger};
    MobMovementManager movement{chars, instances, mobs, logger};
    CooldownService cooldowns{logger};
    SkillSystem skills{chars, instances, mobs, movement, cooldowns, logger};

    void SetUp() override
    {
        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 100;
        c.characterMaxMana = 100;
        c.characterCurrentMana = 100;
        c.characterPosition.positionX = 0.0f;
        c.characterPosition.positionY = 0.0f;
        c.skills = {makeSkill("strike", 10, 1000, 10.0f), makeSkill("free_hit", 0, 0, 10.0f)};
        chars.addCharacter(c);

        MobDataStruct m;
        m.uid = 9001;
        m.id = 1;
        m.level = 5;
        m.maxHealth = 200;
        m.currentHealth = 200;
        m.maxMana = 50;
        m.currentMana = 50;
        m.position.positionX = 100.0f; // in range (10m = 1000u)
        m.position.positionY = 0.0f;
        ASSERT_TRUE(instances.registerMobInstance(m));
    }
};

} // namespace

TEST_F(SystemFixture, LookupCharacterSkill)
{
    auto found = skills.getCharacterSkill(1, "strike");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->costMp, 10);
    EXPECT_FALSE(skills.getCharacterSkill(1, "nope").has_value());
    EXPECT_FALSE(skills.getCharacterSkill(424242, "strike").has_value());
}

TEST_F(SystemFixture, LookupMobSkillFallsBackToTemplate)
{
    MobDataStruct tmpl;
    tmpl.id = 1;
    mobs.setListOfMobs({tmpl});
    // Template skills arrive via a separate mapping (setListOfMobs drops them, 1-1).
    mobs.setListOfMobsSkills({{1, {makeSkill("bite", 0, 0, 5.0f)}}});
    auto found = skills.getMobSkill(9001, "bite"); // instance has no skills
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->skillSlug, "bite");
    EXPECT_FALSE(skills.getMobSkill(9001, "nope").has_value());
}

TEST_F(SystemFixture, UseCharacterSkillSuccess)
{
    auto r = skills.useSkill(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_TRUE(r.success) << r.errorMessage;
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 90); // 100 - 10
    EXPECT_TRUE(skills.isOnCooldown(1, "strike"));                 // shared table
}

TEST_F(SystemFixture, UseSkillInsufficientMana)
{
    CharacterDataStruct c = chars.getCharacterData(1);
    c.characterCurrentMana = 5;
    chars.loadCharacterData(c);
    auto r = skills.useSkill(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Insufficient resources");
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 5); // untouched
    EXPECT_FALSE(skills.isOnCooldown(1, "strike"));               // no cooldown either
}

TEST_F(SystemFixture, UseMobCasterSuccess)
{
    MobDataStruct mob = instances.getMobInstance(9001);
    mob.skills = {makeSkill("bite", 5, 0, 10.0f)};
    instances.unregisterMobInstance(9001);
    ASSERT_TRUE(instances.registerMobInstance(mob));
    auto r = skills.useSkill(9001, "bite", 1, CombatTargetType::PLAYER);
    EXPECT_TRUE(r.success) << r.errorMessage;
    EXPECT_EQ(instances.getMobInstance(9001).currentMana, 45); // 50 - 5
}

TEST_F(SystemFixture, UseSkillGCDMessage)
{
    CharacterDataStruct c = chars.getCharacterData(1);
    for (auto &s : c.skills)
        if (s.skillSlug == "strike")
            s.gcdMs = 500;
    chars.loadCharacterData(c);
    cooldowns.trySetCooldown(1, "other", 60000, 500); // arms GCD only
    auto r = skills.useSkill(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Global cooldown active");
}

TEST_F(SystemFixture, UseSkillOnSelf)
{
    auto r = skills.useSkill(1, "free_hit", 1, CombatTargetType::SELF);
    EXPECT_TRUE(r.success) << r.errorMessage;
}

TEST_F(SystemFixture, UseSkillUnknownCaster)
{
    auto r = skills.useSkill(424242, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Unknown caster type");
}

TEST_F(SystemFixture, UseSkillDeadTarget)
{
    instances.applyDamageToMob(9001, 1000000);
    auto r = skills.useSkill(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Invalid target");
}

TEST_F(SystemFixture, UseSkillOutOfRange)
{
    auto m = instances.getMobInstance(9001);
    m.position.positionX = 50000.0f;
    instances.unregisterMobInstance(9001);
    m.uid = 9001;
    ASSERT_TRUE(instances.registerMobInstance(m));
    auto r = skills.useSkill(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Target is out of range");
}

TEST_F(SystemFixture, UseSkillOnCooldownRefundsMana)
{
    skills.setCooldown(1, "strike", 60000);
    auto r = skills.useSkill(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 100); // refunded
}

TEST_F(SystemFixture, CooldownForwards)
{
    EXPECT_TRUE(skills.isSkillAvailable(1, "strike"));
    skills.setCooldown(1, "strike", 60000);
    EXPECT_FALSE(skills.isSkillAvailable(1, "strike"));
    EXPECT_TRUE(skills.isOnCooldown(1, "strike"));
    bool onGCD = false;
    EXPECT_FALSE(skills.trySetCooldown(1, "strike", 60000, 0, &onGCD));
    EXPECT_FALSE(onGCD);
    skills.restoreCooldown(2, "fresh", 60000);
    EXPECT_TRUE(skills.isOnCooldown(2, "fresh"));
    skills.restoreCooldown(2, "noop", 0);
    EXPECT_TRUE(skills.trySetCooldown(1, "with_gcd", 60000, 500));
    EXPECT_TRUE(skills.isGCDActive(1));
    EXPECT_FALSE(skills.isGCDActive(424242));
    skills.updateCooldowns(); // GC keeps live entries
    EXPECT_TRUE(skills.isOnCooldown(1, "strike"));
    EXPECT_NE(skills.getCombatCalculator(), nullptr);
}

TEST_F(SystemFixture, BestSkillForMob)
{
    MobDataStruct mob;
    mob.uid = 9001;
    SkillStruct fireball = makeSkill("fireball", 0, 5000, 10.0f);
    fireball.coeff = 5.0f; // scores above the zero-cooldown basic (1-1 scoring)
    mob.skills = {makeSkill("bite", 0, 0, 10.0f), fireball};
    CharacterDataStruct tgt;
    auto best = skills.getBestSkillForMob(mob, tgt, 600.0f); // >500: range bonus
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->get().skillSlug, "fireball"); // 55 + 100 > basic
    skills.setCooldown(9001, "fireball", 60000);
    auto fallback = skills.getBestSkillForMob(mob, tgt, 500.0f);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->get().skillSlug, "bite");
    auto none = skills.getBestSkillForMob(mob, tgt, 50000.0f); // out of all ranges
    EXPECT_FALSE(none.has_value());
}
