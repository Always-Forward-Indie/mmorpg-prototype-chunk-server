#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include <nlohmann/json.hpp>
#include <vector>

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
    int64_t serverTimestamp = 0; ///< unix ms when packet was generated (= castStartedAt)
    int64_t castStartedAt = 0;   ///< unix ms when cast started (same as serverTimestamp for initiation)
    int casterId = 0;
    int targetId = 0;
    CombatTargetType targetType = CombatTargetType::NONE;
    std::string skillName;
    std::string skillSlug;
    std::string skillEffectType; // damage, heal, buff, debuff, etc.
    std::string skillSchool;     // physical, fire, ice, etc.
    int cooldownMs = 0;          ///< skill-specific cooldown in ms (for client UI bar)
    int gcdMs = 0;               ///< global cooldown in ms triggered by this skill
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
    int finalCasterMana = 0;     ///< caster's remaining mana after skill use
    int64_t serverTimestamp = 0; ///< unix ms when packet was generated
};

/**
 * @brief Single-target entry inside an AoE broadcast.
 */
struct AoETargetResultEntry
{
    int targetId = 0;
    CombatTargetType targetType = CombatTargetType::NONE;
    int damage = 0;
    bool isCritical = false;
    bool isBlocked = false;
    bool isMissed = false;
    bool targetDied = false;
    int finalTargetHealth = 0;
};

/**
 * @brief Batched result for AoE skill execution — sent as ONE broadcast packet.
 */
struct AoESkillExecutionResult
{
    int casterId = 0;
    std::string skillSlug;
    std::string skillName;
    std::string skillEffectType;
    std::string skillSchool;
    int64_t serverTimestamp = 0;
    int finalCasterMana = 0;
    std::vector<AoETargetResultEntry> targets;
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
     * @brief Создать батч-broadcast для AoE-скила (один пакет на все цели).
     */
    nlohmann::json buildAoESkillExecutionBroadcast(const AoESkillExecutionResult &result);

    /**
     * @brief Создать ответ об ошибке
     */
    nlohmann::json buildErrorResponse(const std::string &errorMessage, const std::string &eventType, int clientId);

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
