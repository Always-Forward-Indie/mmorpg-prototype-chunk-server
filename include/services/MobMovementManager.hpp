#pragma once

#include "data/DataStructs.hpp"
#include "services/MobAIController.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations
class MobInstanceManager;
class SpawnZoneManager;
class CharacterManager;
class EventQueue;
class MobManager;
class GameServices;

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
     * @brief Set reference to MobManager for skill template lookups (plan §2.1)
     */
    void setMobManager(MobManager *mobManager);

    /**
     * @brief Set reference to GameServices for zone-event speed multipliers
     */
    void setGameServices(GameServices *gs);

    /**
     * @brief Handle mob aggro and retaliation when attacked by player
     */
    void handleMobAttacked(int mobUID, int attackerPlayerId, int damage = 0);

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
     * @brief Update per-mob broadcast timestamp (used by unified mob tick for rate limiting)
     */
    void updateLastBroadcastMs(int mobUID, int64_t nowMs);

    /**
     * @brief Set AI configuration for mobs
     */
    void setAIConfig(const MobAIConfig &config);

    /**
     * @brief Get current AI configuration
     */
    const MobAIConfig &getAIConfig() const;

    // ---- helpers used by MobAIController (must be public) ----

    /**
     * @brief Write full movement data for a mob (thread-safe write lock).
     */
    void updateMobMovementData(int mobUID, const MobMovementData &data);

    /**
     * @brief Send mob-target-lost event to all clients.
     */
    void sendMobTargetLost(const MobDataStruct &mob, int lostTargetPlayerId);

    /**
     * @brief True if mob is close enough to its zone to search new targets.
     */
    bool canSearchNewTargets(const PositionStruct &mobPos, const SpawnZoneStruct &zone);

    /**
     * @brief Euclidean distance between two positions.
     */
    static float calculateDistance(const PositionStruct &a, const PositionStruct &b);

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    MobInstanceManager *mobInstanceManager_;
    SpawnZoneManager *spawnZoneManager_;
    class CharacterManager *characterManager_;
    class EventQueue *eventQueue_;
    class CombatSystem *combatSystem_;
    GameServices *gameServices_ = nullptr;

    // Random number generator
    std::mt19937 rng_;
    mutable std::shared_mutex mutex_;

    // Movement parameters per zone
    std::map<int, MobMovementParams> zoneMovementParams_;

    // Movement data per mob
    std::map<int, MobMovementData> mobMovementData_;

    // AI configuration
    MobAIConfig aiConfig_;

    // Log-guard for chase movement: tracks which mob UIDs have had their
    // "reached attack range" message emitted to avoid continuous spam.
    // Protected by logMutex_ (not the heavy shared_mutex).
    std::mutex logMutex_;
    std::unordered_set<int> inRangeSet_;

    // AI controller owns combat-state logic, aggro and attack execution.
    MobAIController mobAIController_;

    /**
     * @brief Calculate new position for mob
     *
     * @param mob Mob data
     * @param zone Zone data
     * @param otherMobs Lightweight {uid, position} pairs for collision detection
     * @param params Pre-fetched movement params for the zone (avoids repeated lock+lookup)
     * @return New position and direction, or nullopt if no valid movement
     */
    std::optional<MobMovementResult> calculateNewPosition(
        const MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const std::vector<std::pair<int, PositionStruct>> &otherMobs,
        const MobMovementParams &params);

    /**
     * @brief Check if position is valid (within bounds, no collisions)
     */
    bool isValidPosition(
        float x, float y, const SpawnZoneStruct &zone, const std::vector<std::pair<int, PositionStruct>> &otherMobs, const MobDataStruct &currentMob, const MobMovementParams &params);

    /**
     * @brief Check if position is valid for chase movement (no zone boundaries, only collision check)
     */
    bool isValidPositionForChase(
        float x, float y, const std::vector<std::pair<int, PositionStruct>> &otherMobs, const MobDataStruct &currentMob, const MobMovementParams &params);

    /**
     * @brief Get default movement parameters for zone
     */
    MobMovementParams getDefaultMovementParams();

    /**
     * @brief Atomically update ONLY direction/timing fields of mob movement data.
     *
     * Avoids the RMW race: between our last re-read of MobMovementData and the
     * final write, another thread (e.g. handleMobAttacked) may have written
     * targetPlayerId / combatState. A full-struct overwrite would silently undo
     * those writes. This method only touches the fields changed by movement
     * calculation, leaving all AI/combat state fields intact.
     */
    void updateMobMovementPositionFields(int mobUID,
        float dirX,
        float dirY,
        float lastMoveTime,
        float nextMoveTime,
        float currentSpeed,
        float deflectionSign = 0.0f);

    /**
     * @brief Run one AI+movement tick for a single mob.
     *
     * Shared implementation used by both moveMobsInZone and moveSingleMob.
     * Handles aggro, combat-state machine, timing checks, movement calculation, and
     * position update. Returns true if the mob physically moved this tick.
     */
    bool runMobTick(MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const MobMovementParams &params,
        const std::vector<std::pair<int, PositionStruct>> &mobPositions,
        float currentTime);

    /**
     * @brief Get movement data for specific mob (internal use)
     */
    MobMovementData getMobMovementDataInternal(int mobUID);

    /**
     * @brief Calculate movement towards target player
     */
    std::optional<MobMovementResult> calculateChaseMovement(
        const MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const std::vector<std::pair<int, PositionStruct>> &otherMobs,
        int targetPlayerId,
        const MobMovementParams &params);

    /**
     * @brief Calculate movement back to spawn zone
     */
    std::optional<MobMovementResult> calculateReturnToSpawnMovement(
        const MobDataStruct &mob,
        const SpawnZoneStruct &zone,
        const std::vector<std::pair<int, PositionStruct>> &otherMobs,
        const PositionStruct &spawnPosition,
        const MobMovementParams &params);

    /**
     * @brief Initialize movement data for a mob with AI config
     */
    void initializeMobMovementData(int mobUID);

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
     * @brief Compute the next patrol-move timestamp for a mob (plan §5.5).
     *
     * Centralises the "normal (patrol) movement timing" logic that was
     * previously duplicated in moveMobsInZone and moveSingleMob.
     *
     * @param currentTime  Current game time (seconds)
     * @param mob          Mob template — provides patrolSpeed
     * @param movementData Used for speedMultiplier fallback
     * @param params       Zone movement params (speedTime ranges)
     * @return             Absolute time after which the next move is allowed
     */
    float calculateNextMoveTime(float currentTime,
        const MobDataStruct &mob,
        const MobMovementData &movementData,
        const MobMovementParams &params);

    /**
     * @brief Check if mob should stop chasing based on zone boundaries
     */
    bool shouldStopChasing(const PositionStruct &mobPos, const SpawnZoneStruct &zone);
};
