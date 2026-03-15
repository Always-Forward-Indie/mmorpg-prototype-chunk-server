#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include <nlohmann/json.hpp>

// Forward declarations
namespace spdlog
{
class logger;
}
class GameServices;

/**
 * @brief Результат инициации использования скила
 */
struct SkillInitiationResult
{
    bool success = false;
    std::string errorMessage;
    float castTime = 0.0f;
    std::string animationName;
    float animationDuration = 0.0f;
    int casterId = 0;
    int targetId = 0;
    CombatTargetType targetType = CombatTargetType::NONE;
    std::string skillName;
    std::string skillSlug;
    std::string skillEffectType; // damage, heal, buff, debuff, etc.
    std::string skillSchool;     // physical, fire, ice, etc.
};

/**
 * @brief Результат выполнения скила
 */
struct SkillExecutionResult
{
    bool success = false;
    std::string errorMessage;
    SkillUsageResult skillResult;
    bool targetDied = false;
    int finalTargetHealth = 0;
    int finalTargetMana = 0;
    bool healthPopulated = false; ///< true once finalTargetHealth/Mana have been explicitly set
    int casterId = 0;
    int targetId = 0;
    CombatTargetType targetType = CombatTargetType::NONE;
    std::string skillName;
    std::string skillSlug;
    std::string skillEffectType; // damage, heal, buff, debuff, etc.
    std::string skillSchool;     // physical, fire, ice, etc.
    float hitDelay = 0.0f;       ///< seconds after receiving this packet before showing the hit effect
};

/**
 * @brief Строитель ответов для системы скилов
 */
class CombatResponseBuilder
{
  public:
    CombatResponseBuilder(GameServices *gameServices);

    /**
     * @brief Создать ответ об инициации использования скила (транслируется всем)
     */
    nlohmann::json buildSkillInitiationBroadcast(const SkillInitiationResult &result);

    /**
     * @brief Создать ответ о результате использования скила (транслируется всем)
     */
    nlohmann::json buildSkillExecutionBroadcast(const SkillExecutionResult &result);

    /**
     * @brief Создать ответ об ошибке
     */
    nlohmann::json buildErrorResponse(const std::string &errorMessage, const std::string &eventType, int clientId);

    /**
     * @brief Создать пакет анимации
     */
    nlohmann::json buildAnimationPacket(int characterId, const std::string &animationName, float duration, const PositionStruct &position, const PositionStruct &targetPosition = {});

    /**
     * @brief Создать broadcast-пакет тика DoT/HoT эффекта.
     *  type: "effectTick"
     *  body: { characterId, effectSlug, effectTypeSlug, value, newHealth, newMana, targetDied }
     */
    nlohmann::json buildEffectTickBroadcast(const EffectTickResult &tick);

  private:
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;

    /**
     * @brief Определить тип персонажа (игрок/моб)
     */
    int determineCharacterType(int characterId);

    /**
     * @brief Получить строковое представление типа персонажа
     */
    std::string getCharacterTypeString(int characterType);

    /**
     * @brief Получить строковое представление типа цели
     */
    std::string getCombatTargetTypeString(CombatTargetType targetType);

    /**
     * @brief Определить тип события на основе эффекта скила
     */
    std::string determineEventType(const std::string &skillEffectType);
};
