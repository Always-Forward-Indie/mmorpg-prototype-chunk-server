#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <map>
#include <shared_mutex>
#include <unordered_map>

// Forward declarations
class LootManager;
class EventQueue;

/**
 * @brief Result of mob health update operation
 */
struct MobHealthUpdateResult
{
    bool success;        // Operation successful
    bool mobDied;        // Mob died from this update
    bool wasAlreadyDead; // Mob was already dead
    int newHealth = 0;   // Health after update
    int currentMana = 0; // Mana at time of update
};

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
     * @brief Get lightweight {uid, position} pairs for all mobs in a zone.
     *
     * Use instead of getMobInstancesInZone when only position/uid data is needed
     * (e.g. for collision detection), to avoid deep-copying attribute and skill vectors.
     */
    std::vector<std::pair<int, PositionStruct>> getMobPositionsInZone(int zoneId) const;

    /**
     * @brief Apply attributes to all live instances matching mob_id.
     *        Called after setMobsAttributes arrives from game-server so that
     *        already-spawned instances gain their attribute values.
     *
     * @param attrs Flat list of MobAttributeStruct (each carries mob_id)
     */
    void applyBulkAttributes(const std::vector<MobAttributeStruct> &attrs);

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
     * @return MobHealthUpdateResult with operation details
     */
    MobHealthUpdateResult updateMobHealth(int mobUID, int health);

    /**
     * @brief Atomically subtract damage from mob HP (no stale-read race).
     *        Fires death/loot event internally if the mob dies.
     */
    MobHealthUpdateResult applyDamageToMob(int mobUID, int damageAmount);

    /**
     * @brief Atomically add healing to mob HP, clamped to maxHealth.
     */
    MobHealthUpdateResult applyHealToMob(int mobUID, int healAmount);

    /**
     * @brief Atomically subtract mana cost, clamped to 0. Returns new mana value.
     */
    int applyManaCostToMob(int mobUID, int costAmount);

    /**
     * @brief Atomically check-and-deduct mana. Returns true and deducts if current >= amount; false otherwise.
     */
    bool trySpendMana(int mobUID, int amount);

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
     * @brief Get all alive mob instances within a given radius of a position.
     *
     * @param centerX  World X coordinate of the AoE center
     * @param centerY  World Y coordinate of the AoE center
     * @param radius   Search radius in world units
     * @return Vector of mob instances (alive, within radius)
     */
    std::vector<MobDataStruct> getMobsInRange(float centerX, float centerY, float radius) const;

    /**
     * @brief Get count of alive mobs in zone
     *
     * @param zoneId Zone identifier
     * @return Number of alive mobs in the zone
     */
    int getAliveMobCountInZone(int zoneId) const;

    /**
     * @brief Set event queue for mob death events
     *
     * @param eventQueue Event queue to send MOB_LOOT_GENERATION events
     */
    void setEventQueue(EventQueue *eventQueue);

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Event queue for sending mob death events
    EventQueue *eventQueue_;

    // Store active mob instances by UID
    std::unordered_map<int, MobDataStruct> mobInstances_;

    // Index by zone for faster zone-based queries
    std::map<int, std::vector<int>> mobsByZone_;

    // Throttle map for position log spam prevention.
    // Protected by mutex_ (already held in updateMobPosition).
    std::unordered_map<int, float> positionLogThrottleMap_;

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
