#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/CombatTargetResolver.hpp"
#include "services/DurabilityService.hpp"
#include "services/MobKillRewardPipeline.hpp"
#include "services/OngoingActionStore.hpp"
#include "services/PlayerDeathPipeline.hpp"
#include "services/SkillSystem.hpp"
#include <functional>
#include <memory>
#include <mutex>

// Forward declarations
namespace spdlog
{
class logger;
}
class GameServices;
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
    SkillExecutionResult executeSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType, bool cooldownAlreadySet = false);

    /**
     * @brief Удалить запись об ongoing action для кастера (без side-effects).
     *        Вызывается после немедленного выполнения скила игрока, чтобы
     *        updateOngoingActions() не выполнил его повторно по таймеру.
     */
    void clearOngoingAction(int casterId);

    /**
     * @brief Обновить ongoing actions
     */
    std::vector<SkillExecutionResult> updateOngoingActions();

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
     * @brief Отправить combatInitiation broadcast при начале атаки/каста моба.
     *        Вызывается из MobAIController при входе в PREPARING_ATTACK.
     */
    void broadcastMobSkillInitiation(int mobId, int targetPlayerId, const SkillStruct &skill);

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

    /**
     * @brief Set callback for persisting durability changes to the game server
     */
    void setSaveDurabilityCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Set callback for triggering character attribute refresh (e.g. on durability threshold crossing)
     * Invoked with characterId when a durability warning boundary is crossed.
     */
    void setRefreshAttributesCallback(std::function<void(int characterId)> callback);

    /**
     * @brief Set callback for persisting Item Soul kill_count to the game server.
     */
    void setSaveItemKillCountCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Restore a persisted skill cooldown for a player (called on character join).
     *        Passes through to the internal SkillSystem::restoreCooldown().
     */
    void restoreSkillCooldown(int characterId, const std::string &skillSlug, int64_t remainingMs);

    /**
     * @brief Admin-RPC kill (DEV only, called from CombatEventHandler).
     *
     * Lethal damage + the genuine death pipeline: the same two calls the
     * skill path makes on a lethal hit (loot event, then handleMobDeath
     * with its XP/quest/bestiary/champion/reputation hooks). No duplicated
     * logic. Setup-only (corpse/harvest states); kill mechanics are proven
     * via playerAttack, never via this path. Callers validate uid/killer.
     */
    void adminKillMob(int mobUid, int killerId);

  private:
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;
    std::unique_ptr<SkillSystem> skillSystem_;
    std::unique_ptr<CombatResponseBuilder> responseBuilder_;
    DurabilityService durability_;
    MobKillRewardPipeline rewardPipeline_;
    PlayerDeathPipeline deathPipeline_;

    // Ongoing actions registry (map + mutex live in the store).
    OngoingActionStore ongoingActions_;

    // Callback для отправки broadcast пакетов
    std::function<void(const nlohmann::json &)> broadcastCallback_;

    // Callback for persisting Item Soul kill_count to game server
    std::function<void(const std::string &)> saveItemKillCountCallback_;

    /**
     * @brief Применить эффекты скила.
     * @param execResult SkillExecutionResult to append applied effects to (for persistence).
     */
    void applySkillEffects(const SkillUsageResult &result, int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType, SkillExecutionResult *execResult = nullptr);

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
    bool executeAoESkillUsage(int casterId, const std::string &skillSlug, bool cooldownAlreadySet = false);

    /**
     * @brief Send an Item Soul kill_count change to the game server for persistence.
     */
    void saveItemKillCountChange(int characterId, int inventoryItemId, int killCount);
};
