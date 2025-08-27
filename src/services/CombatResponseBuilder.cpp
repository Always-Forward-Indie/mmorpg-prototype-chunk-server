#include "services/CombatResponseBuilder.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Logger.hpp"
#include "utils/ResponseBuilder.hpp"

CombatResponseBuilder::CombatResponseBuilder(GameServices *gameServices)
    : gameServices_(gameServices)
{
}

nlohmann::json
CombatResponseBuilder::buildSkillInitiationBroadcast(const SkillInitiationResult &result)
{
    nlohmann::json body;
    body["success"] = result.success;
    body["casterId"] = result.casterId;
    body["targetId"] = result.targetId;
    body["targetType"] = static_cast<int>(result.targetType);
    body["targetTypeString"] = getCombatTargetTypeString(result.targetType);
    body["skillName"] = result.skillName;
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;
    body["castTime"] = result.castTime;
    body["animationName"] = result.animationName;
    body["animationDuration"] = result.animationDuration;

    if (!result.success)
    {
        body["errorReason"] = result.errorMessage;
    }

    // Определяем тип кастера
    int casterType = determineCharacterType(result.casterId);
    body["casterType"] = casterType;
    body["casterTypeString"] = getCharacterTypeString(casterType);

    // Определяем тип события
    std::string eventType = determineEventType(result.skillEffectType);
    std::string messageType = result.success ? "success" : "error";
    std::string message = result.success ? "Skill " + result.skillName + " initiated" : "Skill " + result.skillName + " initiation failed";

    return ResponseBuilder()
        .setHeader("message", message)
        .setHeader("eventType", eventType + "Initiation")
        .setBody("skillInitiation", body)
        .build();
}

nlohmann::json
CombatResponseBuilder::buildSkillExecutionBroadcast(const SkillExecutionResult &result)
{
    nlohmann::json body;
    body["success"] = result.success;
    body["casterId"] = result.casterId;
    body["targetId"] = result.targetId;
    body["targetType"] = static_cast<int>(result.targetType);
    body["targetTypeString"] = getCombatTargetTypeString(result.targetType);
    body["skillName"] = result.skillName;
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;

    if (result.success)
    {
        // Универсальная обработка разных типов эффектов
        if (result.skillEffectType == "damage")
        {
            body["damage"] = result.skillResult.damageResult.totalDamage;
            body["isCritical"] = result.skillResult.damageResult.isCritical;
            body["isBlocked"] = result.skillResult.damageResult.isBlocked;
            body["isMissed"] = result.skillResult.damageResult.isMissed;
            body["targetDied"] = result.targetDied;
        }
        else if (result.skillEffectType == "heal")
        {
            body["healing"] = result.skillResult.healAmount;
        }
        else if (result.skillEffectType == "buff" || result.skillEffectType == "debuff")
        {
            body["appliedEffects"] = result.skillResult.appliedEffects;
        }

        body["finalTargetHealth"] = result.finalTargetHealth;
        body["finalTargetMana"] = result.finalTargetMana;
    }
    else
    {
        body["errorReason"] = result.errorMessage;
    }

    // Добавляем информацию о типе кастера
    int casterType = determineCharacterType(result.casterId);
    body["casterType"] = casterType;
    body["casterTypeString"] = getCharacterTypeString(casterType);

    // Определяем тип события
    std::string eventType = determineEventType(result.skillEffectType);
    std::string message = result.success ? "Skill " + result.skillName + " executed successfully" : "Skill " + result.skillName + " execution failed";

    return ResponseBuilder()
        .setHeader("message", message)
        .setHeader("eventType", eventType + "Result")
        .setBody("skillResult", body)
        .build();
}

nlohmann::json
CombatResponseBuilder::buildErrorResponse(const std::string &errorMessage,
    const std::string &eventType,
    int clientId)
{
    nlohmann::json body;
    body["success"] = false;
    body["errorMessage"] = errorMessage;

    return ResponseBuilder()
        .setHeader("message", "Error: " + errorMessage)
        .setHeader("clientId", clientId)
        .setHeader("eventType", eventType)
        .setBody("error", body)
        .build();
}

nlohmann::json
CombatResponseBuilder::buildAnimationPacket(int characterId, const std::string &animationName, float duration, const PositionStruct &position, const PositionStruct &targetPosition)
{
    nlohmann::json animationData;
    animationData["characterId"] = characterId;
    animationData["animationName"] = animationName;
    animationData["duration"] = duration;
    animationData["position"] = {
        {"x", position.positionX},
        {"y", position.positionY},
        {"z", position.positionZ}};

    // Добавляем позицию цели если она указана
    if (targetPosition.positionX != 0.0f || targetPosition.positionY != 0.0f || targetPosition.positionZ != 0.0f)
    {
        animationData["targetPosition"] = {
            {"x", targetPosition.positionX},
            {"y", targetPosition.positionY},
            {"z", targetPosition.positionZ}};
    }

    return ResponseBuilder()
        .setHeader("message", "Combat animation")
        .setHeader("eventType", "combatAnimation")
        .setBody("animation", animationData)
        .build();
}

std::string
CombatResponseBuilder::determineEventType(const std::string &skillEffectType)
{
    if (skillEffectType == "damage")
        return "combat";
    else if (skillEffectType == "heal")
        return "healing";
    else if (skillEffectType == "buff")
        return "buff";
    else if (skillEffectType == "debuff")
        return "debuff";
    else
        return "skill"; // fallback для неопределенных типов
}

int
CombatResponseBuilder::determineCharacterType(int characterId)
{
    // Проверяем, является ли это игроком
    try
    {
        auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);
        if (characterData.characterId != 0)
        {
            return 1; // PLAYER
        }
    }
    catch (...)
    {
        // Не игрок
    }

    // Проверяем, является ли это мобом
    try
    {
        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(characterId);
        if (mobData.uid != 0)
        {
            return 2; // MOB
        }
    }
    catch (...)
    {
        // Не моб
    }

    return 0; // UNKNOWN
}

std::string
CombatResponseBuilder::getCharacterTypeString(int characterType)
{
    switch (characterType)
    {
    case 1:
        return "PLAYER";
    case 2:
        return "MOB";
    default:
        return "UNKNOWN";
    }
}

std::string
CombatResponseBuilder::getCombatTargetTypeString(CombatTargetType targetType)
{
    switch (targetType)
    {
    case CombatTargetType::SELF:
        return "SELF";
    case CombatTargetType::PLAYER:
        return "PLAYER";
    case CombatTargetType::MOB:
        return "MOB";
    case CombatTargetType::AREA:
        return "AREA";
    case CombatTargetType::NONE:
        return "NONE";
    default:
        return "UNKNOWN";
    }
}
