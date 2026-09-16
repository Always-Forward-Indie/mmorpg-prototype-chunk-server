// Unit tests for CombatResponseBuilder (DI: CharacterManager +
// MobInstanceManager + Logger, no GameServices).
#include "services/CharacterManager.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/MobInstanceManager.hpp"

#include <gtest/gtest.h>

namespace
{

struct ResponseFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    MobInstanceManager mobs{logger};
    CombatResponseBuilder builder{chars, mobs, logger};

    void SetUp() override
    {
        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        chars.addCharacter(c);

        MobDataStruct m;
        m.id = 7;
        m.uid = 9001;
        m.zoneId = 3;
        mobs.registerMobInstance(m);
    }
};

SkillInitiationResult makeInitiation(int caster)
{
    SkillInitiationResult r;
    r.success = true;
    r.casterId = caster;
    r.targetId = 2;
    r.targetType = CombatTargetType::MOB;
    r.skillName = "Fireball";
    r.skillSlug = "fireball";
    r.skillEffectType = "damage";
    r.skillSchool = "fire";
    r.castTime = 1.5f;
    r.cooldownMs = 3000;
    return r;
}

} // namespace

TEST_F(ResponseFixture, CasterTypeResolution)
{
    auto player = builder.buildSkillInitiationBroadcast(makeInitiation(1));
    EXPECT_EQ(player["body"]["skillInitiation"]["casterType"], 1);
    EXPECT_EQ(player["body"]["skillInitiation"]["casterTypeString"], "PLAYER");

    auto mob = builder.buildSkillInitiationBroadcast(makeInitiation(9001));
    EXPECT_EQ(mob["body"]["skillInitiation"]["casterType"], 2);
    EXPECT_EQ(mob["body"]["skillInitiation"]["casterTypeString"], "MOB");

    auto unknown = builder.buildSkillInitiationBroadcast(makeInitiation(424242));
    EXPECT_EQ(unknown["body"]["skillInitiation"]["casterType"], 0);
    EXPECT_EQ(unknown["body"]["skillInitiation"]["casterTypeString"], "UNKNOWN");
}

TEST_F(ResponseFixture, InitiationSuccessAndFailure)
{
    auto ok = builder.buildSkillInitiationBroadcast(makeInitiation(1));
    EXPECT_EQ(ok["header"]["eventType"], "combatInitiation");
    EXPECT_FALSE(ok["body"]["skillInitiation"].contains("errorReason"));

    SkillInitiationResult bad = makeInitiation(1);
    bad.success = false;
    bad.errorMessage = "Not enough mana";
    auto err = builder.buildSkillInitiationBroadcast(bad);
    EXPECT_EQ(err["body"]["skillInitiation"]["errorReason"], "Not enough mana");

    SkillInitiationResult heal = makeInitiation(1);
    heal.skillEffectType = "heal";
    EXPECT_EQ(builder.buildSkillInitiationBroadcast(heal)["header"]["eventType"],
        "healingInitiation");
}

TEST_F(ResponseFixture, ExecutionBroadcast)
{
    SkillExecutionResult r;
    r.success = true;
    r.casterId = 9001;
    r.targetId = 1;
    r.targetType = CombatTargetType::PLAYER;
    r.skillName = "Bite";
    r.skillSlug = "bite";
    r.skillEffectType = "damage";
    r.skillSchool = "physical";
    r.targetDied = false;
    r.finalTargetHealth = 150;
    r.skillResult.damageResult.totalDamage = 25;
    auto out = builder.buildSkillExecutionBroadcast(r);
    EXPECT_EQ(out["header"]["eventType"], "combatResult");
    EXPECT_EQ(out["body"]["skillResult"]["casterTypeString"], "MOB");
    EXPECT_EQ(out["body"]["skillResult"]["finalTargetHealth"], 150);
    EXPECT_EQ(out["body"]["skillResult"]["damage"], 25);
}

TEST_F(ResponseFixture, AoeBroadcastTargets)
{
    AoESkillExecutionResult r;
    r.casterId = 1;
    r.skillSlug = "firestorm";
    AoETargetResultEntry t1;
    t1.targetId = 9001;
    t1.damage = 25;
    AoETargetResultEntry t2;
    t2.targetId = 9002;
    t2.damage = 30;
    t2.isCritical = true;
    r.targets = {t1, t2};
    auto out = builder.buildAoESkillExecutionBroadcast(r);
    EXPECT_EQ(out["header"]["eventType"], "combatAoeResult");
    ASSERT_EQ(out["body"]["aoeResult"]["targets"].size(), 2u);
    EXPECT_EQ(out["body"]["aoeResult"]["targets"][1]["damage"], 30);
    EXPECT_EQ(out["body"]["aoeResult"]["targets"][1]["isCritical"], true);
}

TEST_F(ResponseFixture, ErrorAndTickPackets)
{
    auto err = builder.buildErrorResponse("nope", "useSkill", 5);
    EXPECT_EQ(err["header"]["eventType"], "useSkill");

    EffectTickResult tick;
    tick.characterId = 1;
    tick.effectSlug = "burn";
    tick.value = 7.0f;
    tick.newHealth = 140;
    tick.targetDied = false;
    auto out = builder.buildEffectTickBroadcast(tick);
    EXPECT_EQ(out["header"]["eventType"], "effectTick");
    EXPECT_EQ(out["body"]["characterId"], 1);
    EXPECT_EQ(out["body"]["newHealth"], 140);
}
