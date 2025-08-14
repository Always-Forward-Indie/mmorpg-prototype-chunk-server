#include "services/MobMovementManager.hpp"
#include "data/CombatStructs.hpp"
#include "events/Event.hpp"
#include "events/EventData.hpp"
#include "events/EventQueue.hpp"
#include "services/CharacterManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <unordered_set>

MobMovementManager::MobMovementManager(Logger &logger)
    : logger_(logger),
      mobInstanceManager_(nullptr),
      spawnZoneManager_(nullptr),
      characterManager_(nullptr),
      eventQueue_(nullptr),
      rng_(std::random_device{}())
{
    // Initialize AI config with default values (already set in struct)
    logger_.log("[INFO] MobMovementManager initialized with default AI configuration");
}

void
MobMovementManager::setMobInstanceManager(MobInstanceManager *mobInstanceManager)
{
    mobInstanceManager_ = mobInstanceManager;
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
}

void
MobMovementManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
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
        logger_.logError("MobMovementManager: Dependencies not set");
        return false;
    }

    // Get zone data
    auto zone = spawnZoneManager_->getMobSpawnZoneByID(zoneId);
    if (zone.zoneId == 0)
    {
        logger_.logError("MobMovementManager: Zone " + std::to_string(zoneId) + " not found");
        return false;
    }

    // Get mobs in zone
    auto mobsInZone = mobInstanceManager_->getMobInstancesInZone(zoneId);
    if (mobsInZone.empty())
    {
        return false; // No mobs to move
    }

    // Get movement parameters for zone
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto paramIt = zoneMovementParams_.find(zoneId);
        if (paramIt != zoneMovementParams_.end())
        {
            params = paramIt->second;
        }
    }

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
            handlePlayerAggro(const_cast<MobDataStruct &>(mob), zone, movementData);
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
                logger_.log("[DEBUG] Fixed non-aggressive mob UID: " + std::to_string(mob.uid) + " movement timing");
            }
        }

        // Update combat state for this mob
        updateMobCombatState(const_cast<MobDataStruct &>(mob), movementData, currentTime);

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
        bool hasTarget = (movementData.targetPlayerId > 0 || movementData.isReturningToSpawn);
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

        // Get fresh movement data (it might have been updated by handleMobAttacked)
        movementData = getMobMovementDataInternal(mob.uid);

        // Debug: Log movement state for troubleshooting
        static std::unordered_set<int> debugLoggedMobs;
        if (debugLoggedMobs.find(mob.uid) == debugLoggedMobs.end())
        {
            logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) +
                        " state - isReturning: " + (movementData.isReturningToSpawn ? "true" : "false") +
                        ", targetId: " + std::to_string(movementData.targetPlayerId) +
                        ", nextMoveTime: " + std::to_string(movementData.nextMoveTime) +
                        ", currentTime: " + std::to_string(currentTime));
            debugLoggedMobs.insert(mob.uid);
        }

        // Calculate new position based on mob behavior
        std::optional<MobMovementResult> movementResult;

        if (movementData.isReturningToSpawn)
        {
            // Return to spawn zone
            movementResult = calculateReturnToSpawnMovement(mob, zone, mobsInZone, movementData.spawnPosition);
        }
        else if (movementData.targetPlayerId > 0 && characterManager_)
        {
            // Chase target player
            movementResult = calculateChaseMovement(mob, zone, mobsInZone, movementData.targetPlayerId);
        }
        else
        {
            // Normal random movement
            movementResult = calculateNewPosition(mob, zone, mobsInZone);

            // Debug log for mobs that just returned to spawn
            static std::unordered_set<int> loggedMobs;
            if (loggedMobs.find(mob.uid) == loggedMobs.end())
            {
                logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " starting normal patrol mode");
                loggedMobs.insert(mob.uid);
            }
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
                // Normal movement timing
                std::uniform_real_distribution<float> speedTime(params.speedTimeMin, params.speedTimeMax);
                movementData.nextMoveTime = currentTime + std::max(speedTime(rng_) / movementData.speedMultiplier, 7.0f);

                // Random cooldown
                std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
                if (randFactor(rng_) > 1.15f)
                {
                    std::uniform_real_distribution<float> cooldown(params.cooldownMin, params.cooldownMax);
                    movementData.nextMoveTime += cooldown(rng_) * 0.5f;
                }
            }

            // Store updated movement data
            updateMobMovementData(mob.uid, movementData);

            // Update position in MobInstanceManager
            mobInstanceManager_->updateMobPosition(mob.uid, movementResult->newPosition);

            anyMobMoved = true;
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

    // Get all mobs in zone for collision detection
    auto mobsInZone = mobInstanceManager_->getMobInstancesInZone(zoneId);

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
        handlePlayerAggro(const_cast<MobDataStruct &>(mob), zone, movementData);
        // Refresh movement data after potential aggro changes
        movementData = getMobMovementData(mobUID);
    }

    // Update combat state for this mob
    float currentTime = getCurrentGameTime();
    updateMobCombatState(const_cast<MobDataStruct &>(mob), movementData, currentTime);

    // Check if mob can perform actions (movement) based on combat state
    if (!canPerformAction(movementData, currentTime))
    {
        // Mob is in a state where it shouldn't move (preparing attack, attacking, cooldown)
        return false;
    }

    // Check if it's time to move this mob (respect timing constraints)
    bool hasTarget = (movementData.targetPlayerId > 0 || movementData.isReturningToSpawn);
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

    if (movementData.isReturningToSpawn)
    {
        movementResult = calculateReturnToSpawnMovement(mob, zone, mobsInZone, movementData.spawnPosition);
    }
    else if (movementData.targetPlayerId > 0 && characterManager_)
    {
        movementResult = calculateChaseMovement(mob, zone, mobsInZone, movementData.targetPlayerId);
    }
    else
    {
        // Normal random movement
        movementResult = calculateNewPosition(mob, zone, mobsInZone);
    }

    if (movementResult && movementResult->validMovement)
    {
        // Update movement data
        movementData.movementDirectionX = movementResult->newDirectionX;
        movementData.movementDirectionY = movementResult->newDirectionY;

        // Update movement timing
        movementData.lastMoveTime = currentTime;

        // Update next move time based on mob behavior
        auto params = getDefaultMovementParams();
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
            // Normal movement timing
            std::uniform_real_distribution<float> speedTime(params.speedTimeMin, params.speedTimeMax);
            movementData.nextMoveTime = currentTime + std::max(speedTime(rng_) / movementData.speedMultiplier, 7.0f);
        }

        updateMobMovementData(mobUID, movementData);

        // Update position in MobInstanceManager
        mobInstanceManager_->updateMobPosition(mobUID, movementResult->newPosition);

        return true;
    }

    return false;
}

std::optional<MobMovementResult>
MobMovementManager::calculateNewPosition(
    const MobDataStruct &mob,
    const SpawnZoneStruct &zone,
    const std::vector<MobDataStruct> &otherMobs)
{
    // Get movement parameters for zone
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto paramIt = zoneMovementParams_.find(zone.zoneId);
        if (paramIt != zoneMovementParams_.end())
        {
            params = paramIt->second;
        }
    }

    // Get movement data for this mob
    auto movementData = getMobMovementDataInternal(mob.uid);

    // Calculate zone boundaries
    float minX = zone.posX - (zone.sizeX / 2.0f);
    float maxX = zone.posX + (zone.sizeX / 2.0f);
    float minY = zone.posY - (zone.sizeY / 2.0f);
    float maxY = zone.posY + (zone.sizeY / 2.0f);

    // Check if mob is at border
    float borderThreshold = std::max(zone.sizeX, zone.sizeY) * params.borderThresholdPercent;
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
    float maxStepSize = std::min((zone.sizeX + zone.sizeY) * params.maxStepSizePercent, params.maxStepSizeAbsolute);
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
            float angleToCenter = atan2(zone.posY - mob.position.positionY,
                zone.posX - mob.position.positionX);
            std::uniform_real_distribution<float> borderAngle(params.borderAngleMin, params.borderAngleMax);
            newAngle = angleToCenter + (borderAngle(rng_) * (M_PI / 180.0f));
        }
        else
        {
            // Random direction
            std::uniform_real_distribution<float> randAngle(0.0f, 360.0f);
            newAngle = randAngle(rng_) * (M_PI / 180.0f);
        }

        float tempDirectionX = cos(newAngle);
        float tempDirectionY = sin(newAngle);
        float testNewX = mob.position.positionX + (tempDirectionX * stepSize);
        float testNewY = mob.position.positionY + (tempDirectionY * stepSize);

        // Check if position is valid
        if (isValidPosition(testNewX, testNewY, zone, otherMobs, mob))
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
    if (!isValidPosition(newX, newY, zone, otherMobs, mob))
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

    return result;
}

bool
MobMovementManager::isValidPosition(
    float x, float y, const SpawnZoneStruct &zone, const std::vector<MobDataStruct> &otherMobs, const MobDataStruct &currentMob)
{
    // Check zone boundaries
    float minX = zone.posX - (zone.sizeX / 2.0f);
    float maxX = zone.posX + (zone.sizeX / 2.0f);
    float minY = zone.posY - (zone.sizeY / 2.0f);
    float maxY = zone.posY + (zone.sizeY / 2.0f);

    if (x < minX || x > maxX || y < minY || y > maxY)
    {
        return false;
    }

    // Check collision with other mobs
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto paramIt = zoneMovementParams_.find(zone.zoneId);
        if (paramIt != zoneMovementParams_.end())
        {
            params = paramIt->second;
        }
    }

    for (const auto &otherMob : otherMobs)
    {
        if (otherMob.uid == currentMob.uid)
            continue;

        float distance = sqrt(pow(x - otherMob.position.positionX, 2) +
                              pow(y - otherMob.position.positionY, 2));
        if (distance < params.minSeparationDistance)
        {
            return false;
        }
    }

    return true;
}

bool
MobMovementManager::isValidPositionForChase(
    float x, float y, const std::vector<MobDataStruct> &otherMobs, const MobDataStruct &currentMob)
{
    // For chase movement, we only check collision with other mobs, not zone boundaries
    auto params = getDefaultMovementParams();

    for (const auto &otherMob : otherMobs)
    {
        if (otherMob.uid == currentMob.uid)
            continue;

        float distance = sqrt(pow(x - otherMob.position.positionX, 2) +
                              pow(y - otherMob.position.positionY, 2));
        if (distance < params.minSeparationDistance)
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
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it == mobMovementData_.end())
    {
        // First time, always send
        MobMovementData newData;
        newData.lastSentPosition = currentPosition;
        mobMovementData_[mobUID] = newData;
        return true;
    }

    // Check if mob moved far enough from last sent position
    float distance = calculateDistance(currentPosition, it->second.lastSentPosition);
    if (distance >= it->second.minimumMoveDistance)
    {
        // Update last sent position
        it->second.lastSentPosition = currentPosition;

        // Log movement updates for mobs in combat states
        if (it->second.combatState != MobCombatState::PATROLLING)
        {
            const char *stateNames[] = {"PATROLLING", "CHASING", "PREPARING_ATTACK", "ATTACKING", "ATTACK_COOLDOWN", "RETURNING"};
            int stateIndex = static_cast<int>(it->second.combatState);
            if (stateIndex >= 0 && stateIndex < 6)
            {
                logger_.log("[MOVEMENT] Sending position update for mob " + std::to_string(mobUID) +
                            " (state: " + stateNames[stateIndex] + ", distance moved: " +
                            std::to_string(distance) + ")");
            }
        }

        return true;
    }

    return false;
}

void
MobMovementManager::forceMobStateUpdate(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        // Reset the last sent position to force next update
        it->second.lastSentPosition.positionX = -999999.0f;
        it->second.lastSentPosition.positionY = -999999.0f;

        const char *stateNames[] = {"PATROLLING", "CHASING", "PREPARING_ATTACK", "ATTACKING", "ATTACK_COOLDOWN", "RETURNING"};
        int stateIndex = static_cast<int>(it->second.combatState);
        if (stateIndex >= 0 && stateIndex < 6)
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
MobMovementManager::handleMobAttacked(int mobUID, int attackerPlayerId)
{
    if (!characterManager_)
    {
        logger_.logError("CharacterManager not set when handling mob attack");
        return;
    }

    auto movementData = getMobMovementDataInternal(mobUID);
    movementData.targetPlayerId = attackerPlayerId;
    movementData.isReturningToSpawn = false;

    // Force immediate movement by resetting next move time
    movementData.nextMoveTime = getCurrentGameTime();

    updateMobMovementData(mobUID, movementData);

    logger_.log("[INFO] Mob UID: " + std::to_string(mobUID) + " is now targeting player " + std::to_string(attackerPlayerId));
}

void
MobMovementManager::handlePlayerAggro(MobDataStruct &mob, const SpawnZoneStruct &zone, MobMovementData &movementData)
{
    if (!characterManager_)
        return;

    // 1) Проверяем валидность текущей цели (если есть)
    if (movementData.targetPlayerId > 0)
    {
        auto currentTarget = characterManager_->getCharacterById(movementData.targetPlayerId);
        if (currentTarget.characterId > 0)
        {
            float distanceToTarget = calculateDistance(mob.position, currentTarget.characterPosition);
            float maxChaseDistance = aiConfig_.aggroRange * aiConfig_.chaseDistanceMultiplier;

            // Если цель слишком далеко, сбрасываем и начинаем возвращение
            if (distanceToTarget > maxChaseDistance)
            {
                int lostTargetId = movementData.targetPlayerId; // Save target ID before clearing
                movementData.targetPlayerId = 0;
                movementData.isReturningToSpawn = true;
                updateMobMovementData(mob.uid, movementData);

                // Send target lost event to clients
                sendMobTargetLost(mob, lostTargetId);

                logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                            " lost target (distance " + std::to_string(distanceToTarget) +
                            " > " + std::to_string(maxChaseDistance) + "), returning to spawn");
                return;
            }
        }
        else
        {
            // Цель исчезла — сразу возвращаемся
            int lostTargetId = movementData.targetPlayerId; // Save target ID before clearing
            movementData.targetPlayerId = 0;
            movementData.isReturningToSpawn = true;
            updateMobMovementData(mob.uid, movementData);

            // Send target lost event to clients
            sendMobTargetLost(mob, lostTargetId);

            logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                        " target died, returning to spawn");
            return;
        }
    }

    // 3) Если нет цели и мы не в состоянии возврата — ищем новую
    if (movementData.targetPlayerId == 0 && !movementData.isReturningToSpawn)
    {
        auto nearbyPlayers = characterManager_->getCharactersInZone(
            mob.position.positionX,
            mob.position.positionY,
            aiConfig_.aggroRange);

        if (!nearbyPlayers.empty() && canSearchNewTargets(mob.position, zone))
        {
            float closestDistance = aiConfig_.aggroRange + 1.0f;
            int closestPlayerId = 0;

            for (const auto &player : nearbyPlayers)
            {
                float d = calculateDistance(mob.position, player.characterPosition);
                if (d < closestDistance)
                {
                    closestDistance = d;
                    closestPlayerId = player.characterId;
                }
            }

            if (closestPlayerId > 0)
            {
                movementData.targetPlayerId = closestPlayerId;
                movementData.isReturningToSpawn = false;
                updateMobMovementData(mob.uid, movementData);

                logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                            " found new target: " + std::to_string(closestPlayerId));
            }
        }
    }
}

std::optional<MobMovementResult>
MobMovementManager::calculateChaseMovement(
    const MobDataStruct &mob,
    const SpawnZoneStruct &zone,
    const std::vector<MobDataStruct> &otherMobs,
    int targetPlayerId)
{
    if (!characterManager_)
    {
        logger_.logError("CharacterManager not available in calculateChaseMovement");
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
    float maxChaseDistance = aiConfig_.aggroRange * aiConfig_.chaseDistanceMultiplier;
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

        logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                    " too far from spawn zone, returning");
        return std::nullopt;
    }

    constexpr float ATTACK_BUFFER = 10.0f;

    // 4) If within attack range → stop movement (combat state system will handle attacks)
    static std::unordered_set<int> inRangeSet;
    if (distance <= aiConfig_.attackRange + ATTACK_BUFFER)
    {
        if (inRangeSet.insert(mob.uid).second)
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
        inRangeSet.erase(mob.uid);
    }

    // 5) Avoid jitter if extremely close
    if (distance < 1.0f)
    {
        return std::nullopt;
    }

    // 6) Normalize direction
    dx /= distance;
    dy /= distance;

    // 7) Load movement parameters
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = zoneMovementParams_.find(zone.zoneId);
        if (it != zoneMovementParams_.end())
            params = it->second;
    }

    // 8) Compute step size limited by attackRange
    float maxStep = std::min(params.baseSpeedMax * 1.5f, params.maxStepSizeAbsolute);
    float overshoot = distance - aiConfig_.attackRange;
    float stepSize = std::min(maxStep, overshoot);
    if (stepSize <= 0.0f)
    {
        return std::nullopt;
    }

    // 9) New position
    float newX = mob.position.positionX + dx * stepSize;
    float newY = mob.position.positionY + dy * stepSize;

    // 10) Validate collisions (skip zone bounds)
    if (!isValidPositionForChase(newX, newY, otherMobs, mob))
    {
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
    return result;
}

std::optional<MobMovementResult>
MobMovementManager::calculateReturnToSpawnMovement(
    const MobDataStruct &mob,
    const SpawnZoneStruct & /*zone*/,
    const std::vector<MobDataStruct> & /*otherMobs*/,
    const PositionStruct &spawnPosition)
{
    // Вектор к точке спавна
    float dx = spawnPosition.positionX - mob.position.positionX;
    float dy = spawnPosition.positionY - mob.position.positionY;
    float dist = std::sqrt(dx * dx + dy * dy);

    // Получаем параметры движения
    auto params = getDefaultMovementParams();
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = zoneMovementParams_.find(mob.zoneId);
        if (it != zoneMovementParams_.end())
            params = it->second;
    }
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
    return result;
}

bool
MobMovementManager::canAttackPlayer(const MobDataStruct &mob, int targetPlayerId, const MobMovementData &movementData)
{
    if (!characterManager_)
    {
        return false;
    }

    // Check attack cooldown
    float currentTime = getCurrentGameTime();
    float timeSinceLastAttack = currentTime - movementData.lastAttackTime;

    if (timeSinceLastAttack < movementData.attackCooldown)
    {
        return false;
    }

    // Check if player exists and is in attack range
    auto targetPlayer = characterManager_->getCharacterById(targetPlayerId);
    if (targetPlayer.characterId == 0)
    {
        return false;
    }

    float distance = calculateDistance(mob.position, targetPlayer.characterPosition);
    return distance <= movementData.attackRange;
}

void
MobMovementManager::executeMobAttack(const MobDataStruct &mob, int targetPlayerId, MobMovementData &movementData)
{
    // Update attack time
    movementData.lastAttackTime = getCurrentGameTime();
    updateMobMovementData(mob.uid, movementData);

    // Get target player
    if (!characterManager_)
    {
        return;
    }

    auto targetPlayer = characterManager_->getCharacterById(targetPlayerId);
    if (targetPlayer.characterId == 0)
    {
        return;
    }

    // Calculate damage based on mob level (simple calculation)
    int baseDamage = 10 + (mob.level * 5);
    int damage = baseDamage + (rand() % (baseDamage / 2)); // Add some randomness

    // Apply damage to player
    int newHealth = std::max(0, targetPlayer.characterCurrentHealth - damage);
    characterManager_->updateCharacterHealth(targetPlayerId, newHealth);

    logger_.log("=== [COMBAT] === Mob UID: " + std::to_string(mob.uid) +
                " attacks player " + std::to_string(targetPlayerId) +
                " for " + std::to_string(damage) + " damage. Player health: " +
                std::to_string(newHealth) + "/" + std::to_string(targetPlayer.characterMaxHealth) + " ===");

    // Create and send combat result event
    if (eventQueue_)
    {
        CombatResultStruct result;
        result.casterId = mob.uid; // Mob as attacker
        result.targetId = targetPlayerId;
        result.actionId = 0; // Basic attack
        result.targetType = CombatTargetType::PLAYER;

        result.damageDealt = damage;
        result.healingDone = 0;

        result.isCritical = false; // TODO: Implement critical hits for mobs
        result.isBlocked = false;
        result.isDodged = false;
        result.isResisted = false;

        result.remainingHealth = newHealth;
        result.remainingMana = targetPlayer.characterCurrentMana;

        result.effectsApplied = "{}"; // Empty JSON for now
        result.isDamaged = (damage > 0);
        result.targetDied = (newHealth <= 0);

        // Create EventData directly with CombatResultStruct
        EventData eventData = result;

        // Create event
        Event combatEvent(Event::COMBAT_RESULT, targetPlayerId, eventData);

        // Add event to queue for processing
        eventQueue_->push(combatEvent);

        logger_.log("[INFO] Combat result event sent for mob " + std::to_string(mob.uid) +
                    " attacking player " + std::to_string(targetPlayerId));
    }
    else
    {
        logger_.logError("EventQueue not set - cannot send combat result event");
    }
}

void
MobMovementManager::sendMobInitiateCombatAction(const MobDataStruct &mob, int targetPlayerId)
{
    if (!eventQueue_)
    {
        logger_.logError("EventQueue not set - cannot send initiate combat action");
        return;
    }

    // Create simplified combat action packet for network
    CombatActionPacket actionPacket;
    actionPacket.actionId = 0; // Basic mob attack
    actionPacket.actionName = "Mob Basic Attack";
    actionPacket.actionType = CombatActionType::BASIC_ATTACK;
    actionPacket.targetType = CombatTargetType::PLAYER;
    actionPacket.casterId = mob.uid;
    actionPacket.targetId = targetPlayerId;

    // Create and send event - broadcast to all clients
    EventData eventData = actionPacket;
    Event combatEvent(Event::INITIATE_COMBAT_ACTION, 0, eventData); // clientID = 0 means broadcast
    eventQueue_->push(combatEvent);

    logger_.log("[COMBAT] Initiate combat action event sent - Mob " + std::to_string(mob.uid) +
                " targeting player " + std::to_string(targetPlayerId));
}

void
MobMovementManager::sendMobCombatAnimation(const MobDataStruct &mob, int targetPlayerId)
{
    if (!eventQueue_)
    {
        logger_.logError("EventQueue not set - cannot send combat animation");
        return;
    }

    // Create simplified combat animation packet for network
    CombatAnimationPacket animationPacket;
    animationPacket.characterId = mob.uid;
    animationPacket.animationName = "mob_attack_basic";
    animationPacket.duration = 1.0f;
    animationPacket.isLooping = false;

    // Create and send event - broadcast to all clients
    EventData eventData = animationPacket;
    Event animationEvent(Event::COMBAT_ANIMATION, 0, eventData); // clientID = 0 means broadcast
    eventQueue_->push(animationEvent);

    logger_.log("[COMBAT] Combat animation event sent - Mob " + std::to_string(mob.uid) +
                " playing attack animation");
}

void
MobMovementManager::sendMobTargetLost(const MobDataStruct &mob, int lostTargetPlayerId)
{
    if (!eventQueue_)
    {
        logger_.logError("EventQueue not set - cannot send mob target lost event");
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
    logger_.log("[INFO] MobMovementManager: AI configuration updated");
}

void
MobMovementManager::initializeMobMovementData(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (mobMovementData_.find(mobUID) == mobMovementData_.end())
    {
        MobMovementData newData;
        // Initialize with AI config values
        newData.aggroRange = aiConfig_.aggroRange;
        newData.attackRange = aiConfig_.attackRange;
        newData.attackCooldown = aiConfig_.attackCooldown;
        newData.minimumMoveDistance = aiConfig_.minimumMoveDistance;

        // Initialize combat state
        newData.combatState = MobCombatState::PATROLLING;
        newData.stateChangeTime = getCurrentGameTime();

        mobMovementData_[mobUID] = newData;
    }
}

void
MobMovementManager::updateMobCombatState(MobDataStruct &mob, MobMovementData &movementData, float currentTime)
{
    if (!characterManager_)
        return;

    float timeSinceStateChange = currentTime - movementData.stateChangeTime;

    switch (movementData.combatState)
    {
    case MobCombatState::PATROLLING:
        // Transition to chasing if target found
        if (movementData.targetPlayerId > 0)
        {
            movementData.combatState = MobCombatState::CHASING;
            movementData.stateChangeTime = currentTime;
            updateMobMovementData(mob.uid, movementData); // Save state change
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " entering CHASING state");
        }
        break;

    case MobCombatState::CHASING:
        if (movementData.targetPlayerId == 0)
        {
            // Lost target, return to patrolling or returning
            if (movementData.isReturningToSpawn)
            {
                movementData.combatState = MobCombatState::RETURNING;
            }
            else
            {
                movementData.combatState = MobCombatState::PATROLLING;
            }
            movementData.stateChangeTime = currentTime;
            updateMobMovementData(mob.uid, movementData); // Save state change
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " lost target, returning to patrol");
        }
        else
        {
            // Check if target is still valid and alive
            auto targetPlayer = characterManager_->getCharacterById(movementData.targetPlayerId);
            if (targetPlayer.characterId == 0)
            {
                // Target no longer exists, clear target and return to patrolling
                int lostTargetId = movementData.targetPlayerId; // Save target ID before clearing
                movementData.targetPlayerId = 0;
                movementData.combatState = MobCombatState::PATROLLING;
                movementData.stateChangeTime = currentTime;
                updateMobMovementData(mob.uid, movementData);

                // Send target lost event to clients
                sendMobTargetLost(mob, lostTargetId);

                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " target no longer exists, returning to patrol");
            }
            else
            {
                // Check if we've been chasing too long (add timeout to prevent infinite chase)
                float timeSinceChasing = currentTime - movementData.stateChangeTime;
                const float maxChaseTime = 30.0f; // 30 seconds timeout
                if (timeSinceChasing > maxChaseTime)
                {
                    // Chase timeout, give up and return to spawn
                    int lostTargetId = movementData.targetPlayerId; // Save target ID before clearing
                    movementData.targetPlayerId = 0;
                    movementData.combatState = MobCombatState::RETURNING;
                    movementData.stateChangeTime = currentTime;
                    movementData.isReturningToSpawn = true;
                    updateMobMovementData(mob.uid, movementData);

                    // Send target lost event to clients
                    sendMobTargetLost(mob, lostTargetId);

                    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " chase timeout, returning to spawn");
                }
                else
                {
                    // Check if in attack range
                    float distance = calculateDistance(mob.position, targetPlayer.characterPosition);
                    if (distance <= aiConfig_.attackRange)
                    {
                        // Send initiate combat action event - mob enters combat
                        sendMobInitiateCombatAction(mob, movementData.targetPlayerId);

                        // Enter prepare attack state
                        movementData.combatState = MobCombatState::PREPARING_ATTACK;
                        movementData.stateChangeTime = currentTime;
                        updateMobMovementData(mob.uid, movementData); // Save state change
                        forceMobStateUpdate(mob.uid);                 // Force state update to clients
                        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " initiated combat, preparing to attack player " + std::to_string(movementData.targetPlayerId) + " (distance: " + std::to_string(distance) + ")");
                    }
                    else if (distance > aiConfig_.aggroRange * aiConfig_.chaseDistanceMultiplier)
                    {
                        // Target moved too far away, give up chase and return to spawn
                        int lostTargetId = movementData.targetPlayerId; // Save target ID before clearing
                        movementData.targetPlayerId = 0;
                        movementData.combatState = MobCombatState::RETURNING;
                        movementData.stateChangeTime = currentTime;
                        movementData.isReturningToSpawn = true;
                        updateMobMovementData(mob.uid, movementData);

                        // Send target lost event to clients
                        sendMobTargetLost(mob, lostTargetId);

                        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " target too far away (distance: " + std::to_string(distance) + "), returning to spawn");
                    }
                }
            }
        }
        break;

    case MobCombatState::PREPARING_ATTACK:
        if (timeSinceStateChange >= movementData.attackPrepareTime)
        {
            // Check if target is still in range and can attack
            if (movementData.targetPlayerId > 0 && canAttackPlayer(mob, movementData.targetPlayerId, movementData))
            {
                // Send combat animation event when starting attack animation
                sendMobCombatAnimation(mob, movementData.targetPlayerId);

                movementData.combatState = MobCombatState::ATTACKING;
                movementData.stateChangeTime = currentTime;
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " starting attack animation");

                updateMobMovementData(mob.uid, movementData); // Save state change
                forceMobStateUpdate(mob.uid);                 // Force state update to clients
            }
            else
            {
                // Target moved away or can't attack, check if we should continue chasing
                float timeSinceLastChase = currentTime - movementData.stateChangeTime;

                // If been trying to attack for too long, give up and return to spawn
                if (timeSinceLastChase > 10.0f) // 10 seconds timeout
                {
                    int lostTargetId = movementData.targetPlayerId; // Save target ID before clearing
                    movementData.targetPlayerId = 0;
                    movementData.combatState = MobCombatState::RETURNING;
                    movementData.stateChangeTime = currentTime;
                    movementData.isReturningToSpawn = true;

                    // Send target lost event to clients
                    sendMobTargetLost(mob, lostTargetId);

                    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " attack timeout, returning to spawn");
                }
                else
                {
                    // Return to chasing, but limit log spam
                    movementData.combatState = MobCombatState::CHASING;
                    movementData.stateChangeTime = currentTime;

                    // Only log occasionally to avoid spam
                    static std::map<int, float> lastLogTime;
                    if (currentTime - lastLogTime[mob.uid] > 2.0f) // Log max once per 2 seconds per mob
                    {
                        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " target moved away, returning to chase");
                        lastLogTime[mob.uid] = currentTime;
                    }
                }
            }
        }
        break;

    case MobCombatState::ATTACKING:
        if (timeSinceStateChange >= movementData.attackDuration)
        {
            // Attack duration finished, execute the attack now
            if (movementData.targetPlayerId > 0 && canAttackPlayer(mob, movementData.targetPlayerId, movementData))
            {
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " executing attack after animation");
                executeMobAttack(mob, movementData.targetPlayerId, movementData);
            }
            else
            {
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " attack missed - target moved away");
            }

            // Attack finished, enter cooldown
            movementData.combatState = MobCombatState::ATTACK_COOLDOWN;
            movementData.stateChangeTime = currentTime;
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " attack finished, entering cooldown");
            updateMobMovementData(mob.uid, movementData); // Save state change
            forceMobStateUpdate(mob.uid);                 // Force state update to clients
        }
        break;

    case MobCombatState::ATTACK_COOLDOWN:
        if (timeSinceStateChange >= movementData.postAttackCooldown)
        {
            // Cooldown finished, return to chasing if target still exists
            if (movementData.targetPlayerId > 0)
            {
                movementData.combatState = MobCombatState::CHASING;
                movementData.stateChangeTime = currentTime;
                updateMobMovementData(mob.uid, movementData); // Save state change
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " cooldown finished, resuming chase");
            }
            else if (movementData.isReturningToSpawn)
            {
                movementData.combatState = MobCombatState::RETURNING;
                movementData.stateChangeTime = currentTime;
                updateMobMovementData(mob.uid, movementData); // Save state change
            }
            else
            {
                movementData.combatState = MobCombatState::PATROLLING;
                movementData.stateChangeTime = currentTime;
                updateMobMovementData(mob.uid, movementData); // Save state change
            }
        }
        break;

    case MobCombatState::RETURNING:
        if (!movementData.isReturningToSpawn)
        {
            // Finished returning, back to patrolling
            movementData.combatState = MobCombatState::PATROLLING;
            movementData.stateChangeTime = currentTime;
            updateMobMovementData(mob.uid, movementData); // Save state change
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " finished returning, back to patrol");
        }
        break;
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
        return false; // No movement during attack phases
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
bool
MobMovementManager::shouldStopChasing(const PositionStruct &mobPos, const SpawnZoneStruct &zone)
{
    // Zone-based calculation using zone boundaries
    ZoneBounds bounds(zone);
    float distanceFromZoneEdge = bounds.distanceToZone(mobPos);

    // Stop chasing if too far from zone edge
    return distanceFromZoneEdge > aiConfig_.maxChaseFromZoneEdge;
}
