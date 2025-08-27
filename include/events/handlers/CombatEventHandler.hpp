#pragma once

#include "data/CombatStructs.hpp"
#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include <memory>

// Forward declarations
class CombatSystem;
class SkillSystem;
class CombatResponseBuilder;
struct SkillInitiationResult;
struct SkillExecutionResult;

/**
 * @brief Handler for combat-related events
 *
 * Refactored to delegate to CombatSystem for actual combat logic.
 * This class only handles event parsing and response sending.
 */
class CombatEventHandler : public BaseEventHandler
{
  public:
    CombatEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    // Custom destructor to handle unique_ptr with forward declarations
    ~CombatEventHandler();

    /**
     * @brief Handle player attack event
     */
    void handlePlayerAttack(const Event &event);

    /**
     * @brief Handle skill usage event
     */
    void handleSkillUsage(const Event &event);

    /**
     * @brief Handle AI attack for mobs
     */
    void handleAIAttack(int characterId);

    /**
     * @brief Handle combat action interruption
     */
    void handleInterruptCombatAction(const Event &event);

    /**
     * @brief Handle combat action initiation
     */
    void handleInitiateCombatAction(const Event &event);

    /**
     * @brief Handle combat action completion
     */
    void handleCompleteCombatAction(const Event &event);

    /**
     * @brief Handle combat animation
     */
    void handleCombatAnimation(const Event &event);

    /**
     * @brief Handle combat result
     */
    void handleCombatResult(const Event &event);

    /**
     * @brief Update ongoing combat actions
     */
    void updateOngoingActions();

    /**
     * @brief Get reference to the CombatSystem for other services
     */
    CombatSystem *getCombatSystem() const;

    /**
     * @brief Send broadcast packet to all clients
     */
    void sendBroadcast(const nlohmann::json &packet);

  private:
    /**
     * @brief Parse player attack request
     */
    bool parsePlayerAttackRequest(const nlohmann::json &requestData,
        std::string &skillSlug,
        int &targetId,
        CombatTargetType &targetType);

    /**
     * @brief Parse skill usage request
     */
    bool parseSkillUsageRequest(const nlohmann::json &requestData,
        std::string &skillSlug,
        int &targetId,
        CombatTargetType &targetType);

    /**
     * @brief Send skill responses to all clients (broadcast)
     */
    void broadcastSkillInitiation(const SkillInitiationResult &result);
    void broadcastSkillExecution(const SkillExecutionResult &result);

  private:
    std::unique_ptr<CombatSystem> combatSystem_;
    std::unique_ptr<SkillSystem> skillSystem_;
    std::unique_ptr<CombatResponseBuilder> responseBuilder_;
};
