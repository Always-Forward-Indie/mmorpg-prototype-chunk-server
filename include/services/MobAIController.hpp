#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <mutex>
#include <optional>
#include <unordered_map>

// Forward declarations
class MobMovementManager;
class CharacterManager;
class EventQueue;
class CombatSystem;
class MobInstanceManager;
class MobManager;

/**
 * @brief Handles mob AI state machine, aggro logic, and combat decisions.
 *
 * Extracted from MobMovementManager to separate movement physics from
 * AI/combat logic. Interacts with MobMovementManager for shared state
 * (movement data map) via its public accessor methods.
 *
 * Responsible for:
 *   - Aggro detection and target management (handlePlayerAggro)
 *   - Combat state machine transitions (updateMobCombatState)
 *   - Attack execution (canAttackPlayer, executeMobAttack)
 *   - Retaliation when mob is attacked (handleMobAttacked)
 */
class MobAIController
{
  public:
    explicit MobAIController(Logger &logger);

    // ---- dependency injection ----
    void setMobMovementManager(MobMovementManager *mm);
    void setCharacterManager(CharacterManager *cm);
    void setEventQueue(EventQueue *eq);
    void setCombatSystem(CombatSystem *cs);
    void setMobInstanceManager(MobInstanceManager *mi);
    void setMobManager(MobManager *mm);

    // ---- public interface called from MobMovementManager ----

    /**
     * @brief Called when a player attacks this mob — set aggro target immediately.
     */
    void handleMobAttacked(int mobUID, int attackerPlayerId, int damage = 0);

    /**
     * @brief Check for aggro transitions: validate current target, search new targets.
     *        Mutates movementData in-place and persists changes via MobMovementManager.
     */
    void handlePlayerAggro(MobDataStruct &mob, const SpawnZoneStruct &zone, MobMovementData &movementData);

    /**
     * @brief Drive the combat state machine for one tick.
     */
    void updateMobCombatState(MobDataStruct &mob, MobMovementData &movementData, float currentTime);

  private:
    Logger &logger_;
    MobMovementManager *mobMovementManager_;
    CharacterManager *characterManager_;
    EventQueue *eventQueue_;
    CombatSystem *combatSystem_;
    MobInstanceManager *mobInstanceManager_;
    MobManager *mobManager_;

    // ---- private helpers ----

    /**
     * @brief Return true if the target player is alive (health > 0, exists in manager).
     */
    bool isTargetAlive(int targetPlayerId);

    /**
     * @brief Return true if mob is within attack range AND cooldown has elapsed.
     */
    bool canAttackPlayer(const MobDataStruct &mob, int targetPlayerId, const MobMovementData &movementData);

    /**
     * @brief Fire the actual attack via CombatSystem, update lastAttackTime.
     *        Uses movementData.pendingSkillSlug if set; clears it after use.
     */
    void executeMobAttack(const MobDataStruct &mob, int targetPlayerId, MobMovementData &movementData);

    /**
     * @brief Select the best skill for the mob to use against the target.
     *        Returns nullptr if no suitable skill is available (use base attack).
     *        Filters by maxRange, per-skill cooldown.
     *        Prefers skills with cooldownMs > 0 over basic attacks.
     */
    std::optional<SkillStruct> selectAttackSkill(const MobDataStruct &mob,
        const MobMovementData &movementData,
        float distanceToTarget);

    /**
     * @brief Euclidean distance between two positions.
     */
    static float calculateDistance(const PositionStruct &a, const PositionStruct &b);

    /**
     * @brief Check if the mob should enter FLEEING state (HP < fleeHpThreshold).
     *        If so, mutates movementData (sets isFleeing, fleeTargetPosition, state)
     *        and persists changes.  Returns true if FLEEING was triggered.
     */
    bool checkAndTriggerFlee(MobDataStruct &mob, MobMovementData &movementData, float currentTime, int attackerPlayerId);

    /**
     * @brief Count mobs (excluding excludeUID) that are currently occupying a
     *        melee slot: targeting targetPlayerId and in state PREPARING_ATTACK,
     *        ATTACKING, or ATTACK_COOLDOWN within the given range.
     *        Used to gate crowding — excess mobs wait outside the melee ring
     *        instead of jittering and slow-rotating toward the player.
     */
    int countMobsEngagingTarget(int targetPlayerId, int excludeUID, float range) const;
};
