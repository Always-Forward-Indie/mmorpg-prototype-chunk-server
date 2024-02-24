#include "events/EventHandler.hpp"
#include "events/Event.hpp"

EventHandler::EventHandler(NetworkManager &networkManager, Logger &logger)
    : networkManager_(networkManager),
          logger_(logger)
{
}

//TODO review getClientSocket - do we need it or we could use clientData to get it?
//TODO review also getData and getClientID - do we need it or we could use clientData to get it?

void EventHandler::handleJoinGameEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the init data of the character when it's joined in the object and send it back to the game server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    // Extract init data
    try
    {
        // Try to extract the data as a ClientDataStruct
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct initData = std::get<ClientDataStruct>(data);
            // Save the clientData object with the new init data
            clientData.storeClientData(initData);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if the authentication is not successful
            if (initData.clientId == 0 || initData.hash == "")
            {
                // Add response data
                response = builder
                               .setHeader("message", "Authentication failed for user!")
                               .setHeader("hash", initData.hash)
                               .setHeader("clientId", initData.clientId)
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
                           .setHeader("hash", initData.hash)
                           .setHeader("clientId", initData.clientId)
                           .setHeader("eventType", "joinGame")
                           .setBody("characterId", initData.characterData.characterId)
                           .setBody("characterClass", initData.characterData.characterClass)
                           .setBody("characterLevel", initData.characterData.characterLevel)
                           .setBody("characterName", initData.characterData.characterName)
                           .setBody("characterRace", initData.characterData.characterRace)
                           .setBody("characterExp", initData.characterData.characterExperiencePoints)
                           .setBody("characterCurrentHealth", initData.characterData.characterCurrentHealth)
                           .setBody("characterCurrentMana", initData.characterData.characterCurrentMana)
                           .setBody("characterPosX", initData.characterData.characterPosition.positionX)
                           .setBody("characterPosY", initData.characterData.characterPosition.positionY)
                           .setBody("characterPosZ", initData.characterData.characterPosition.positionZ)
                           .setBody("characterRotZ", initData.characterData.characterPosition.rotationZ)
                           .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            std::cout << "Error with extracting data!" << std::endl;
            // Handle the case where the data is not of type ClientDataStruct
            // This might be logging the error, throwing another exception, etc.
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        std::cout << "Error here: " << ex.what() << std::endl;
        // Handle the case where the data is not of type ClientDataStruct
        // This might be logging the error, throwing another exception, etc.
    }
}

void EventHandler::handleGetConnectedCharactersEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the init data of the character when it's joined in the object and send it back to the game server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    // Get all existing clients data as array
    std::unordered_map<int, ClientDataStruct> clientDataMap = clientData.getClientsDataMap();

    // Extract init data
    try
    {
        // Try to extract the data as a ClientDataStruct
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct initData = std::get<ClientDataStruct>(data);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if client id is 0
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Getting connected characters failed...")
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

            // create json list of characters
            nlohmann::json charactersList;
            for (const auto &clientDataItem : clientDataMap)
            {
                // Add the message to the response
                charactersList.push_back({
                    {"clientId", clientDataItem.second.clientId},
                    {"characterId", clientDataItem.second.characterData.characterId},
                    {"characterClass", clientDataItem.second.characterData.characterClass},
                    {"characterLevel", clientDataItem.second.characterData.characterLevel},
                    {"characterName", clientDataItem.second.characterData.characterName},
                    {"characterRace", clientDataItem.second.characterData.characterRace},
                    {"characterExp", clientDataItem.second.characterData.characterExperiencePoints},
                    {"characterCurrentHealth", clientDataItem.second.characterData.characterCurrentHealth},
                    {"characterCurrentMana", clientDataItem.second.characterData.characterCurrentMana},
                    {"characterPosX", clientDataItem.second.characterData.characterPosition.positionX},
                    {"characterPosY", clientDataItem.second.characterData.characterPosition.positionY},
                    {"characterPosZ", clientDataItem.second.characterData.characterPosition.positionZ},
                    {"characterRotZ", clientDataItem.second.characterData.characterPosition.rotationZ}
                });
            }


            // Add the message to the response
            response = builder
                           .setHeader("message", "Getting connected characters success!")
                           .setHeader("hash", "")
                           .setHeader("clientId", clientID)
                           .setHeader("eventType", "getConnectedCharacters")
                           .setBody("charactersList", charactersList)
                            .build();
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

                   // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            std::cout << "Error with extracting data!" << std::endl;
            // Handle the case where the data is not of type ClientDataStruct
            // This might be logging the error, throwing another exception, etc.
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        std::cout << "Error here: " << ex.what() << std::endl;
        // Handle the case where the data is not of type ClientDataStruct
        // This might be logging the error, throwing another exception, etc.
    }
}     

void EventHandler::handleMoveEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the position of the character in the object and send it back to the game server

    // Retrieve the data from the event
    const auto &data = event.getData();
    int clientID = event.getClientID();
    int characterID = event.getCharacterID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    PositionStruct characterPosition;
    // Extract movement data
    try
    {
        // Try to extract the data as a ClientDataStruct
        if (std::holds_alternative<PositionStruct>(data))
        {
            // Try to extract the data as a PositionStruct
            characterPosition = std::get<PositionStruct>(data);

            // Prepare the response message
            nlohmann::json response;
            ResponseBuilder builder;

            // Check if client id is 0
            if (clientID == 0)
            {
                // Add response data
                response = builder
                               .setHeader("message", "Character movement failed!")
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

            // Add the message to the response
            response = builder
                        .setHeader("message", "Character movement success!")
                        .setHeader("hash", "")
                        .setHeader("clientId", clientID)
                        .setHeader("eventType", "moveCharacter")
                        .setBody("characterId", characterID)
                        .setBody("characterPosX", characterPosition.positionX)
                        .setBody("characterPosY", characterPosition.positionY)
                        .setBody("characterPosZ", characterPosition.positionZ)
                        .setBody("characterRotZ", characterPosition.rotationZ)
                        .build();

            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Update the clientData object with the new position
            clientData.updateCharacterPositionData(clientID, characterPosition);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            std::cout << "Error with extracting data!" << std::endl;
            // Handle the case where the data is not of type ClientDataStruct
            // This might be logging the error, throwing another exception, etc.
        }
        
    }
    catch (const std::bad_variant_access &ex)
    {
        std::cout << "Error here: " << ex.what() << std::endl;
        // Handle the case where the data is not of type PositionStruct
        // This might be logging the error, throwing another exception, etc.
    }
}

// disconnect the client
void EventHandler::handleDisconnectClientEvent(const Event &event, ClientData &clientData)
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
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

            // Remove the client data
            clientData.removeClientData(passedClientData.clientId);

            //send the response to all clients
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

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here:" + std::string(ex.what()));
    }
}

// ping the client
void EventHandler::handlePingClientEvent(const Event &event, ClientData &clientData)
{
    // Here we will ping the client
    const auto data = event.getData();

    // Extract init data
    try
    {
        // Try to extract the data
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);
            std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

            //send the response to all clients
            nlohmann::json response;
            ResponseBuilder builder;
            response = builder
                           .setHeader("message", "Pong!")
                           .setHeader("eventType", "pingClient")
                           .setBody("", "")
                           .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Send the response to the client
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            logger_.log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        logger_.log("Error here:" + std::string(ex.what()));
    }
}

void EventHandler::handleInteractEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the interaction of the character in the object and send it back to the game server
    //  TODO - Implement this method
}

void EventHandler::dispatchEvent(const Event &event, ClientData &clientData)
{
    switch (event.getType())
    {
    case Event::JOIN_GAME:
        handleJoinGameEvent(event, clientData);
        break;
    case Event::GET_CONNECTED_CHARACTERS:
        handleGetConnectedCharactersEvent(event, clientData);
        break;
    case Event::CHARACTER_MOVEMENT:
        handleMoveEvent(event, clientData);
        break;
    case Event::CHARACTER_INTERACT:
        handleInteractEvent(event, clientData);
        break;
    case Event::DISCONNECT_CLIENT:
        handleDisconnectClientEvent(event, clientData);
        break;
    case Event::PING_CLIENT:
        handlePingClientEvent(event, clientData);
        break;
    }
}