// Unit tests for SkillManager (DI: explicit managers + Logger, no GameServices).
#include "services/CharacterManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"
#include "services/SkillManager.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace
{

SkillStruct makeSkill(const std::string &slug, int costMp = 10, int cooldown = 0)
{
    SkillStruct s;
    s.skillSlug = slug;
    s.skillName = slug;
    s.costMp = costMp;
    s.cooldownMs = cooldown;
    s.maxRange = 10.0f; // x100 => 1000u
    s.flatAdd = 50.0f;
    s.coeff = 0.0f;
    return s;
}

CharacterDataStruct makeCaster(int id, int mana = 100)
{
    CharacterDataStruct c;
    c.characterId = id;
    c.characterLevel = 10;
    c.characterMaxHealth = 200;
    c.characterCurrentHealth = 200;
    c.characterMaxMana = mana;
    c.characterCurrentMana = mana;
    c.skills = {makeSkill("fireball", 10), makeSkill("frostbolt", 200)};
    return c;
}

MobDataStruct makeMobTemplate()
{
    MobDataStruct m;
    m.id = 5;
    m.uid = 5;
    m.level = 5;
    m.maxHealth = 100;
    m.currentHealth = 100;
    m.skills = {makeSkill("bite", 0)};
    return m;
}

struct SkillFixture : ::testing::Test
{
    Logger logger{"test"};
    GameConfigService config{logger};
    CharacterManager chars{logger};
    MobManager mobs{logger};
    MobInstanceManager instances{logger};
    MobMovementManager movement{chars, instances, mobs, logger};
    SkillManager skills{chars, mobs, instances, movement, config, logger};

    void SetUp() override
    {
        chars.addCharacter(makeCaster(1));
        chars.addCharacter(makeCaster(2));
        mobs.setListOfMobs({makeMobTemplate()});
    }
};

} // namespace

TEST_F(SkillFixture, LookupBySlug)
{
    CharacterDataStruct c = makeCaster(1);
    ASSERT_NE(skills.getCharacterSkill(c, "fireball"), nullptr);
    EXPECT_EQ(skills.getCharacterSkill(c, "nope"), nullptr);

    MobDataStruct m = makeMobTemplate();
    ASSERT_NE(skills.getMobSkill(m, "bite"), nullptr);
    EXPECT_EQ(skills.getMobSkill(m, "nope"), nullptr);
}

TEST_F(SkillFixture, AvailabilityManaAndCooldown)
{
    CharacterDataStruct rich = makeCaster(1, 100);
    CharacterDataStruct poor = makeCaster(1, 5);
    SkillStruct cheap = makeSkill("fireball", 10);
    SkillStruct pricey = makeSkill("frostbolt", 200);

    EXPECT_TRUE(skills.isSkillAvailable(1, cheap, rich));
    EXPECT_FALSE(skills.isSkillAvailable(1, pricey, rich)); // not enough mana
    EXPECT_FALSE(skills.isSkillAvailable(1, cheap, poor));

    skills.setCooldown(1, "fireball", 60000);
    EXPECT_TRUE(skills.isOnCooldown(1, "fireball"));
    EXPECT_FALSE(skills.isSkillAvailable(1, cheap, rich)); // cooldown blocks
    EXPECT_FALSE(skills.isOnCooldown(1, "frostbolt"));
    EXPECT_FALSE(skills.isOnCooldown(2, "fireball"));
}

TEST_F(SkillFixture, UpdateCooldownsExpiresElapsed)
{
    skills.setCooldown(1, "instant", 0); // already elapsed
    skills.setCooldown(1, "slow", 3600000);
    skills.updateCooldowns();
    EXPECT_FALSE(skills.isOnCooldown(1, "instant"));
    EXPECT_TRUE(skills.isOnCooldown(1, "slow"));
}

TEST_F(SkillFixture, UseCharacterSkillSuccess)
{
    CharacterDataStruct c = makeCaster(1);
    c.skills[0].cooldownMs = 60000;
    chars.addCharacter(c); // overwrite fixture entry with a cooldown-bearing skill
    // 5% base miss chance: retry with a fresh manager (fresh cooldowns) until
    // a non-missed hit lands. P(all 10 miss) ~= 1e-13.
    SkillUsageResult r;
    std::unique_ptr<SkillManager> used;
    for (int i = 0; i < 10; ++i)
    {
        auto mgr = std::make_unique<SkillManager>(
            chars, mobs, instances, movement, config, logger);
        r = mgr->useCharacterSkill(1, "fireball", 2);
        EXPECT_TRUE(r.success) << r.errorMessage;
        if (!r.damageResult.isMissed)
        {
            used = std::move(mgr);
            break;
        }
        // Missed: restore target HP and caster mana so the next attempt
        // measures cleanly.
        chars.updateCharacterHealth(2, 200);
        chars.restoreManaToCharacter(1, 100);
    }
    ASSERT_NE(used, nullptr);
    EXPECT_GT(r.damageResult.totalDamage, 0);
    // Target hurt, caster paid mana, cooldown armed.
    EXPECT_LT(chars.getCharacterData(2).characterCurrentHealth, 200);
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 90);
    EXPECT_TRUE(used->isOnCooldown(1, "fireball"));
}

TEST_F(SkillFixture, UseCharacterSkillFailures)
{
    EXPECT_EQ(skills.useCharacterSkill(424242, "fireball", 2).errorMessage, "Caster not found");
    EXPECT_EQ(skills.useCharacterSkill(1, "nope", 2).errorMessage, "Skill not found");
    EXPECT_EQ(skills.useCharacterSkill(1, "fireball", 424242).errorMessage, "Target not found");

    SkillStruct passive = makeSkill("aura", 0);
    passive.isPassive = true;
    CharacterDataStruct c = makeCaster(3);
    c.skills = {passive};
    chars.addCharacter(c);
    EXPECT_EQ(skills.useCharacterSkill(3, "aura", 2).errorMessage, "Cannot cast a passive skill");

    // Second immediate cast hits the cooldown.
    CharacterDataStruct c1 = makeCaster(1);
    c1.skills[0].cooldownMs = 60000;
    chars.addCharacter(c1);
    EXPECT_TRUE(skills.useCharacterSkill(1, "fireball", 2).success);
    auto r2 = skills.useCharacterSkill(1, "fireball", 2);
    EXPECT_FALSE(r2.success);
    EXPECT_EQ(r2.errorMessage, "Skill is on cooldown");
}

TEST_F(SkillFixture, UseCharacterSkillOutOfRange)
{
    CharacterDataStruct far = makeCaster(4);
    far.characterPosition.positionX = 50000.0f;
    chars.addCharacter(far);
    SkillStruct shortBow = makeSkill("poke", 0);
    shortBow.maxRange = 0.01f; // 1u reach
    CharacterDataStruct c = makeCaster(5);
    c.skills = {shortBow};
    chars.addCharacter(c);
    auto r = skills.useCharacterSkill(5, "poke", 4);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Target is out of range");
}

TEST_F(SkillFixture, UseMobSkillSuccess)
{
    mobs.setListOfMobs({makeMobTemplate()}); // template id 5...
    mobs.setListOfMobsSkills({{5, {makeSkill("bite", 0)}}}); // ...with "bite"
    MobDataStruct inst = makeMobTemplate();
    inst.uid = 9001;
    inst.zoneId = 7;
    ASSERT_TRUE(instances.registerMobInstance(inst));
    // Mob is 5 levels below the target: 15% miss chance per roll, and every
    // attempt (hit or miss) arms the cooldown — so retry with a fresh manager
    // until a non-missed hit lands (P(all 10 miss) ~= 1e-12).
    SkillUsageResult r;
    bool hit = false;
    for (int i = 0; i < 10 && !hit; ++i)
    {
        SkillManager fresh(chars, mobs, instances, movement, config, logger);
        r = fresh.useMobSkill(5, "bite", 2);
        EXPECT_TRUE(r.success) << r.errorMessage;
        hit = !r.damageResult.isMissed;
    }
    ASSERT_TRUE(hit);
    EXPECT_LT(chars.getCharacterData(2).characterCurrentHealth, 200);
}

TEST_F(SkillFixture, UseMobSkillUnknownMob)
{
    auto r = skills.useMobSkill(424242, "bite", 2);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.errorMessage, "Mob not found");
}
