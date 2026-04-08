#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// Forward declarations
namespace spdlog
{
class logger;
}
class GameServices;
class CombatCalculator;

/**
 * @brief Единая система управления скилами для всех сущностей (игроки, мобы)
 */
class SkillSystem
{
  public:
    SkillSystem(GameServices *gameServices);
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
     * @brief Проверить доступность скила
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
     * @brief Установить кулдаун
     */
    void setCooldown(int casterId, const std::string &skillSlug, int cooldownMs);

    /**
     * @brief Проверить кулдаун
     */
    bool isOnCooldown(int casterId, const std::string &skillSlug);

    /**
     * @brief Проверить, активен ли Global Cooldown для кастера (реад-онли, без потребления).
     */
    bool isGCDActive(int casterId);

    /**
     * @brief HIGH-1 fix: Atomically check that the skill is NOT on cooldown and
     *        immediately set it if so.  Returns true (cooldown set, proceed with
     *        skill execution) or false (already on cooldown, reject).  Both
     *        check and set happen under the same unique_lock, eliminating the
     *        TOCTOU window between isSkillAvailable() and setCooldown().
     *
     * @param gcdMs   If > 0, also checks the per-caster Global Cooldown (stored
     *                under the internal "__gcd__" key) and sets it atomically
     *                alongside the per-skill cooldown.  Pass 0 to skip GCD.
     * @param outOnGCD  Set to true when the rejection reason is GCD (vs per-skill
     *                  cooldown), so callers can send the right error message.
     */
    bool trySetCooldown(int casterId, const std::string &skillSlug, int cooldownMs, int gcdMs = 0, bool *outOnGCD = nullptr);

    /**
     * @brief Получить лучший скил для моба (AI). Возвращает nullopt если подходящего скила нет.
     */
    std::optional<std::reference_wrapper<const SkillStruct>> getBestSkillForMob(const MobDataStruct &mobData,
        const CharacterDataStruct &targetData,
        float distance);

    /**
     * @brief Обновить кулдауны
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
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;
    std::unique_ptr<CombatCalculator> combatCalculator_;

    // Кулдауны: entityId -> (skillSlug -> timepoint)
    std::unordered_map<int, std::unordered_map<std::string, std::chrono::steady_clock::time_point>> cooldowns_;
    mutable std::shared_mutex cooldownsMutex_; // protects cooldowns_

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
