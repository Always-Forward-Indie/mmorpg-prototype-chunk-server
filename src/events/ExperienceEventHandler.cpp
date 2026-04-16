#include "events/ExperienceEventHandler.hpp"
#include "services/ExperienceManager.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"
#include <spdlog/logger.h>

ExperienceEventHandler::ExperienceEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "experience")
{
    log_ = gameServices_.getLogger().getSystem("experience");
}

void
ExperienceEventHandler::handleExperienceGrantEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();

        if (std::holds_alternative<ExperienceEventStruct>(data))
        {
            ExperienceEventStruct expEvent = std::get<ExperienceEventStruct>(data);
            auto &experienceManager = gameServices_.getExperienceManager();

            // Начисляем опыт
            auto result = experienceManager.grantExperience(
                expEvent.characterId,
                expEvent.experienceChange,
                expEvent.reason,
                expEvent.sourceId);

            if (result.success)
            {
                gameServices_.getLogger().log("Successfully granted " +
                                                  std::to_string(expEvent.experienceChange) +
                                                  " experience to character " +
                                                  std::to_string(expEvent.characterId),
                    GREEN);
            }
            else
            {
                log_->error("Failed to grant experience: " + result.errorMessage);
            }
        }
        else
        {
            log_->error("Invalid data type for EXPERIENCE_GRANT event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error handling experience grant event: " + std::string(e.what()));
    }
}

void
ExperienceEventHandler::handleExperienceRemoveEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();

        if (std::holds_alternative<ExperienceEventStruct>(data))
        {
            ExperienceEventStruct expEvent = std::get<ExperienceEventStruct>(data);
            auto &experienceManager = gameServices_.getExperienceManager();

            // Снимаем опыт (используем отрицательное значение)
            auto result = experienceManager.removeExperience(
                expEvent.characterId,
                abs(expEvent.experienceChange),
                expEvent.reason);

            if (result.success)
            {
                gameServices_.getLogger().log("Successfully removed " +
                                                  std::to_string(abs(expEvent.experienceChange)) +
                                                  " experience from character " +
                                                  std::to_string(expEvent.characterId),
                    YELLOW);
            }
            else
            {
                log_->error("Failed to remove experience: " + result.errorMessage);
            }
        }
        else
        {
            log_->error("Invalid data type for EXPERIENCE_REMOVE event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error handling experience remove event: " + std::string(e.what()));
    }
}

void
ExperienceEventHandler::handleExperienceUpdateEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();

        if (std::holds_alternative<ExperienceEventStruct>(data))
        {
            ExperienceEventStruct expEvent = std::get<ExperienceEventStruct>(data);

            // Создаем пакет для отправки клиенту с правильной структурой
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Experience updated successfully!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", event.getClientID())
                                          .setHeader("eventType", "experienceUpdate")
                                          .setTimestamps(event.getTimestamps())
                                          .setBody("characterId", expEvent.characterId)
                                          .setBody("experienceChange", expEvent.experienceChange)
                                          .setBody("oldExperience", expEvent.oldExperience)
                                          .setBody("newExperience", expEvent.newExperience)
                                          .setBody("oldLevel", expEvent.oldLevel)
                                          .setBody("newLevel", expEvent.newLevel)
                                          .setBody("expForCurrentLevel", expEvent.expForCurrentLevel)
                                          .setBody("expForNextLevel", expEvent.expForNextLevel)
                                          .setBody("reason", expEvent.reason)
                                          .setBody("sourceId", expEvent.sourceId)
                                          .setBody("levelUp", (expEvent.newLevel > expEvent.oldLevel))
                                          .build();

            // Отправляем пакет всем подключенным клиентам
            try
            {
                auto clientsList = gameServices_.getClientManager().getClientsListReadOnly();
                std::string responseData = networkManager_.generateResponseMessage("success", response, event.getTimestamps());

                for (const auto &client : clientsList)
                {
                    auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
                    if (clientSocket && clientSocket->is_open())
                    {
                        networkManager_.sendResponse(clientSocket, responseData);
                    }
                }

                log_->info("Sent experience update packet to all clients for character " +
                           std::to_string(expEvent.characterId));
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Failed to send experience update packet: " + std::string(e.what()));
            }
        }
        else
        {
            log_->error("Invalid data type for EXPERIENCE_UPDATE event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error handling experience update event: " + std::string(e.what()));
    }
}

void
ExperienceEventHandler::handleLevelUpEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();

        if (std::holds_alternative<ExperienceEventStruct>(data))
        {
            ExperienceEventStruct expEvent = std::get<ExperienceEventStruct>(data);

            // Создаем специальный пакет для повышения уровня с правильной структурой
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Level up achieved!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", event.getClientID())
                                          .setHeader("eventType", "levelUp")
                                          .setTimestamps(event.getTimestamps())
                                          .setBody("characterId", expEvent.characterId)
                                          .setBody("oldLevel", expEvent.oldLevel)
                                          .setBody("newLevel", expEvent.newLevel)
                                          .setBody("newExperience", expEvent.newExperience)
                                          .setBody("expForNextLevel", expEvent.expForNextLevel)
                                          .setBody("newAbilities", nlohmann::json::array()) // может быть расширено в будущем
                                          .build();

            // Отправляем пакет всем подключенным клиентам
            try
            {
                auto clientsList = gameServices_.getClientManager().getClientsListReadOnly();
                std::string responseData = networkManager_.generateResponseMessage("success", response, event.getTimestamps());

                for (const auto &client : clientsList)
                {
                    auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
                    if (clientSocket && clientSocket->is_open())
                    {
                        networkManager_.sendResponse(clientSocket, responseData);
                    }
                }

                gameServices_.getLogger().log("Sent level up packet for character " +
                                                  std::to_string(expEvent.characterId) +
                                                  " (level " + std::to_string(expEvent.oldLevel) +
                                                  " -> " + std::to_string(expEvent.newLevel) + ")",
                    CYAN);

                // Title auto-grant: check level conditions
                nlohmann::json titleEvent;
                titleEvent["level"] = expEvent.newLevel;
                gameServices_.getTitleManager().checkAndGrantTitles(
                    expEvent.characterId, "level", titleEvent);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Failed to send level up packet: " + std::string(e.what()));
            }

            // Immediately persist the new level and experience to the game server DB
            // so a crash/disconnect right after level-up does not lose progress.
            try
            {
                nlohmann::json charEntry;
                charEntry["characterId"] = expEvent.characterId;
                charEntry["exp"] = expEvent.newExperience;
                charEntry["level"] = expEvent.newLevel;

                nlohmann::json savePacket;
                savePacket["header"]["eventType"] = "saveCharacterProgress";
                savePacket["header"]["clientId"] = 0;
                savePacket["header"]["hash"] = "";
                savePacket["body"]["characters"] = nlohmann::json::array({charEntry});

                gameServerWorker_.sendDataToGameServer(savePacket.dump() + "\n");

                gameServices_.getLogger().log(
                    "[LEVEL_UP_SAVE] Sent saveCharacterProgress for character " +
                        std::to_string(expEvent.characterId) +
                        " level=" + std::to_string(expEvent.newLevel) +
                        " exp=" + std::to_string(expEvent.newExperience),
                    GREEN);

                // Request fresh attributes from game-server (stats scale with level via class_stat_formula)
                nlohmann::json attrsReq;
                attrsReq["header"]["eventType"] = "getCharacterAttributes";
                attrsReq["header"]["clientId"] = 0;
                attrsReq["header"]["hash"] = "";
                attrsReq["body"]["characterId"] = expEvent.characterId;
                gameServerWorker_.sendDataToGameServer(attrsReq.dump() + "\n");

                log_->info(
                    "[LEVEL_UP] Requested attribute refresh for character " +
                    std::to_string(expEvent.characterId));
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Failed to send saveCharacterProgress on level-up: " + std::string(e.what()));
            }
        }
        else
        {
            log_->error("Invalid data type for LEVEL_UP event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error handling level up event: " + std::string(e.what()));
    }
}
