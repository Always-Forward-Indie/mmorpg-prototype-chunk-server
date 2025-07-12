#include "events/handlers/CharacterEventHandler.hpp"
#include "events/EventData.hpp"

CharacterEventHandler::CharacterEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
}

bool
CharacterEventHandler::validateCharacterAuthentication(int clientId, int characterId)
{
    return clientId != 0 && characterId != 0;
}

nlohmann::json
CharacterEventHandler::characterToJson(const CharacterDataStruct &characterData)
{
    nlohmann::json characterJson = {
        {"id", characterData.characterId},
        {"name", characterData.characterName},
        {"class", characterData.characterClass},
        {"race", characterData.characterRace},
        {"level", characterData.characterLevel},
        {"exp", {{"current", characterData.characterExperiencePoints}, {"nextLevel", characterData.expForNextLevel}}},
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
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            gameServices_.getLogger().log("Passed Character ID: " + std::to_string(passedCharacterData.characterId));

            // set client character ID
            gameServices_.getClientManager().setClientCharacterId(clientID, passedCharacterData.characterId);

            // Get character data
            CharacterDataStruct characterData = gameServices_.getCharacterManager().getCharacterData(passedCharacterData.characterId);

            // Prepare character data in json format
            nlohmann::json characterJson = characterToJson(characterData);

            // Validate authentication
            if (!validateCharacterAuthentication(clientID, characterData.characterId))
            {
                sendErrorResponse(clientSocket, "Authentication failed for character!", "joinGameCharacter", clientID);
                return;
            }

            // Prepare and broadcast success response to all clients (including sender)
            nlohmann::json broadcastResponse = ResponseBuilder()
                                                   .setHeader("message", "Authentication success for character!")
                                                   .setHeader("hash", "")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "joinGameCharacter")
                                                   .setBody("character", characterJson)
                                                   .build();

            std::string responseData = networkManager_.generateResponseMessage("success", broadcastResponse);
            broadcastToAllClients(responseData); // This includes the sender, so no need for separate sendSuccessResponse
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
                sendErrorResponse(clientSocket, "Movement failed for character!", "moveCharacter", clientID);
                return;
            }

            // Prepare and broadcast movement response
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Movement success for character!")
                                                 .setHeader("hash", "")
                                                 .setHeader("clientId", clientID)
                                                 .setHeader("eventType", "moveCharacter")
                                                 .setBody("character", characterJson)
                                                 .build();

            std::string responseData = networkManager_.generateResponseMessage("success", successResponse);

            gameServices_.getLogger().log("Client data map size: " + std::to_string(gameServices_.getClientManager().getClientsList().size()));

            // Broadcast to all clients
            broadcastToAllClients(responseData);
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
            sendErrorResponse(clientSocket, "Getting connected characters failed!", "getConnectedCharacters", clientID);
            return;
        }

        sendSuccessResponse(clientSocket, "Getting connected characters success!", "getConnectedCharacters", clientID, "characters", charactersListJson);
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
