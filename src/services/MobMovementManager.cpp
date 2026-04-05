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
#include "services/SpawnZoneManager.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <spdlog/logger.h>
#include <unordered_set>

MobMovementManager::MobMovementManager(Logger &logger)
    : logger_(logger),
      mobInstanceManager_(nullptr),
      spawnZoneManager_(nullptr),
      characterManager_(nullptr),
      eventQueue_(nullptr),
      combatSystem_(nullptr),
      rng_(std::random_device{}()),
      mobAIController_(logger)
{
    log_ = logger.getSystem("mob");
    // Wire back-pointer so AI controller can call our public helpers.
    mobAIController_.setMobMovementManager(this);
    log_->info("[INFO] MobMovementManager initialized with default AI configuration");
}

void
MobMovementManager::setMobInstanceManager(MobInstanceManager *mobInstanceManager)
{
    mobInstanceManager_ = mobInstanceManager;
    mobAIController_.setMobInstanceManager(mobInstanceManager);
}

void
MobMovementManager::setSpawnZoneManager(SpawnZoneManager *spawnZoneManager)
{
    spawnZoneManager_ = spawnZoneManager;
}

void
MobMovementManager::setCharacterManager(CharacterManager *characterManager)
{
    characterManager_ = characterManager;
    mobAIController_.setCharacterManager(characterManager);
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
MobMovementManager::setMobManager(MobManager *mobManager)
{
    mobAIController_.setMobManager(mobManager);
}

void
MobMovementManager::setGameServices(GameServices *gs)
{
    gameServices_ = gs;
}

void
MobMovementManager::setZoneMovementParams(int zoneId, const MobMovementParams &params)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    zoneMovementParams_[zoneId] = params;
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

    // Get movement parameters for zone (fetched once per tick)
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto paramIt = zoneMovementParams_.find(zoneId);
        if (paramIt != zoneMovementParams_.end())
        {
            params = paramIt->second;
        }
    }

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

    // Fetch movement params once for the whole function (4.2 optimization)
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto paramIt = zoneMovementParams_.find(zoneId);
        if (paramIt != zoneMovementParams_.end())
            params = paramIt->second;
    }

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
            movementData.nextMoveTime = currentTime + moveTime(rng_);
            updateMobMovementData(mob.uid, movementData);
            log_->info("[DEBUG] Fixed non-aggressive mob UID: " + std::to_string(mob.uid) + " movement timing");
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
        movementData.nextMoveTime = currentTime + initialDelay(rng_) + moveTime(rng_);
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
        movementResult = calculateReturnToSpawnMovement(mob, zone, mobPositions, movementData.fleeTargetPosition, params);
        if (!movementResult.has_value())
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
        movementResult = calculateReturnToSpawnMovement(mob, zone, mobPositions, movementData.spawnPosition, params);
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
    // every 50 ms.  Also reset direction and waypoint so the next attempt explores
    // a fresh random heading instead of repeating the one that just failed.
    if (!hasTarget)
    {
        auto stuckData = getMobMovementDataInternal(mob.uid);
        if (stuckData.combatState == MobCombatState::PATROLLING)
        {
            std::uniform_real_distribution<float> waitTime(params.moveTimeMin, params.moveTimeMax);
            stuckData.nextMoveTime = currentTime + waitTime(rng_);
            stuckData.movementDirectionX = 0.0f;
            stuckData.movementDirectionY = 0.0f;
            stuckData.hasPatrolTarget = false; // force a new waypoint on next attempt
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

    // Calculate zone boundaries (posX/Y = min_spawn, sizeX/Y = max_spawn — два угла AABB из БД)
    float minX = zone.posX;
    float maxX = zone.sizeX;
    float minY = zone.posY;
    float maxY = zone.sizeY;

    // Check if mob is at border
    float borderThreshold = std::max(maxX - minX, maxY - minY) * params.borderThresholdPercent;
    bool atBorder = (mob.position.positionX <= minX + borderThreshold ||
                     mob.position.positionX >= maxX - borderThreshold ||
                     mob.position.positionY <= minY + borderThreshold ||
                     mob.position.positionY >= maxY - borderThreshold);

    // Initialize step multiplier if needed
    if (movementData.stepMultiplier == 0.0f)
    {
        std::uniform_real_distribution<float> stepMultiplier(params.stepMultiplierMin, params.stepMultiplierMax);
        movementData.stepMultiplier = stepMultiplier(rng_);
        // Сохраняем обновленный stepMultiplier
        updateMobMovementData(mob.uid, movementData);

        logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " initialized stepMultiplier: " +
                    std::to_string(movementData.stepMultiplier) + " for normal patrol");
    }

    // Calculate step size
    std::uniform_real_distribution<float> baseSpeed(params.baseSpeedMin, params.baseSpeedMax);
    std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
    float maxStepSize = std::min(((maxX - minX) + (maxY - minY)) * params.maxStepSizePercent, params.maxStepSizeAbsolute);
    float stepSize = std::clamp(baseSpeed(rng_) * movementData.stepMultiplier * randFactor(rng_),
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
            // Move towards center when at border
            float centerX = (minX + maxX) * 0.5f;
            float centerY = (minY + maxY) * 0.5f;
            float angleToCenter = atan2(centerY - mob.position.positionY,
                centerX - mob.position.positionX);
            std::uniform_real_distribution<float> borderAngle(params.borderAngleMin, params.borderAngleMax);
            newAngle = angleToCenter + (borderAngle(rng_) * (M_PI / 180.0f));
        }
        else
        {
            // Waypoint patrol (plan §5.1): pick/reuse a random target and steer toward it.
            float wpDX = movementData.patrolTargetPoint.positionX - mob.position.positionX;
            float wpDY = movementData.patrolTargetPoint.positionY - mob.position.positionY;
            float wpDist = std::sqrt(wpDX * wpDX + wpDY * wpDY);

            bool needNewWaypoint = !movementData.hasPatrolTarget || wpDist < 2.0f;
            if (needNewWaypoint)
            {
                float innerMinX = minX + borderThreshold + 1.0f;
                float innerMaxX = maxX - borderThreshold - 1.0f;
                float innerMinY = minY + borderThreshold + 1.0f;
                float innerMaxY = maxY - borderThreshold - 1.0f;

                if (innerMaxX > innerMinX && innerMaxY > innerMinY)
                {
                    std::uniform_real_distribution<float> ptX(innerMinX, innerMaxX);
                    std::uniform_real_distribution<float> ptY(innerMinY, innerMaxY);
                    movementData.patrolTargetPoint.positionX = ptX(rng_);
                    movementData.patrolTargetPoint.positionY = ptY(rng_);
                    movementData.hasPatrolTarget = true;
                    updateMobMovementData(mob.uid, movementData);
                    // Recalculate direction vector to new waypoint
                    wpDX = movementData.patrolTargetPoint.positionX - mob.position.positionX;
                    wpDY = movementData.patrolTargetPoint.positionY - mob.position.positionY;
                }
            }

            float angleToWaypoint = std::atan2(wpDY, wpDX);

            // Direction inertia (plan §5.2): 70% chance of continuing ±30° from
            // previous heading, 30% chance of re-aligning straight toward waypoint.
            bool hasPrevDir = (movementData.movementDirectionX != 0.0f ||
                               movementData.movementDirectionY != 0.0f);
            std::uniform_real_distribution<float> coinFlip(0.0f, 1.0f);
            if (hasPrevDir && coinFlip(rng_) < 0.70f)
            {
                float prevAngle = std::atan2(movementData.movementDirectionY,
                    movementData.movementDirectionX);
                // Blend ±30° (π/6) around previous heading
                std::uniform_real_distribution<float> inertiaAngle(-M_PI / 6.0f, M_PI / 6.0f);
                newAngle = prevAngle + inertiaAngle(rng_);
            }
            else
            {
                // Re-align to waypoint with a small random scatter (±15°)
                std::uniform_real_distribution<float> scatter(-M_PI / 12.0f, M_PI / 12.0f);
                newAngle = angleToWaypoint + scatter(rng_);
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
            // Emergency escape: aim straight at zone centre with no angle jitter.
            // Regular retries already tried centre±scatter and all failed (border corner);
            // a pure centre vector is the only direction guaranteed to clear the wall.
            float cx = (minX + maxX) * 0.5f;
            float cy = (minY + maxY) * 0.5f;
            float ex = cx - mob.position.positionX;
            float ey = cy - mob.position.positionY;
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
            float adjustFactor = directionAdjust(rng_);
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

    // Calculate rotation
    std::uniform_real_distribution<float> rotationJitter(params.rotationJitterMin, params.rotationJitterMax);
    result.newPosition.rotationZ = atan2(newDirectionY, newDirectionX) * (180.0f / M_PI) + rotationJitter(rng_);

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
    // Check zone boundaries (posX/Y = min_spawn, sizeX/Y = max_spawn — два угла AABB из БД)
    float minX = zone.posX;
    float maxX = zone.sizeX;
    float minY = zone.posY;
    float maxY = zone.sizeY;

    if (x < minX || x > maxX || y < minY || y > maxY)
    {
        return false;
    }

    // Derive minimum separation from mob's own collision radius.
    // Two mobs must be at least (radiusA + radiusB) apart to avoid overlap.
    // Since otherMobs is a lightweight {uid,pos} list without radius data,
    // we conservatively assume the neighbours share the same radius.
    // Falls back to params.minSeparationDistance when radius is not set.
    const float mobRadius = (currentMob.radius > 0) ? static_cast<float>(currentMob.radius) : 0.0f;
    const float minSep = (mobRadius > 0.0f) ? (mobRadius * 2.0f) : params.minSeparationDistance;

    for (const auto &otherMob : otherMobs)
    {
        if (otherMob.first == currentMob.uid)
            continue;

        float dx = x - otherMob.second.positionX;
        float dy = y - otherMob.second.positionY;
        if (dx * dx + dy * dy < minSep * minSep)
        {
            return false;
        }
    }

    return true;
}

bool
MobMovementManager::isValidPositionForChase(
    float x, float y, const std::vector<std::pair<int, PositionStruct>> &otherMobs, const MobDataStruct &currentMob, const MobMovementParams &params)
{
    // Same radius-based separation as isValidPosition.
    const float mobRadius = (currentMob.radius > 0) ? static_cast<float>(currentMob.radius) : 0.0f;
    const float minSep = (mobRadius > 0.0f) ? (mobRadius * 2.0f) : params.minSeparationDistance;

    for (const auto &otherMob : otherMobs)
    {
        if (otherMob.first == currentMob.uid)
            continue;

        float dx = x - otherMob.second.positionX;
        float dy = y - otherMob.second.positionY;
        if (dx * dx + dy * dy < minSep * minSep)
        {
            return false;
        }
    }

    return true;
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
    {
        std::shared_lock<std::shared_mutex> rlock(mutex_);
        auto it = mobMovementData_.find(mobUID);
        if (it != mobMovementData_.end())
        {
            // Skip the distance check if a forced update is pending.
            if (!it->second.forceNextUpdate)
            {
                float distance = calculateDistance(currentPosition, it->second.lastSentPosition);
                if (distance < aiConfig_.minimumMoveDistance)
                    return false; // No write needed — bail out without exclusive lock
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

    float distance = calculateDistance(currentPosition, it->second.lastSentPosition);
    if (!it->second.forceNextUpdate && distance < aiConfig_.minimumMoveDistance)
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

    // 1) Compute vector to player
    float dx = targetPlayer.characterPosition.positionX - mob.position.positionX;
    float dy = targetPlayer.characterPosition.positionY - mob.position.positionY;
    float distance = std::sqrt(dx * dx + dy * dy);

    // 2) Abandon if too far
    float maxChaseDistance = mob.aggroRange * mob.chaseMultiplier;
    if (distance > maxChaseDistance)
    {
        auto updated = getMobMovementDataInternal(mob.uid);
        int lostTargetId = updated.targetPlayerId; // Save target ID before clearing
        updated.targetPlayerId = 0;
        updated.isReturningToSpawn = true;
        updated.threatTable.clear();
        updated.attackerTimestamps.clear();
        updateMobMovementData(mob.uid, updated);

        // Send target lost event to clients
        sendMobTargetLost(mob, lostTargetId);

        logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                    " lost target (too far: " + std::to_string(distance) +
                    "/" + std::to_string(maxChaseDistance) + "), returning to spawn");
        return std::nullopt;
    }

    // 3) Abandon if zone boundary exceeded
    if (shouldStopChasing(mob.position, zone))
    {
        auto updated = getMobMovementDataInternal(mob.uid);
        int lostTargetId = updated.targetPlayerId; // Save target ID before clearing
        updated.targetPlayerId = 0;
        updated.isReturningToSpawn = true;
        updated.threatTable.clear();
        updated.attackerTimestamps.clear();
        updateMobMovementData(mob.uid, updated);

        // Send target lost event to clients
        sendMobTargetLost(mob, lostTargetId);

        log_->info("[INFO] Mob UID: " + std::to_string(mob.uid) +
                   " too far from spawn zone, returning");
        return std::nullopt;
    }

    // 4) If within attack range → stop movement (combat state system will handle attacks)
    // NOTE: previously there was a constant ATTACK_BUFFER = 10.0f added here, which caused
    // a dead zone: the mob would stop moving at (attackRange + 10) but the combat state
    // machine only transitions to PREPARING_ATTACK at (attackRange). The mob would
    // stand still without attacking until the player closed the gap manually.
    {
        std::lock_guard<std::mutex> lg(logMutex_);
        if (distance <= mob.attackRange)
        {
            if (inRangeSet_.insert(mob.uid).second)
            {
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                            " reached attack range of player " + std::to_string(targetPlayerId) +
                            " (distance: " + std::to_string(distance) + ") - stopping movement");
            }
            // Don't move, let combat state system handle the attack sequence
            return std::nullopt;
        }
        else
        {
            inRangeSet_.erase(mob.uid);
        }
    }

    // 4b) Melee-slot waiting: park this mob just outside the melee ring so it
    // doesn't jitter while waiting for a free spot. The AI controller sets
    // waitingForMeleeSlot when all physical slots around the target are taken.
    // We stop movement at (attackRange + kWaitBuffer) — the mob stands still
    // facing its target without twitching or slow-rotating in place.
    // prevDeflectionSign is read here too, sharing the same lock acquisition.
    float prevDeflectionSign = 0.0f;
    {
        constexpr float kWaitBuffer = 20.0f;
        auto md = getMobMovementDataInternal(mob.uid);
        if (md.waitingForMeleeSlot && distance <= mob.attackRange + kWaitBuffer)
            return std::nullopt;
        prevDeflectionSign = md.lastDeflectionSign;
    }

    // 5) Avoid jitter if extremely close
    if (distance < 1.0f)
    {
        return std::nullopt;
    }

    // 6) Normalize direction
    dx /= distance;
    dy /= distance;

    // 7) Compute step size from the mob's move_speed attribute (same stat system as
    // players: stat_value * MOVE_SPEED_SCALE = units/sec).  Falls back to the global
    // aiConfig_.chaseSpeedUnitsPerSec when the mob has no move_speed stat defined.
    // This speed is also sent verbatim in the packet so the client Dead Reckoning
    // uses exactly the same value:  TargetPos = ServerPos + velocity * dt.
    static constexpr float MOVE_SPEED_SCALE = 40.0f;
    float mobChaseSpeed = aiConfig_.chaseSpeedUnitsPerSec; // default
    for (const auto &attr : mob.attributes)
    {
        if (attr.slug == "move_speed" && attr.value > 0)
        {
            mobChaseSpeed = static_cast<float>(attr.value) * MOVE_SPEED_SCALE;
            break;
        }
    }

    const float kAttackEntryBuffer = 2.0f;
    float stepSize = mobChaseSpeed * aiConfig_.chaseMovementInterval;
    stepSize = std::min(stepSize, params.maxStepSizeAbsolute);
    float overshoot = distance - std::max(0.0f, mob.attackRange - kAttackEntryBuffer);
    stepSize = std::min(stepSize, overshoot);
    if (stepSize <= 0.0f)
    {
        return std::nullopt;
    }

    // 9) Report the per-mob speed so client Dead Reckoning velocity is consistent.
    const float chaseSpeed = mobChaseSpeed;
    float newX = mob.position.positionX + dx * stepSize;
    float newY = mob.position.positionY + dy * stepSize;

    // 8+1) Validate collisions (skip zone bounds).
    // If the direct path is blocked, try deflection angles so mobs steer around
    // each other instead of stacking. The candidate angles are ordered so that
    // the same sign as the previous tick is always tried first at each magnitude.
    // This prevents the ±90° alternation that causes a mob to oscillate ~20 units
    // back and forth when all smaller deflections are blocked by a crowd.
    float usedDeflectionSign = 0.0f; // 0 = direct path; set below when deflecting
    if (!isValidPositionForChase(newX, newY, otherMobs, mob, params))
    {
        // Build angle list: prefer same-sign as last tick at every magnitude so the
        // mob maintains a consistent side when threading through a crowd.
        const float pos30 = static_cast<float>(M_PI) / 6.0f;
        const float neg30 = -static_cast<float>(M_PI) / 6.0f;
        const float pos60 = static_cast<float>(M_PI) / 3.0f;
        const float neg60 = -static_cast<float>(M_PI) / 3.0f;
        const float pos90 = static_cast<float>(M_PI) / 2.0f;
        const float neg90 = -static_cast<float>(M_PI) / 2.0f;

        float kDeflectAngles[6];
        if (prevDeflectionSign > 0.0f)
        {
            kDeflectAngles[0] = pos30;
            kDeflectAngles[1] = pos60;
            kDeflectAngles[2] = pos90;
            kDeflectAngles[3] = neg30;
            kDeflectAngles[4] = neg60;
            kDeflectAngles[5] = neg90;
        }
        else if (prevDeflectionSign < 0.0f)
        {
            kDeflectAngles[0] = neg30;
            kDeflectAngles[1] = neg60;
            kDeflectAngles[2] = neg90;
            kDeflectAngles[3] = pos30;
            kDeflectAngles[4] = pos60;
            kDeflectAngles[5] = pos90;
        }
        else
        {
            // No prior preference — interleave ± to minimise deviation on average.
            kDeflectAngles[0] = pos30;
            kDeflectAngles[1] = neg30;
            kDeflectAngles[2] = pos60;
            kDeflectAngles[3] = neg60;
            kDeflectAngles[4] = pos90;
            kDeflectAngles[5] = neg90;
        }

        const float baseAngle = std::atan2(dy, dx);
        bool steeredAround = false;
        for (float delta : kDeflectAngles)
        {
            const float testAngle = baseAngle + delta;
            const float testDx = std::cos(testAngle);
            const float testDy = std::sin(testAngle);
            const float testX = mob.position.positionX + testDx * stepSize;
            const float testY = mob.position.positionY + testDy * stepSize;
            if (isValidPositionForChase(testX, testY, otherMobs, mob, params))
            {
                newX = testX;
                newY = testY;
                dx = testDx;
                dy = testDy;
                steeredAround = true;
                usedDeflectionSign = (delta > 0.0f) ? 1.0f : -1.0f;
                break;
            }
        }

        if (!steeredAround)
            return std::nullopt;
    }

    // 11) Return movement result
    MobMovementResult result;
    result.newPosition = mob.position;
    result.newPosition.positionX = newX;
    result.newPosition.positionY = newY;
    result.newPosition.rotationZ = atan2(dy, dx) * (180.0f / M_PI);
    result.newDirectionX = dx;
    result.newDirectionY = dy;
    result.validMovement = true;
    result.speed = chaseSpeed;
    result.deflectionSign = usedDeflectionSign;
    return result;
}

std::optional<MobMovementResult>
MobMovementManager::calculateReturnToSpawnMovement(
    const MobDataStruct &mob,
    const SpawnZoneStruct & /*zone*/,
    const std::vector<std::pair<int, PositionStruct>> & /*otherMobs*/,
    const PositionStruct &spawnPosition,
    const MobMovementParams &params)
{
    // Вектор к точке спавна
    float dx = spawnPosition.positionX - mob.position.positionX;
    float dy = spawnPosition.positionY - mob.position.positionY;
    float dist = std::sqrt(dx * dx + dy * dy);

    // Use the configured return speed (units/sec) so the mob walks back at a
    // controlled pace instead of using the raw baseSpeedMax patrol step.
    const float returnSpeed = aiConfig_.returnSpeedUnitsPerSec;
    float stepSize = returnSpeed * aiConfig_.returnMovementInterval;

    // Проверяем, находится ли моб уже очень близко к точке спавна
    const float SPAWN_THRESHOLD = 10.0f; // Минимальное расстояние до точки спавна для считания "на месте"

    if (dist <= SPAWN_THRESHOLD)
    {
        // Моб достаточно близко к точке спавна, переключаем в режим патруля
        auto md = getMobMovementDataInternal(mob.uid);
        md.isReturningToSpawn = false;
        md.targetPlayerId = 0;

        // Инициализируем время следующего движения для нормального патруля
        float currentTime = getCurrentGameTime();
        std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
        md.nextMoveTime = currentTime + moveTime(rng_);

        md.stepMultiplier = 0.0f; // Будет переинициализирован при следующем движении
        md.movementDirectionX = 0.0f;
        md.movementDirectionY = 0.0f;
        updateMobMovementData(mob.uid, md);

        logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) + " reached spawn area (distance: " +
                    std::to_string(dist) + "), switching to patrol mode");

        // Если моб уже на точке спавна, не двигаем его - просто переключаем режим
        // Возвращаем nullopt, чтобы не было лишнего обновления позиции
        return std::nullopt;
    }

    // Если до точки спавна ближе, чем один шаг — телепортируем
    if (dist <= stepSize)
    {
        MobMovementResult result;
        result.newPosition = mob.position;
        result.newPosition.positionX = spawnPosition.positionX;
        result.newPosition.positionY = spawnPosition.positionY;
        result.newPosition.rotationZ = atan2(dy, dx) * 180.0f / M_PI;
        result.newDirectionX = dx / dist;
        result.newDirectionY = dy / dist;
        result.validMovement = true;
        result.speed = (aiConfig_.returnMovementInterval > 0.0f)
                           ? stepSize / aiConfig_.returnMovementInterval
                           : stepSize;

        logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) + " teleporting to spawn (distance: " +
                    std::to_string(dist) + ")");
        return result;
    }

    // Нормализуем направление
    dx /= dist;
    dy /= dist;

    // Двигаем моба на один шаг
    MobMovementResult result;
    result.newPosition = mob.position;
    result.newPosition.positionX = mob.position.positionX + dx * stepSize;
    result.newPosition.positionY = mob.position.positionY + dy * stepSize;
    result.newPosition.rotationZ = atan2(dy, dx) * 180.0f / M_PI;
    result.newDirectionX = dx;
    result.newDirectionY = dy;
    result.validMovement = true;
    result.speed = (aiConfig_.returnMovementInterval > 0.0f)
                       ? stepSize / aiConfig_.returnMovementInterval
                       : stepSize;
    return result;
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

float
MobMovementManager::calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2)
{
    float dx = pos1.positionX - pos2.positionX;
    float dy = pos1.positionY - pos2.positionY;
    return std::sqrt(dx * dx + dy * dy);
}

void
MobMovementManager::setAIConfig(const MobAIConfig &config)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    aiConfig_ = config;
    log_->info("[INFO] MobMovementManager: AI configuration updated");
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
    // Only allow movement during certain states
    switch (movementData.combatState)
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

const MobAIConfig &
MobMovementManager::getAIConfig() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return aiConfig_;
}

float
MobMovementManager::calculateDistanceFromZone(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // AABB-based calculation using zone boundaries
    ZoneBounds bounds(zone);
    return bounds.distanceToZone(mobPos);
}

bool
MobMovementManager::shouldReturnToSpawn(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // Zone-based calculation using zone boundaries
    ZoneBounds bounds(zone);
    float distanceFromZoneEdge = bounds.distanceToZone(mobPos);

    // If mob is outside zone by more than allowed distance, return to spawn
    return distanceFromZoneEdge > aiConfig_.returnToSpawnZoneDistance;
}

bool
MobMovementManager::canSearchNewTargets(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // Zone-based calculation using zone boundaries
    ZoneBounds bounds(zone);
    float distanceFromZoneEdge = bounds.distanceToZone(mobPos);

    // Can search for targets if close enough to zone
    return distanceFromZoneEdge <= aiConfig_.newTargetZoneDistance;
}

float
MobMovementManager::calculateNextMoveTime(float currentTime,
    const MobDataStruct &mob,
    const MobMovementData &movementData,
    const MobMovementParams &params)
{
    std::uniform_real_distribution<float> speedTime(params.speedTimeMin, params.speedTimeMax);
    float patrolSpeedFactor = (mob.patrolSpeed > 0.01f) ? mob.patrolSpeed : movementData.speedMultiplier;
    float nextTime = currentTime + std::max(speedTime(rng_) / patrolSpeedFactor, 2.0f);

    // Optional random cooldown pause to add unpredictability
    std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
    if (randFactor(rng_) > 1.15f)
    {
        std::uniform_real_distribution<float> cooldown(params.cooldownMin, params.cooldownMax);
        nextTime += cooldown(rng_) * 0.5f;
    }
    return nextTime;
}

bool
MobMovementManager::shouldStopChasing(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // Zone-based calculation using zone boundaries
    ZoneBounds bounds(zone);
    float distanceFromZoneEdge = bounds.distanceToZone(mobPos);

    // Stop chasing if too far from zone edge
    return distanceFromZoneEdge > aiConfig_.maxChaseFromZoneEdge;
}
