// Unit tests for MobAIController chase-timeout semantics + MobAIFormulas.
//
// Regression (L4 death swarm): the old code dropped EVERY chase after
// chaseDuration even with the target standing in attack range, so mobs could
// never kill a passive player (1 mob hit per 10 min). Timeout now governs
// pursuit only: in-range live targets keep the fight.
#include "services/CharacterManager.hpp"
#include "services/MobAIController.hpp"
#include "services/MobAIFormulas.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"

#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

namespace
{

MobDataStruct makeMob()
{
    MobDataStruct m;
    m.id = 1;
    m.uid = 9001;
    m.zoneId = 7;
    m.level = 5;
    m.maxHealth = 100;
    m.currentHealth = 100;
    m.attackRange = 150.0f;
    m.aggroRange = 2000.0f;
    m.chaseDuration = 10.0f;
    m.fleeHpThreshold = 0.0f;
    m.position.positionX = 0.0f;
    m.position.positionY = 0.0f;
    return m;
}

CharacterDataStruct makeChar()
{
    CharacterDataStruct c;
    c.characterId = 1;
    c.characterLevel = 10;
    c.characterMaxHealth = 200;
    c.characterCurrentHealth = 200;
    c.characterPosition.positionX = 50.0f; // inside attackRange
    c.characterPosition.positionY = 0.0f;
    return c;
}

struct AiFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    MobInstanceManager instances{logger};
    MobManager mobs{logger};
    MobMovementManager movement{chars, instances, mobs, logger};
    // eventQueue/combatSystem late-wire: nullptr, guarded (matches production
    // order where ChunkServer wires them after GameServices construction).
    MobAIController ai{chars, instances, mobs, movement, nullptr, nullptr, logger};

    void SetUp() override
    {
        chars.addCharacter(makeChar());
    }

    MobMovementData chasingData()
    {
        MobMovementData d;
        d.combatState = MobCombatState::CHASING;
        d.targetPlayerId = 1;
        d.stateChangeTime = 0.0f; // chasing "since forever"
        return d;
    }
};

} // namespace

TEST_F(AiFixture, ExpiredChaseKeepsInRangeTarget)
{
    MobDataStruct mob = makeMob();
    MobMovementData d = chasingData();
    // currentTime far past chaseDuration, target alive and in range.
    ai.updateMobCombatState(mob, d, 1000.0f);
    EXPECT_NE(d.combatState, MobCombatState::RETURNING);
    EXPECT_EQ(d.targetPlayerId, 1);
}

TEST_F(AiFixture, ExpiredChaseDropsFarTarget)
{
    MobDataStruct mob = makeMob();
    CharacterDataStruct far = makeChar();
    far.characterId = 2;
    far.characterPosition.positionX = 5000.0f; // beyond attackRange
    chars.addCharacter(far);
    MobMovementData d = chasingData();
    d.targetPlayerId = 2;
    ai.updateMobCombatState(mob, d, 1000.0f);
    EXPECT_EQ(d.combatState, MobCombatState::RETURNING);
    EXPECT_EQ(d.targetPlayerId, 0);
}

TEST_F(AiFixture, LowHpTriggersFlee)
{
    MobDataStruct mob = makeMob();
    mob.fleeHpThreshold = 0.5f;
    mob.maxHealth = 100;
    mob.currentHealth = 10;
    mob.aggroRange = 2000.0f;
    mob.chaseMultiplier = 1.0f;
    ASSERT_TRUE(instances.registerMobInstance(mob));
    MobMovementData d = chasingData();
    ai.updateMobCombatState(mob, d, 1000.0f);
    EXPECT_EQ(d.combatState, MobCombatState::FLEEING);
    EXPECT_TRUE(d.isFleeing);
}

TEST_F(AiFixture, HandleMobAttackedSetsThreatTarget)
{
    MobDataStruct mob = makeMob();
    ASSERT_TRUE(instances.registerMobInstance(mob));
    ai.handleMobAttacked(9001, 1, 10);
    auto md = movement.getMobMovementData(9001);
    EXPECT_EQ(md.targetPlayerId, 1);
    EXPECT_GT(md.threatTable[1], 0);
}

TEST_F(AiFixture, LateWireSettersAreNullSafe)
{
    // Production order: ChunkServer wires these after construction.
    ai.setEventQueue(nullptr);
    ai.setCombatSystem(nullptr);
    MobDataStruct mob = makeMob();
    MobMovementData d = chasingData();
    ai.updateMobCombatState(mob, d, 1000.0f); // must not crash
    SUCCEED();
}

TEST_F(AiFixture, HandlePlayerAggroAcquiresNearbyTarget)
{
    MobDataStruct mob = makeMob();
    mob.isAggressive = true;
    SpawnZoneStruct zone;
    zone.zoneId = 7;
    zone.minX = -1000.0f;
    zone.maxX = 1000.0f;
    zone.minY = -1000.0f;
    zone.maxY = 1000.0f;
    MobMovementData d; // PATROLLING, no target
    ai.handlePlayerAggro(mob, zone, d);
    EXPECT_EQ(d.targetPlayerId, 1); // char 1 at (50,0), in aggro range
}

namespace
{

SkillStruct formulaSkill(const std::string &slug, int cooldownMs, float maxRange)
{
    SkillStruct s;
    s.skillSlug = slug;
    s.cooldownMs = cooldownMs;
    s.maxRange = maxRange;
    return s;
}

} // namespace

TEST(MobAIFormulas, SelectSkillAbilityFirst)
{
    std::vector<SkillStruct> skills = {
        formulaSkill("bite", 0, 0.0f),      // basic, infinite range
        formulaSkill("fireball", 5000, 10.0f),
    };
    std::unordered_map<std::string, float> lastUsed;
    // distance 500 < 10*100: ability available -> wins over basic
    EXPECT_EQ(MobAIFormulas::selectMobSkillIndex(skills, lastUsed, 100.0f, 500.0f), 1);
    // ability on cooldown (used 1s ago, cd 5s) -> basic
    lastUsed["fireball"] = 99.0f;
    EXPECT_EQ(MobAIFormulas::selectMobSkillIndex(skills, lastUsed, 100.0f, 500.0f), 0);
    // ability cooled down (used 10s ago) -> ability again
    lastUsed["fireball"] = 90.0f;
    EXPECT_EQ(MobAIFormulas::selectMobSkillIndex(skills, lastUsed, 100.0f, 500.0f), 1);
    // ability out of range (distance 1500 > 10*100) -> basic
    lastUsed.clear();
    EXPECT_EQ(MobAIFormulas::selectMobSkillIndex(skills, lastUsed, 100.0f, 1500.0f), 0);
    // empty list -> none
    EXPECT_EQ(MobAIFormulas::selectMobSkillIndex({}, lastUsed, 100.0f, 10.0f), -1);
}

TEST(MobAIFormulas, FleeVector)
{
    PositionStruct mob{100.0f, 100.0f, 0.0f};
    PositionStruct attacker{130.0f, 100.0f, 0.0f}; // east of mob
    // aggroRange 2000, chaseMult 1 -> distance 2000*1*1.1 = 2200 west
    auto t = MobAIFormulas::computeFleeTarget(mob, true, attacker, 2000.0f, 1.0f);
    EXPECT_FLOAT_EQ(t.positionX, 100.0f - 2200.0f);
    EXPECT_FLOAT_EQ(t.positionY, 100.0f);
    // unknown attacker -> north default
    auto d = MobAIFormulas::computeFleeTarget(mob, false, attacker, 2000.0f, 1.0f);
    EXPECT_FLOAT_EQ(d.positionX, 100.0f);
    EXPECT_FLOAT_EQ(d.positionY, 100.0f + 2200.0f);
}

TEST(MobAIFormulas, ThreatDecay)
{
    // ~50%/s: factor(1.0) = 0.95^10 ≈ 0.599
    EXPECT_NEAR(MobAIFormulas::threatDecayFactor(1.0f), 0.5987f, 0.001f);
    EXPECT_FLOAT_EQ(MobAIFormulas::threatDecayFactor(0.0f), 1.0f);
    EXPECT_EQ(MobAIFormulas::applyThreatDecay(100, 0.5f), 50);
    EXPECT_EQ(MobAIFormulas::applyThreatDecay(1, 0.5f), 0); // caller erases
}

TEST(MobAIFormulas, MeleeCapacity)
{
    // 2�?*150/140 �%? 6.7 -> 6 slots
    EXPECT_EQ(MobAIFormulas::maxMeleeSlots(150.0f, 0.0f), 6);
    // radius 70 -> diameter 140 -> same 6
    EXPECT_EQ(MobAIFormulas::maxMeleeSlots(150.0f, 70.0f), 6);
    // degenerate range -> at least 1
    EXPECT_EQ(MobAIFormulas::maxMeleeSlots(0.0f, 0.0f), 1);
    EXPECT_FLOAT_EQ(MobAIFormulas::mobDiameter(0.0f), 140.0f);
    EXPECT_FLOAT_EQ(MobAIFormulas::mobDiameter(70.0f), 140.0f);
}

TEST(MobAIFormulas, CountMeleeOccupants)
{
    // Wave 4.1 pin: extracted from countMobsEngagingTarget. Only alive,
    // non-excluded, same-target mobs in attack state occupy a slot.
    using MobAIFormulas::MeleeOccupant;
    std::vector<MeleeOccupant> mobs = {
        {9001, false, 1, true},
        {9002, false, 1, true},
        {9003, false, 1, false}, // patrolling: no slot
        {9004, false, 2, true},  // other target: no slot
        {9005, true, 1, true},   // dead: no slot
    };
    EXPECT_EQ(MobAIFormulas::countMeleeOccupants(mobs, 1, 0), 2);
    EXPECT_EQ(MobAIFormulas::countMeleeOccupants(mobs, 1, 9001), 1);
    EXPECT_EQ(MobAIFormulas::countMeleeOccupants(mobs, 2, 0), 1);
    EXPECT_EQ(MobAIFormulas::countMeleeOccupants(mobs, 424242, 0), 0);
    EXPECT_EQ(MobAIFormulas::countMeleeOccupants({}, 1, 0), 0);
}
