#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include "services/CombatResponseBuilder.hpp"
#include <functional>
#include <memory>

// Forward declarations
class GameServices;
class SkillSystem;
class CombatResponseBuilder;

/**
 * @brief Основная система боя - управляет всей боевой логикой
 */
class CombatSystem
{
  public:
    CombatSystem(GameServices *gameServices);
    ~CombatSystem() = default;

    /**
     * @brief Инициировать использование скила
     */
    SkillInitiationResult initiateSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType);

    /**
     * @brief Выполнить использование скила
     */
    SkillExecutionResult executeSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType);

    /**
     * @brief Прервать использование скила
     */
    void interruptSkillUsage(int casterId, InterruptionReason reason);

    /**
     * @brief Обновить ongoing actions
     */
    void updateOngoingActions();

    /**
     * @brief AI атака для мобов
     */
    void processAIAttack(int mobId);

    /**
     * @brief AI атака для мобов с конкретной целью
     */
    void processAIAttack(int mobId, int targetPlayerId);

    /**
     * @brief Установить callback для отправки broadcast пакетов
     */
    void setBroadcastCallback(std::function<void(const nlohmann::json &)> callback);

    /**
     * @brief Получить доступные цели для атаки
     */
    std::vector<int> getAvailableTargets(int attackerId, const SkillStruct &skill);

  private:
    GameServices *gameServices_;
    std::unique_ptr<SkillSystem> skillSystem_;
    std::unique_ptr<CombatResponseBuilder> responseBuilder_;

    // Ongoing actions: casterId -> action data
    std::unordered_map<int, std::shared_ptr<CombatActionStruct>> ongoingActions_;

    // Callback для отправки broadcast пакетов
    std::function<void(const nlohmann::json &)> broadcastCallback_;

    /**
     * @brief Применить эффекты скила
     */
    void applySkillEffects(const SkillUsageResult &result, int casterId, int targetId, CombatTargetType targetType);

    /**
     * @brief Обработать смерть цели
     */
    void handleTargetDeath(int targetId, CombatTargetType targetType);

    /**
     * @brief Обработать аггро мобов
     */
    void handleMobAggro(int attackerId, int targetId, int damage);
};
