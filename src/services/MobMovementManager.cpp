#include "services/MobMovementManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

MobMovementManager::MobMovementManager(Logger &logger)
    : logger_(logger),
      mobInstanceManager_(nullptr),
      spawnZoneManager_(nullptr),
      rng_(std::random_device{}())
{
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

    logger_.log("[DEBUG] Zone " + std::to_string(zoneId) + " - Current time: " + std::to_string(currentTime) +
                ", Found " + std::to_string(mobsInZone.size()) + " mobs");

    for (auto &mob : mobsInZone)
    {
        // Skip dead mobs
        if (mob.isDead || mob.currentHealth <= 0)
        {
            logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " is dead, skipping");
            continue;
        }

        // Get movement data for this mob
        auto movementData = getMobMovementData(mob.uid);

        // Initialize movement timing if needed
        if (movementData.nextMoveTime == 0.0f)
        {
            std::uniform_real_distribution<float> initialDelay(0.0f, params.initialDelayMax);
            std::uniform_real_distribution<float> moveTime(params.moveTimeMin, params.moveTimeMax);
            movementData.nextMoveTime = currentTime + initialDelay(rng_) + moveTime(rng_);
            updateMobMovementData(mob.uid, movementData);
            logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " initialized next move time: " +
                        std::to_string(movementData.nextMoveTime));
        }

        // Check if it's time to move
        if (currentTime < movementData.nextMoveTime)
        {
            logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " not ready to move. Current: " +
                        std::to_string(currentTime) + ", Next: " + std::to_string(movementData.nextMoveTime));
            continue;
        }

        logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " is ready to move");

        // Calculate new position
        logger_.log("[DEBUG] Calculating new position for mob UID: " + std::to_string(mob.uid));
        auto movementResult = calculateNewPosition(mob, zone, mobsInZone);
        if (movementResult && movementResult->validMovement)
        {
            // Update movement data
            movementData.movementDirectionX = movementResult->newDirectionX;
            movementData.movementDirectionY = movementResult->newDirectionY;

            // Update next move time
            std::uniform_real_distribution<float> speedTime(params.speedTimeMin, params.speedTimeMax);
            movementData.nextMoveTime = currentTime + std::max(speedTime(rng_) / movementData.speedMultiplier, 7.0f);

            // Random cooldown
            std::uniform_real_distribution<float> randFactor(0.85f, 1.2f);
            if (randFactor(rng_) > 1.15f)
            {
                std::uniform_real_distribution<float> cooldown(params.cooldownMin, params.cooldownMax);
                movementData.nextMoveTime += cooldown(rng_) * 0.5f;
            }

            // Store updated movement data
            updateMobMovementData(mob.uid, movementData);

            // Update position in MobInstanceManager
            mobInstanceManager_->updateMobPosition(mob.uid, movementResult->newPosition);

            logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                        " moved to (" + std::to_string(movementResult->newPosition.positionX) + ", " +
                        std::to_string(movementResult->newPosition.positionY) + ") with rotation " +
                        std::to_string(movementResult->newPosition.rotationZ));

            anyMobMoved = true;
        }
        else
        {
            logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " skipped move - no valid position found");
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

    // Calculate new position
    auto movementResult = calculateNewPosition(mob, zone, mobsInZone);
    if (movementResult && movementResult->validMovement)
    {
        // Update movement data
        auto movementData = getMobMovementData(mobUID);
        movementData.movementDirectionX = movementResult->newDirectionX;
        movementData.movementDirectionY = movementResult->newDirectionY;
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
    auto movementData = getMobMovementData(mob.uid);

    logger_.log("[DEBUG] Mob UID: " + std::to_string(mob.uid) + " current pos: (" +
                std::to_string(mob.position.positionX) + ", " + std::to_string(mob.position.positionY) + ")");
    logger_.log("[DEBUG] Movement data - stepMultiplier: " + std::to_string(movementData.stepMultiplier) +
                ", directionX: " + std::to_string(movementData.movementDirectionX) +
                ", directionY: " + std::to_string(movementData.movementDirectionY));

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

MobMovementParams
MobMovementManager::getDefaultMovementParams()
{
    return MobMovementParams{}; // Use default values from struct
}

MobMovementData
MobMovementManager::getMobMovementData(int mobUID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = mobMovementData_.find(mobUID);
    if (it != mobMovementData_.end())
    {
        return it->second;
    }
    return MobMovementData{}; // Return default values
}

void
MobMovementManager::updateMobMovementData(int mobUID, const MobMovementData &data)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    mobMovementData_[mobUID] = data;
}
