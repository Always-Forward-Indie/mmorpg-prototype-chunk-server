#include "services/MobMovementManager.hpp"
#include "data/CombatStructs.hpp"
#include "events/Event.hpp"
#include "events/EventData.hpp"
#include "events/EventQueue.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatSystem.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementMath.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/DistanceUtils.hpp"
#include "utils/MovementUnits.hpp"
#include "utils/RandomUtils.hpp"
#include "utils/SpawnGeometry.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <spdlog/logger.h>
#include <unordered_set>

MobMovementManager::MobMovementManager(CharacterManager &characters,
    MobInstanceManager &mobInstances,
    MobManager &mobs,
    Logger &logger)
    : logger_(logger),
      mobInstanceManager_(&mobInstances),
      spawnZoneManager_(nullptr),
      characterManager_(&characters),
      eventQueue_(nullptr),
      combatSystem_(nullptr),
      mobAIController_(characters, mobInstances, mobs, *this, nullptr, nullptr, logger)
{
    log_ = logger.getSystem("mob");
    log_->info("[INFO] MobMovementManager initialized with default AI configuration");
}

void
MobMovementManager::setSpawnZoneManager(SpawnZoneManager *spawnZoneManager)
{
    spawnZoneManager_ = spawnZoneManager;
}

void
MobMovementManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
    mobAIController_.setEventQueue(eventQueue);
}

void
MobMovementManager::setCombatSystem(CombatSystem *combatSystem)
{
    combatSystem_ = combatSystem;
    mobAIController_.setCombatSystem(combatSystem);
}

void
MobMovementManager::setGameServices(GameServices *gs)
{
    gameServices_ = gs;
}

bool
MobMovementManager::moveMobsInZone(int zoneId)
{
    if (!mobInstanceManager_ || !spawnZoneManager_)
    {
        log_->error("MobMovementManager: Dependencies not set");
        return false;
    }

    // Get zone data
    auto zone = spawnZoneManager_->getMobSpawnZoneByID(zoneId);
    if (zone.zoneId == 0)
    {
        log_->error("MobMovementManager: Zone " + std::to_string(zoneId) + " not found");
        return false;
    }

    // Get mobs in zone (full data for AI/combat logic)
    auto mobsInZone = mobInstanceManager_->getMobInstancesInZone(zoneId);
    if (mobsInZone.empty())
    {
        return false; // No mobs to move
    }

    // Movement parameters: always the defaults (per-zone overrides were dead
    // code — setZoneMovementParams had zero callers — so the map was removed).
    auto params = getDefaultMovementParams();

    // Zone event mob speed multiplier (Stage 4)
    if (gameServices_)
    {
        float speedMult = gameServices_->getZoneEventManager().getMobSpeedMultiplier(zoneId);
        if (speedMult != 1.0f)
        {
            params.baseSpeedMin *= speedMult;
            params.baseSpeedMax *= speedMult;
            params.maxStepSizeAbsolute *= speedMult;
        }
    }

    // Lightweight positions for collision detection (avoids deep-copying attribute/skill vectors)
    auto mobPositions = mobInstanceManager_->getMobPositionsInZone(zoneId);

    float currentTime = getCurrentGameTime();
    bool anyMobMoved = false;

    for (auto &mob : mobsInZone)
    {
        // Skip dead mobs
        if (mob.isDead || mob.currentHealth <= 0)
            continue;

        if (runMobTick(mob, zone, params, mobPositions, currentTime))
            anyMobMoved = true;
    }

    return anyMobMoved;
}

bool
MobMovementManager::moveSingleMob(int mobUID, int zoneId)
{
    if (!mobInstanceManager_ || !spawnZoneManager_)
    {
        return false;
    }

    auto mob = mobInstanceManager_->getMobInstance(mobUID);
    if (mob.uid == 0) // Invalid mob (empty struct)
    {
        return false;
    }

    // Get zone data
    auto zone = spawnZoneManager_->getMobSpawnZoneByID(zoneId);
    if (zone.zoneId == 0)
    {
        return false;
    }

    // Get lightweight {uid,position} pairs for collision detection (4.1 optimization)
    auto mobPositions = mobInstanceManager_->getMobPositionsInZone(zoneId);

    // Movement parameters: always the defaults (see moveMobsInZone).
    auto params = getDefaultMovementParams();

    // Delegate to the shared per-mob AI+movement tick.
    return runMobTick(mob, zone, params, mobPositions, getCurrentGameTime());
}

bool
MobMovementManager::runMobTick(
    MobDataStruct &mob,
    const SpawnZoneStruct &zone,
    const MobMovementParams &params,
    const std::vector<std::pair<int, PositionStruct>> &mobPositions,
    float currentTime)
{
    // Get movement data for this mob
    auto movementData = getMobMovementDataInternal(mob.uid);

    // Initialize spawn position if not set
    if (movementData.spawnPosition.positionX == 0.0f && movementData.spawnPosition.positionY == 0.0f)
    {
        movementData.spawnPosition = mob.position;
        updateMobMovementData(mob.uid, movementData);
        logger_.log("[INFO] Initialized spawn position for mob UID: " + std::to_string(mob.uid) +
                    " at (" + std::to_string(mob.position.positionX) + ", " +
                    std::to_string(mob.position.positionY) + ")");
    }

    // Handle player aggro and AI behavior
    if ((mob.isAggressive || movementData.targetPlayerId > 0) && characterManager_)
    {
        mobAIController_.handlePlayerAggro(mob, zone, movementData);
        movementData = getMobMovementDataInternal(mob.uid);
    }
    else if (!mob.isAggressive && !movementData.isReturningToSpawn && movementData.targetPlayerId == 0)
    {
        // For non-aggressive patrol mobs, fix up uninitialised move timing so they don't get stuck.
        if (movementData.nextMoveTime == 0.0f)
        {
            std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
            movementData.nextMoveTime = currentTime + moveTime(RandomUtils::engine());
            updateMobMovementData(mob.uid, movementData);
            log_->debug("Fixed non-aggressive mob UID: " + std::to_string(mob.uid) + " movement timing");
        }
    }

    // Update combat state machine
    mobAIController_.updateMobCombatState(mob, movementData, currentTime);

    // Refresh after state machine may have written new state
    movementData = getMobMovementDataInternal(mob.uid);

    // Initialize movement timing if needed
    if (movementData.nextMoveTime == 0.0f)
    {
        std::uniform_real_distribution<float> initialDelay(0.0f, params.initialDelayMax);
        std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
        movementData.nextMoveTime = currentTime + initialDelay(RandomUtils::engine()) + moveTime(RandomUtils::engine());
        updateMobMovementData(mob.uid, movementData);
    }

    // Mob in a non-moving state (attacking, evading, …) — nothing to do this tick.
    if (!canPerformAction(movementData, currentTime))
        return false;

    // Movement-interval gate
    const bool hasTarget = (movementData.targetPlayerId > 0 || movementData.isReturningToSpawn ||
                            movementData.isFleeing || movementData.isBackpedaling);
    bool timeToMove = (currentTime >= movementData.nextMoveTime);
    if (hasTarget)
    {
        const float minInterval = movementData.isReturningToSpawn
                                      ? aiConfig_.returnMovementInterval
                                      : aiConfig_.chaseMovementInterval;
        timeToMove = (movementData.nextMoveTime == 0.0f ||
                      (currentTime - movementData.lastMoveTime) >= minInterval);
    }
    if (!timeToMove)
        return false;

    // Select movement calculation based on current behaviour
    std::optional<MobMovementResult> movementResult;

    if (movementData.isFleeing || movementData.isBackpedaling)
    {
        // Fleeing prey runs at 0.5x return speed: visibly flees, but a
        // pursuing player (full speed) closes the gap and melee range checks
        // use live positions, so the prey stays killable. At 0.8x+ the prey
        // outran both pursuers and position broadcasts (unkillable kiting,
        // broke fox-kill quests). Backpedal keeps full speed (short hop).
        const float fleeSpeedMult = movementData.isFleeing ? 0.5f : 1.0f;
        movementResult = calculateReturnToSpawnMovement(
            mob, zone, mobPositions, movementData.fleeTargetPosition, params, fleeSpeedMult);
        // AAA: explicit hold-at-target handoff. calculateReturnToSpawnMovement now
        // returns speed=0 holds instead of nullopt (continuous velocity stream),
        // so arrival is detected by hold + proximity, not by missing result.
        const bool bFleeHoldAtTarget =
            (movementResult.has_value() && movementResult->validMovement &&
             movementResult->speed <= 0.001f);
        if (!movementResult.has_value() || bFleeHoldAtTarget)
        {
            movementData.isBackpedaling = false;
            if (movementData.isFleeing)
            {
                // Flee destination reached — leash back immediately instead of letting
                // the FLEEING state timer run out and causing a U-turn toward the player.
                movementData.isFleeing = false;
                int lostTargetId = movementData.targetPlayerId;
                movementData.targetPlayerId = 0;
                movementData.combatState = MobCombatState::RETURNING;
                movementData.stateChangeTime = currentTime;
                movementData.isReturningToSpawn = true;
                movementData.threatTable.clear();
                movementData.attackerTimestamps.clear();
                updateMobMovementData(mob.uid, movementData);
                forceMobStateUpdate(mob.uid);
                if (lostTargetId > 0)
                    sendMobTargetLost(mob, lostTargetId);
            }
            else
            {
                updateMobMovementData(mob.uid, movementData);
            }
        }
    }
    else if (movementData.isReturningToSpawn)
    {
        // Pick a return destination inside the zone near the mob's current
        // position. The mob walks to this point (not the exact spawn pixel,
        // which may be on the opposite side of a large zone) and then resumes
        // PATROLLING. This avoids long hikes across the zone after a chase ends.
        // pickReturnDestination returns origin as "no destination" sentinel
        // (sampling missed the zone) — then returnTarget below falls back
        // to the exact spawn position.
        if (!movementData.hasReturnDestination)
        {
            PositionStruct dest = pickReturnDestination(mob.position, zone);
            if (dest.positionX != 0.0f || dest.positionY != 0.0f)
            {
                movementData.returnDestination = dest;
                movementData.hasReturnDestination = true;
                updateMobMovementData(mob.uid, movementData);
            }
        }

        const PositionStruct &returnTarget = movementData.hasReturnDestination
                                                 ? movementData.returnDestination
                                                 : movementData.spawnPosition;
        movementResult = calculateReturnToSpawnMovement(mob, zone, mobPositions, returnTarget, params);
    }
    else if (movementData.targetPlayerId > 0 && characterManager_)
    {
        movementResult = calculateChaseMovement(mob, zone, mobPositions, movementData.targetPlayerId, params);
    }
    else
    {
        movementResult = calculateNewPosition(mob, zone, mobPositions, params);
    }

    if (movementResult && movementResult->validMovement)
    {
        // AAA telemetry-honesty tripwire: the broadcast speed must equal the
        // actual step velocity — the client dead-reckons on it, and any
        // overstatement renders as overshoot past the player + snap-back.
        // Throttled dev alarm (5 s): it must never fire; if it does, a
        // movement path mis-reports and needs an immediate look.
        {
            const float actualDist = DistanceUtils::dist2D(movementResult->newPosition, mob.position);
            const bool bReturnPath = (movementData.isReturningToSpawn ||
                                      movementData.isFleeing || movementData.isBackpedaling);
            const float stepInterval = movementData.targetPlayerId > 0 && !bReturnPath
                                           ? aiConfig_.chaseMovementInterval
                                           : (bReturnPath ? aiConfig_.returnMovementInterval
                                                          : 1.0f); // patrol transition seconds
            const float expectedDist = movementResult->speed * stepInterval;
            const float tolerance = std::max(2.0f, expectedDist * 0.25f);
            if (std::fabs(expectedDist - actualDist) > tolerance)
            {
                static std::atomic<int64_t> s_lastTelemetryWarnMs{0};
                const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (nowMs - s_lastTelemetryWarnMs.exchange(nowMs) > 5000)
                {
                    log_->warn("Telemetry drift mob UID " + std::to_string(mob.uid) +
                               ": reported speed " + std::to_string(movementResult->speed) +
                               " vs actual step " + std::to_string(actualDist) +
                               " (interval " + std::to_string(stepInterval) + ")");
                }
            }
        }

        movementData.movementDirectionX = movementResult->newDirectionX;
        movementData.movementDirectionY = movementResult->newDirectionY;
        movementData.lastMoveTime = currentTime;

        if (movementData.targetPlayerId > 0)
            movementData.nextMoveTime = currentTime + aiConfig_.chaseMovementInterval;
        else if (movementData.isReturningToSpawn)
            movementData.nextMoveTime = currentTime + aiConfig_.returnMovementInterval;
        else
            movementData.nextMoveTime = calculateNextMoveTime(currentTime, mob, movementData, params);

        // Targeted write — avoids RMW race with handleMobAttacked on another thread.
        updateMobMovementPositionFields(
            mob.uid,
            movementData.movementDirectionX,
            movementData.movementDirectionY,
            movementData.lastMoveTime,
            movementData.nextMoveTime,
            movementResult->speed,
            movementResult->deflectionSign);

        mobInstanceManager_->updateMobPosition(mob.uid, movementResult->newPosition);

        // Reset stuck counter on successful movement
        auto successData = getMobMovementDataInternal(mob.uid);
        successData.stuckCount = 0;
        updateMobMovementData(mob.uid, successData);

        return true;
    }

    // Movement blocked (in attack range, collision, …).
    // Re-run state machine so CHASING → PREPARING_ATTACK fires even when the mob
    // stopped because the player moved between the pre-movement state-machine read
    // and the calculateChaseMovement distance check (concurrent network thread race).
    {
        auto freshData = getMobMovementDataInternal(mob.uid);
        if (freshData.combatState == MobCombatState::CHASING && freshData.targetPlayerId > 0)
            mobAIController_.updateMobCombatState(mob, freshData, currentTime);
    }

    // STUCK-GUARD: patrolling mob failed to find a valid position (corner / cluster).
    // Back off nextMoveTime so the scheduler does not retry the same blocked spot
    // every 50 ms.  Also re-randomize stepMultiplier (set to 0 triggers re-roll at
    // line 450) so mobs with permanently-small step sizes get a fresh chance.
    // Track consecutive failures and teleport to a random position after N attempts
    // to break separation deadlocks in small crowded zones.
    if (!hasTarget)
    {
        auto stuckData = getMobMovementDataInternal(mob.uid);
        if (stuckData.combatState == MobCombatState::PATROLLING)
        {
            std::uniform_real_distribution<float> waitTime(params.moveTimeMin, params.moveTimeMax);
            stuckData.nextMoveTime = currentTime + waitTime(RandomUtils::engine());
            stuckData.movementDirectionX = 0.0f;
            stuckData.movementDirectionY = 0.0f;
            stuckData.hasPatrolTarget = false; // force a new waypoint on next attempt
            stuckData.stepMultiplier = 0.0f;   // re-roll on next calculateNewPosition call
            stuckData.stuckCount++;

            if (stuckData.stuckCount >= 10)
            {
                // Force-teleport to random position inside the zone to break deadlock
                bool teleported = false;
                for (int attempt = 0; attempt < 20; ++attempt)
                {
                    PositionStruct randPos;
                    randPos.positionZ = mob.position.positionZ;

                    if (zone.shape == ZoneShape::CIRCLE || zone.shape == ZoneShape::ANNULUS)
                    {
                        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
                        float angle = angleDist(RandomUtils::engine());
                        float rMin = (zone.shape == ZoneShape::ANNULUS) ? zone.innerRadius : 0.0f;
                        std::uniform_real_distribution<float> radiusDist(rMin, zone.outerRadius);
                        float radius = radiusDist(RandomUtils::engine());
                        randPos.positionX = zone.centerX + radius * std::cos(angle);
                        randPos.positionY = zone.centerY + radius * std::sin(angle);
                    }
                    else // RECT
                    {
                        std::uniform_real_distribution<float> dxDist(zone.minX, zone.maxX);
                        std::uniform_real_distribution<float> dyDist(zone.minY, zone.maxY);
                        randPos.positionX = dxDist(RandomUtils::engine());
                        randPos.positionY = dyDist(RandomUtils::engine());
                    }

                    if (ZoneBounds::contains(zone, randPos))
                    {
                        mobInstanceManager_->updateMobPosition(mob.uid, randPos);
                        // Self-heal, not an alert: info (was shouty tag at info).
                        logger_.info("Stuck-guard teleported mob UID " + std::to_string(mob.uid) +
                                    " after " + std::to_string(stuckData.stuckCount) +
                                    " consecutive stuck cycles in zone " + std::to_string(zone.zoneId));
                        teleported = true;
                        break;
                    }
                }
                if (!teleported)
                {
                    // No valid teleport found: actionable for world design, keep visible.
                    logger_.warn("Stuck-guard failed to find teleport position for mob UID " +
                                std::to_string(mob.uid) + " after " +
                                std::to_string(stuckData.stuckCount) + " stuck cycles");
                }
                stuckData.stuckCount = 0;
            }

            updateMobMovementData(mob.uid, stuckData);
        }
    }

    return false;
}

std::optional<MobMovementResult>
MobMovementManager::calculateNewPosition(
    const MobDataStruct &mob,
    const SpawnZoneStruct &zone,
    const std::vector<std::pair<int, PositionStruct>> &otherMobs,
    const MobMovementParams &params)
{
    // Get movement data for this mob
    auto movementData = getMobMovementDataInternal(mob.uid);

    // Derive AABB-like extents for border-detection and step-size calculations.
    // For non-RECT shapes we use the enclosing box of the circle/annulus.
    float minX, maxX, minY, maxY;
    if (zone.shape == ZoneShape::RECT)
    {
        minX = zone.minX;
        maxX = zone.maxX;
        minY = zone.minY;
        maxY = zone.maxY;
    }
    else
    {
        minX = zone.centerX - zone.outerRadius;
        maxX = zone.centerX + zone.outerRadius;
        minY = zone.centerY - zone.outerRadius;
        maxY = zone.centerY + zone.outerRadius;
    }

    // Check if mob is at border (shape-aware)
    // For ANNULUS use ring width so threshold is proportional to usable space,
    // not the enclosing AABB (which would make thin rings perpetually "at border").
    float borderThreshold;
    if (zone.shape == ZoneShape::ANNULUS)
        borderThreshold = (zone.outerRadius - zone.innerRadius) * params.borderThresholdPercent;
    else if (zone.shape == ZoneShape::CIRCLE)
        borderThreshold = zone.outerRadius * params.borderThresholdPercent;
    else
        borderThreshold = std::max(maxX - minX, maxY - minY) * params.borderThresholdPercent;
    bool atBorder;
    if (zone.shape == ZoneShape::RECT)
    {
        atBorder = (mob.position.positionX <= minX + borderThreshold ||
                    mob.position.positionX >= maxX - borderThreshold ||
                    mob.position.positionY <= minY + borderThreshold ||
                    mob.position.positionY >= maxY - borderThreshold);
    }
    else if (zone.shape == ZoneShape::CIRCLE)
    {
        const float dx = mob.position.positionX - zone.centerX;
        const float dy = mob.position.positionY - zone.centerY;
        const float border = zone.outerRadius - borderThreshold;
        atBorder = (dx * dx + dy * dy >= border * border);
    }
    else // ANNULUS
    {
        const float dx = mob.position.positionX - zone.centerX;
        const float dy = mob.position.positionY - zone.centerY;
        const float d2 = dx * dx + dy * dy;
        const float outer = zone.outerRadius - borderThreshold;
        const float inner = zone.innerRadius + borderThreshold;
        atBorder = (d2 >= outer * outer || d2 <= inner * inner);
    }

    // Initialize step multiplier if needed
    if (movementData.stepMultiplier == 0.0f)
    {
        std::uniform_real_distribution<float> stepMultiplier(params.stepMultiplierMin, params.stepMultiplierMax);
        movementData.stepMultiplier = stepMultiplier(RandomUtils::engine());
        // Сохраняем обновленный stepMultiplier
        updateMobMovementData(mob.uid, movementData);

        logger_.debug("Mob UID: " + std::to_string(mob.uid) + " initialized stepMultiplier: " +
                    std::to_string(movementData.stepMultiplier) + " for normal patrol");
    }

    // Calculate step size
    std::uniform_real_distribution<float> baseSpeed(params.baseSpeedMin, params.baseSpeedMax);
    std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
    float maxStepSize = std::min(((maxX - minX) + (maxY - minY)) * params.maxStepSizePercent, params.maxStepSizeAbsolute);
    float stepSize = std::clamp(baseSpeed(RandomUtils::engine()) * movementData.stepMultiplier * randFactor(RandomUtils::engine()),
        params.minMoveDistance * 0.75f,
        maxStepSize);

    if (stepSize < params.minMoveDistance)
    {
        return std::nullopt; // No movement
    }

    // Try to find valid direction
    float newDirectionX = movementData.movementDirectionX;
    float newDirectionY = movementData.movementDirectionY;
    bool foundValidDirection = false;

    for (int i = 0; i < params.maxRetries; ++i)
    {
        float newAngle;

        if (atBorder)
        {
            // Move towards zone interior when at border (shape-aware).
            // ANNULUS: escape direction depends on which boundary we hit:
            //   - outer border → aim inward (toward midRadius along radial)
            //   - inner border → aim outward (away from center toward midRadius)
            // CIRCLE/RECT: aim at geometric center as before.
            float angleToEscape;
            if (zone.shape == ZoneShape::ANNULUS)
            {
                float dx = mob.position.positionX - zone.centerX;
                float dy = mob.position.positionY - zone.centerY;
                float d = std::sqrt(dx * dx + dy * dy);
                float midR = (zone.innerRadius + zone.outerRadius) * 0.5f;
                float radialAngle = std::atan2(dy, dx);
                // at inner border d < midR → aim outward; at outer border → aim inward
                if (d < midR)
                    angleToEscape = radialAngle; // outward
                else
                    angleToEscape = radialAngle + static_cast<float>(M_PI); // inward
            }
            else
            {
                float centerX = (minX + maxX) * 0.5f;
                float centerY = (minY + maxY) * 0.5f;
                angleToEscape = std::atan2(centerY - mob.position.positionY,
                    centerX - mob.position.positionX);
            }
            std::uniform_real_distribution<float> borderAngle(params.borderAngleMin, params.borderAngleMax);
            newAngle = angleToEscape + (borderAngle(RandomUtils::engine()) * (M_PI / 180.0f));
        }
        else
        {
            // Waypoint patrol (plan §5.1): pick/reuse a random target and steer toward it.
            // Squared arrival check (no sqrt on the hot path).
            float wpDX = movementData.patrolTargetPoint.positionX - mob.position.positionX;
            float wpDY = movementData.patrolTargetPoint.positionY - mob.position.positionY;

            bool needNewWaypoint = !movementData.hasPatrolTarget ||
                (wpDX * wpDX + wpDY * wpDY < 2.0f * 2.0f);
            if (needNewWaypoint)
            {
                bool waypointSet = false;
                if (zone.shape == ZoneShape::CIRCLE)
                {
                    const float safeRadius = zone.outerRadius - borderThreshold - 1.0f;
                    if (safeRadius > 0.0f)
                    {
                        const auto [wx, wy] = SpawnGeometry::sampleCircle(
                            zone.centerX, zone.centerY, safeRadius);
                        movementData.patrolTargetPoint.positionX = wx;
                        movementData.patrolTargetPoint.positionY = wy;
                        waypointSet = true;
                    }
                }
                else if (zone.shape == ZoneShape::ANNULUS)
                {
                    const float safeOuter = zone.outerRadius - borderThreshold - 1.0f;
                    const float safeInner = zone.innerRadius + borderThreshold + 1.0f;
                    if (safeOuter > safeInner)
                    {
                        // Limit waypoint to ±90° arc from mob's current angular position.
                        // A waypoint on the opposite side of the ring forces a straight-line
                        // path through the inner hole → all retries fail → orbit via inertia.
                        const float mobAngle = std::atan2(mob.position.positionY - zone.centerY,
                            mob.position.positionX - zone.centerX);
                        const auto [wx, wy] = SpawnGeometry::sampleAnnulusArc(
                            zone.centerX, zone.centerY, safeInner, safeOuter,
                            mobAngle, static_cast<float>(M_PI) * 0.5f);
                        movementData.patrolTargetPoint.positionX = wx;
                        movementData.patrolTargetPoint.positionY = wy;
                        waypointSet = true;
                    }
                }
                else // RECT
                {
                    float innerMinX = minX + borderThreshold + 1.0f;
                    float innerMaxX = maxX - borderThreshold - 1.0f;
                    float innerMinY = minY + borderThreshold + 1.0f;
                    float innerMaxY = maxY - borderThreshold - 1.0f;
                    if (innerMaxX > innerMinX && innerMaxY > innerMinY)
                    {
                        std::uniform_real_distribution<float> ptX(innerMinX, innerMaxX);
                        std::uniform_real_distribution<float> ptY(innerMinY, innerMaxY);
                        movementData.patrolTargetPoint.positionX = ptX(RandomUtils::engine());
                        movementData.patrolTargetPoint.positionY = ptY(RandomUtils::engine());
                        waypointSet = true;
                    }
                }
                if (waypointSet)
                {
                    // Clamp waypoint to patrol radius from spawn so the mob never
                    // wanders beyond the chase leash boundary during patrol.
                    float sdX = movementData.patrolTargetPoint.positionX - movementData.spawnPosition.positionX;
                    float sdY = movementData.patrolTargetPoint.positionY - movementData.spawnPosition.positionY;
                    float spawnDist = std::sqrt(sdX * sdX + sdY * sdY);
                    if (spawnDist > mob.patrolRadius && spawnDist > 0.0f)
                    {
                        float scale = mob.patrolRadius / spawnDist;
                        movementData.patrolTargetPoint.positionX = movementData.spawnPosition.positionX + sdX * scale;
                        movementData.patrolTargetPoint.positionY = movementData.spawnPosition.positionY + sdY * scale;
                    }
                    movementData.hasPatrolTarget = true;
                    updateMobMovementData(mob.uid, movementData);
                    wpDX = movementData.patrolTargetPoint.positionX - mob.position.positionX;
                    wpDY = movementData.patrolTargetPoint.positionY - mob.position.positionY;
                }
            }

            float angleToWaypoint = std::atan2(wpDY, wpDX);

            // Direction inertia (plan §5.2): 70% chance of continuing ±30° from
            // previous heading, 30% chance of re-aligning straight toward waypoint.
            // For ANNULUS reduce to 40%: high inertia in a ring naturally produces
            // orbit behavior as the tangential heading is always "valid" and gets
            // reinforced every step.
            bool hasPrevDir = (movementData.movementDirectionX != 0.0f ||
                               movementData.movementDirectionY != 0.0f);
            std::uniform_real_distribution<float> coinFlip(0.0f, 1.0f);
            const float inertiaProbability = (zone.shape == ZoneShape::ANNULUS) ? 0.40f : 0.70f;
            if (hasPrevDir && coinFlip(RandomUtils::engine()) < inertiaProbability)
            {
                float prevAngle = std::atan2(movementData.movementDirectionY,
                    movementData.movementDirectionX);
                // Blend ±30° (π/6) around previous heading
                std::uniform_real_distribution<float> inertiaAngle(-M_PI / 6.0f, M_PI / 6.0f);
                newAngle = prevAngle + inertiaAngle(RandomUtils::engine());
            }
            else
            {
                // Re-align to waypoint with a small random scatter (±15°)
                std::uniform_real_distribution<float> scatter(-M_PI / 12.0f, M_PI / 12.0f);
                newAngle = angleToWaypoint + scatter(RandomUtils::engine());
            }
        }

        float tempDirectionX = cos(newAngle);
        float tempDirectionY = sin(newAngle);
        float testNewX = mob.position.positionX + (tempDirectionX * stepSize);
        float testNewY = mob.position.positionY + (tempDirectionY * stepSize);

        // Check if position is valid
        if (isValidPosition(testNewX, testNewY, zone, otherMobs, mob, params))
        {
            newDirectionX = tempDirectionX;
            newDirectionY = tempDirectionY;
            foundValidDirection = true;
            break;
        }
    }

    if (!foundValidDirection)
    {
        if (atBorder)
        {
            // Emergency escape: pure radial direction toward zone interior, no jitter.
            // For ANNULUS: aim toward midRadius along current radial direction
            //   (center would cross the inner hole → invalid).
            // For CIRCLE/RECT: aim at geometric center as before.
            float ex, ey;
            if (zone.shape == ZoneShape::ANNULUS)
            {
                float dx = mob.position.positionX - zone.centerX;
                float dy = mob.position.positionY - zone.centerY;
                float d = std::sqrt(dx * dx + dy * dy);
                float midR = (zone.innerRadius + zone.outerRadius) * 0.5f;
                if (d > 0.0f)
                {
                    // Target = point at midRadius along current radial direction
                    float targetX = zone.centerX + (dx / d) * midR;
                    float targetY = zone.centerY + (dy / d) * midR;
                    ex = targetX - mob.position.positionX;
                    ey = targetY - mob.position.positionY;
                }
                else
                {
                    ex = 1.0f;
                    ey = 0.0f;
                }
            }
            else
            {
                float cx = (minX + maxX) * 0.5f;
                float cy = (minY + maxY) * 0.5f;
                ex = cx - mob.position.positionX;
                ey = cy - mob.position.positionY;
            }
            float ed = std::sqrt(ex * ex + ey * ey);
            if (ed > 0.0f)
            {
                newDirectionX = ex / ed;
                newDirectionY = ey / ed;
            }
        }
        else
        {
            // Blend with previous heading for interior mobs.
            std::uniform_real_distribution<float> directionAdjust(params.directionAdjustMin, params.directionAdjustMax);
            float adjustFactor = directionAdjust(RandomUtils::engine());
            newDirectionX = (newDirectionX * adjustFactor) + (movementData.movementDirectionX * (1.0f - adjustFactor));
            newDirectionY = (newDirectionY * adjustFactor) + (movementData.movementDirectionY * (1.0f - adjustFactor));
        }
    }

    // Calculate final position
    float newX = std::clamp(mob.position.positionX + (newDirectionX * stepSize), minX, maxX);
    float newY = std::clamp(mob.position.positionY + (newDirectionY * stepSize), minY, maxY);

    // Overshoot guard: if this step carried the mob past its patrol waypoint,
    // stop exactly at the waypoint.  Without this, (waypoint - newPos) points
    // BACKWARD in the next broadcast, causing the client to move the mob in
    // reverse until the server picks a fresh waypoint on the next step.
    if (!atBorder && movementData.hasPatrolTarget)
    {
        float oldToWpX = movementData.patrolTargetPoint.positionX - mob.position.positionX;
        float oldToWpY = movementData.patrolTargetPoint.positionY - mob.position.positionY;
        float newToWpX = movementData.patrolTargetPoint.positionX - newX;
        float newToWpY = movementData.patrolTargetPoint.positionY - newY;
        // Dot-product sign flip means the mob crossed the waypoint
        if ((oldToWpX * newToWpX + oldToWpY * newToWpY) < 0.f)
        {
            newX = std::clamp(movementData.patrolTargetPoint.positionX, minX, maxX);
            newY = std::clamp(movementData.patrolTargetPoint.positionY, minY, maxY);
            // Recalculate step length so result.speed stays correct
            float dx = newX - mob.position.positionX;
            float dy = newY - mob.position.positionY;
            stepSize = std::sqrt(dx * dx + dy * dy);
            if (stepSize < 0.001f)
                stepSize = 0.001f;
            // Clear waypoint — next call will pick a fresh patrol target
            movementData.hasPatrolTarget = false;
            updateMobMovementData(mob.uid, movementData);
        }
    }

    // Final validation.
    // If the full step is blocked by a nearby mob, try a minimal nudge in the
    // same direction so a tightly-packed cluster can shuffle itself free.
    if (!isValidPosition(newX, newY, zone, otherMobs, mob, params))
    {
        const float nudge = params.minMoveDistance * 0.5f;
        float nudgeX = std::clamp(mob.position.positionX + (newDirectionX * nudge), minX, maxX);
        float nudgeY = std::clamp(mob.position.positionY + (newDirectionY * nudge), minY, maxY);
        if (isValidPosition(nudgeX, nudgeY, zone, otherMobs, mob, params))
        {
            newX = nudgeX;
            newY = nudgeY;
            // AAA telemetry honesty: recompute step length from the actual
            // nudge displacement so result.speed matches reality.
            const float ndx = newX - mob.position.positionX;
            const float ndy = newY - mob.position.positionY;
            stepSize = std::sqrt(ndx * ndx + ndy * ndy);
        }
        else
        {
            return std::nullopt; // No valid movement
        }
    }

    // Create result
    MobMovementResult result;
    result.newPosition = mob.position;
    result.newPosition.positionX = newX;
    result.newPosition.positionY = newY;

    // AAA telemetry honesty: the border clamps / nudge above may have eaten
    // part of the step — recompute from the actual displacement so the
    // reported speed matches reality (client animates/advances on it).
    {
        const float rdx = newX - mob.position.positionX;
        const float rdy = newY - mob.position.positionY;
        stepSize = std::sqrt(rdx * rdx + rdy * rdy);
    }

    // Calculate rotation
    std::uniform_real_distribution<float> rotationJitter(params.rotationJitterMin, params.rotationJitterMax);
    result.newPosition.rotationZ = atan2(newDirectionY, newDirectionX) * (180.0f / M_PI) + rotationJitter(RandomUtils::engine());

    result.newDirectionX = newDirectionX;
    result.newDirectionY = newDirectionY;
    result.validMovement = true;
    // Patrol: speed is set so the client can lerp from old position to new position
    // over exactly 1.0 second, then stop (NOT extrapolate indefinitely).
    // Client rule for combatState=0: animate toward packet.position over (stepSize/speed) seconds,
    // then set velocity=0. This gives smooth visual movement without drift.
    static constexpr float kPatrolTransitionSec = 1.0f;
    result.speed = stepSize / kPatrolTransitionSec;

    return result;
}

bool
MobMovementManager::isValidPosition(
    float x, float y, const SpawnZoneStruct &zone, const std::vector<std::pair<int, PositionStruct>> &otherMobs, const MobDataStruct &currentMob, const MobMovementParams &params)
{
    // Math lives in MobMovementMath (1-1 with the inline block).
    return MobMovementMath::isValidPosition(
        x, y, zone, otherMobs, currentMob.uid, static_cast<float>(currentMob.radius), params.minSeparationDistance);
}

bool
MobMovementManager::isValidPositionForChase(
    float x, float y, const std::vector<std::pair<int, PositionStruct>> &otherMobs, const MobDataStruct &currentMob, const MobMovementParams &params)
{
    // Same radius-based separation as isValidPosition, no zone boundaries.
    return MobMovementMath::isValidPositionForChase(
        x, y, otherMobs, currentMob.uid, static_cast<float>(currentMob.radius), params.minSeparationDistance);
}

MobMovementParams
MobMovementManager::getDefaultMovementParams()
{
    return MobMovementParams{}; // Use default values from struct
}

MobMovementData
MobMovementManager::getMobMovementData(int mobUID) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        return it->second;
    }
    return MobMovementData{}; // Return default values
}

bool
MobMovementManager::shouldSendMobUpdate(int mobUID, const PositionStruct &currentPosition)
{
    // Phase-1: shared (read) lock – fast path for the common case where the mob
    // has not moved far enough. Avoids exclusive contention on every tick.
    // AAA: combat states (CHASING/RETURNING/FLEEING) bypass the distance gate —
    // the velocity stream must stay continuous at 10 Hz (holds with speed=0
    // included). Rate limiting in ChunkServer (100 ms combat) already bounds
    // bandwidth; the distance filter starved arrival-deceleration micro-steps
    // (<10 units) and produced client stop-start flicker. PATROLLING keeps
    // the 10-unit gate (discrete 2–6 s steps, client interpolates).
    {
        std::shared_lock<std::shared_mutex> rlock(mutex_);
        auto it = mobMovementData_.find(mobUID);
        if (it != mobMovementData_.end())
        {
            // Skip the distance check if a forced update is pending.
            if (!it->second.forceNextUpdate)
            {
                const bool bCombatStream = (it->second.combatState == MobCombatState::CHASING ||
                                            it->second.combatState == MobCombatState::RETURNING ||
                                            it->second.combatState == MobCombatState::FLEEING);
                if (!bCombatStream)
                {
                    float distance = DistanceUtils::dist2D(currentPosition, it->second.lastSentPosition);
                    if (distance < aiConfig_.minimumMoveDistance)
                        return false; // No write needed — bail out without exclusive lock
                }
            }
        }
    }

    // Phase-2: exclusive (write) lock – only reached when the mob has moved enough
    // or its record doesn’t exist yet. Re-check distance after acquiring the lock
    // to handle the race where another thread already updated lastSentPosition.
    std::unique_lock<std::shared_mutex> wlock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it == mobMovementData_.end())
    {
        MobMovementData newData;
        newData.lastSentPosition = currentPosition;
        mobMovementData_[mobUID] = newData;
        return true;
    }

    float distance = DistanceUtils::dist2D(currentPosition, it->second.lastSentPosition);
    const bool bCombatStreamPhase2 = (it->second.combatState == MobCombatState::CHASING ||
                                      it->second.combatState == MobCombatState::RETURNING ||
                                      it->second.combatState == MobCombatState::FLEEING);
    if (!it->second.forceNextUpdate && !bCombatStreamPhase2 && distance < aiConfig_.minimumMoveDistance)
        return false; // Another thread may have already updated lastSentPosition

    it->second.forceNextUpdate = false;
    it->second.lastSentPosition = currentPosition;

    if (it->second.combatState != MobCombatState::PATROLLING)
    {
        const char *stateNames[] = {"PATROLLING", "CHASING", "PREPARING_ATTACK", "ATTACKING", "ATTACK_COOLDOWN", "RETURNING", "EVADING", "FLEEING"};
        int stateIndex = static_cast<int>(it->second.combatState);
        if (stateIndex >= 0 && stateIndex < 8)
        {
            logger_.log("[MOVEMENT] Sending position update for mob " + std::to_string(mobUID) +
                        " (state: " + stateNames[stateIndex] + ", distance moved: " +
                        std::to_string(distance) + ")");
        }
    }

    return true;
}

void
MobMovementManager::forceMobStateUpdate(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        // Mark that the next shouldSendPositionUpdate call must bypass the
        // distance threshold. Do NOT touch lastSentPosition — that would
        // produce a spurious multi-million "distance moved" log entry.
        it->second.forceNextUpdate = true;

        const char *stateNames[] = {"PATROLLING", "CHASING", "PREPARING_ATTACK", "ATTACKING", "ATTACK_COOLDOWN", "RETURNING", "EVADING", "FLEEING"};
        int stateIndex = static_cast<int>(it->second.combatState);
        if (stateIndex >= 0 && stateIndex < 8)
        {
            logger_.log("[COMBAT] Forcing state update for mob " + std::to_string(mobUID) +
                        " (state: " + stateNames[stateIndex] + ")");
        }
    }
}

void
MobMovementManager::updateLastBroadcastMs(int mobUID, int64_t nowMs)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        it->second.lastBroadcastMs = nowMs;
    }
}

MobMovementData
MobMovementManager::getMobMovementDataInternal(int mobUID)
{
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = mobMovementData_.find(mobUID);
        if (it != mobMovementData_.end())
        {
            return it->second;
        }
    }

    // Initialize new mob data with AI config if not found
    initializeMobMovementData(mobUID);

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = mobMovementData_.find(mobUID);
        if (it != mobMovementData_.end())
        {
            return it->second;
        }
    }

    return MobMovementData{}; // Fallback
}

void
MobMovementManager::updateMobMovementData(int mobUID, const MobMovementData &data)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    mobMovementData_[mobUID] = data;
}

void
MobMovementManager::updateMobMovementPositionFields(
    int mobUID, float dirX, float dirY, float lastMoveTime, float nextMoveTime, float currentSpeed, float deflectionSign)
{
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                              .count();

    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        it->second.movementDirectionX = dirX;
        it->second.movementDirectionY = dirY;
        it->second.lastMoveTime = lastMoveTime;
        it->second.nextMoveTime = nextMoveTime;
        it->second.currentSpeedUnitsPerSec = currentSpeed;
        it->second.lastStepTimestampMs = nowMs;
        it->second.lastDeflectionSign = deflectionSign;
    }
}

void
MobMovementManager::handleMobAttacked(int mobUID, int attackerPlayerId, int damage)
{
    mobAIController_.handleMobAttacked(mobUID, attackerPlayerId, damage);
}

std::optional<MobMovementResult>
MobMovementManager::calculateChaseMovement(
    const MobDataStruct &mob,
    const SpawnZoneStruct &zone,
    const std::vector<std::pair<int, PositionStruct>> &otherMobs,
    int targetPlayerId,
    const MobMovementParams &params)
{
    if (!characterManager_)
    {
        log_->error("CharacterManager not available in calculateChaseMovement");
        return std::nullopt;
    }

    auto targetPlayer = characterManager_->getCharacterById(targetPlayerId);
    if (targetPlayer.characterId == 0)
    {
        return std::nullopt;
    }

    // ---- 1. Vector to target (squared range check — no sqrt) ----
    const float targetX = targetPlayer.characterPosition.positionX;
    const float targetY = targetPlayer.characterPosition.positionY;
    const float dx = targetX - mob.position.positionX;
    const float dy = targetY - mob.position.positionY;
    const float dist2 = dx * dx + dy * dy;

    // ---- 2. Zone boundary check (leash if mob is dragged outside its zone) ----
    if (shouldStopChasing(mob.position, zone))
    {
        auto updated = getMobMovementDataInternal(mob.uid);
        int lostTargetId = updated.targetPlayerId;
        updated.targetPlayerId = 0;
        updated.isReturningToSpawn = true;
        updated.threatTable.clear();
        updated.attackerTimestamps.clear();
        updateMobMovementData(mob.uid, updated);
        sendMobTargetLost(mob, lostTargetId);

        log_->info("[INFO] Mob UID: " + std::to_string(mob.uid) +
                   " too far from spawn zone, returning");
        return std::nullopt;
    }

    // ---- 2. Attack range — hold position, let combat state machine handle attacks ----
    // AAA: return an explicit speed=0 hold packet instead of nullopt (silence).
    // Silence starves the client's dead reckoning (extrapolation freeze → micro-stop).
    // A hold keeps the 10 Hz velocity stream continuous; the state machine
    // transitions CHASING → PREPARING_ATTACK on the next tick with forceNextUpdate.
    {
        std::lock_guard<std::mutex> lg(logMutex_);
        if (dist2 <= mob.attackRange * mob.attackRange)
        {
            if (inRangeSet_.insert(mob.uid).second)
            {
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                            " reached attack range of player " + std::to_string(targetPlayerId) +
                            " (dist2: " + std::to_string(dist2) + ") - holding position");
            }
            MobMovementResult hold;
            hold.newPosition = mob.position;
            hold.validMovement = true;
            hold.speed = 0.0f;
            hold.deflectionSign = 0.0f;
            const float holdLen = std::sqrt(dist2);
            if (holdLen > 0.001f)
            {
                hold.newDirectionX = dx / holdLen;
                hold.newDirectionY = dy / holdLen;
                hold.newPosition.rotationZ =
                    std::atan2(dy, dx) * (180.0f / static_cast<float>(M_PI));
            }
            else
            {
                auto holdMd = getMobMovementDataInternal(mob.uid);
                hold.newDirectionX = holdMd.movementDirectionX;
                hold.newDirectionY = holdMd.movementDirectionY;
            }
            return hold;
        }
        else
        {
            inRangeSet_.erase(mob.uid);
        }
    }

    // ---- 5. Melee-slot waiting: hold outside melee ring to prevent crowd jitter ----
    // AAA: hold packet (speed=0) instead of silence — see §2 above.
    auto md = getMobMovementDataInternal(mob.uid);
    {
        constexpr float kWaitBuffer = 20.0f;
        const float waitR = mob.attackRange + kWaitBuffer;
        if (md.waitingForMeleeSlot && dist2 <= waitR * waitR)
        {
            MobMovementResult hold;
            hold.newPosition = mob.position;
            hold.validMovement = true;
            hold.speed = 0.0f;
            hold.deflectionSign = 0.0f;
            const float holdLen = std::sqrt(dist2);
            if (holdLen > 0.001f)
            {
                hold.newDirectionX = dx / holdLen;
                hold.newDirectionY = dy / holdLen;
                hold.newPosition.rotationZ =
                    std::atan2(dy, dx) * (180.0f / static_cast<float>(M_PI));
            }
            else
            {
                hold.newDirectionX = md.movementDirectionX;
                hold.newDirectionY = md.movementDirectionY;
            }
            return hold;
        }
    }

    // ---- 6. Micro-movements: hold instead of silence (keeps velocity stream continuous) ----
    if (dist2 < 1.0f)
    {
        MobMovementResult hold;
        hold.newPosition = mob.position;
        hold.validMovement = true;
        hold.speed = 0.0f;
        hold.deflectionSign = 0.0f;
        hold.newDirectionX = md.movementDirectionX;
        hold.newDirectionY = md.movementDirectionY;
        return hold;
    }

    // ---- 7. Desired direction (length needed for normalization) ----
    const float distance = std::sqrt(dist2);
    float desiredDx = dx / distance;
    float desiredDy = dy / distance;

    // ---- 8. Separation steering ----
    // Soft repulsion force from nearby mobs. Produces smooth crowd navigation
    // instead of the discrete ±30/60/90° deflection angles that caused oscillation.
    float sepX = 0.0f;
    float sepY = 0.0f;
    {
        const float mobRadius = (mob.radius > 0) ? static_cast<float>(mob.radius) : 70.0f;
        const float sepRadius = std::max(aiConfig_.separationRadius, mobRadius * 4.0f);

        for (const auto &other : otherMobs)
        {
            if (other.first == mob.uid)
                continue;

            float ox = mob.position.positionX - other.second.positionX;
            float oy = mob.position.positionY - other.second.positionY;
            float od2 = ox * ox + oy * oy;

            if (od2 >= sepRadius * sepRadius || od2 < 0.01f)
                continue;

            float od = std::sqrt(od2);
            // Strength: inverse-linear falloff (strongest when touching, zero at sepRadius).
            float strength = (sepRadius - od) / sepRadius;
            // Square the strength for a sharper near-field repulsion.
            strength *= strength;
            sepX += (ox / od) * strength;
            sepY += (oy / od) * strength;
        }
    }

    // ---- 9. Direction smoothing (exponential steering) ----
    // Blend desired chase direction with separation, then smoothly steer from
    // the previous tick's heading. Eliminates the "saw" pattern caused by
    // snapping to a new direction vector every tick.
    float blendedDx = desiredDx + sepX * aiConfig_.separationWeight;
    float blendedDy = desiredDy + sepY * aiConfig_.separationWeight;
    {
        float len = std::sqrt(blendedDx * blendedDx + blendedDy * blendedDy);
        if (len > 0.0001f)
        {
            blendedDx /= len;
            blendedDy /= len;
        }
        else
        {
            blendedDx = desiredDx;
            blendedDy = desiredDy;
        }
    }

    // Exponential steering: smoothly rotate from previous direction toward blended target.
    // factor = 1 - exp(-turnSpeed * dt); e.g. turnSpeed=10, dt=0.1 → factor≈0.63
    float prevDx = md.movementDirectionX;
    float prevDy = md.movementDirectionY;
    bool hasPrev = (prevDx * prevDx + prevDy * prevDy) > 0.0001f;

    float finalDx, finalDy;
    if (hasPrev)
    {
        float turnFactor = 1.0f - std::exp(-aiConfig_.chaseTurnSpeed * aiConfig_.chaseMovementInterval);
        finalDx = prevDx + (blendedDx - prevDx) * turnFactor;
        finalDy = prevDy + (blendedDy - prevDy) * turnFactor;
        float fLen = std::sqrt(finalDx * finalDx + finalDy * finalDy);
        if (fLen > 0.0001f)
        {
            finalDx /= fLen;
            finalDy /= fLen;
        }
        else
        {
            finalDx = blendedDx;
            finalDy = blendedDy;
        }
    }
    else
    {
        finalDx = blendedDx;
        finalDy = blendedDy;
    }

    // ---- 10. Speed calculation ----
    // Read mob's move_speed attribute; fallback to global config.
    // Scale from MovementUnits (single source; must match the UE client).
    float mobChaseSpeed = aiConfig_.chaseSpeedUnitsPerSec;
    for (const auto &attr : mob.attributes)
    {
        if (attr.slug == "move_speed" && attr.value > 0)
        {
            mobChaseSpeed = static_cast<float>(attr.value) * MovementUnits::kMoveSpeedScale;
            break;
        }
    }

    // Arrival deceleration: smoothly reduce speed approaching attack range.
    // Prevents the jarring hard-stop at attackRange. Pure ease-out curve —
    // no speed floor: the reported speed must always equal the actual step
    // (telemetry honesty invariant), and combat broadcasts have no distance
    // gate, so micro-steps transmit fine.
    float distToAttackRange = distance - mob.attackRange;
    if (aiConfig_.arrivalSlowdownDistance > 0.0f &&
        distToAttackRange > 0.0f &&
        distToAttackRange < aiConfig_.arrivalSlowdownDistance)
    {
        float t = distToAttackRange / aiConfig_.arrivalSlowdownDistance; // 0 at range, 1 at full speed
        float minSpeed = mobChaseSpeed * aiConfig_.arrivalMinSpeedFraction;
        mobChaseSpeed = minSpeed + (mobChaseSpeed - minSpeed) * t;
    }

    // Step size — distance the mob moves this tick.
    // AAA: arrive exactly at attackRange (no 2-unit early-stop buffer that caused
    // alternating move/stop ticks at the boundary). At/below range → explicit hold.
    float stepSize = mobChaseSpeed * aiConfig_.chaseMovementInterval;
    stepSize = std::min(stepSize, params.maxStepSizeAbsolute);
    float overshoot = distance - mob.attackRange;
    if (overshoot <= 0.0f)
    {
        MobMovementResult hold;
        hold.newPosition = mob.position;
        hold.validMovement = true;
        hold.speed = 0.0f;
        hold.deflectionSign = 0.0f;
        hold.newDirectionX = finalDx;
        hold.newDirectionY = finalDy;
        hold.newPosition.rotationZ =
            std::atan2(finalDy, finalDx) * (180.0f / static_cast<float>(M_PI));
        return hold;
    }
    stepSize = std::min(stepSize, overshoot);
    if (stepSize <= 0.0f)
    {
        MobMovementResult hold;
        hold.newPosition = mob.position;
        hold.validMovement = true;
        hold.speed = 0.0f;
        hold.deflectionSign = 0.0f;
        hold.newDirectionX = finalDx;
        hold.newDirectionY = finalDy;
        hold.newPosition.rotationZ =
            std::atan2(finalDy, finalDx) * (180.0f / static_cast<float>(M_PI));
        return hold;
    }

    // ---- 11. Compute new position ----
    // No hard collision check during active pursuit: separation steering (step 8)
    // continuously pushes mobs apart via soft forces, so rigid per-tick position
    // validation is unnecessary. Hard checks caused alternating blocked/free ticks
    // (the oscillation "saw") and are not how AAA MMORPGs handle mob crowds during
    // combat — slight overlap is acceptable and visually unnoticeable at these scales.
    float newX = mob.position.positionX + finalDx * stepSize;
    float newY = mob.position.positionY + finalDy * stepSize;

    // ---- 12. Return result ----
    // AAA telemetry honesty: the broadcast speed MUST equal the actual step
    // velocity (stepSize already clamped by overshoot above). The client
    // dead-reckons on this value — any overstatement renders as overshoot
    // past the player followed by a correction snap.
    MobMovementResult result;
    result.newPosition = mob.position;
    result.newPosition.positionX = newX;
    result.newPosition.positionY = newY;
    result.newPosition.rotationZ = std::atan2(finalDy, finalDx) * (180.0f / static_cast<float>(M_PI));
    result.newDirectionX = finalDx;
    result.newDirectionY = finalDy;
    result.validMovement = true;
    result.speed = (aiConfig_.chaseMovementInterval > 0.0f)
                       ? stepSize / aiConfig_.chaseMovementInterval
                       : stepSize;
    result.deflectionSign = 0.0f; // No longer used — steering handles collision avoidance
    return result;
}

std::optional<MobMovementResult>
MobMovementManager::calculateReturnToSpawnMovement(
    const MobDataStruct &mob,
    const SpawnZoneStruct & /*zone*/,
    const std::vector<std::pair<int, PositionStruct>> & /*otherMobs*/,
    const PositionStruct &spawnPosition,
    const MobMovementParams &params,
    float speedMultiplier)
{
    // Вектор к точке спавна (squared arrival check; length below for stepping)
    float dx = spawnPosition.positionX - mob.position.positionX;
    float dy = spawnPosition.positionY - mob.position.positionY;
    const float dist2 = dx * dx + dy * dy;

    // Use the configured return speed (units/sec) so the mob walks back at a
    // controlled pace instead of using the raw baseSpeedMax patrol step.
    // AAA: arrival deceleration in the last 60 units so short returns
    // (kill → return a few meters) ease out instead of hard-stopping.
    float returnSpeed = aiConfig_.returnSpeedUnitsPerSec * speedMultiplier;
    const float distPre = std::sqrt(dist2);
    constexpr float kReturnSlowdownDist = 60.0f;
    if (distPre < kReturnSlowdownDist && distPre > 0.0f)
    {
        const float t = distPre / kReturnSlowdownDist; // 0 at target, 1 at full speed
        constexpr float kMinReturnSpeed = 60.0f;       // units/sec — never crawl below
        returnSpeed = kMinReturnSpeed + (returnSpeed - kMinReturnSpeed) * t;
    }
    float stepSize = returnSpeed * aiConfig_.returnMovementInterval;

    // Проверяем, находится ли моб уже очень близко к точке спавна
    constexpr float SPAWN_THRESHOLD = 10.0f; // Минимальное расстояние до точки спавна для считания "на месте"

    if (dist2 <= SPAWN_THRESHOLD * SPAWN_THRESHOLD)
    {
        auto md = getMobMovementDataInternal(mob.uid);
        // AAA: flee/backpedal arrival must not touch return/patrol flags here —
        // the caller (runMobTick flee branch) owns the FLEEING → RETURNING
        // handoff on explicit hold-at-target. Clearing targetPlayerId here
        // would desync the caller's handoff (lostTargetId) and strand isFleeing.
        if (md.isFleeing || md.isBackpedaling)
        {
            MobMovementResult fleeHold;
            fleeHold.newPosition = mob.position;
            fleeHold.validMovement = true;
            fleeHold.speed = 0.0f;
            fleeHold.deflectionSign = 0.0f;
            const float fleeHoldLen = std::sqrt(dist2);
            if (fleeHoldLen > 0.001f)
            {
                fleeHold.newDirectionX = dx / fleeHoldLen;
                fleeHold.newDirectionY = dy / fleeHoldLen;
                fleeHold.newPosition.rotationZ = atan2(dy, dx) * 180.0f / M_PI;
            }
            else
            {
                fleeHold.newDirectionX = 0.0f;
                fleeHold.newDirectionY = 0.0f;
            }
            return fleeHold;
        }
        // Моб достаточно близко к точке спавна: фиксируем прибытие явным
        // hold-пакетом (speed=0), а переход RETURNING → EVADING → PATROLLING
        // выполняет MobAIController на следующем тике (single owner).
        // AAA: nullopt здесь голодал клиент (экстраполяция → snap назад).
        md.isReturningToSpawn = false;
        md.targetPlayerId = 0;
        md.hasReturnDestination = false;

        // Инициализируем время следующего движения для нормального патруля
        float currentTime = getCurrentGameTime();
        std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
        md.nextMoveTime = currentTime + moveTime(RandomUtils::engine());

        md.stepMultiplier = 0.0f; // Будет переинициализирован при следующем движении
        updateMobMovementData(mob.uid, md);

        logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) + " reached spawn area (dist2: " +
                    std::to_string(dist2) + "), holding for EVADING handoff");

        MobMovementResult hold;
        hold.newPosition = mob.position;
        hold.validMovement = true;
        hold.speed = 0.0f;
        hold.deflectionSign = 0.0f;
        const float holdLen = std::sqrt(dist2);
        if (holdLen > 0.001f)
        {
            hold.newDirectionX = dx / holdLen;
            hold.newDirectionY = dy / holdLen;
            hold.newPosition.rotationZ = atan2(dy, dx) * 180.0f / M_PI;
        }
        else
        {
            hold.newDirectionX = 0.0f;
            hold.newDirectionY = 0.0f;
        }
        return hold;
    }

    // Length needed below for clamped arrival (no teleport — see below).
    const float dist = std::sqrt(dist2);

    // AAA: точное прибытие — клампим шаг к дистанции вместо телепорта.
    // Скорость последнего пакета масштабируется под фактический шаг, чтобы
    // клиентская децелерация шла непрерывно и не было snap/отката.
    // Нормализуем направление
    dx /= dist;
    dy /= dist;

    const float clampedStep = std::min(stepSize, dist);
    const bool bFinalStep = (clampedStep >= dist - 0.001f);

    // Двигаем моба на один шаг
    MobMovementResult result;
    result.newPosition = mob.position;
    result.newPosition.positionX = mob.position.positionX + dx * clampedStep;
    result.newPosition.positionY = mob.position.positionY + dy * clampedStep;
    result.newPosition.rotationZ = atan2(dy, dx) * 180.0f / M_PI;
    result.newDirectionX = dx;
    result.newDirectionY = dy;
    result.validMovement = true;
    result.speed = (aiConfig_.returnMovementInterval > 0.0f && !bFinalStep)
                       ? stepSize / aiConfig_.returnMovementInterval
                       : clampedStep / std::max(aiConfig_.returnMovementInterval, 0.001f);
    return result;
}

PositionStruct
MobMovementManager::pickReturnDestination(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // Sampling math lives in MobMovementMath; logging stays here 1-1.
    if (auto dest = MobMovementMath::pickReturnDestination(mobPos, zone); dest.has_value())
    {
        log_->info("Picked return destination for mob in zone " + std::to_string(zone.zoneId) +
                   " at (" + std::to_string(dest->positionX) + "," +
                   std::to_string(dest->positionY) + ")");
        return *dest;
    }

    // No valid point found — return the "no destination" sentinel (origin).
    // The caller treats origin as absent and walks the mob to its spawn
    // position instead. Common when a mob chased far outside a small zone
    // (random points near the mob rarely land back inside); routine, debug.
    log_->debug("No return destination sampled in zone " + std::to_string(zone.zoneId) +
               " — caller falls back to spawn");
    return PositionStruct{};
}

void
MobMovementManager::sendMobTargetLost(const MobDataStruct &mob, int lostTargetPlayerId)
{
    if (!eventQueue_)
    {
        log_->error("EventQueue not set - cannot send mob target lost event");
        return;
    }

    // Create mob target lost packet for network
    nlohmann::json targetLostData;
    targetLostData["mobUID"] = mob.uid;
    targetLostData["mobId"] = mob.id;
    targetLostData["lostTargetPlayerId"] = lostTargetPlayerId;
    targetLostData["positionX"] = mob.position.positionX;
    targetLostData["positionY"] = mob.position.positionY;
    targetLostData["positionZ"] = mob.position.positionZ;
    targetLostData["rotationZ"] = mob.position.rotationZ;

    // Create and send event - broadcast to all clients in area
    EventData eventData = targetLostData;
    Event targetLostEvent(Event::MOB_TARGET_LOST, 0, eventData); // clientID = 0 means broadcast
    eventQueue_->push(targetLostEvent);

    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " lost target player " +
                std::to_string(lostTargetPlayerId) + " - target lost event sent");
}

void
MobMovementManager::initializeMobMovementData(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (mobMovementData_.find(mobUID) == mobMovementData_.end())
    {
        MobMovementData newData;
        // Combat state and timing initialisation only.
        // aggroRange / attackRange / attackCooldown are read directly from
        // MobDataStruct on use (migration 011 fields), so no copy is needed here.
        newData.combatState = MobCombatState::PATROLLING;
        newData.stateChangeTime = getCurrentGameTime();

        mobMovementData_[mobUID] = newData;
    }
}

bool
MobMovementManager::canPerformAction(const MobMovementData &movementData, float currentTime) const
{
    (void)currentTime;
    // CC hold set by MobAIController (stun/root): stand still, keep attacking
    // via the AI state machine (which reads range, not movement).
    if (movementData.movementHeld)
        return false;
    // State table lives in MobMovementMath (1-1 with the inline switch).
    return MobMovementMath::canPerformAction(movementData.combatState);
}

float
MobMovementManager::calculateDistanceFromZone(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    return ZoneBounds::distanceToZone(mobPos, zone);
}

bool
MobMovementManager::shouldReturnToSpawn(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // Predicate lives in MobMovementMath; threshold from const aiConfig_.
    return MobMovementMath::shouldReturnToSpawn(mobPos, zone, aiConfig_.returnToSpawnZoneDistance);
}

bool
MobMovementManager::canSearchNewTargets(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    return MobMovementMath::canSearchNewTargets(mobPos, zone, aiConfig_.newTargetZoneDistance);
}

float
MobMovementManager::calculateNextMoveTime(float currentTime,
    const MobDataStruct &mob,
    const MobMovementData &movementData,
    const MobMovementParams &params)
{
    // Timing math lives in MobMovementMath (1-1 with the inline block).
    return MobMovementMath::calculateNextMoveTime(
        currentTime, mob.patrolSpeed, movementData.speedMultiplier, params);
}

bool
MobMovementManager::shouldStopChasing(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    return MobMovementMath::shouldStopChasing(mobPos, zone, aiConfig_.maxChaseFromZoneEdge);
}
