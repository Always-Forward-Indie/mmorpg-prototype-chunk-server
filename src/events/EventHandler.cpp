#include "events/EventHandler.hpp"
#include "events/Event.hpp"

EventHandler::EventHandler(NetworkManager &networkManager)
    : networkManager_(networkManager)
{
}

//TODO - Use Logger to log instead of std::cout

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
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();
    PositionStruct characterPosition;
    // Extract movement data
    try
    {
        // Try to extract the data as a PositionStruct
        characterPosition = std::get<PositionStruct>(data);
    }
    catch (const std::bad_variant_access &ex)
    {
        std::cout << "Error here: " << ex.what() << std::endl;
        // Handle the case where the data is not of type PositionStruct
        // This might be logging the error, throwing another exception, etc.
    }

    // Update the clientData object with the new position
    clientData.updateCharacterPositionData(clientID, characterPosition);

    // TODO - Send the updated object back to the game server
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
    case Event::MOVE:
        handleMoveEvent(event, clientData);
        break;
    case Event::INTERACT:
        handleInteractEvent(event, clientData);
        break;
        // Other cases...
    }
}