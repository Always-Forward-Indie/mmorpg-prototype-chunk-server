#include "events/EventHandler.hpp"
#include "events/Event.hpp"

EventHandler::EventHandler(NetworkManager &networkManager)
    : networkManager_(networkManager)
{
}

void EventHandler::handleJoinEvent(const Event &event, ClientData &clientData)
{
    // Here we will update the init data of the character when it's joined in the object and send it back to the game server
    // Retrieve the data from the event
    const auto data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = event.getClientSocket();

    std::cout << "Client ID: " << clientID << std::endl;


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

                // Check if the authentication was successful
            if (initData.clientId == 0 || initData.hash == "")
            {
                // Add response data
                response["header"]["message"] = "Authentication failed for user!";
                response["header"]["hash"] = initData.hash;
                response["header"]["clientId"] = initData.clientId;
                response["header"]["eventType"] = "joinGame";
                response["body"] = "";
                // Prepare a response message
                std::string responseData = networkManager_.generateResponseMessage("error", response);
                // Send the response to the client
                networkManager_.sendResponse(clientSocket, responseData);
                return;
            }

            // Add the message to the response
            response["header"]["message"] = "Authentication success for user!";
            response["header"]["hash"] = initData.hash;
            response["header"]["clientId"] = initData.clientId;
            response["body"]["characterId"] = initData.characterData.characterId;
            response["body"]["characterPosX"] = initData.characterData.characterPosition.positionX;
            response["body"]["characterPosY"] = initData.characterData.characterPosition.positionY;
            response["body"]["characterPosZ"] = initData.characterData.characterPosition.positionZ;
            // Prepare a response message
            std::string responseData = networkManager_.generateResponseMessage("success", response);
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
    PositionStruct characterPosition;
    // Extract movement data
    try
    {
        // Try to extract the data as a PositionStruct
        characterPosition = std::get<PositionStruct>(data);
    }
    catch (const std::bad_variant_access &ex)
    {
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
    case Event::JOIN:
        handleJoinEvent(event, clientData);
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