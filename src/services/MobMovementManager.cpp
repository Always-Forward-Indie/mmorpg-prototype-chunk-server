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
        {
            continue;
        }

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
        // Only call aggro handling if mob is aggressive, has a target, or is already in combat state
        if ((mob.isAggressive || movementData.targetPlayerId > 0) && characterManager_)
        {
            mobAIController_.handlePlayerAggro(mob, zone, movementData);
            // Refresh movement data after aggro handling, as it may have changed
            movementData = getMobMovementDataInternal(mob.uid);
        }
        else if (!mob.isAggressive && !movementData.isReturningToSpawn && movementData.targetPlayerId == 0)
        {
            // For non-aggressive mobs that are just patrolling, ensure they're not stuck
            if (movementData.nextMoveTime == 0.0f)
            {
                std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
                movementData.nextMoveTime = currentTime + moveTime(rng_);
                updateMobMovementData(mob.uid, movementData);
                log_->info("[DEBUG] Fixed non-aggressive mob UID: " + std::to_string(mob.uid) + " movement timing");
            }
        }

        // Update combat state for this mob
        mobAIController_.updateMobCombatState(mob, movementData, currentTime);

        // Get fresh movement data after combat state update to ensure we have the latest state
        movementData = getMobMovementDataInternal(mob.uid);

        // Initialize movement timing if needed
        if (movementData.nextMoveTime == 0.0f)
        {
            std::uniform_real_distribution<float> initialDelay(0.0f, params.initialDelayMax);
            std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
            movementData.nextMoveTime = currentTime + initialDelay(rng_) + moveTime(rng_);
            updateMobMovementData(mob.uid, movementData);
        }

        // Check if mob can perform actions (movement) based on combat state
        if (!canPerformAction(movementData, currentTime))
        {
            // Mob is in a state where it shouldn't move (preparing attack, attacking, cooldown)
            continue;
        }

        // Check if it's time to move - respect timing even for mobs with targets
        bool hasTarget = (movementData.targetPlayerId > 0 || movementData.isReturningToSpawn ||
                          movementData.isFleeing || movementData.isBackpedaling);
        bool timeToMove = (currentTime >= movementData.nextMoveTime);

        // For mobs with targets or returning to spawn, allow faster movement
        // This applies to both aggressive and non-aggressive mobs
        if (hasTarget)
        {
            float minInterval = movementData.isReturningToSpawn ? aiConfig_.returnMovementInterval : aiConfig_.chaseMovementInterval;
            timeToMove = (movementData.nextMoveTime == 0.0f ||
                          (currentTime - movementData.lastMoveTime) >= minInterval);
        }

        // Only move if it's time to move
        if (!timeToMove)
        {
            continue;
        }

        // Calculate new position based on mob behavior
        std::optional<MobMovementResult> movementResult;

        if (movementData.isFleeing || movementData.isBackpedaling)
        {
            // Flee or caster backpedal: move toward precomputed fleeTargetPosition
            movementResult = calculateReturnToSpawnMovement(mob, zone, mobPositions, movementData.fleeTargetPosition, params);
            if (!movementResult.has_value())
            {
                // Reached flee destination — clear flags
                movementData.isFleeing = false;
                movementData.isBackpedaling = false;
                updateMobMovementData(mob.uid, movementData);
            }
        }
        else if (movementData.isReturningToSpawn)
        {
            // Return to spawn zone
            movementResult = calculateReturnToSpawnMovement(mob, zone, mobPositions, movementData.spawnPosition, params);
        }
        else if (movementData.targetPlayerId > 0 && characterManager_)
        {
            // Chase target player
            movementResult = calculateChaseMovement(mob, zone, mobPositions, movementData.targetPlayerId, params);
        }
        else
        {
            // Normal random movement
            movementResult = calculateNewPosition(mob, zone, mobPositions, params);
        }
        if (movementResult && movementResult->validMovement)
        {
            // Update movement data
            movementData.movementDirectionX = movementResult->newDirectionX;
            movementData.movementDirectionY = movementResult->newDirectionY;
            movementData.lastMoveTime = currentTime;

            // Update next move time - faster movement when chasing or returning
            if (movementData.targetPlayerId > 0)
            {
                // Use AI config for chase movement timing
                movementData.nextMoveTime = currentTime + aiConfig_.chaseMovementInterval;
            }
            else if (movementData.isReturningToSpawn)
            {
                // Use AI config for return movement timing
                movementData.nextMoveTime = currentTime + aiConfig_.returnMovementInterval;
            }
            else
            {
                // Normal movement timing — use per-mob patrol speed (plan §5.3)
                movementData.nextMoveTime = calculateNextMoveTime(currentTime, mob, movementData, params);
            }

            // Targeted write: update only direction/timing fields.
            // This prevents overwriting targetPlayerId / combatState that may
            // have been set by handleMobAttacked on another thread between our
            // last re-read and now (RMW race fix).
            updateMobMovementPositionFields(
                mob.uid,
                movementData.movementDirectionX,
                movementData.movementDirectionY,
                movementData.lastMoveTime,
                movementData.nextMoveTime,
                movementResult->speed);

            // Update position in MobInstanceManager
            mobInstanceManager_->updateMobPosition(mob.uid, movementResult->newPosition);

            anyMobMoved = true;
        }
        else
        {
            // Same race-condition guard as in moveSingleMob: re-run the state machine
            // when chase movement was stopped so CHASING → PREPARING_ATTACK fires even
            // when the player moved between the pre-movement state-machine read and the
            // movement distance check.
            auto freshData = getMobMovementDataInternal(mob.uid);
            if (freshData.combatState == MobCombatState::CHASING && freshData.targetPlayerId > 0)
            {
                mobAIController_.updateMobCombatState(mob, freshData, currentTime);
            }
        }
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

    // Get movement data for this mob
    auto movementData = getMobMovementDataInternal(mobUID);

    // Initialize spawn position if not set
    if (movementData.spawnPosition.positionX == 0.0f && movementData.spawnPosition.positionY == 0.0f)
    {
        movementData.spawnPosition = mob.position;
        updateMobMovementData(mobUID, movementData);
        logger_.log("[INFO] Initialized spawn position for mob UID: " + std::to_string(mobUID) +
                    " at (" + std::to_string(mob.position.positionX) + ", " +
                    std::to_string(mob.position.positionY) + ")");
    }

    // Handle player aggro and AI behavior
    // Both aggressive mobs and non-aggressive mobs that have been attacked can have targets
    if ((mob.isAggressive || movementData.targetPlayerId > 0) && characterManager_)
    {
        mobAIController_.handlePlayerAggro(mob, zone, movementData);
        // Refresh movement data after potential aggro changes to get latest state
        movementData = getMobMovementDataInternal(mobUID);
    }

    // Update combat state for this mob
    float currentTime = getCurrentGameTime();
    mobAIController_.updateMobCombatState(mob, movementData, currentTime);

    // Get fresh movement data after combat state update
    movementData = getMobMovementDataInternal(mobUID);

    // Check if mob can perform actions (movement) based on combat state
    if (!canPerformAction(movementData, currentTime))
    {
        // Mob is in a state where it shouldn't move (preparing attack, attacking, cooldown)
        return false;
    }

    // Check if it's time to move this mob (respect timing constraints)
    bool hasTarget = (movementData.targetPlayerId > 0 || movementData.isReturningToSpawn ||
                      movementData.isFleeing || movementData.isBackpedaling);
    bool timeToMove = (currentTime >= movementData.nextMoveTime);

    // For mobs with targets or returning to spawn, allow more frequent movement
    // This applies to both aggressive and non-aggressive mobs
    if (hasTarget)
    {
        // Allow movement if enough time has passed (use config values)
        float minInterval = movementData.isReturningToSpawn ? aiConfig_.returnMovementInterval : aiConfig_.chaseMovementInterval;
        timeToMove = (movementData.nextMoveTime == 0.0f ||
                      (currentTime - movementData.lastMoveTime) >= minInterval);
    } // Don't move if it's not time yet
    if (!timeToMove)
    {
        return false;
    }

    // Calculate new position based on mob behavior
    std::optional<MobMovementResult> movementResult;

    if (movementData.isFleeing || movementData.isBackpedaling)
    {
        // Flee or caster backpedal: move toward precomputed fleeTargetPosition
        movementResult = calculateReturnToSpawnMovement(mob, zone, mobPositions, movementData.fleeTargetPosition, params);
        if (!movementResult.has_value())
        {
            movementData.isFleeing = false;
            movementData.isBackpedaling = false;
            updateMobMovementData(mobUID, movementData);
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
        // Normal random movement
        movementResult = calculateNewPosition(mob, zone, mobPositions, params);
    }

    if (movementResult && movementResult->validMovement)
    {
        // Update movement data
        movementData.movementDirectionX = movementResult->newDirectionX;
        movementData.movementDirectionY = movementResult->newDirectionY;

        // Update movement timing
        movementData.lastMoveTime = currentTime;

        // Update next move time based on mob behavior (params already fetched above)
        if (movementData.targetPlayerId > 0)
        {
            // Use AI config for chase movement timing
            movementData.nextMoveTime = currentTime + aiConfig_.chaseMovementInterval;
        }
        else if (movementData.isReturningToSpawn)
        {
            // Use AI config for return movement timing
            movementData.nextMoveTime = currentTime + aiConfig_.returnMovementInterval;
        }
        else
        {
            // Normal movement timing — use per-mob patrol speed (plan §5.3)
            movementData.nextMoveTime = calculateNextMoveTime(currentTime, mob, movementData, params);
        }

        updateMobMovementPositionFields(
            mobUID,
            movementData.movementDirectionX,
            movementData.movementDirectionY,
            movementData.lastMoveTime,
            movementData.nextMoveTime,
            movementResult->speed);

        // Update position in MobInstanceManager
        mobInstanceManager_->updateMobPosition(mobUID, movementResult->newPosition);

        return true;
    }

    // Movement was stopped (mob reached attack range, collision, or other reason).
    // Re-run the combat state machine so CHASING → PREPARING_ATTACK fires immediately
    // when the mob stopped because it is within attack range.  This is necessary
    // because the earlier updateMobCombatState call and calculateChaseMovement each
    // call getCharacterById independently; a concurrent network-thread position update
    // between those two reads can leave the mob frozen in CHASING even though it is
    // already within attack range.
    {
        auto freshData = getMobMovementDataInternal(mobUID);
        if (freshData.combatState == MobCombatState::CHASING && freshData.targetPlayerId > 0)
        {
            mobAIController_.updateMobCombatState(mob, freshData, currentTime);
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
        // Try to adjust current direction
        std::uniform_real_distribution<float> directionAdjust(params.directionAdjustMin, params.directionAdjustMax);
        float adjustFactor = directionAdjust(rng_);
        newDirectionX = (newDirectionX * adjustFactor) + (movementData.movementDirectionX * (1.0f - adjustFactor));
        newDirectionY = (newDirectionY * adjustFactor) + (movementData.movementDirectionY * (1.0f - adjustFactor));
    }

    // Calculate final position
    float newX = std::clamp(mob.position.positionX + (newDirectionX * stepSize), minX, maxX);
    float newY = std::clamp(mob.position.positionY + (newDirectionY * stepSize), minY, maxY);

    // Final validation
    if (!isValidPosition(newX, newY, zone, otherMobs, mob, params))
    {
        return std::nullopt; // No valid movement
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
    // Patrol: server tick is 1 s, so stepSize units per tick ≈ units/second
    result.speed = stepSize;

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
    int mobUID, float dirX, float dirY, float lastMoveTime, float nextMoveTime, float currentSpeed)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        it->second.movementDirectionX = dirX;
        it->second.movementDirectionY = dirY;
        it->second.lastMoveTime = lastMoveTime;
        it->second.nextMoveTime = nextMoveTime;
        it->second.currentSpeedUnitsPerSec = currentSpeed;
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

    // 5) Avoid jitter if extremely close
    if (distance < 1.0f)
    {
        return std::nullopt;
    }

    // 6) Normalize direction
    dx /= distance;
    dy /= distance;

    // 7) Compute step size limited by attackRange (params passed from caller).
    // Overshoot the attack-range boundary by a small buffer so floating-point
    // rounding cannot land the mob at attackRange+epsilon (just outside), which
    // would prevent updateMobCombatState from triggering the attack.
    // The mob targets (attackRange - kAttackEntryBuffer) as its stop point, ensuring
    // it lands clearly inside attack range. Step-4 above still returns nullopt any
    // time the mob is already within attackRange, so the buffer has no effect once
    // the mob is already close.
    const float kAttackEntryBuffer = 2.0f;
    float maxStep = std::min(params.baseSpeedMax * 1.5f, params.maxStepSizeAbsolute);
    float overshoot = distance - std::max(0.0f, mob.attackRange - kAttackEntryBuffer);
    float stepSize = std::min(maxStep, overshoot);
    if (stepSize <= 0.0f)
    {
        return std::nullopt;
    }

    // 9) New position; record speed for client interpolation before computing final pos
    const float chaseSpeed = (aiConfig_.chaseMovementInterval > 0.0f)
                                 ? stepSize / aiConfig_.chaseMovementInterval
                                 : stepSize;
    float newX = mob.position.positionX + dx * stepSize;
    float newY = mob.position.positionY + dy * stepSize;

    // 8+1) Validate collisions (skip zone bounds).
    // If the direct path is blocked by another mob, try progressively wider
    // deflection angles so mobs steer around each other instead of stacking.
    if (!isValidPositionForChase(newX, newY, otherMobs, mob, params))
    {
        // Angles tried in order of increasing deviation so the mob always
        // picks the least deflection that clears the obstacle.
        static const float kDeflectAngles[] = {
            static_cast<float>(M_PI) / 6.0f,  //  30°
            -static_cast<float>(M_PI) / 6.0f, // -30°
            static_cast<float>(M_PI) / 3.0f,  //  60°
            -static_cast<float>(M_PI) / 3.0f, // -60°
            static_cast<float>(M_PI) / 2.0f,  //  90°
            -static_cast<float>(M_PI) / 2.0f, // -90°
        };

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

    float stepSize = params.baseSpeedMax;

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
