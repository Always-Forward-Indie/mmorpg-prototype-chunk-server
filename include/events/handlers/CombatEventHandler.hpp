#pragma once

#include "data/AttackSystem.hpp"
#include "data/CombatStructs.hpp"
#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include <memory>
#include <unordered_map>

/**
 * @brief Handler for combat-related events
 *
 * Handles all combat actions including attacks, spells, skills,
 * interruptions, and combat state management.
 */
class CombatEventHandler : public BaseEventHandler
{
  public:
    CombatEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle combat action initiation
     *
     * Validates action requirements, starts cast/channel, sends animation
     *
     * @param event Event containing combat action data
     */
    void handleInitiateCombatAction(const Event &event);

    /**
     * @brief Handle combat action completion
     *
     * Applies damage/effects, broadcasts results
     *
     * @param event Event containing completed action data
     */
    void handleCompleteCombatAction(const Event &event);

    /**
     * @brief Handle combat action interruption
     *
     * Cancels ongoing action, handles resource refunds
     *
     * @param event Event containing interruption data
     */
    void handleInterruptCombatAction(const Event &event);

    /**
     * @brief Handle combat animation sync
     *
     * Synchronizes animations across all clients
     *
     * @param event Event containing animation data
     */
    void handleCombatAnimation(const Event &event);

    /**
     * @brief Handle combat result broadcasting
     *
     * Broadcasts damage/healing results to relevant clients
     *
     * @param event Event containing combat results
     */
    void handleCombatResult(const Event &event);

    /**
     * @brief Update ongoing combat actions
     *
     * Called periodically to check cast completion, timeouts, etc.
     */
    void updateOngoingActions();

    /**
     * @brief Handle player attack initiation
     *
     * Initiates an attack using the AttackSystem for target selection and validation
     *
     * @param event Event containing attack request data
     */
    void handlePlayerAttack(const Event &event);

    /**
     * @brief Handle AI-driven attack (for mobs/NPCs)
     *
     * Uses AttackSystem AI to select targets and actions
     *
     * @param characterId ID of the attacking character
     */
    void handleAIAttack(int characterId);

    /**
     * @brief Get available targets for attack
     *
     * @param attackerId ID of the attacking character
     * @param criteria Target selection criteria
     * @return Vector of potential targets
     */
    std::vector<TargetCandidate> getAvailableTargets(int attackerId, const TargetCriteria &criteria);

  private:
    /**
     * @brief Validate combat action requirements
     *
     * @param action Combat action to validate
     * @return true if action can be performed, false otherwise
     */
    bool validateCombatAction(const CombatActionStruct &action);

    /**
     * @brief Check if target is valid and in range
     *
     * @param casterId ID of the caster
     * @param targetId ID of the target
     * @param targetType Type of the target (player, mob, etc.)
     * @param range Maximum range
     * @param requiresLOS Whether line of sight is required
     * @return true if target is valid, false otherwise
     */
    bool validateTarget(int casterId, int targetId, CombatTargetType targetType, float range, bool requiresLOS);

    /**
     * @brief Check if caster has sufficient resources
     *
     * @param casterId ID of the caster
     * @param resourceType Type of resource required
     * @param resourceCost Amount of resource required
     * @return true if resources are sufficient, false otherwise
     */
    bool validateResources(int casterId, ResourceType resourceType, int resourceCost);

    /**
     * @brief Apply resource cost to caster
     *
     * @param casterId ID of the caster
     * @param resourceType Type of resource to consume
     * @param resourceCost Amount to consume
     */
    void consumeResources(int casterId, ResourceType resourceType, int resourceCost);

    /**
     * @brief Calculate damage based on action and participants
     *
     * @param action Combat action
     * @param casterId ID of the caster
     * @param targetId ID of the target
     * @return Combat result with damage calculations
     */
    CombatResultStruct calculateDamage(const CombatActionStruct &action, int casterId, int targetId);

    /**
     * @brief Apply damage to target
     *
     * @param targetId ID of the target
     * @param damage Amount of damage to apply
     * @return Actual damage dealt after mitigation
     */
    int applyDamage(int targetId, int damage);

    /**
     * @brief Apply damage to target of specific type
     *
     * @param targetId ID of the target
     * @param targetType Type of the target (player, mob, etc.)
     * @param damage Amount of damage to apply
     * @return Actual damage dealt after mitigation
     */
    int applyDamageToTarget(int targetId, CombatTargetType targetType, int damage);

    /**
     * @brief Apply healing to target
     *
     * @param targetId ID of the target
     * @param healing Amount of healing to apply
     * @return Actual healing done
     */
    int applyHealing(int targetId, int healing);

    /**
     * @brief Apply healing to target of specific type
     *
     * @param targetId ID of the target
     * @param targetType Type of the target (player, mob, etc.)
     * @param healing Amount of healing to apply
     * @return Actual healing done
     */
    int applyHealingToTarget(int targetId, CombatTargetType targetType, int healing);

    /**
     * @brief Check if action is on cooldown
     *
     * @param casterId ID of the caster
     * @param actionId ID of the action
     * @return true if on cooldown, false otherwise
     */
    bool isOnCooldown(int casterId, int actionId);

    /**
     * @brief Set action cooldown
     *
     * @param casterId ID of the caster
     * @param actionId ID of the action
     * @param cooldownMs Cooldown duration in milliseconds
     */
    void setCooldown(int casterId, int actionId, int cooldownMs);

    /**
     * @brief Get distance between two characters
     *
     * @param char1Id First character ID
     * @param char2Id Second character ID
     * @return Distance between characters
     */
    float getDistance(int char1Id, int char2Id);

    /**
     * @brief Get distance between caster and target of specific type
     *
     * @param casterId ID of the caster
     * @param targetId ID of the target
     * @param targetType Type of the target (player, mob, etc.)
     * @return Distance between caster and target
     */
    float getDistanceToTarget(int casterId, int targetId, CombatTargetType targetType);

    /**
     * @brief Convert combat action to JSON
     *
     * @param action Combat action structure
     * @return JSON representation
     */
    nlohmann::json combatActionToJson(const CombatActionStruct &action);

    /**
     * @brief Convert combat result to JSON
     *
     * @param result Combat result structure
     * @return JSON representation
     */
    nlohmann::json combatResultToJson(const CombatResultStruct &result);

    /**
     * @brief Convert animation data to JSON
     *
     * @param animation Animation structure
     * @return JSON representation
     */
    nlohmann::json combatAnimationToJson(const CombatAnimationStruct &animation);

    /**
     * @brief Convert simplified combat action packet to JSON
     */
    nlohmann::json combatActionPacketToJson(const CombatActionPacket &actionPacket);

    /**
     * @brief Convert simplified combat animation packet to JSON
     */
    nlohmann::json combatAnimationPacketToJson(const CombatAnimationPacket &animationPacket);

    /**
     * @brief Interrupt all actions for a character
     *
     * Used when character dies, gets stunned, etc.
     *
     * @param characterId ID of the character
     * @param reason Reason for interruption
     */
    void interruptAllActionsForCharacter(int characterId, InterruptionReason reason);

    /**
     * @brief Initialize action definitions
     *
     * Loads predefined combat actions (could be from database/config)
     */
    void initializeActionDefinitions();

    /**
     * @brief Complete combat action internally
     *
     * Helper method to complete an action and apply effects
     *
     * @param actionPtr Pointer to the action to complete
     */
    void completeCombatActionInternal(std::shared_ptr<CombatActionStruct> actionPtr);

    // Storage for ongoing combat actions
    std::unordered_map<int, std::shared_ptr<CombatActionStruct>> ongoingActions_;

    // Cooldown tracking: characterId -> (actionId -> endTime)
    std::unordered_map<int, std::unordered_map<int, std::chrono::steady_clock::time_point>> cooldowns_;

    // Action definitions: actionId -> CombatActionStruct template
    std::unordered_map<int, CombatActionStruct> actionDefinitions_;

    // Last update time for periodic updates
    std::chrono::steady_clock::time_point lastUpdateTime_;

    // Attack system for intelligent targeting and action selection
    std::unique_ptr<AttackSystem> attackSystem_;
};
