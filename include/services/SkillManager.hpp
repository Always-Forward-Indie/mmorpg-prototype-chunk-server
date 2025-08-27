#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include "services/CombatCalculator.hpp"
#include <chrono>
#include <memory>
#include <unordered_map>

// Forward declarations
class GameServices;

/**
 * @brief Менеджер для управления скилами персонажей и мобов
 */
class SkillManager
{
  public:
    SkillManager();
    SkillManager(GameServices *gameServices);
    ~SkillManager() = default;

    /**
     * @brief Установить ссылку на GameServices
     * @param gameServices Указатель на GameServices
     */
    void setGameServices(GameServices *gameServices);

    /**
     * @brief Использовать скил персонажа
     * @param casterId ID персонажа
     * @param skillSlug Slug скила
     * @param targetId ID цели
     * @return Результат использования скила
     */
    SkillUsageResult useCharacterSkill(int casterId, const std::string &skillSlug, int targetId);

    /**
     * @brief Использовать скил персонажа с указанием типа цели
     * @param casterId ID персонажа
     * @param skillSlug Slug скила
     * @param targetId ID цели
     * @param targetType Тип цели (моб или игрок)
     * @return Результат использования скила
     */
    SkillUsageResult useCharacterSkillWithTargetType(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType);

    /**
     * @brief Использовать скил моба
     * @param mobId ID моба
     * @param skillSlug Slug скила
     * @param targetId ID цели
     * @return Результат использования скила
     */
    SkillUsageResult useMobSkill(int mobId, const std::string &skillSlug, int targetId);

    /**
     * @brief Проверить доступность скила (кулдаун, мана, etc.)
     * @param casterId ID кастера
     * @param skill Скил для проверки
     * @param casterData Данные кастера
     * @return true если скил доступен
     */
    bool isSkillAvailable(int casterId, const SkillStruct &skill, const CharacterDataStruct &casterData);

    /**
     * @brief Проверить доступность скила для моба
     * @param mobId ID моба
     * @param skill Скил для проверки
     * @param mobData Данные моба
     * @return true если скил доступен
     */
    bool isSkillAvailable(int mobId, const SkillStruct &skill, const MobDataStruct &mobData);

    /**
     * @brief Получить скил персонажа по slug
     * @param characterData Данные персонажа
     * @param skillSlug Slug скила
     * @return Указатель на скил или nullptr
     */
    const SkillStruct *getCharacterSkill(const CharacterDataStruct &characterData, const std::string &skillSlug);

    /**
     * @brief Получить скил моба по slug
     * @param mobData Данные моба
     * @param skillSlug Slug скила
     * @return Указатель на скил или nullptr
     */
    const SkillStruct *getMobSkill(const MobDataStruct &mobData, const std::string &skillSlug);

    /**
     * @brief Установить скил на кулдаун
     * @param casterId ID кастера
     * @param skillSlug Slug скила
     * @param cooldownMs Время кулдауна в миллисекундах
     */
    void setCooldown(int casterId, const std::string &skillSlug, int cooldownMs);

    /**
     * @brief Проверить кулдаун скила
     * @param casterId ID кастера
     * @param skillSlug Slug скила
     * @return true если скил на кулдауне
     */
    bool isOnCooldown(int casterId, const std::string &skillSlug);

    /**
     * @brief Обновить кулдауны (вызывать периодически)
     */
    void updateCooldowns();

    /**
     * @brief Получить лучший скил для моба (для AI)
     * @param mobData Данные моба
     * @param targetData Данные цели
     * @param distance Расстояние до цели
     * @return Указатель на лучший скил или nullptr
     */
    const SkillStruct *getBestSkillForMob(
        const MobDataStruct &mobData,
        const CharacterDataStruct &targetData,
        float distance);

  private:
    std::unique_ptr<CombatCalculator> combatCalculator_;
    GameServices *gameServices_;

    // Кулдауны: characterId -> (skillSlug -> timepoint)
    std::unordered_map<int, std::unordered_map<std::string, std::chrono::steady_clock::time_point>> cooldowns_;

    /**
     * @brief Проверить дистанцию для скила
     * @param skill Скил
     * @param distance Расстояние до цели
     * @return true если цель в радиусе действия
     */
    bool isInRange(const SkillStruct &skill, float distance);
};
