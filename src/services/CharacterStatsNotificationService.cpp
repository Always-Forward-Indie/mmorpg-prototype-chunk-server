#include "services/CharacterStatsNotificationService.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "utils/Logger.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"
#include <nlohmann/json.hpp>

CharacterStatsNotificationService::CharacterStatsNotificationService(GameServices *gameServices)
    : gameServices_(gameServices)
{
}

void
CharacterStatsNotificationService::sendStatsUpdate(int characterId)
{
    if (statsUpdateCallback_)
    {
        try
        {
            auto packet = buildStatsUpdatePacket(characterId);
            statsUpdateCallback_(packet);
        }
        catch (const std::exception &e)
        {
            gameServices_->getLogger().logError("Failed to send stats update for character " +
                                                std::to_string(characterId) + ": " + e.what());
        }
    }
}

void
CharacterStatsNotificationService::setStatsUpdateCallback(std::function<void(const nlohmann::json &)> callback)
{
    statsUpdateCallback_ = callback;
}

nlohmann::json
CharacterStatsNotificationService::buildStatsUpdatePacket(int characterId)
{
    auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);
    std::string requestId = "stats_update_" + std::to_string(characterId);

    ResponseBuilder builder;

    TimestampStruct timestamps = TimestampUtils::createReceiveTimestamp(0, requestId);

    builder.setHeader("eventType", "stats_update")
        .setHeader("status", "success")
        .setHeader("requestId", requestId)
        .setTimestamps(timestamps);

    builder.setBody("characterId", characterId)
        .setBody("level", characterData.characterLevel)
        .setBody("experience", nlohmann::json{{"current", characterData.characterExperiencePoints}, {"nextLevel", characterData.expForNextLevel}})
        .setBody("health", nlohmann::json{{"current", characterData.characterCurrentHealth}, {"max", characterData.characterMaxHealth}})
        .setBody("mana", nlohmann::json{{"current", characterData.characterCurrentMana}, {"max", characterData.characterMaxMana}});

    return builder.build();
}
