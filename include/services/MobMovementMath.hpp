#pragma once
// Pure mob-movement math (A5 extract).
//
// MobMovementManager carried these predicates inline among locks, logging
// and manager calls; they depend only on their arguments. All call sites
// delegate here 1-1 (thresholds come from the manager's const aiConfig_).
// Header-only and unit-testable without managers or sockets.
#include "data/DataStructs.hpp"
#include "utils/RandomUtils.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

class MobMovementMath
{
  public:
    /// Separation + containment check for patrol placement.
    static bool isValidPosition(float x,
        float y,
        const SpawnZoneStruct &zone,
        const std::vector<std::pair<int, PositionStruct>> &otherMobs,
        int currentMobUid,
        float currentMobRadius,
        float minSeparationFallback)
    {
        // Shape-aware containment check (RECT / CIRCLE / ANNULUS)
        if (!ZoneBounds::contains(zone, x, y))
            return false;
        return hasSeparation(
            x, y, otherMobs, currentMobUid, currentMobRadius, minSeparationFallback);
    }

    /// Chase placement: no zone boundaries, only collision check.
    static bool isValidPositionForChase(float x,
        float y,
        const std::vector<std::pair<int, PositionStruct>> &otherMobs,
        int currentMobUid,
        float currentMobRadius,
        float minSeparationFallback)
    {
        return hasSeparation(
            x, y, otherMobs, currentMobUid, currentMobRadius, minSeparationFallback);
    }

    /// Movement permission by combat state (attack phases and the evade
    /// window freeze the mob; flee still moves via the fleeing path).
    static bool canPerformAction(MobCombatState combatState)
    {
        switch (combatState)
        {
        case MobCombatState::PATROLLING:
        case MobCombatState::CHASING:
        case MobCombatState::RETURNING:
        case MobCombatState::FLEEING: // Flee movement is handled via isFleeing flag path
            return true;

        case MobCombatState::PREPARING_ATTACK:
        case MobCombatState::ATTACKING:
        case MobCombatState::ATTACK_COOLDOWN:
        case MobCombatState::EVADING:
            return false; // No movement during attack phases or evade window
        }
        return false;
    }

    /// Leash predicates: distance outside the zone edge vs config thresholds.
    static bool shouldReturnToSpawn(
        const PositionStruct &mobPos, const SpawnZoneStruct &zone, float returnToSpawnZoneDistance)
    {
        return ZoneBounds::distanceToZone(mobPos, zone) > returnToSpawnZoneDistance;
    }

    static bool shouldStopChasing(
        const PositionStruct &mobPos, const SpawnZoneStruct &zone, float maxChaseFromZoneEdge)
    {
        return ZoneBounds::distanceToZone(mobPos, zone) > maxChaseFromZoneEdge;
    }

    static bool canSearchNewTargets(
        const PositionStruct &mobPos, const SpawnZoneStruct &zone, float newTargetZoneDistance)
    {
        return ZoneBounds::distanceToZone(mobPos, zone) <= newTargetZoneDistance;
    }

    /// Next patrol-move timestamp: speedTime roll scaled by patrol speed
    /// (floor 2s) plus an occasional short cooldown pause (~15% of rolls).
    static float calculateNextMoveTime(float currentTime,
        float patrolSpeed,
        float speedMultiplier,
        const MobMovementParams &params)
    {
        std::uniform_real_distribution<float> speedTime(params.speedTimeMin, params.speedTimeMax);
        const float patrolSpeedFactor =
            (patrolSpeed > 0.01f) ? patrolSpeed : speedMultiplier;
        float nextTime =
            currentTime + std::max(speedTime(RandomUtils::engine()) / patrolSpeedFactor, 2.0f);

        // Optional random cooldown pause to add unpredictability
        std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
        if (randFactor(RandomUtils::engine()) > 1.15f)
        {
            std::uniform_real_distribution<float> cooldown(params.cooldownMin, params.cooldownMax);
            nextTime += cooldown(RandomUtils::engine()) * 0.5f;
        }
        return nextTime;
    }

    /// Random return destination within maxReturnWalkDistance of the mob and
    /// inside the zone; nullopt when 20 attempts fail (caller falls back to
    /// the original spawn position).
    static std::optional<PositionStruct> pickReturnDestination(
        const PositionStruct &mobPos, const SpawnZoneStruct &zone)
    {
        constexpr float maxReturnWalkDistance = 400.0f;
        constexpr int maxAttempts = 20;

        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265358979323846f);
        std::uniform_real_distribution<float> distDist(50.0f, maxReturnWalkDistance);

        for (int i = 0; i < maxAttempts; ++i)
        {
            const float angle = angleDist(RandomUtils::engine());
            const float dist = distDist(RandomUtils::engine());
            PositionStruct candidate;
            candidate.positionX = mobPos.positionX + std::cos(angle) * dist;
            candidate.positionY = mobPos.positionY + std::sin(angle) * dist;

            if (ZoneBounds::contains(zone, candidate))
                return candidate;
        }
        return std::nullopt;
    }

    MobMovementMath() = delete;

  private:
    /// Radius-based separation: own collision diameter wins, else the zone
    /// fallback; self is always skipped.
    static bool hasSeparation(float x,
        float y,
        const std::vector<std::pair<int, PositionStruct>> &otherMobs,
        int currentMobUid,
        float currentMobRadius,
        float minSeparationFallback)
    {
        const float mobRadius = (currentMobRadius > 0) ? currentMobRadius : 0.0f;
        const float minSep = (mobRadius > 0.0f) ? (mobRadius * 2.0f) : minSeparationFallback;

        for (const auto &otherMob : otherMobs)
        {
            if (otherMob.first == currentMobUid)
                continue;

            const float dx = x - otherMob.second.positionX;
            const float dy = y - otherMob.second.positionY;
            if (dx * dx + dy * dy < minSep * minSep)
                return false;
        }
        return true;
    }
};
