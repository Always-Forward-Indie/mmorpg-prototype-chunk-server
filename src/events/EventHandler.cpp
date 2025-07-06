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

// Helper method to safely get client socket
std::shared_ptr<boost::asio::ip::tcp::socket>
EventHandler::getClientSocket(const Event &event)
{
    int clientID = event.getClientID();

    // Always get socket from ClientManager, never from Event
    // This prevents use-after-free issues with socket references in Events
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = nullptr;
    try
    {
        clientSocket = gameServices_.getClientManager().getClientSocket(clientID);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting socket for client ID " + std::to_string(clientID) + ": " + e.what(), RED);
        clientSocket = nullptr;
    }

    return clientSocket;
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
    // get client socket using safe method
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

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
                if (clientSocket && clientSocket->is_open())
                {
                    try
                    {
                        networkManager_.sendResponse(clientSocket, responseData);
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError("Error sending error response in joinClient: " + std::string(ex.what()));
                    }
                }
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

            //  Get all existing clients data as array (this now automatically cleans up invalid clients)
            std::vector<ClientDataStruct> clientDataMap;
            try
            {
                clientDataMap = gameServices_.getClientManager().getClientsList();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error getting client list in handleJoinClientEvent: " + std::string(ex.what()));
                return;
            }

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // Get socket for this client using ClientManager
                auto itemSocket = gameServices_.getClientManager().getClientSocket(clientDataItem.clientId);

                // Validate socket before using
                if (itemSocket && itemSocket->is_open())
                {
                    try
                    {
                        // Send the response to the current item Client
                        networkManager_.sendResponse(itemSocket, responseData);
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError("Error sending join notification to client " +
                                                           std::to_string(clientDataItem.clientId) + ": " + ex.what());
                    }
                }
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
    // get client socket using safe method
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        // get all connected clients (this now automatically cleans up invalid clients)
        std::vector<ClientDataStruct> clientsList;
        try
        {
            clientsList = gameServices_.getClientManager().getClientsList();
        }
        catch (const std::exception &ex)
        {
            gameServices_.getLogger().logError("Error getting client list in handleGetConnectedClientsChunkEvent: " + std::string(ex.what()));

            // Send error response
            nlohmann::json errorResponse = ResponseBuilder()
                                               .setHeader("message", "Getting connected clients failed!")
                                               .setHeader("hash", "")
                                               .setHeader("clientId", clientID)
                                               .setHeader("eventType", "getConnectedClients")
                                               .setBody("", "")
                                               .build();
            std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket && clientSocket->is_open())
            {
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Convert the clientsList to json
        nlohmann::json clientsListJson = nlohmann::json::array();
        for (const auto &client : clientsList)
        {
            // Check socket status using ClientManager
            auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
            bool isConnected = clientSocket && clientSocket->is_open();

            nlohmann::json clientJson = {
                {"clientId", client.clientId},
                {"characterId", client.characterId},
                {"status", isConnected ? "connected" : "disconnected"} // Indicate if the socket is connected or not
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
            if (clientSocket && clientSocket->is_open())
            {
                try
                {
                    networkManager_.sendResponse(clientSocket, responseData);
                }
                catch (const std::exception &ex)
                {
                    gameServices_.getLogger().logError("Error sending error response in getConnectedClients: " + std::string(ex.what()));
                }
            }
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
        if (clientSocket && clientSocket->is_open())
        {
            try
            {
                networkManager_.sendResponse(clientSocket, responseData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error sending response in getConnectedClients: " + std::string(ex.what()));
            }
        }
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
    // get client socket using safe method
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

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
                if (clientSocket && clientSocket->is_open())
                {
                    try
                    {
                        networkManager_.sendResponse(clientSocket, responseData);
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError("Error sending error response in joinCharacter: " + std::string(ex.what()));
                    }
                }
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

            //  Get all existing clients data as array (this now automatically cleans up invalid clients)
            std::vector<ClientDataStruct> clientDataMap;
            try
            {
                clientDataMap = gameServices_.getClientManager().getClientsList();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error getting client list in handleJoinCharacterEvent: " + std::string(ex.what()));
                return;
            }

            // Iterate through all exist clients to send data to them
            for (const auto &clientDataItem : clientDataMap)
            {
                // Get socket for this client using ClientManager
                auto itemSocket = gameServices_.getClientManager().getClientSocket(clientDataItem.clientId);

                // Validate socket before using
                if (itemSocket && itemSocket->is_open())
                {
                    try
                    {
                        // Send the response to the current item Client
                        networkManager_.sendResponse(itemSocket, responseData);
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError("Error sending character join notification to client " +
                                                           std::to_string(clientDataItem.clientId) + ": " + ex.what());
                    }
                }
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
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<MovementDataStruct>(data))
        {
            // Use const reference to avoid copying the struct
            const MovementDataStruct &movementData = std::get<MovementDataStruct>(data);

            // Обновляем позицию персонажа на сервере
            gameServices_.getCharacterManager().setCharacterPosition(
                movementData.characterId, movementData.position);

            // Create minimal character data for response (only essential movement data)
            nlohmann::json characterJson = {
                {"id", movementData.characterId},
                {"position", {{"x", movementData.position.positionX}, {"y", movementData.position.positionY}, {"z", movementData.position.positionZ}, {"rotationZ", movementData.position.rotationZ}}}};

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
                if (clientSocket && clientSocket->is_open())
                {
                    networkManager_.sendResponse(clientSocket, responseData);
                }

                return;
            }

            // Формируем финальный ответ один раз
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Movement success for character!")
                                                 .setHeader("hash", "")
                                                 .setHeader("clientId", clientID)
                                                 .setHeader("eventType", "moveCharacter")
                                                 .setBody("character", characterJson)
                                                 .build();

            std::string responseData = networkManager_.generateResponseMessage("success", successResponse);

            //  Get all existing clients data as array (this now automatically cleans up invalid clients)
            std::vector<ClientDataStruct> clientDataMap;
            try
            {
                clientDataMap = gameServices_.getClientManager().getClientsList();
                // Reserve capacity to avoid reallocations during iteration
                if (clientDataMap.capacity() > clientDataMap.size() * 2)
                {
                    clientDataMap.shrink_to_fit();
                }
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error getting client list in handleMoveCharacterClientEvent: " + std::string(ex.what()));
                return;
            }

            // debug log client data map size
            gameServices_.getLogger().log("Client data map size: " + std::to_string(clientDataMap.size()));

            // Send response data to all clients using the pre-created response
            for (const auto &clientDataItem : clientDataMap)
            {
                // debug log
                gameServices_.getLogger().log("Sending move character response to client ID: " + std::to_string(clientDataItem.clientId));

                // Get socket for this client using ClientManager
                auto itemSocket = gameServices_.getClientManager().getClientSocket(clientDataItem.clientId);

                // Validate socket before using
                if (itemSocket && itemSocket->is_open())
                {
                    try
                    {
                        // Reuse the same responseData string - no need to recreate for each client
                        networkManager_.sendResponse(itemSocket, responseData);
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError("Error sending move character notification to client " +
                                                           std::to_string(clientDataItem.clientId) + ": " + ex.what());
                    }
                }
                else
                {
                    gameServices_.getLogger().logError("Invalid or closed socket for client ID: " + std::to_string(clientDataItem.clientId));
                }
            }
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data in moveCharacter - variant doesn't contain MovementDataStruct or CharacterDataStruct!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Bad variant access in handleMoveCharacterClientEvent: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("General error in handleMoveCharacterClientEvent: " + std::string(ex.what()));
    }
}

// get connected characters list
void
EventHandler::handleGetConnectedCharactersChunkEvent(const Event &event)
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
                gameServices_.getLogger().log("Client ID is 0, handling graceful disconnect without specific client identification!");

                // Since we don't have a valid client ID or socket reference,
                // we can't identify the specific client to remove.
                // This usually happens when a client disconnects abruptly.
                // We'll let the cleanup happen elsewhere (e.g., periodic cleanup or when sending fails).

                gameServices_.getLogger().log("Disconnect event processed for unknown client.");
                return;
            }

            // Get the list of clients BEFORE removing the disconnecting client
            std::vector<ClientDataStruct> clientDataMap;
            try
            {
                clientDataMap = gameServices_.getClientManager().getClientsList();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error getting client list in handleDisconnectClientEvent: " + std::string(ex.what()));
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
            for (auto const &client : clientDataMap)
            {
                // Skip the disconnecting client and get socket using ClientManager
                if (client.clientId != passedClientData.clientId)
                {
                    auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);

                    // Validate socket before using
                    if (clientSocket && clientSocket->is_open())
                    {
                        try
                        {
                            networkManager_.sendResponse(clientSocket, responseData);
                        }
                        catch (const std::exception &ex)
                        {
                            gameServices_.getLogger().logError("Error sending disconnect notification to client " +
                                                               std::to_string(client.clientId) + ": " + ex.what());
                        }
                    }
                }
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
    // Get the socket using safe method
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (!clientSocket->is_open())
        {
            gameServices_.getLogger().log("Skipping ping - socket is closed for client ID: " + std::to_string(event.getClientID()), GREEN);
            return;
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().log("Skipping ping - socket validation failed for client ID: " + std::to_string(event.getClientID()) + " - " + e.what(), GREEN);
        return;
    }

    // Here we will ping the client
    const auto data = event.getData();

    gameServices_.getLogger().log("Handling PING event for client ID: " + std::to_string(event.getClientID()), GREEN);

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

            gameServices_.getLogger().log("Sending PING response to Client ID: " + std::to_string(event.getClientID()), GREEN);

            // Use the validated socket from the event, not from the data
            try
            {
                networkManager_.sendResponse(clientSocket, responseData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error sending ping response to client ID " + std::to_string(event.getClientID()) + ": " + std::string(ex.what()));
            }
        }
        else
        {
            gameServices_.getLogger().logError("Error extracting data from ping event for client ID: " + std::to_string(event.getClientID()));
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Variant access error in ping event for client ID " + std::to_string(event.getClientID()) + ": " + std::string(ex.what()));
    }
}

void
EventHandler::handleSpawnMobsInZoneEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<SpawnZoneStruct>(data))
        {
            SpawnZoneStruct spawnZoneData = std::get<SpawnZoneStruct>(data);

            // Build JSON for spawn zone
            nlohmann::json zoneJson = {
                {"id", spawnZoneData.zoneId},
                {"name", spawnZoneData.zoneName},
                {"bounds", {{"minX", spawnZoneData.posX}, {"maxX", spawnZoneData.sizeX}, {"minY", spawnZoneData.posY}, {"maxY", spawnZoneData.sizeY}, {"minZ", spawnZoneData.posZ}, {"maxZ", spawnZoneData.sizeZ}}},
                {"spawnMobId", spawnZoneData.spawnMobId},
                {"maxSpawnCount", spawnZoneData.spawnCount},
                {"spawnedMobsCount", spawnZoneData.spawnedMobsList.size()},
                {"respawnTime", spawnZoneData.respawnTime.count()},
                {"spawnEnabled", true}};

            // Add mobs
            nlohmann::json mobsArray = nlohmann::json::array();
            for (const auto &mob : spawnZoneData.spawnedMobsList)
            {
                nlohmann::json mobJson = {
                    {"id", mob.id},
                    {"uid", mob.uid},
                    {"zoneId", mob.zoneId},
                    {"name", mob.name},
                    {"race", mob.raceName},
                    {"level", mob.level},
                    {"isAggressive", mob.isAggressive},
                    {"isDead", mob.isDead},
                    {"stats", {{"health", {{"current", mob.currentHealth}, {"max", mob.maxHealth}}}, {"mana", {{"current", mob.currentMana}, {"max", mob.maxMana}}}}},
                    {"isAggressive", mob.isAggressive},
                    {"isDead", mob.isDead},
                    {"position", {{"x", mob.position.positionX}, {"y", mob.position.positionY}, {"z", mob.position.positionZ}, {"rotationZ", mob.position.rotationZ}}},
                    {"attributes", nlohmann::json::array()}};

                for (const auto &attr : mob.attributes)
                {
                    mobJson["attributes"].push_back({{"id", attr.id},
                        {"name", attr.name},
                        {"slug", attr.slug},
                        {"value", attr.value}});
                }

                mobsArray.push_back(mobJson);
            }

            // Build response
            nlohmann::json response;
            ResponseBuilder builder;

            if (clientID == 0)
            {
                response = builder.setHeader("message", "Spawning mobs failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "spawnMobsInZone")
                               .setBody("", "")
                               .build();
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            response = builder.setHeader("message", "Spawning mobs success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "spawnMobsInZone")
                           .setBody("spawnZone", zoneJson)
                           .setBody("mobs", mobsArray)
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);
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
EventHandler::handleZoneMoveMobsEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        // Check if the data is a zone ID (int) instead of a full vector
        if (std::holds_alternative<int>(data))
        {
            int zoneId = std::get<int>(data);
            // Fetch mobs data from the manager instead of from Event
            std::vector<MobDataStruct> mobsList = gameServices_.getSpawnZoneManager().getMobsInZone(zoneId);

            nlohmann::json mobsArray = nlohmann::json::array();

            for (const auto &mob : mobsList)
            {
                nlohmann::json mobJson = {
                    {"id", mob.id},
                    {"uid", mob.uid},
                    {"zoneId", mob.zoneId},
                    {"name", mob.name},
                    {"race", mob.raceName},
                    {"level", mob.level},
                    {"isAggressive", mob.isAggressive},
                    {"isDead", mob.isDead},
                    {"stats", {{"health", {{"current", mob.currentHealth}, {"max", mob.maxHealth}}}, {"mana", {{"current", mob.currentMana}, {"max", mob.maxMana}}}}},
                    {"position", {{"x", mob.position.positionX}, {"y", mob.position.positionY}, {"z", mob.position.positionZ}, {"rotationZ", mob.position.rotationZ}}},
                    {"attributes", nlohmann::json::array()}};

                for (const auto &attr : mob.attributes)
                {
                    mobJson["attributes"].push_back({{"id", attr.id},
                        {"name", attr.name},
                        {"slug", attr.slug},
                        {"value", attr.value}});
                }

                mobsArray.push_back(mobJson);
            }

            ResponseBuilder builder;
            nlohmann::json response;

            if (clientID == 0)
            {
                response = builder.setHeader("message", "Moving mobs failed!")
                               .setHeader("hash", "")
                               .setHeader("clientId", clientID)
                               .setHeader("eventType", "zoneMoveMobs")
                               .setBody("", "")
                               .build();
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            response = builder.setHeader("message", "Moving mobs success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "zoneMoveMobs")
                           .setBody("mobs", mobsArray)
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);
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

void
EventHandler::handleGetSpawnZoneDataEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();
}

void
EventHandler::handleGetMobDataEvent(const Event &event)
{
    // set the data from the event
    const auto &data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<MobDataStruct>(data))
        {
            MobDataStruct mobData = std::get<MobDataStruct>(data);

            // get mob data from the mob manager
            MobDataStruct mobDataFromManager = gameServices_.getMobManager().getMobById(mobData.id);

            // prepare mob data in json format
            nlohmann::json mobJson = {
                {"id", mobDataFromManager.id},
                {"uid", mobDataFromManager.uid},
                {"zoneId", mobDataFromManager.zoneId},
                {"name", mobDataFromManager.name},
            };

            // send the response to the client
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Getting mob data success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", event.getClientID())
                           .setHeader("eventType", "getMobData")
                           .setBody("mob", mobJson)
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
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

            // debug mob attributes
            for (const auto &mobAttribute : mobAttributesList)
            {
                gameServices_.getLogger().log("Mob Attribute ID: " + std::to_string(mobAttribute.id) +
                                              ", Name: " + mobAttribute.name +
                                              ", Slug: " + mobAttribute.slug +
                                              ", Value: " + std::to_string(mobAttribute.value));
            }

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