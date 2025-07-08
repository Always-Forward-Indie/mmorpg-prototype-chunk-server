#include "events/handlers/ClientEventHandler.hpp"
#include "events/EventData.hpp"

ClientEventHandler::ClientEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
}

bool
ClientEventHandler::validateClientAuthentication(const ClientDataStruct &clientData)
{
    return clientData.clientId != 0 && !clientData.hash.empty();
}

void
ClientEventHandler::handlePingClientEvent(const Event &event)
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);
    int clientID = event.getClientID();

    gameServices_.getLogger().log("Handling PING event for client ID: " + std::to_string(clientID), GREEN);

    if (!clientSocket || !clientSocket->is_open())
    {
        gameServices_.getLogger().log("Skipping ping - socket is closed for client ID: " + std::to_string(clientID), GREEN);
        return;
    }

    const auto data = event.getData();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            sendSuccessResponse(clientSocket, "Pong!", "pingClient", clientID);
            gameServices_.getLogger().log("Sending PING response to Client ID: " + std::to_string(clientID), GREEN);
        }
        else
        {
            gameServices_.getLogger().logError("Error extracting data from ping event for client ID: " + std::to_string(clientID));
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Variant access error in ping event for client ID " + std::to_string(clientID) + ": " + std::string(ex.what()));
    }
}

void
ClientEventHandler::handleJoinClientEvent(const Event &event)
{
    const auto data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Validate authentication
            if (!validateClientAuthentication(passedClientData))
            {
                sendErrorResponse(clientSocket, "Authentication failed for user!", "joinGameClient", clientID, passedClientData.hash);
                return;
            }

            // Set the current client data
            gameServices_.getClientManager().loadClientData(passedClientData);

            // Set client socket
            gameServices_.getClientManager().setClientSocket(clientID, clientSocket);

            // Prepare success response and broadcast to all clients (including sender)
            nlohmann::json broadcastResponse = ResponseBuilder()
                                                   .setHeader("message", "Authentication success for user!")
                                                   .setHeader("hash", passedClientData.hash)
                                                   .setHeader("clientId", passedClientData.clientId)
                                                   .setHeader("eventType", "joinGameClient")
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
ClientEventHandler::handleGetConnectedClientsEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        // Get all connected clients
        std::vector<ClientDataStruct> clientsList;
        try
        {
            clientsList = gameServices_.getClientManager().getClientsList();
        }
        catch (const std::exception &ex)
        {
            gameServices_.getLogger().logError("Error getting client list in handleGetConnectedClientsEvent: " + std::string(ex.what()));
            sendErrorResponse(clientSocket, "Getting connected clients failed!", "getConnectedClients", clientID);
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
                {"status", isConnected ? "connected" : "disconnected"}};
            clientsListJson.push_back(clientJson);
        }

        // Check if the authentication is not successful
        if (clientID == 0)
        {
            sendErrorResponse(clientSocket, "Getting connected clients failed!", "getConnectedClients", clientID);
            return;
        }

        // Send success response with clients list
        sendSuccessResponse(clientSocket, "Getting connected clients success!", "getConnectedClients", clientID, "clientsList", clientsListJson);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
ClientEventHandler::handleDisconnectClientEvent(const Event &event)
{
    const auto data = event.getData();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            gameServices_.getLogger().log("Handling disconnect event for client ID: " + std::to_string(passedClientData.clientId) +
                                          " and character ID: " + std::to_string(passedClientData.characterId));

            if (passedClientData.clientId == 0)
            {
                gameServices_.getLogger().log("Client ID is 0, handling graceful disconnect without specific client identification!");
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

            // Remove character data
            gameServices_.getCharacterManager().removeCharacter(passedClientData.characterId);

            // Prepare disconnect notification
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Client disconnected!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", passedClientData.clientId)
                                          .setHeader("eventType", "disconnectClient")
                                          .setBody("", "")
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Broadcast to all existing clients except the disconnecting one
            broadcastToAllClients(responseData, passedClientData.clientId);
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
