#pragma once

#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Pure mob-AI math extracted from MobAIController (Increment 8).
 *
 * Header-only free functions with no manager dependencies — unit-testable in
 * isolation. MobAIController fetches live data (templates, positions, threat
 * tables) and delegates the arithmetic here. Formulas are 1-1 with the
 * pre-extraction inline code.
 */
namespace MobAIFormulas
{

/// Flee destination scale: FLEE_DISTANCE = aggroRange * chaseMultiplier * 1.1
inline constexpr float kFleeDistanceFactor = 1.1f;

/// Threat decay base: factor = 0.95^(deltaSec*10) (~50%/s for out-of-range players)
inline constexpr float kThreatDecayBase = 0.95f;

/// Fallback mob diameter when template radius is 0
inline constexpr float kDefaultMobDiameter = 140.0f;

/// DB range units (meters) vs position units are 100x larger
inline constexpr float kRangeUnitScale = 100.0f;

/**
 * @brief Melee-slot occupancy count (Wave 4.1).
 *
 * Extracted from MobAIController::countMobsEngagingTarget: a mob occupies a
 * melee slot on a target when it is alive, not excluded, targets this player
 * and is in an attack state (PREPARING_ATTACK / ATTACKING / ATTACK_COOLDOWN —
 * mapped to inAttackState by the caller, which owns the enum).
 */
struct MeleeOccupant
{
    int uid = 0;
    bool isDead = false;
    int targetPlayerId = 0;
    bool inAttackState = false;
};

inline int countMeleeOccupants(const std::vector<MeleeOccupant> &mobs,
    int targetPlayerId,
    int excludeUID)
{
    int count = 0;
    for (const auto &m : mobs)
    {
        if (m.uid == excludeUID || m.isDead)
            continue;
        if (m.targetPlayerId != targetPlayerId)
            continue;
        if (m.inAttackState)
            ++count;
    }
    return count;
}

/**
 * @brief Pick a skill index from a template skill list.
 *
 * Mirrors MobAIController::selectAttackSkill scoring: skip out-of-range
 * (maxRange > 0 && distance > maxRange*100), skip abilities still on cooldown
 * (nowSec - lastUsed < cooldownMs/1000), first available ability
 * (cooldownMs > 0) wins, otherwise first basic attack.
 *
 * @param skills    Template skills (ownership stays with caller)
 * @param lastUsed  skillSlug -> last-use game time (seconds)
 * @param nowSec    Current game time (seconds)
 * @param distance  Distance to target (position units)
 * @return Index into skills, or -1 when nothing is usable.
 */
inline int selectMobSkillIndex(const std::vector<SkillStruct> &skills,
    const std::unordered_map<std::string, float> &lastUsed,
    float nowSec,
    float distance)
{
    int bestAbility = -1;
    int bestBasic = -1;

    for (int i = 0; i < static_cast<int>(skills.size()); ++i)
    {
        const auto &skill = skills[i];
        if (skill.maxRange > 0.0f && distance > skill.maxRange * kRangeUnitScale)
            continue;

        if (skill.cooldownMs > 0)
        {
            auto it = lastUsed.find(skill.skillSlug);
            if (it != lastUsed.end())
            {
                float cdSec = static_cast<float>(skill.cooldownMs) / 1000.0f;
                if (nowSec - it->second < cdSec)
                    continue; // Still on cooldown.
            }
            if (bestAbility < 0)
                bestAbility = i; // First available ability wins.
        }
        else
        {
            if (bestBasic < 0)
                bestBasic = i;
        }
    }

    if (bestAbility >= 0)
        return bestAbility;
    return bestBasic;
}

/**
 * @brief Flee destination: away from the attacker (or north by default).
 *
 * fleeTarget = mobPos + fleeVec * (aggroRange * chaseMultiplier * 1.1),
 * fleeVec = normalized (mob - attacker), default (0,1) when the attacker is
 * unknown (id <= 0, not found, or standing exactly on the mob).
 */
inline PositionStruct computeFleeTarget(const PositionStruct &mobPos,
    bool hasAttacker,
    const PositionStruct &attackerPos,
    float aggroRange,
    float chaseMultiplier)
{
    float fleeVecX = 0.0f;
    float fleeVecY = 1.0f; // default: north
    if (hasAttacker)
    {
        float dx = mobPos.positionX - attackerPos.positionX;
        float dy = mobPos.positionY - attackerPos.positionY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > 0.0f)
        {
            fleeVecX = dx / dist;
            fleeVecY = dy / dist;
        }
    }

    const float fleeDistance = aggroRange * chaseMultiplier * kFleeDistanceFactor;
    PositionStruct target = mobPos;
    target.positionX = mobPos.positionX + fleeVecX * fleeDistance;
    target.positionY = mobPos.positionY + fleeVecY * fleeDistance;
    return target;
}

/**
 * @brief Exponential threat decay factor for out-of-range players.
 */
inline float threatDecayFactor(float deltaSec)
{
    return std::pow(kThreatDecayBase, deltaSec * 10.0f);
}

/**
 * @brief Apply one decay step to a threat value (caller erases when <= 0).
 */
inline int applyThreatDecay(int threatValue, float decayFactor)
{
    return static_cast<int>(threatValue * decayFactor);
}

/**
 * @brief Mob diameter for melee-ring capacity (fallback 140 when radius is 0).
 */
inline float mobDiameter(float mobRadius)
{
    return (mobRadius > 0) ? mobRadius * 2.0f : kDefaultMobDiameter;
}

/**
 * @brief Melee-ring capacity: max(1, int(2π*attackRange / diameter)).
 */
inline int maxMeleeSlots(float attackRange, float mobRadius)
{
    return std::max(
        1, static_cast<int>(2.0f * static_cast<float>(M_PI) * attackRange / mobDiameter(mobRadius)));
}

} // namespace MobAIFormulas
