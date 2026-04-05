#include "services/CombatResponseBuilder.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Logger.hpp"
#include "utils/ResponseBuilder.hpp"
#include <spdlog/logger.h>

CombatResponseBuilder::CombatResponseBuilder(GameServices *gameServices)
    : gameServices_(gameServices)
{
    log_ = gameServices_->getLogger().getSystem("combat");
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
    body["skillSlug"] = result.skillSlug;
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;
    body["castTime"] = result.castTime;
    body["animationName"] = result.animationName;
    body["animationDuration"] = result.animationDuration;
    body["cooldownMs"] = result.cooldownMs;
    body["gcdMs"] = result.gcdMs;
    body["serverTimestamp"] = result.serverTimestamp;
    body["castStartedAt"] = result.castStartedAt;

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
    body["skillSlug"] = result.skillSlug;
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;
    body["serverTimestamp"] = result.serverTimestamp;

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
        body["finalCasterMana"] = result.finalCasterMana;
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
CombatResponseBuilder::buildAoESkillExecutionBroadcast(const AoESkillExecutionResult &result)
{
    nlohmann::json body;
    body["casterId"] = result.casterId;
    body["skillSlug"] = result.skillSlug;
    body["skillName"] = result.skillName;
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;
    body["serverTimestamp"] = result.serverTimestamp;
    body["finalCasterMana"] = result.finalCasterMana;

    int casterType = determineCharacterType(result.casterId);
    body["casterType"] = casterType;
    body["casterTypeString"] = getCharacterTypeString(casterType);

    nlohmann::json targetsArr = nlohmann::json::array();
    for (const auto &t : result.targets)
    {
        nlohmann::json entry;
        entry["targetId"] = t.targetId;
        entry["targetType"] = static_cast<int>(t.targetType);
        entry["targetTypeString"] = getCombatTargetTypeString(t.targetType);
        entry["damage"] = t.damage;
        entry["isCritical"] = t.isCritical;
        entry["isBlocked"] = t.isBlocked;
        entry["isMissed"] = t.isMissed;
        entry["targetDied"] = t.targetDied;
        entry["finalTargetHealth"] = t.finalTargetHealth;
        targetsArr.push_back(entry);
    }
    body["targets"] = targetsArr;

    return ResponseBuilder()
        .setHeader("eventType", "combatAoeResult")
        .setHeader("message", "AoE skill executed")
        .setBody("aoeResult", body)
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
    // HIGH-8: no exceptions — managers return default structs (id==0) when not found
    auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);
    if (characterData.characterId != 0)
        return 1; // PLAYER

    auto mobData = gameServices_->getMobInstanceManager().getMobInstance(characterId);
    if (mobData.uid != 0)
        return 2; // MOB

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

nlohmann::json
CombatResponseBuilder::buildEffectTickBroadcast(const EffectTickResult &tick)
{
    nlohmann::json packet;
    packet["header"]["eventType"] = "effectTick";
    packet["body"]["characterId"] = tick.characterId;
    packet["body"]["effectSlug"] = tick.effectSlug;
    packet["body"]["effectTypeSlug"] = tick.effectTypeSlug;
    packet["body"]["value"] = tick.value;
    packet["body"]["newHealth"] = tick.newHealth;
    packet["body"]["newMana"] = tick.newMana;
    packet["body"]["targetDied"] = tick.targetDied;
    return packet;
}
