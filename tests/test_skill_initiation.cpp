// Unit tests for SkillInitiationValidator (pure predicate, no world).
#include "services/SkillInitiationValidator.hpp"

#include <gtest/gtest.h>

namespace
{

InitiationCheckInput baseInput()
{
    InitiationCheckInput in;
    in.casterMana = 100;
    in.skillCostMp = 10;
    in.targetType = CombatTargetType::MOB;
    in.targetKnown = true;
    in.targetAlive = true;
    in.skillEffect = "damage";
    return in;
}

} // namespace

TEST(SkillInitiationValidator, AcceptClean)
{
    EXPECT_EQ(validateSkillInitiation(baseInput()), InitiationReject::None);
}

TEST(SkillInitiationValidator, PrecedenceCastingFirst)
{
    auto in = baseInput();
    in.alreadyCasting = true;
    in.casterMana = 0; // mana also bad...
    in.checkRange = true;
    in.distance = 1e9f; // ...range also bad...
    in.targetAlive = false; // ...target dead...
    in.targetType = CombatTargetType::PLAYER; // ...and PvP
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::AlreadyCasting);
}

TEST(SkillInitiationValidator, ManaBoundary)
{
    auto in = baseInput();
    in.casterMana = 9;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::InsufficientMana);
    in.casterMana = 10; // exact cost passes
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
}

TEST(SkillInitiationValidator, RangeSkipsAndBoundary)
{
    auto in = baseInput();
    // AREA/NONE never check range
    in.targetType = CombatTargetType::AREA;
    in.checkRange = false;
    in.distance = 1e9f;
    in.skillMaxRange = 1.0f;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
    // unresolvable positions skip the check (1-1)
    in.targetType = CombatTargetType::MOB;
    in.checkRange = false;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
    // boundary: distance == maxRange*100 passes
    in.checkRange = true;
    in.skillMaxRange = 10.0f;
    in.distance = 1000.0f;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
    in.distance = 1000.01f;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::OutOfRange);
}

TEST(SkillInitiationValidator, TargetLiveness)
{
    auto in = baseInput();
    in.targetKnown = false;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::TargetDead);
    in.targetKnown = true;
    in.targetAlive = false;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::TargetDead);

    // PLAYER other: same rules
    in.targetType = CombatTargetType::PLAYER;
    in.isSelfTarget = false;
    in.skillEffect = "heal";
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::TargetDead);

    // SELF skips PLAYER-alive (and PvP) even when flags say dead
    in.isSelfTarget = true;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);

    // MOB is always liveness-checked even when framed as self (1-1)
    in.targetType = CombatTargetType::MOB;
    in.isSelfTarget = true;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::TargetDead);
}

TEST(SkillInitiationValidator, PvpGuard)
{
    auto in = baseInput();
    in.targetType = CombatTargetType::PLAYER;
    in.isSelfTarget = false;
    in.skillEffect = "damage";
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::PvpBlocked);
    in.skillEffect = "debuff";
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::PvpBlocked);
    in.skillEffect = "heal";
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
    in.skillEffect = "buff";
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
    // self damage is fine
    in.skillEffect = "damage";
    in.isSelfTarget = true;
    EXPECT_EQ(validateSkillInitiation(in), InitiationReject::None);
}
