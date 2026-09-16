#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Forward declarations
namespace spdlog
{
class logger;
}
class CharacterManager;
class MobInstanceManager;
class MobManager;
class MobMovementManager;
class CooldownService;
class CombatCalculator;

/**
 * @brief Единая система управления скилами для всех сущностей (игроки, мобы)
 *
 * Skill resolution (lookup, range, target, mana, damage pipeline) with
 * explicit dependencies. Cooldown storage lives in the shared CooldownService
 * (one instance per server, owned by GameServices) — the 7 cooldown methods
 * below are thin forwards so existing callers are untouched.
 */
class SkillSystem
{
  public:
    /// Explicit dependencies (no GameServices).
    SkillSystem(CharacterManager &characters,
        MobInstanceManager &mobInstances,
        MobManager &mobs,
        MobMovementManager &mobMovement,
        CooldownService &cooldowns,
        Logger &logger);
    ~SkillSystem() = default;

    /**
     * @brief Использовать скил
     * @param casterId ID кастера (игрок или моб)
     * @param skillSlug Slug скила
     * @param targetId ID цели
     * @param targetType Тип цели
     * @param cooldownAlreadySet When true, skip the trySetCooldown check —
     *        the cooldown was already set at initiateSkillUsage time so we
     *        must not try to set it again (it would be "on cooldown" and fail).
     * @return Результат использования скила
     */
    SkillUsageResult useSkill(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType, bool cooldownAlreadySet = false);

    /**
     * @brief Проверить доступность скила (forward → CooldownService).
     */
    bool isSkillAvailable(int casterId, const std::string &skillSlug);

    /**
     * @brief Получить скил по slug для персонажа
     */
    std::optional<SkillStruct> getCharacterSkill(int characterId, const std::string &skillSlug);

    /**
     * @brief Получить скил по slug для моба
     */
    std::optional<SkillStruct> getMobSkill(int mobId, const std::string &skillSlug);

    /**
     * @brief Установить кулдаун (forward → CooldownService).
     */
    void setCooldown(int casterId, const std::string &skillSlug, int cooldownMs);

    /**
     * @brief Проверить кулдаун (forward → CooldownService).
     */
    bool isOnCooldown(int casterId, const std::string &skillSlug);

    /**
     * @brief Проверить, активен ли Global Cooldown для кастера (реад-онли, без потребления).
     *        Forward → CooldownService.
     */
    bool isGCDActive(int casterId);

    /**
     * @brief HIGH-1 fix: Atomically check that the skill is NOT on cooldown and
     *        immediately set it if so (forward → CooldownService).
     */
    bool trySetCooldown(int casterId, const std::string &skillSlug, int cooldownMs, int gcdMs = 0, bool *outOnGCD = nullptr);

    /**
     * @brief Restore a cooldown from a persisted remaining duration (forward → CooldownService).
     */
    void restoreCooldown(int casterId, const std::string &skillSlug, int64_t remainingMs);

    /**
     * @brief Получить лучший скил для моба (AI). Возвращает nullopt если подходящего скила нет.
     */
    std::optional<std::reference_wrapper<const SkillStruct>> getBestSkillForMob(const MobDataStruct &mobData,
        const CharacterDataStruct &targetData,
        float distance);

    /**
     * @brief Обновить кулдауны (forward → CooldownService).
     */
    void updateCooldowns();

    /**
     * @brief Предоставить доступ к CombatCalculator для AoE и других внешних расчётов.
     */
    CombatCalculator *getCombatCalculator() const
    {
        return combatCalculator_.get();
    }

  private:
    CharacterManager &characters_;
    MobInstanceManager &mobInstances_;
    MobManager &mobs_;
    MobMovementManager &mobMovement_;
    CooldownService &cooldowns_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    std::unique_ptr<CombatCalculator> combatCalculator_;

    /**
     * @brief Определить тип кастера (игрок или моб)
     */
    enum class CasterType
    {
        PLAYER,
        MOB,
        UNKNOWN
    };
    CasterType determineCasterType(int casterId);

    /**
     * @brief Проверить дистанцию
     * MEDIUM-2: accepts pre-computed casterType to avoid redundant determineCasterType() call
     */
    bool isInRange(const SkillStruct &skill, int casterId, int targetId, CombatTargetType targetType, CasterType casterType);

    /**
     * @brief Валидировать цель
     */
    bool validateTarget(int casterId, int targetId, CombatTargetType targetType, CasterType casterType);

    /**
     * @brief Атомарная проверка и списание маны. Возвращает false если маны недостаточно (без списания).
     * MEDIUM-2: accepts pre-computed casterType
     */
    bool tryConsumeResources(int casterId, const SkillStruct &skill, CasterType casterType);
};
