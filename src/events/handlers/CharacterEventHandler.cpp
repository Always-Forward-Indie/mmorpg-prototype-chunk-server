#include "events/handlers/CharacterEventHandler.hpp"
#include "events/EventData.hpp"
#include "events/handlers/NPCEventHandler.hpp"
#include "utils/TimestampUtils.hpp"

CharacterEventHandler::CharacterEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices),
      skillEventHandler_(nullptr),
      npcEventHandler_(nullptr)
{
}

void
CharacterEventHandler::setSkillEventHandler(SkillEventHandler *skillEventHandler)
{
    skillEventHandler_ = skillEventHandler;
}

void
CharacterEventHandler::setNPCEventHandler(NPCEventHandler *npcEventHandler)
{
    npcEventHandler_ = npcEventHandler;
}

bool
CharacterEventHandler::validateCharacterAuthentication(int clientId, int characterId)
{
    return clientId != 0 && characterId != 0;
}

nlohmann::json
CharacterEventHandler::characterToJson(const CharacterDataStruct &characterData)
{
    // Получаем опыт для текущего уровня (предыдущего порога)
    int expForCurrentLevel = gameServices_.getExperienceManager().getExperienceForLevel(characterData.characterLevel);

    nlohmann::json characterJson = {
        {"id", characterData.characterId},
        {"name", characterData.characterName},
        {"class", characterData.characterClass},
        {"race", characterData.characterRace},
        {"level", characterData.characterLevel},
        {"exp", {{"current", characterData.characterExperiencePoints}, {"levelStart", expForCurrentLevel}, {"levelEnd", characterData.expForNextLevel}}},
        {"stats", {{"health", {{"current", characterData.characterCurrentHealth}, {"max", characterData.characterMaxHealth}}}, {"mana", {{"current", characterData.characterCurrentMana}, {"max", characterData.characterMaxMana}}}}},
        {"position", {{"x", characterData.characterPosition.positionX}, {"y", characterData.characterPosition.positionY}, {"z", characterData.characterPosition.positionZ}, {"rotationZ", characterData.characterPosition.rotationZ}}},
        {"attributes", nlohmann::json::array()}};

    // Add attributes to character json
    for (const auto &attribute : characterData.attributes)
    {
        characterJson["attributes"].push_back({{"id", attribute.id},
            {"name", attribute.name},
            {"slug", attribute.slug},
            {"value", attribute.value}});
    }

    return characterJson;
}

void
CharacterEventHandler::handleJoinCharacterEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    const auto &timestamps = event.getTimestamps();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            gameServices_.getLogger().log("Passed Character ID: " + std::to_string(passedCharacterData.characterId));

            // set client character ID - now handles missing clients automatically
            gameServices_.getClientManager().setClientCharacterId(clientID, passedCharacterData.characterId);

            // Get character data
            CharacterDataStruct characterData = gameServices_.getCharacterManager().getCharacterData(passedCharacterData.characterId);

            // Check if character data exists in local storage
            if (characterData.characterId == 0)
            {
                gameServices_.getLogger().log("Character ID " + std::to_string(passedCharacterData.characterId) + " not found in local storage, adding to pending requests");

                // Add this request to pending list
                PendingJoinRequest pendingRequest;
                pendingRequest.clientID = clientID;
                pendingRequest.characterId = passedCharacterData.characterId;
                pendingRequest.timestamps = timestamps;
                pendingRequest.clientSocket = clientSocket;

                pendingJoinRequests_[passedCharacterData.characterId].push_back(pendingRequest);

                gameServices_.getLogger().log("Pending join request added for character ID: " + std::to_string(passedCharacterData.characterId) + ", total pending: " + std::to_string(pendingJoinRequests_[passedCharacterData.characterId].size()));
                return;
            }

            // Character data is available, process current request immediately
            gameServices_.getLogger().log("Character ID " + std::to_string(passedCharacterData.characterId) + " found in local storage, processing immediately");

            // Prepare character data in json format
            nlohmann::json characterJson = characterToJson(characterData);

            // Validate authentication
            if (!validateCharacterAuthentication(clientID, characterData.characterId))
            {
                sendErrorResponseWithTimestamps(clientSocket, "Authentication failed for character!", "joinGameCharacter", clientID, timestamps);
                return;
            }

            // Prepare and broadcast success response to all clients (including sender)
            nlohmann::json broadcastResponse = ResponseBuilder()
                                                   .setHeader("message", "Authentication success for character!")
                                                   .setHeader("hash", "")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "joinGameCharacter")
                                                   .setTimestamps(timestamps)
                                                   .setBody("character", characterJson)
                                                   .build();

            broadcastToAllClientsWithTimestamps("success", broadcastResponse, timestamps);

            // Initialize player skills after successful character join
            if (skillEventHandler_)
            {
                skillEventHandler_->initializeFromCharacterData(characterData, clientID, clientSocket);
            }
            else
            {
                gameServices_.getLogger().logError("SkillEventHandler not set in CharacterEventHandler");
            }

            // Send NPC spawn data to player after successful character join
            if (npcEventHandler_)
            {
                npcEventHandler_->sendNPCSpawnDataToClient(clientID, characterData.characterPosition, 50000.0f);
            }
            else
            {
                gameServices_.getLogger().logError("NPCEventHandler not set in CharacterEventHandler");
            }

            // Also process any pending requests for this character
            processPendingJoinRequests(passedCharacterData.characterId);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleMoveCharacterEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    const auto &timestamps = event.getTimestamps();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<MovementDataStruct>(data))
        {
            const MovementDataStruct &movementData = std::get<MovementDataStruct>(data);

            // Update character position on server
            gameServices_.getCharacterManager().setCharacterPosition(
                movementData.characterId, movementData.position);

            // Create minimal character data for response (only essential movement data)
            nlohmann::json characterJson = {
                {"id", movementData.characterId},
                {"position", {{"x", movementData.position.positionX}, {"y", movementData.position.positionY}, {"z", movementData.position.positionZ}, {"rotationZ", movementData.position.rotationZ}}}};

            // Validate authentication
            if (clientID == 0)
            {
                sendErrorResponseWithTimestamps(clientSocket, "Movement failed for character!", "moveCharacter", clientID, timestamps);
                return;
            }

            // Prepare and broadcast movement response
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Movement success for character!")
                                                 .setHeader("hash", "")
                                                 .setHeader("clientId", clientID)
                                                 .setHeader("eventType", "moveCharacter")
                                                 .setTimestamps(timestamps)
                                                 .setBody("character", characterJson)
                                                 .build();

            gameServices_.getLogger().log("Client data map size: " + std::to_string(gameServices_.getClientManager().getClientsList().size()));

            // Broadcast to all clients with timestamps
            broadcastToAllClientsWithTimestamps("success", successResponse, timestamps);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data in moveCharacter - variant doesn't contain MovementDataStruct!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Bad variant access in handleMoveCharacterEvent: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("General error in handleMoveCharacterEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleGetConnectedCharactersEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    const auto &timestamps = event.getTimestamps();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        std::vector<CharacterDataStruct> charactersList = gameServices_.getCharacterManager().getCharactersList();

        nlohmann::json charactersListJson = nlohmann::json::array();
        for (const auto &character : charactersList)
        {
            nlohmann::json characterJson = characterToJson(character);

            nlohmann::json fullEntry = {
                {"clientId", character.clientId},
                {"character", characterJson}};

            charactersListJson.push_back(fullEntry);
        }

        if (clientID == 0)
        {
            sendErrorResponseWithTimestamps(clientSocket, "Getting connected characters failed!", "getConnectedCharacters", clientID, timestamps);
            return;
        }

        sendSuccessResponseWithTimestamps(clientSocket, "Getting connected characters success!", "getConnectedCharacters", clientID, timestamps, "characters", charactersListJson);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleGetConnectedCharactersEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSetCharacterDataEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct characterData = std::get<CharacterDataStruct>(data);
            gameServices_.getCharacterManager().addCharacter(characterData);

            // Process any pending join requests for this character
            processPendingJoinRequests(characterData.characterId);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSetCharactersListEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            std::vector<CharacterDataStruct> charactersList = std::get<std::vector<CharacterDataStruct>>(data);
            gameServices_.getCharacterManager().loadCharactersList(charactersList);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSetCharacterAttributesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<CharacterAttributeStruct>>(data))
        {
            std::vector<CharacterAttributeStruct> characterAttributesList = std::get<std::vector<CharacterAttributeStruct>>(data);
            gameServices_.getCharacterManager().loadCharacterAttributes(characterAttributesList);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::processPendingJoinRequests(int characterId)
{
    gameServices_.getLogger().log("processPendingJoinRequests called for character ID: " + std::to_string(characterId));
    gameServices_.getLogger().log("Current pendingJoinRequests_ size: " + std::to_string(pendingJoinRequests_.size()));

    auto it = pendingJoinRequests_.find(characterId);
    if (it == pendingJoinRequests_.end())
    {
        gameServices_.getLogger().log("No pending requests map entry found for character ID: " + std::to_string(characterId));
        return;
    }

    if (it->second.empty())
    {
        gameServices_.getLogger().log("Pending requests vector is empty for character ID: " + std::to_string(characterId));
        return;
    }

    gameServices_.getLogger().log("Processing " + std::to_string(it->second.size()) + " pending join requests for character ID: " + std::to_string(characterId));

    // Get character data
    CharacterDataStruct characterData = gameServices_.getCharacterManager().getCharacterData(characterId);

    if (characterData.characterId == 0)
    {
        gameServices_.getLogger().logError("Character data still not available for ID: " + std::to_string(characterId));
        return;
    }

    // Process all pending requests for this character
    for (const auto &request : it->second)
    {
        // Prepare character data in json format
        nlohmann::json characterJson = characterToJson(characterData);

        // Validate authentication
        if (!validateCharacterAuthentication(request.clientID, characterData.characterId))
        {
            sendErrorResponseWithTimestamps(request.clientSocket, "Authentication failed for character!", "joinGameCharacter", request.clientID, request.timestamps);
            continue;
        }

        // Prepare and broadcast success response to all clients (including sender)
        nlohmann::json broadcastResponse = ResponseBuilder()
                                               .setHeader("message", "Authentication success for character!")
                                               .setHeader("hash", "")
                                               .setHeader("clientId", request.clientID)
                                               .setHeader("eventType", "joinGameCharacter")
                                               .setTimestamps(request.timestamps)
                                               .setBody("character", characterJson)
                                               .build();

        broadcastToAllClientsWithTimestamps("success", broadcastResponse, request.timestamps);

        // Initialize player skills after successful character join for pending request
        if (skillEventHandler_)
        {
            skillEventHandler_->initializeFromCharacterData(characterData, request.clientID, request.clientSocket);
        }
        else
        {
            gameServices_.getLogger().logError("SkillEventHandler not set in CharacterEventHandler");
        }

        gameServices_.getLogger().log("Processed pending join request for client ID: " + std::to_string(request.clientID) + ", character ID: " + std::to_string(characterId));
    }

    // Clear processed requests
    pendingJoinRequests_.erase(it);
    gameServices_.getLogger().log("Cleared pending requests for character ID: " + std::to_string(characterId));
}
