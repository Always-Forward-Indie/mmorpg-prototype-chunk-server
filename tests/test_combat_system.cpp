// Unit tests for the CombatSystem orchestrator (Wave 4.1).
//
// CombatCalculator/SkillInitiationValidator/MobKillRewardPipeline were covered
// earlier; this drives the orchestrator itself with a real GameServices:
// initiate → execute, dead-target and PvP guards, unknown-skill rejection.
// Network-free by construction (CombatSystem only touches managers).
#include "services/CombatSystem.hpp"
#include "services/GameServices.hpp"
#include "utils/RandomUtils.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{

SkillStruct makeSkill(const std::string &slug)
{
    SkillStruct s;
    s.skillSlug = slug;
    s.skillName = slug;
    s.costMp = 10;
    s.cooldownMs = 1000;
    s.gcdMs = 0;
    s.maxRange = 10.0f;
    s.school = "physical";
    s.scaleStat = "strength";
    s.flatAdd = 100.0f;
    s.coeff = 1.0f;
    return s;
}

struct CombatFixture : ::testing::Test
{
    Logger logger{"test"};
    GameServices gs{logger};
    CombatSystem *combat = nullptr;

    void SetUp() override
    {
        // Deterministic RNG for the whole fixture: combat rolls (miss/crit/
        // block/variance) run on this thread's engine. Hit chance here is at
        // the 0.95 cap, so an unseeded engine misses ~1 run in 20 and the
        // damage test flakes. Seed chosen so the opening rolls hit; if the
        // calculator's roll order ever changes, re-verify the seed instead
        // of deleting the determinism.
        RandomUtils::seedForTests(12345u);
        combat = new CombatSystem(&gs);

        CharacterDataStruct c;
        c.characterId = 1;
        c.characterLevel = 10;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 100;
        c.characterMaxMana = 100;
        c.characterCurrentMana = 100;
        c.characterPosition.positionX = 0.0f;
        c.characterPosition.positionY = 0.0f;
        c.skills = {makeSkill("strike")};
        gs.getCharacterManager().addCharacter(c);

        CharacterDataStruct c2 = c;
        c2.characterId = 2;
        gs.getCharacterManager().addCharacter(c2);

        MobDataStruct m;
        m.uid = 9001;
        m.id = 1;
        m.level = 5;
        m.maxHealth = 200;
        m.currentHealth = 200;
        m.maxMana = 50;
        m.currentMana = 50;
        m.position.positionX = 100.0f; // in range
        m.position.positionY = 0.0f;
        ASSERT_TRUE(gs.getMobInstanceManager().registerMobInstance(m));
    }

    void TearDown() override { delete combat; }
};

} // namespace

TEST_F(CombatFixture, InitiateAndExecuteStrikeDamagesMob)
{
    auto init = combat->initiateSkillUsage(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_TRUE(init.success) << init.errorMessage;
    auto exec = combat->executeSkillUsage(1, "strike", 9001, CombatTargetType::MOB, true);
    EXPECT_TRUE(exec.success) << exec.errorMessage;
    // Branch-aware: the seed above makes the opening rolls hit, but the
    // assertions stay correct under ANY rng — a miss legitimately deals 0
    // and leaves HP intact, a hit must damage. Mana is paid either way
    // (resources are consumed before the miss roll).
    const auto &dmg = exec.skillResult.damageResult;
    if (dmg.isMissed)
    {
        EXPECT_EQ(dmg.totalDamage, 0);
        EXPECT_EQ(gs.getMobInstanceManager().getMobInstance(9001).currentHealth, 200);
    }
    else
    {
        EXPECT_GT(dmg.totalDamage, 0);
        EXPECT_LT(gs.getMobInstanceManager().getMobInstance(9001).currentHealth, 200);
    }
    // Caster paid mana through the shared skill path.
    EXPECT_LT(gs.getCharacterManager().getCharacterData(1).characterCurrentMana, 100);
}

TEST_F(CombatFixture, UnknownSkillRejected)
{
    auto init = combat->initiateSkillUsage(1, "nope", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(init.success);
    EXPECT_FALSE(init.errorMessage.empty());
}

TEST_F(CombatFixture, DeadMobTargetRejected)
{
    MobDataStruct m = gs.getMobInstanceManager().getMobInstance(9001);
    m.currentHealth = 0;
    m.isDead = true;
    // push the dead state back through unregister/register cycle
    gs.getMobInstanceManager().unregisterMobInstance(9001);
    ASSERT_TRUE(gs.getMobInstanceManager().registerMobInstance(m));
    auto init = combat->initiateSkillUsage(1, "strike", 9001, CombatTargetType::MOB);
    EXPECT_FALSE(init.success);
    EXPECT_EQ(init.errorMessage, "Target is dead");
}

TEST_F(CombatFixture, PvpDamageBlocked)
{
    // The PvP guard lives in the execution path (initiation only resolves
    // the skill): initiate succeeds, execute refuses with PvP disabled.
    auto init = combat->initiateSkillUsage(1, "strike", 2, CombatTargetType::PLAYER);
    EXPECT_TRUE(init.success) << init.errorMessage;
    auto exec = combat->executeSkillUsage(1, "strike", 2, CombatTargetType::PLAYER, true);
    // The PvP guard lives behind the damage>0 branch, so this test needs a
    // hit. The fixture seed provides one; a miss here means the seed drifted
    // (calculator roll order changed) — fail loud, do not assert the guard.
    ASSERT_FALSE(exec.skillResult.damageResult.isMissed) << "fixture seed drifted: re-pick seedForTests";
    EXPECT_FALSE(exec.success);
    EXPECT_EQ(exec.errorMessage, "PvP is not available");
    // Target untouched.
    EXPECT_EQ(gs.getCharacterManager().getCharacterData(2).characterCurrentHealth, 100);
}
