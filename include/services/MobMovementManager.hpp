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
class CharacterManager;
class EventQueue;

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

    /**
     * @brief Set reference to CharacterManager for player tracking
     */
    void setCharacterManager(class CharacterManager *characterManager);

    /**
     * @brief Set reference to EventQueue for combat events
     */
    void setEventQueue(class EventQueue *eventQueue);

    /**
     * @brief Set reference to CombatSystem for new combat integration
     */
    void setCombatSystem(class CombatSystem *combatSystem);

    /**
     * @brief Handle mob aggro and retaliation when attacked by player
     */
    void handleMobAttacked(int mobUID, int attackerPlayerId);

    /**
     * @brief Get movement data for specific mob (public version)
     */
    MobMovementData getMobMovementData(int mobUID) const;

    /**
     * @brief Check if mob movement should be sent to clients
     */
    bool shouldSendMobUpdate(int mobUID, const PositionStruct &currentPosition);

    /**
     * @brief Force sending mob state update (for combat state changes)
     */
    void forceMobStateUpdate(int mobUID);

    /**
     * @brief Set AI configuration for mobs
     */
    void setAIConfig(const MobAIConfig &config);

    /**
     * @brief Get current AI configuration
     */
    const MobAIConfig &getAIConfig() const;

  private:
    Logger &logger_;
    MobInstanceManager *mobInstanceManager_;
    SpawnZoneManager *spawnZoneManager_;
    class CharacterManager *characterManager_;
    class EventQueue *eventQueue_;
    class CombatSystem *combatSystem_;

    // Random number generator
    std::mt19937 rng_;
    mutable std::shared_mutex mutex_;

    // Movement parameters per zone
    std::map<int, MobMovementParams> zoneMovementParams_;

    // Movement data per mob
    std::map<int, MobMovementData> mobMovementData_;

    // AI configuration
    MobAIConfig aiConfig_;

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
     * @brief Check if position is valid for chase movement (no zone boundaries, only collision check)
     */
    bool isValidPositionForChase(
        float x, float y, const std::vector<MobDataStruct> &otherMobs, const MobDataStruct &currentMob);

    /**
     * @brief Get default movement parameters for zone
     */
    MobMovementParams getDefaultMovementParams();

    /**
     * @brief Update movement data for specific mob
     */
    void updateMobMovementData(int mobUID, const MobMovementData &data);

    /**
     * @brief Get movement data for specific mob (internal use)
     */
    MobMovementData getMobMovementDataInternal(int mobUID);

    /**
     * @brief Check for nearby players and handle aggro
     */
    void handlePlayerAggro(MobDataStruct &mob, const SpawnZoneStruct &zone, MobMovementData &movementData);

    /**
     * @brief Calculate movement towards target player
     */
    std::optional<MobMovementResult> calculateChaseMovement(
        const MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const std::vector<MobDataStruct> &otherMobs,
        int targetPlayerId);

    /**
     * @brief Calculate movement back to spawn zone
     */
    std::optional<MobMovementResult> calculateReturnToSpawnMovement(
        const MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const std::vector<MobDataStruct> &otherMobs,
        const PositionStruct &spawnPosition);

    /**
     * @brief Check if mob can attack target player
     */
    bool canAttackPlayer(const MobDataStruct &mob, int targetPlayerId, const MobMovementData &movementData);

    /**
     * @brief Check if target player is alive and valid
     */
    bool isTargetAlive(int targetPlayerId);

    /**
     * @brief Execute mob attack on player (integrated with CombatSystem)
     */
    void executeMobAttack(const MobDataStruct &mob, int targetPlayerId, MobMovementData &movementData);

    /**
     * @brief Send mob target lost event to clients
     */
    void sendMobTargetLost(const MobDataStruct &mob, int lostTargetPlayerId);

    /**
     * @brief Calculate distance between two positions
     */
    float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2);

    /**
     * @brief Initialize movement data for a mob with AI config
     */
    void initializeMobMovementData(int mobUID);

    /**
     * @brief Update combat state for mob based on current situation
     */
    void updateMobCombatState(MobDataStruct &mob, MobMovementData &movementData, float currentTime);

    /**
     * @brief Check if mob can perform action based on current combat state
     */
    bool canPerformAction(const MobMovementData &movementData, float currentTime) const;

    /**
     * @brief Calculate distance from mob to zone boundary (AABB-based)
     */
    float calculateDistanceFromZone(const PositionStruct &mobPos, const SpawnZoneStruct &zone);

    /**
     * @brief Check if mob should return to spawn based on zone boundaries
     */
    bool shouldReturnToSpawn(const PositionStruct &mobPos, const SpawnZoneStruct &zone);

    /**
     * @brief Check if mob can search for new targets based on zone boundaries
     */
    bool canSearchNewTargets(const PositionStruct &mobPos, const SpawnZoneStruct &zone);

    /**
     * @brief Check if mob should stop chasing based on zone boundaries
     */
    bool shouldStopChasing(const PositionStruct &mobPos, const SpawnZoneStruct &zone);
};
