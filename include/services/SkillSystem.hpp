#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Forward declarations
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
     * @return Результат использования скила
     */
    SkillUsageResult useSkill(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType);

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
     * @brief Получить лучший скил для моба (AI)
     */
    const SkillStruct *getBestSkillForMob(const MobDataStruct &mobData,
        const CharacterDataStruct &targetData,
        float distance);

    /**
     * @brief Обновить кулдауны
     */
    void updateCooldowns();

  private:
    GameServices *gameServices_;
    std::unique_ptr<CombatCalculator> combatCalculator_;

    // Кулдауны: entityId -> (skillSlug -> timepoint)
    std::unordered_map<int, std::unordered_map<std::string, std::chrono::steady_clock::time_point>> cooldowns_;

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
     */
    bool isInRange(const SkillStruct &skill, int casterId, int targetId, CombatTargetType targetType);

    /**
     * @brief Валидировать цель
     */
    bool validateTarget(int casterId, int targetId, CombatTargetType targetType);

    /**
     * @brief Проверить ресурсы
     */
    bool validateResources(int casterId, const SkillStruct &skill);

    /**
     * @brief Потратить ресурсы
     */
    void consumeResources(int casterId, const SkillStruct &skill);
};
