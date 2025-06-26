#include "events/EventHandler.hpp"
#include "events/Event.hpp"

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices
    )
    : networkManager_(networkManager),
      gameServerWorker_(gameServerWorker),
      gameServices_(gameServices)
{
}

void EventHandler::handleJoinChunkEvent(const Event &event)
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

void EventHandler::handleJoinClientEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();
    
    //set client socket
    gameServices_.getClientManager().setClientSocket(clientID, clientSocket);

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
                               .setHeader("eventType", "joinGame")
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
                           .setHeader("message", "Authentication success for user!")
                           .setHeader("hash", passedClientData.hash)
                           .setHeader("clientId", passedClientData.clientId)
                           .setHeader("eventType", "joinGame")
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            //  Get all existing clients data as array
            std::vector<ClientDataStruct> clientDataMap =  gameServices_.getClientManager().getClientsList();

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

void EventHandler::handleMoveCharacterClientEvent(const Event &event)
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
            
            // set character position data
            gameServices_.getCharacterManager().setCharacterPosition(passedCharacterData.characterId, passedCharacterData.characterPosition);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Movement failed for character!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "moveCharacter")
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
                           .setHeader("message", "Movement success for character!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "moveCharacter")
                           .setBody("characterId", passedCharacterData.characterId)
                           .setBody("posX", passedCharacterData.characterPosition.positionX)
                           .setBody("posY", passedCharacterData.characterPosition.positionY)
                           .setBody("posZ", passedCharacterData.characterPosition.positionZ)
                           .setBody("rotZ", passedCharacterData.characterPosition.rotationZ)
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

// get connected characters list
void EventHandler::handleGetConnectedCharactersChunkEvent(const Event &event)
{
    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    // get client socket
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getSocket();

    try
    {
            // get all connected characters
            std::vector<CharacterDataStruct> charactersList = gameServices_.getCharacterManager().getCharactersList();


            //TODO - FIX THIS CODE WITH JSON

            // convert the charactersList to json
            nlohmann::json charactersListJson = nlohmann::json::array();
            for (const auto &character : charactersList)
            {
                nlohmann::json characterJson = {
                    {"characterId", character.characterId},
                    {"characterLevel", character.characterLevel},
                    {"characterExperiencePoints", character.characterExperiencePoints},
                    {"characterCurrentHealth", character.characterCurrentHealth},
                    {"characterCurrentMana", character.characterCurrentMana},
                    {"characterMaxHealth", character.characterMaxHealth},
                    {"characterMaxMana", character.characterMaxMana},
                    {"expForNextLevel", character.expForNextLevel},
                    {"characterName", character.characterName},
                    {"characterClass", character.characterClass},
                    {"characterRace", character.characterRace},
                    {"characterPosition", {
                        {"positionX", character.characterPosition.positionX},
                        {"positionY", character.characterPosition.positionY},
                        {"positionZ", character.characterPosition.positionZ},
                        {"rotationZ", character.characterPosition.rotationZ}
                    }}
                };
                charactersListJson.push_back(characterJson);
            }

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Getting connected characters failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "getConnectedCharacters")
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
                           .setHeader("message", "Getting connected characters success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "getConnectedCharacters")
                           .setBody("charactersList", charactersListJson)
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

// disconnect the client
void EventHandler::handleDisconnectClientEvent(const Event &event)
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

            if(passedClientData.clientId == 0)
            {
                gameServices_.getLogger().log("Client ID is 0, so we will just remove client from our list!");

                  // Remove the client data
                gameServices_.getClientManager().removeClientDataBySocket(passedClientData.socket);

                return;
            }

            // Remove the client data
            gameServices_.getClientManager().removeClientData(passedClientData.clientId);

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
void EventHandler::handleDisconnectChunkEvent(const Event &event)
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
void EventHandler::handlePingClientEvent(const Event &event)
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

void EventHandler::handleSpawnMobsInZoneEvent(const Event &event)
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
            //format the mob data to json using for loop
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

                for(auto &mobAttributeItem : mob.attributes)
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

void EventHandler::handleGetSpawnZoneDataEvent(const Event &event)
{
    //  TODO - Implement this method
}

void EventHandler::handleGetMobDataEvent(const Event &event)
{
    //  TODO - Implement this method
}

void EventHandler::handleZoneMoveMobsEvent(const Event &event)
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

            //TODO - Review attributes, do we need all this data for mob? mb just coordinates?
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

                for(auto &mobAttributeItem : mob.attributes)
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

void EventHandler::handleSetAllMobsListEvent(const Event &event)
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
            
            //set data to the mob manager
            gameServices_.getMobManager().loadListOfAllMobs(mobsList);
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
void EventHandler::handleSetMobsAttributesEvent(const Event &event)
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
            
            //set data to the mob manager
            gameServices_.getMobManager().loadListOfMobsAttributes(mobAttributesList);
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
void EventHandler::handleSetAllSpawnZonesEvent(const Event &event)
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
            
            //set data to the mob manager
            gameServices_.getSpawnZoneManager().loadMobSpawnZones(spawnZonesList);
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
void EventHandler::handleSetCharacterDataEvent(const Event &event)
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
            
            //set data to the mob manager
            gameServices_.getCharacterManager().loadCharacterData(characterData);
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
void EventHandler::handleSetCharactersListEvent(const Event &event)
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
            
            //set data to the mob manager
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

//set character attributes
void EventHandler::handleSetCharacterAttributesEvent(const Event &event)
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
            
            //set data to the mob manager
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


void EventHandler::handleInitChunkEvent(const Event &event)
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
                               .setHeader("eventType", "setChunkData")
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
                            .setHeader("eventType", "setChunkData")
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

   //TODO - Analyze code for events and refactor it

void EventHandler::dispatchEvent(const Event& event)
{
    switch (event.getType())
    {
    case Event::PING_CLIENT:
        handlePingClientEvent(event);
        break;
    case Event::SET_CHUNK_DATA:
        handleInitChunkEvent(event);
    break;
    case Event::JOIN_CLIENT:
        handleJoinClientEvent(event);
        break;
    case Event::JOIN_CLIENT_CHUNK:
        handleJoinChunkEvent(event);
        break;
    case Event::GET_CONNECTED_CHARACTERS_CHUNK:
        handleGetConnectedCharactersChunkEvent(event);
        break;
    case Event::MOVE_CHARACTER_CLIENT:
        handleMoveCharacterClientEvent(event);
        break;
    case Event::DISCONNECT_CLIENT:
        handleDisconnectClientEvent(event);
        break;
    case Event::DISCONNECT_CLIENT_CHUNK:
        handleDisconnectChunkEvent(event);
        break;
    case Event::SPAWN_MOBS_IN_ZONE:
        handleSpawnMobsInZoneEvent(event);
        break;
    case Event::GET_SPAWN_ZONE_DATA:
        handleGetSpawnZoneDataEvent(event);
        break;
    case Event::GET_MOB_DATA:
        handleGetMobDataEvent(event);
        break;
    case Event::SPAWN_ZONE_MOVE_MOBS:
        handleZoneMoveMobsEvent(event);
        break;
    case Event::SET_ALL_MOBS_LIST
        : handleSetAllMobsListEvent(event);
        break;
    case Event::SET_ALL_MOBS_ATTRIBUTES
        : handleSetMobsAttributesEvent(event);
        break;
    case Event::SET_ALL_SPAWN_ZONES
        : handleSetAllSpawnZonesEvent(event);
        break;
    case Event::SET_CHARACTER_DATA
        : handleSetCharacterDataEvent(event);
        break;
    case Event::SET_CONNECTED_CHARACTERS_LIST
        : handleSetCharactersListEvent(event);
        break;
    }
}