#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include "services/CombatResponseBuilder.hpp"
#include <functional>
#include <memory>
#include <mutex>

// Forward declarations
namespace spdlog { class logger; }
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
     * @brief Обработать тики всех активных DoT/HoT эффектов.
     *  Вызывается каждый игровой тик из CombatEventHandler.
     *  Применяет урон/лечение, удаляет истёкшие эффекты,
     *  рассылает broadcast-пакеты "effectTick".
     */
    void tickEffects();

    /**
     * @brief AI атака для мобов с конкретной целью.
     *        Единственный актуальный путь боя — всегда передаётся явный targetPlayerId.
     *        @param forcedSkillSlug  Если не пуст — использовать именно этот скил
     *                                (выбранный MobAIController при подготовке атаки).
     *                                Если пуст — CombatSystem выбирает лучший скил сам.
     */
    void processAIAttack(int mobId, int targetPlayerId, const std::string &forcedSkillSlug = "");

    /**
     * @brief Установить callback для отправки broadcast пакетов
     */
    void setBroadcastCallback(std::function<void(const nlohmann::json &)> callback);

    /**
     * @brief Установить callback для отправки пакетов опыта
     */
    void setupExperienceCallbacks();

    /**
     * @brief Получить доступные цели для атаки
     */
    std::vector<int> getAvailableTargets(int attackerId, const SkillStruct &skill);

  private:
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;
    std::unique_ptr<SkillSystem> skillSystem_;
    std::unique_ptr<CombatResponseBuilder> responseBuilder_;

    // Ongoing actions: casterId -> action data
    std::unordered_map<int, std::shared_ptr<CombatActionStruct>> ongoingActions_;
    mutable std::mutex actionsMutex_; // protects ongoingActions_

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
     * @brief Обработать смерть моба с указанием убийцы
     */
    void handleMobDeath(int mobId, int killerId);

    /**
     * @brief Обработать аггро мобов
     */
    void handleMobAggro(int attackerId, int targetId, int damage);

    /**
     * @brief Обработать AoE-скил: найти все цели в area_radius вокруг кастера,
     *  применить урон, отправить broadcast на каждую цель.
     */
    void executeAoESkillUsage(int casterId, const std::string &skillSlug);
};
