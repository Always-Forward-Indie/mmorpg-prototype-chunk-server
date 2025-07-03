#include "events/EventHandler.hpp"
#include "events/Event.hpp"

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : networkManager_(networkManager),
      gameServerWorker_(gameServerWorker),
      gameServices_(gameServices)
{
}

void
EventHandler::handleJoinChunkEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Set the current client data
            gameServices_.getClientManager().loadClientData(passedClientData);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "joinGame")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response
                gameServerWorker_.sendDataToGameServer(responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", passedClientData.characterId)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send data to the the game server
            gameServerWorker_.sendDataToGameServer(responseData);
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
EventHandler::handleJoinClientEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedClientData.clientId == 0 || passedClientData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", passedClientData.hash)
                               .setHeader("clientId", passedClientData.clientId)
                               .setHeader("eventType", "joinGameClient")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Set the current client data
            gameServices_.getClientManager().loadClientData(passedClientData);

            // set client socket
            gameServices_.getClientManager().setClientSocket(clientID, clientSocket);

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGameClient")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            //  Get all existing clients data as array
            std::vector<ClientDataStruct> clientDataMap = gameServices_.getClientManager().getClientsList();

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // Send the response to the current item Client
                networkManager_.sendResponse(clientDataItem.socket, responseData);
            }
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
EventHandler::handleGetConnectedClientsChunkEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
        // get all connected clients
        std::vector<ClientDataStruct> clientsList = gameServices_.getClientManager().getClientsList();

        // Convert the clientsList to json
        nlohmann::json clientsListJson = nlohmann::json::array();
        for (const auto &client : clientsList)
        {
            nlohmann::json clientJson = {
                {"clientId", client.clientId},
                {"characterId", client.characterId},
                {"status", client.socket ? "connected" : "disconnected"} // Indicate if the socket is connected or not
            };
            clientsListJson.push_back(clientJson);
        }

        // Prepare the response message
        nlohmann::json response;
        ResponseBuilder builder;

        // Check if the authentication is not successful
        if (clientID == 0)
        {
            // Add response data
            response = builder
                           .setHeader("message", "Getting connected clients failed!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "getConnectedClients")
                           .setBody("", "")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("error", response);
            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
            return;
        }

        //  Add the message to the response
        response = builder
                       .setHeader("message", "Getting connected clients success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "getConnectedClients")
                       .setBody("clientsList", clientsListJson)
                       .build();
        // Prepare a response message
        std::string responseData = networkManager_.generateResponseMessage("success", response);

        // Send the response to the client
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
EventHandler::handleJoinCharacterEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            // debug character ID
            gameServices_.getLogger().log("Passed Character ID: " + std::to_string(passedCharacterData.characterId));

            // get character data
            CharacterDataStruct characterData = gameServices_.getCharacterManager().getCharacterData(passedCharacterData.characterId);

            // prepare character data in json format
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

            // add attributes to character json
            for (const auto &attribute : characterData.attributes)
            {
                characterJson["attributes"].push_back({{"id", attribute.id},
                    {"name", attribute.name},
                    {"slug", attribute.slug},
                    {"value", attribute.value}});
            }

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0 || characterData.characterId == 0)
            {
                response = builder
                               .setHeader("message", "Authentication failed for character!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "joinGameCharacter")
                               .setBody("character", characterJson)
                               .build();

                std::string responseData = networkManager_.generateResponseMessage("error", response);
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Authentication success for character!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "joinGameCharacter")
                           .setBody("character", characterJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            //  Get all existing clients data as array
            std::vector<ClientDataStruct> clientDataMap = gameServices_.getClientManager().getClientsList();

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // Send the response to the current item Client
                networkManager_.sendResponse(clientDataItem.socket, responseData);
            }
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
EventHandler::handleMoveCharacterClientEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            // Обновляем позицию персонажа на сервере
            gameServices_.getCharacterManager().setCharacterPosition(
                passedCharacterData.characterId, passedCharacterData.characterPosition);

            // prepare character data in json format
            nlohmann::json characterJson = {
                {"id", passedCharacterData.characterId},
                {"position", {{"x", passedCharacterData.characterPosition.positionX}, {"y", passedCharacterData.characterPosition.positionY}, {"z", passedCharacterData.characterPosition.positionZ}, {"rotationZ", passedCharacterData.characterPosition.rotationZ}}}};

            // Проверка авторизации
            if (clientID == 0)
            {
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Movement failed for character!")
                                                   .setHeader("hash", "")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "moveCharacter")
                                                   .setBody("character", characterJson)
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Формируем финальный ответ
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Movement success for character!")
                                                 .setHeader("hash", "")
                                                 .setHeader("clientId", clientID)
                                                 .setHeader("eventType", "moveCharacter")
                                                 .setBody("character", characterJson)
                                                 .build();

            std::string responseData = networkManager_.generateResponseMessage("success", successResponse);

            //  Get all existing clients data as array
            std::vector<ClientDataStruct> clientDataMap = gameServices_.getClientManager().getClientsList();

            // debug log client data map size
            gameServices_.getLogger().log("Client data map size: " + std::to_string(clientDataMap.size()));

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // debug log
                gameServices_.getLogger().log("Sending move character response to client ID: " + std::to_string(clientDataItem.clientId));

                networkManager_.sendResponse(clientDataItem.socket, responseData);
            }
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

// get connected characters list
void
EventHandler::handleGetConnectedCharactersChunkEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
        std::vector<CharacterDataStruct> charactersList = gameServices_.getCharacterManager().getCharactersList();

        nlohmann::json charactersListJson = nlohmann::json::array();
        for (const auto &character : charactersList)
        {
            nlohmann::json characterJson = {
                {"id", character.characterId},
                {"name", character.characterName},
                {"class", character.characterClass},
                {"race", character.characterRace},
                {"level", character.characterLevel},
                {"exp", {{"current", character.characterExperiencePoints}, {"nextLevel", character.expForNextLevel}}},
                {"stats", {{"health", {{"current", character.characterCurrentHealth}, {"max", character.characterMaxHealth}}}, {"mana", {{"current", character.characterCurrentMana}, {"max", character.characterMaxMana}}}}},
                {"position", {{"x", character.characterPosition.positionX}, {"y", character.characterPosition.positionY}, {"z", character.characterPosition.positionZ}, {"rotationZ", character.characterPosition.rotationZ}}}};

            nlohmann::json attributesJson = nlohmann::json::array();
            for (const auto &attribute : character.attributes)
            {
                attributesJson.push_back({{"id", attribute.id},
                    {"name", attribute.name},
                    {"slug", attribute.slug},
                    {"value", attribute.value}});
            }

            characterJson["attributes"] = attributesJson;

            nlohmann::json fullEntry = {
                {"clientId", character.clientId},
                {"character", characterJson}};

            charactersListJson.push_back(fullEntry);
        }

        ResponseBuilder builder;
        nlohmann::json response;

        if (clientID == 0)
        {
            response = builder
                           .setHeader("message", "Getting connected characters failed!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "getConnectedCharacters")
                           .setBody("", "")
                           .build();

            std::string responseData = networkManager_.generateResponseMessage("error", response);
            networkManager_.sendResponse(clientSocket, responseData);
            return;
        }

        response = builder
                       .setHeader("message", "Getting connected characters success!")
                       .setHeader("hash", "")
                       .setHeader("clientId", clientID)
                       .setHeader("eventType", "getConnectedCharacters")
                       .setBody("characters", charactersListJson)
                       .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleGetConnectedCharactersChunkEvent: " + std::string(ex.what()));
    }
}

// disconnect the client
void
EventHandler::handleDisconnectClientEvent(const Event &event)
{
    // Here we will disconnect the client
    const auto data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // debug log client ID
            gameServices_.getLogger().log("Handling disconnect event for client ID: " + std::to_string(passedClientData.clientId) + " and character ID: " + std::to_string(passedClientData.characterId));

            if (passedClientData.clientId == 0)
            {
                gameServices_.getLogger().log("Client ID is 0, so we will just remove client from our list!");

                // Remove the client data
                gameServices_.getClientManager().removeClientDataBySocket(passedClientData.socket);

                return;
            }

            // Remove the client data
            gameServices_.getClientManager().removeClientData(passedClientData.clientId);

            // remove character data
            gameServices_.getCharacterManager().removeCharacter(passedClientData.characterId);

            // send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Client disconnected!")
                           .setHeader("hash", "")
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "disconnectClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the all existing clients in the clientDataMap
            for (auto const &client : gameServices_.getClientManager().getClientsList())
            {
                networkManager_.sendResponse(client.socket, responseData);
            }
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

// disconnect the client
void
EventHandler::handleDisconnectChunkEvent(const Event &event)
{
    // Here we will disconnect the client
    const auto data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Client disconnected!")
                           .setHeader("hash", "")
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "disconnectClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the chunk server
            gameServerWorker_.sendDataToGameServer(responseData);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

//  ping the client
void
EventHandler::handlePingClientEvent(const Event &event)
{
    // Here we will ping the client
    const auto data = event.getData();

    gameServices_.getLogger().log("Handling PING event!", YELLOW);

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Pong!")
                           .setHeader("eventType", "pingClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            gameServices_.getLogger().log("Sending PING data to Client: " + responseData, YELLOW);

            // Send the response to the client
            networkManager_.sendResponse(passedClientData.socket, responseData);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}

void
EventHandler::handleSpawnMobsInZoneEvent(const Event &event)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<SpawnZoneStruct>(data))
        {
            SpawnZoneStruct spawnZoneData = std::get<SpawnZoneStruct>(data);

            // format the zone data to json
            nlohmann::json spawnZoneDataJson;
            // format the mob data to json using for loop
            nlohmann::json mobsListJson;

            spawnZoneDataJson["id"] = spawnZoneData.zoneId;
            spawnZoneDataJson["name"] = spawnZoneData.zoneName;
            spawnZoneDataJson["minX"] = spawnZoneData.posX;
            spawnZoneDataJson["maxX"] = spawnZoneData.sizeX;
            spawnZoneDataJson["minY"] = spawnZoneData.posY;
            spawnZoneDataJson["maxY"] = spawnZoneData.sizeY;
            spawnZoneDataJson["minZ"] = spawnZoneData.posZ;
            spawnZoneDataJson["maxZ"] = spawnZoneData.sizeZ;
            spawnZoneDataJson["spawnMobId"] = spawnZoneData.spawnMobId;
            spawnZoneDataJson["maxSpawnCount"] = spawnZoneData.spawnCount;
            spawnZoneDataJson["spawnedMobsCount"] = spawnZoneData.spawnedMobsList.size();
            spawnZoneDataJson["respawnTime"] = spawnZoneData.respawnTime.count();
            spawnZoneDataJson["spawnEnabled"] = true;

            for (auto &mob : spawnZoneData.spawnedMobsList)
            {
                nlohmann::json mobJson;
                mobJson["id"] = mob.id;
                mobJson["UID"] = mob.uid;
                mobJson["zoneId"] = mob.zoneId;
                mobJson["name"] = mob.name;
                mobJson["race"] = mob.raceName;
                mobJson["level"] = mob.level;
                mobJson["currentHealth"] = mob.currentHealth;
                mobJson["currentMana"] = mob.currentMana;
                mobJson["maxHealth"] = mob.maxHealth;
                mobJson["maxMana"] = mob.maxMana;
                mobJson["isAggressive"] = mob.isAggressive;
                mobJson["isDead"] = mob.isDead;
                mobJson["posX"] = mob.position.positionX;
                mobJson["posY"] = mob.position.positionY;
                mobJson["posZ"] = mob.position.positionZ;
                mobJson["rotZ"] = mob.position.rotationZ;

                for (auto &mobAttributeItem : mob.attributes)
                {
                    nlohmann::json mobItemJson;
                    mobItemJson["Id"] = mobAttributeItem.id;
                    mobItemJson["name"] = mobAttributeItem.name;
                    mobItemJson["slug"] = mobAttributeItem.slug;
                    mobItemJson["value"] = mobAttributeItem.value;
                    mobJson["attributesData"].push_back(mobItemJson);
                }

                mobsListJson.push_back(mobJson);
            }

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Spawning mobs failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "spawnMobsInZone")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Spawning mobs success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "spawnMobsInZone")
                           .setBody("spawnZoneData", spawnZoneDataJson)
                           .setBody("mobsData", mobsListJson)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
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
EventHandler::handleGetSpawnZoneDataEvent(const Event &event)
{
    //  TODO - Implement this method
}

void
EventHandler::handleGetMobDataEvent(const Event &event)
{
    //  TODO - Implement this method
}

void
EventHandler::handleZoneMoveMobsEvent(const Event &event)
{
    // get the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<MobDataStruct>>(data))
        {
            std::vector<MobDataStruct> mobsList = std::get<std::vector<MobDataStruct>>(data);

            // format the mob data to json using for loop
            nlohmann::json mobsListJson;

            // TODO - Review attributes, do we need all this data for mob? mb just coordinates?
            for (auto &mob : mobsList)
            {
                nlohmann::json mobJson;
                mobJson["id"] = mob.id;
                mobJson["UID"] = mob.uid;
                mobJson["zoneId"] = mob.zoneId;
                mobJson["name"] = mob.name;
                mobJson["race"] = mob.raceName;
                mobJson["level"] = mob.level;
                mobJson["currentHealth"] = mob.currentHealth;
                mobJson["currentMana"] = mob.currentMana;
                mobJson["maxHealth"] = mob.maxHealth;
                mobJson["maxMana"] = mob.maxMana;
                mobJson["posX"] = mob.position.positionX;
                mobJson["posY"] = mob.position.positionY;
                mobJson["posZ"] = mob.position.positionZ;
                mobJson["rotZ"] = mob.position.rotationZ;

                for (auto &mobAttributeItem : mob.attributes)
                {
                    nlohmann::json mobItemJson;
                    mobItemJson["id"] = mobAttributeItem.id;
                    mobItemJson["name"] = mobAttributeItem.name;
                    mobItemJson["slug"] = mobAttributeItem.slug;
                    mobItemJson["value"] = mobAttributeItem.value;
                    mobJson["attributesData"].push_back(mobItemJson);
                }

                mobsListJson.push_back(mobJson);
            }

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Moving mobs failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "zoneMoveMobs")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Moving mobs success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "zoneMoveMobs")
                           .setBody("mobsData", mobsListJson)
                           .build();

            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
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
EventHandler::handleSetAllMobsListEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<MobDataStruct>>(data))
        {
            std::vector<MobDataStruct> mobsList = std::get<std::vector<MobDataStruct>>(data);

            // set data to the mob manager
            gameServices_.getMobManager().setListOfMobs(mobsList);

            gameServices_.getLogger().log("Loaded all mobs data from the event handler!", GREEN);
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

// set mob attributes
void
EventHandler::handleSetMobsAttributesEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<MobAttributeStruct>>(data))
        {
            std::vector<MobAttributeStruct> mobAttributesList = std::get<std::vector<MobAttributeStruct>>(data);

            // set data to the mob manager
            gameServices_.getMobManager().setListOfMobsAttributes(mobAttributesList);
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

// set all spawn zones
void
EventHandler::handleSetAllSpawnZonesEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<SpawnZoneStruct>>(data))
        {
            std::vector<SpawnZoneStruct> spawnZonesList = std::get<std::vector<SpawnZoneStruct>>(data);

            // debug spawn zones
            for (const auto &spawnZone : spawnZonesList)
            {
                gameServices_.getLogger().log("Spawn Zone ID: " + std::to_string(spawnZone.zoneId) +
                                              ", Name: " + spawnZone.zoneName +
                                              ", MinX: " + std::to_string(spawnZone.posX) +
                                              ", MaxX: " + std::to_string(spawnZone.sizeX) +
                                              ", MinY: " + std::to_string(spawnZone.posY) +
                                              ", MaxY: " + std::to_string(spawnZone.sizeY) +
                                              ", MinZ: " + std::to_string(spawnZone.posZ) +
                                              ", MaxZ: " + std::to_string(spawnZone.sizeZ) +
                                              ", Spawn Mob ID: " + std::to_string(spawnZone.spawnMobId) +
                                              ", Max Spawn Count: " + std::to_string(spawnZone.spawnCount) +
                                              ", Respawn Time: " + std::to_string(spawnZone.respawnTime.count()) +
                                              ", Spawn Enabled: " + std::to_string(spawnZone.spawnEnabled));
            }

            // set data to the mob manager
            gameServices_.getSpawnZoneManager().loadMobSpawnZones(spawnZonesList);

            gameServices_.getLogger().log("Loaded all spawn zones data from the event handler!", GREEN);
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

// set character data
void
EventHandler::handleSetCharacterDataEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct characterData = std::get<CharacterDataStruct>(data);

            // set data to the mob manager
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

// set character list
void
EventHandler::handleSetCharactersListEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            std::vector<CharacterDataStruct> charactersList = std::get<std::vector<CharacterDataStruct>>(data);

            // set data to the mob manager
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

// set character attributes
void
EventHandler::handleSetCharacterAttributesEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<std::vector<CharacterAttributeStruct>>(data))
        {
            std::vector<CharacterAttributeStruct> characterAttributesList = std::get<std::vector<CharacterAttributeStruct>>(data);

            // set data to the mob manager
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
EventHandler::handleInitChunkEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ChunkInfoStruct>(data))
        {
            ChunkInfoStruct passedChunkData = std::get<ChunkInfoStruct>(data);

            // Set the current client data
            gameServices_.getChunkManager().loadChunkInfo(passedChunkData);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (passedChunkData.id == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Init failed for chunk!")
                               .setHeader("chunkId", passedChunkData.id)
                               .setHeader("eventType", "chunkServerData")
                               .setBody("", "")
                               .build();
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response
                gameServerWorker_.sendDataToGameServer(responseData);
                return;
            }

            // Add the message to the response
            response = builder
                           .setHeader("message", "Init success for chunk!")
                           .setHeader("chunkId", passedChunkData.id)
                           .setHeader("eventType", "chunkServerData")
                           .setBody("", "")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send data to the the game server
            gameServerWorker_.sendDataToGameServer(responseData);
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
EventHandler::dispatchEvent(const Event &event)
{
    switch (event.getType())
    {
    case Event::SET_CHUNK_DATA:
        handleInitChunkEvent(event);
        break;

    // Client Events
    case Event::PING_CLIENT:
        handlePingClientEvent(event);
        break;
    case Event::JOIN_CLIENT:
        handleJoinClientEvent(event);
        break;
    case Event::GET_CONNECTED_CLIENTS:
        handleGetConnectedClientsChunkEvent(event);
        break;
    case Event::DISCONNECT_CLIENT:
        handleDisconnectClientEvent(event);
        break;

    // Characters Events
    case Event::JOIN_CHARACTER:
        handleJoinCharacterEvent(event);
    case Event::GET_CONNECTED_CHARACTERS:
        handleGetConnectedCharactersChunkEvent(event);
        break;
    case Event::SET_CHARACTER_DATA:
        handleSetCharacterDataEvent(event);
        break;
    case Event::MOVE_CHARACTER:
        handleMoveCharacterClientEvent(event);
        break;

    // Spawn Zones Events
    case Event::SET_ALL_SPAWN_ZONES:
        handleSetAllSpawnZonesEvent(event);
        break;
    case Event::GET_SPAWN_ZONE_DATA:
        handleGetSpawnZoneDataEvent(event);
        break;

    // Mobs Events
    case Event::SET_ALL_MOBS_LIST:
        handleSetAllMobsListEvent(event);
        break;
    case Event::SET_ALL_MOBS_ATTRIBUTES:
        handleSetMobsAttributesEvent(event);
        break;
    case Event::GET_MOB_DATA:
        handleGetMobDataEvent(event);
        break;
    case Event::SPAWN_MOBS_IN_ZONE:
        handleSpawnMobsInZoneEvent(event);
        break;
    case Event::SPAWN_ZONE_MOVE_MOBS:
        handleZoneMoveMobsEvent(event);
        break;
    }
}