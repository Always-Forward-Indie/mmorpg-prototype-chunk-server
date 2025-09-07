#include "events/handlers/SkillEventHandler.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>

SkillEventHandler::SkillEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
    gameServices_.getLogger().log("SkillEventHandler initialized", GREEN);
}

void
SkillEventHandler::handleInitializePlayerSkills(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (!std::holds_alternative<PlayerSkillInitStruct>(data))
        {
            gameServices_.getLogger().logError("Invalid data format for INITIALIZE_PLAYER_SKILLS event");
            sendErrorResponse("Invalid skill data format", clientID, clientSocket);
            return;
        }

        auto skillInitData = std::get<PlayerSkillInitStruct>(data);
        handleInitializePlayerSkillsDirect(skillInitData, clientID, clientSocket);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleInitializePlayerSkills: " + std::string(ex.what()));
        sendErrorResponse("Internal server error", clientID, clientSocket);
    }
}

void
SkillEventHandler::handleInitializePlayerSkillsDirect(const PlayerSkillInitStruct &skillInitData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    try
    {
        gameServices_.getLogger().log("Initializing skills for character " +
                                          std::to_string(skillInitData.characterId) +
                                          " with " + std::to_string(skillInitData.skills.size()) + " skills",
            GREEN);

        // Build skills response
        nlohmann::json response = buildSkillsResponse(skillInitData);

        // Send response to client
        sendSkillsResponse(response, clientID, clientSocket);

        gameServices_.getLogger().log("Player skills initialized successfully for client " +
                                          std::to_string(clientID),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleInitializePlayerSkillsDirect: " + std::string(ex.what()));
        sendErrorResponse("Failed to initialize player skills", clientID, clientSocket);
    }
}

void
SkillEventHandler::initializeFromCharacterData(const CharacterDataStruct &characterData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    try
    {
        gameServices_.getLogger().log("Initializing skills from character data for character " +
                                          std::to_string(characterData.characterId) +
                                          " (client " + std::to_string(clientID) + ")",
            GREEN);

        // Create skill initialization structure
        PlayerSkillInitStruct skillInitData;
        skillInitData.characterId = characterData.characterId;
        skillInitData.skills = characterData.skills;

        // Use direct method to avoid event overhead
        handleInitializePlayerSkillsDirect(skillInitData, clientID, clientSocket);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in initializeFromCharacterData: " + std::string(ex.what()));
        sendErrorResponse("Failed to initialize skills from character data", clientID, clientSocket);
    }
}

nlohmann::json
SkillEventHandler::buildSkillsResponse(const PlayerSkillInitStruct &skillInitData)
{
    nlohmann::json response;

    // Create header structure
    response["header"]["eventType"] = "initializePlayerSkills";
    response["header"]["message"] = "Player skills initialized successfully";

    // Create body structure
    response["body"]["characterId"] = skillInitData.characterId;

    nlohmann::json skillsArray = nlohmann::json::array();

    gameServices_.getLogger().log("Building skills response for " + std::to_string(skillInitData.skills.size()) + " skills", GREEN);

    for (size_t i = 0; i < skillInitData.skills.size(); ++i)
    {
        const auto &skill = skillInitData.skills[i];

        gameServices_.getLogger().log("Processing skill " + std::to_string(i) + ": " + skill.skillName + " (" + skill.skillSlug + ")", GREEN);

        try
        {
            nlohmann::json skillData;
            skillData["skillSlug"] = skill.skillSlug;
            skillData["skillLevel"] = skill.skillLevel;
            skillData["coeff"] = skill.coeff;
            skillData["flatAdd"] = skill.flatAdd;
            skillData["cooldownMs"] = skill.cooldownMs;
            skillData["gcdMs"] = skill.gcdMs;
            skillData["castMs"] = skill.castMs;
            skillData["costMp"] = skill.costMp;
            skillData["maxRange"] = skill.maxRange;

            skillsArray.push_back(skillData);
            gameServices_.getLogger().log("Successfully processed skill " + std::to_string(i), GREEN);
        }
        catch (const std::exception &ex)
        {
            gameServices_.getLogger().logError("Error processing skill " + std::to_string(i) + ": " + std::string(ex.what()));
            continue; // Skip this skill and continue with the next one
        }
    }

    response["body"]["skills"] = skillsArray;
    gameServices_.getLogger().log("Skills response built with " + std::to_string(skillsArray.size()) + " skills", GREEN);
    return response;
}

void
SkillEventHandler::sendSkillsResponse(const nlohmann::json &response, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    if (!clientSocket)
    {
        gameServices_.getLogger().log("Client socket not found for player skills initialization", YELLOW);
        return;
    }

    std::string responseData = networkManager_.generateResponseMessage("success", response);
    networkManager_.sendResponse(clientSocket, responseData);
}

void
SkillEventHandler::sendErrorResponse(const std::string &errorMessage, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    if (!clientSocket)
    {
        return;
    }

    nlohmann::json errorResponse;
    errorResponse["header"]["eventType"] = "initializePlayerSkills";
    errorResponse["header"]["message"] = errorMessage;
    errorResponse["body"]["error"] = errorMessage;

    std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
    networkManager_.sendResponse(clientSocket, errorData);
}
