#include "events/ExperienceEventHandler.hpp"
#include "services/ExperienceManager.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"

ExperienceEventHandler::ExperienceEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
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
                gameServices_.getLogger().logError("Failed to grant experience: " + result.errorMessage);
            }
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data type for EXPERIENCE_GRANT event");
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
                gameServices_.getLogger().logError("Failed to remove experience: " + result.errorMessage);
            }
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data type for EXPERIENCE_REMOVE event");
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

                gameServices_.getLogger().log("Sent experience update packet to all clients for character " +
                                                  std::to_string(expEvent.characterId),
                    GREEN);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Failed to send experience update packet: " + std::string(e.what()));
            }
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data type for EXPERIENCE_UPDATE event");
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
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Failed to send level up packet: " + std::string(e.what()));
            }
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data type for LEVEL_UP event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error handling level up event: " + std::string(e.what()));
    }
}
