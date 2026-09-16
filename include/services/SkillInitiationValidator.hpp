#pragma once

#include "data/CombatStructs.hpp"
#include <string>

/**
 * @brief Pure skill-initiation precondition checker (Increment 10.2).
 *
 * Decides initiateSkillUsage accept/reject 1-1 with the pre-extraction guard
 * chain (already-casting → mana → range → alive → PvP). The orchestrator
 * resolves live data (ongoing-action state, mana, positions, target liveness)
 * into InitiationCheckInput; cooldown claiming + persistence stay in
 * CombatSystem (they mutate shared state). Header-only, unit-testable.
 */
enum class InitiationReject
{
    None,             ///< all preconditions hold — proceed to cooldown claim
    AlreadyCasting,   ///< "Already casting"
    InsufficientMana, ///< "Not enough mana"
    OutOfRange,       ///< "Target is out of range"
    TargetDead,       ///< "Target is dead"
    PvpBlocked,       ///< "PvP is not available"
};

struct InitiationCheckInput
{
    bool alreadyCasting = false;
    int casterMana = 0;
    int skillCostMp = 0;
    CombatTargetType targetType = CombatTargetType::NONE;
    /// targetId == casterId: skips PLAYER-alive and PvP branches, 1-1
    /// (MOB targets are always liveness-checked)
    bool isSelfTarget = false;
    /// false for AREA/NONE or unresolvable positions → range check skipped, 1-1
    bool checkRange = false;
    float distance = 0.0f;
    float skillMaxRange = 0.0f; // DB units (meters); positions are 100x larger
    /// target registry hit (mob uid != 0 / character id != 0)
    bool targetKnown = true;
    /// mob !isDead / player !isDead
    bool targetAlive = true;
    /// skill.skillEffectType ("damage"/"debuff" arm the PvP guard)
    std::string skillEffect;
};

/// DB range units (meters) vs position units are 100x larger.
inline constexpr float kInitiationRangeUnitScale = 100.0f;

inline InitiationReject validateSkillInitiation(const InitiationCheckInput &in)
{
    if (in.alreadyCasting)
        return InitiationReject::AlreadyCasting;

    if (in.casterMana < in.skillCostMp)
        return InitiationReject::InsufficientMana;

    if (in.checkRange && in.distance > in.skillMaxRange * kInitiationRangeUnitScale)
        return InitiationReject::OutOfRange;

    // MOB targets are always liveness-checked (no self-target concept for mobs, 1-1);
    // PLAYER targets only when targeting somebody else.
    if (in.targetType == CombatTargetType::MOB)
    {
        if (!in.targetKnown || !in.targetAlive)
            return InitiationReject::TargetDead;
    }
    else if (!in.isSelfTarget && in.targetType == CombatTargetType::PLAYER)
    {
        if (!in.targetKnown || !in.targetAlive)
            return InitiationReject::TargetDead;
    }

    if (!in.isSelfTarget && in.targetType == CombatTargetType::PLAYER)
    {
        if (in.skillEffect == "damage" || in.skillEffect == "debuff")
            return InitiationReject::PvpBlocked;
    }

    return InitiationReject::None;
}
