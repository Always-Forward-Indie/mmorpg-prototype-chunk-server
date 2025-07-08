#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <optional>
#include <random>
#include <shared_mutex>
#include <vector>

// Forward declarations
class MobInstanceManager;
class SpawnZoneManager;

/**
 * @brief Manages mob movement within zones
 *
 * Handles AI movement, pathfinding, collision detection,
 * and movement synchronization for mobs within their spawn zones.
 */
class MobMovementManager
{
  public:
    MobMovementManager(Logger &logger);

    /**
     * @brief Set dependencies
     */
    void setMobInstanceManager(MobInstanceManager *mobInstanceManager);
    void setSpawnZoneManager(SpawnZoneManager *spawnZoneManager);

    /**
     * @brief Move all mobs in a specific zone
     *
     * @param zoneId Zone identifier
     * @return true if any mob moved, false otherwise
     */
    bool moveMobsInZone(int zoneId);

    /**
     * @brief Move a specific mob
     *
     * @param mobUID Mob unique identifier
     * @param zoneId Zone where mob is located
     * @return true if movement was successful
     */
    bool moveSingleMob(int mobUID, int zoneId);

    /**
     * @brief Set movement parameters for zone
     *
     * @param zoneId Zone identifier
     * @param params Movement parameters
     */
    void setZoneMovementParams(int zoneId, const MobMovementParams &params);

  private:
    Logger &logger_;
    MobInstanceManager *mobInstanceManager_;
    SpawnZoneManager *spawnZoneManager_;

    // Random number generator
    std::mt19937 rng_;
    mutable std::shared_mutex mutex_;

    // Movement parameters per zone
    std::map<int, MobMovementParams> zoneMovementParams_;

    // Movement data per mob
    std::map<int, MobMovementData> mobMovementData_;

    /**
     * @brief Calculate new position for mob
     *
     * @param mob Mob data
     * @param zone Zone data
     * @param otherMobs Other mobs in zone for collision detection
     * @return New position and direction, or nullopt if no valid movement
     */
    std::optional<MobMovementResult> calculateNewPosition(
        const MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const std::vector<MobDataStruct> &otherMobs);

    /**
     * @brief Check if position is valid (within bounds, no collisions)
     */
    bool isValidPosition(
        float x, float y, const SpawnZoneStruct &zone, const std::vector<MobDataStruct> &otherMobs, const MobDataStruct &currentMob);

    /**
     * @brief Get default movement parameters for zone
     */
    MobMovementParams getDefaultMovementParams();

    /**
     * @brief Get movement data for specific mob
     */
    MobMovementData getMobMovementData(int mobUID);

    /**
     * @brief Update movement data for specific mob
     */
    void updateMobMovementData(int mobUID, const MobMovementData &data);
};
