#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <shared_mutex>
#include <unordered_map>

/**
 * @brief Manages living mob instances in the game world
 *
 * This class handles the runtime state of individual mob instances,
 * including their health, mana, position, and other dynamic properties.
 */
class MobInstanceManager
{
  public:
    MobInstanceManager(Logger &logger);

    /**
     * @brief Register a new mob instance
     *
     * @param mobInstance The mob instance to register
     * @return true if successfully registered, false if UID already exists
     */
    bool registerMobInstance(const MobDataStruct &mobInstance);

    /**
     * @brief Unregister a mob instance (when it dies or despawns)
     *
     * @param mobUID Unique identifier of the mob instance
     */
    void unregisterMobInstance(int mobUID);

    /**
     * @brief Get mob instance by UID
     *
     * @param mobUID Unique identifier of the mob instance
     * @return MobDataStruct object, or empty struct if not found
     */
    MobDataStruct getMobInstance(int mobUID) const;

    /**
     * @brief Get all mob instances in a specific zone
     *
     * @param zoneId Zone identifier
     * @return Vector of mob instances in the zone
     */
    std::vector<MobDataStruct> getMobInstancesInZone(int zoneId) const;

    /**
     * @brief Update mob position
     *
     * @param mobUID Unique identifier of the mob instance
     * @param position New position
     * @return true if successful, false if mob not found
     */
    bool updateMobPosition(int mobUID, const PositionStruct &position);

    /**
     * @brief Update mob health
     *
     * @param mobUID Unique identifier of the mob instance
     * @param health New health value
     * @return true if successful, false if mob not found
     */
    bool updateMobHealth(int mobUID, int health);

    /**
     * @brief Update mob mana
     *
     * @param mobUID Unique identifier of the mob instance
     * @param mana New mana value
     * @return true if successful, false if mob not found
     */
    bool updateMobMana(int mobUID, int mana);

    /**
     * @brief Check if mob is alive
     *
     * @param mobUID Unique identifier of the mob instance
     * @return true if alive, false if dead or not found
     */
    bool isMobAlive(int mobUID) const;

    /**
     * @brief Mark mob as dead
     *
     * @param mobUID Unique identifier of the mob instance
     * @return true if successful, false if mob not found
     */
    bool markMobAsDead(int mobUID);

    /**
     * @brief Get all registered mob instances
     *
     * @return Map of all mob instances (UID -> MobDataStruct)
     */
    std::unordered_map<int, MobDataStruct> getAllMobInstances() const;

    /**
     * @brief Get count of alive mobs in zone
     *
     * @param zoneId Zone identifier
     * @return Number of alive mobs in the zone
     */
    int getAliveMobCountInZone(int zoneId) const;

  private:
    Logger &logger_;

    // Store active mob instances by UID
    std::unordered_map<int, MobDataStruct> mobInstances_;

    // Index by zone for faster zone-based queries
    std::map<int, std::vector<int>> mobsByZone_;

    // Mutex for thread safety
    mutable std::shared_mutex mutex_;

    /**
     * @brief Update zone index when mob changes zone
     *
     * @param mobUID Mob UID
     * @param oldZoneId Old zone ID (0 if new mob)
     * @param newZoneId New zone ID
     */
    void updateZoneIndex(int mobUID, int oldZoneId, int newZoneId);
};
