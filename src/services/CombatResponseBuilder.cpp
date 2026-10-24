#include "services/CombatResponseBuilder.hpp"
#include "services/CharacterManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Logger.hpp"
#include "utils/ResponseBuilder.hpp"
#include <spdlog/logger.h>

CombatResponseBuilder::CombatResponseBuilder(CharacterManager &characterManager,
    MobInstanceManager &mobInstanceManager,
    Logger &logger)
    : characterManager_(characterManager),
      mobInstanceManager_(mobInstanceManager),
      logger_(logger)
{
    log_ = logger_.getSystem("combat");
}

nlohmann::json
CombatResponseBuilder::buildSkillInitiationBroadcast(const SkillInitiationResult &result)
{
    nlohmann::json body;
    body["success"] = result.success;
    body["casterId"] = result.casterId;
    body["targetId"] = result.targetId;
    body["targetType"] = static_cast<int>(result.targetType);
    body["skillSlug"] = result.skillSlug;
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;
    body["castTime"] = result.castTime;
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

    // Определяем тип события
    std::string eventType = determineEventType(result.skillEffectType);
    std::string messageType = result.success ? "success" : "error";
    std::string message = result.success ? "Skill initiated" : "Skill initiation failed";

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

    // Определяем тип события
    std::string eventType = determineEventType(result.skillEffectType);
    std::string message = result.success ? "Skill executed successfully" : "Skill execution failed";

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
    body["skillEffectType"] = result.skillEffectType;
    body["skillSchool"] = result.skillSchool;
    body["serverTimestamp"] = result.serverTimestamp;
    body["finalCasterMana"] = result.finalCasterMana;

    int casterType = determineCharacterType(result.casterId);
    body["casterType"] = casterType;

    nlohmann::json targetsArr = nlohmann::json::array();
    for (const auto &t : result.targets)
    {
        nlohmann::json entry;
        entry["targetId"] = t.targetId;
        entry["targetType"] = static_cast<int>(t.targetType);
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
    // HIGH-8: no exceptions — managers return default structs (id==0) when not found.
    // Values match client ECasterType (Self=1, Player=2, Mob=3, Area=4, None=5, NPC=6):
    // the client maps the int directly, no display strings on the wire.
    auto characterData = characterManager_.getCharacterData(characterId);
    if (characterData.characterId != 0)
        return 2; // Player

    auto mobData = mobInstanceManager_.getMobInstance(characterId);
    if (mobData.uid != 0)
        return 3; // Mob

    return 0; // Invalid/unknown
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
