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
 * Dependencies: the four registry/movement refs are required (ctor-injected);
 * EventQueue and CombatSystem are late-wired by ChunkServer (nullable) and
 * null-guarded at every use site.
 *
 * Pure math (skill selection, flee vector, threat decay, melee capacity)
 * lives in MobAIFormulas.hpp — this class only fetches live data.
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
    MobAIController(CharacterManager &characters,
        MobInstanceManager &mobInstances,
        MobManager &mobs,
        MobMovementManager &mobMovement,
        EventQueue *eventQueue,
        CombatSystem *combatSystem,
        Logger &logger);

    // Late-wire for ChunkServer-assembled dependencies (nullable, guarded).
    void setEventQueue(EventQueue *eq);
    void setCombatSystem(CombatSystem *cs);

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
    CharacterManager &characters_;
    MobInstanceManager &mobInstances_;
    MobManager &mobs_;
    MobMovementManager &mobMovement_; // back-pointer to owner, never null
    EventQueue *eventQueue_;          // late-wire, may be null
    CombatSystem *combatSystem_;      // late-wire, may be null
    Logger &logger_;

    // ---- private helpers ----

    /**
     * @brief Return true if the target player is alive (health > 0, exists in manager).
     */
    bool isTargetAlive(int targetPlayerId);

    // NOTE (Wave 4.1): canAttackPlayer was deleted — zero callers anywhere
    // (verified by grep over src/include/tests). Range gating lives in
    // SkillSystem::isInRange and the ATTACK_COOLDOWN state machine.

    /**
     * @brief Fire the actual attack via CombatSystem, update lastAttackTime.
     *        Uses movementData.pendingSkillSlug if set; clears it after use.
     */
    void executeMobAttack(const MobDataStruct &mob, int targetPlayerId, MobMovementData &movementData);

    /**
     * @brief Select the best skill for the mob to use against the target.
     *        Returns nullptr if no suitable skill is available (use base attack).
     *        Scoring (range filter, per-skill cooldown, ability-first) is the
     *        pure MobAIFormulas::selectMobSkillIndex; this fetches the template.
     */
    std::optional<SkillStruct> selectAttackSkill(const MobDataStruct &mob,
        const MobMovementData &movementData,
        float distanceToTarget);

    // NOTE: planar distance now lives in utils/DistanceUtils (dist2D) — the
    // per-class copy was removed (Wave 2.1).

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
