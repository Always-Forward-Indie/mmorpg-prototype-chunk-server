#include "events/handlers/BaseEventHandler.hpp"
#include "utils/TimestampUtils.hpp"
#include <nlohmann/json.hpp>

BaseEventHandler::BaseEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : networkManager_(networkManager),
      gameServerWorker_(gameServerWorker),
      gameServices_(gameServices)
{
}

std::shared_ptr<boost::asio::ip::tcp::socket>
BaseEventHandler::getClientSocket(const Event &event)
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
BaseEventHandler::sendErrorResponse(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        gameServices_.getLogger().logError("Cannot send error response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", message)
                                  .setHeader("hash", hash)
                                  .setHeader("clientId", clientId)
                                  .setHeader("eventType", eventType)
                                  .setBody("", "")
                                  .build();

    std::string responseData = networkManager_.generateResponseMessage("error", response);

    try
    {
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error sending error response for " + eventType +
                                           " to client " + std::to_string(clientId) + ": " + ex.what());
    }
}

void
BaseEventHandler::sendSuccessResponse(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const std::string &bodyKey,
    const nlohmann::json &bodyValue,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        gameServices_.getLogger().logError("Cannot send success response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    ResponseBuilder builder;
    builder.setHeader("message", message)
        .setHeader("hash", hash)
        .setHeader("clientId", clientId)
        .setHeader("eventType", eventType);

    if (!bodyKey.empty())
    {
        builder.setBody(bodyKey, bodyValue);
    }
    else
    {
        builder.setBody("", "");
    }

    nlohmann::json response = builder.build();
    std::string responseData = networkManager_.generateResponseMessage("success", response);

    try
    {
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error sending success response for " + eventType +
                                           " to client " + std::to_string(clientId) + ": " + ex.what());
    }
}

void
BaseEventHandler::sendGameServerResponse(const std::string &status, const nlohmann::json &response)
{
    std::string responseData = networkManager_.generateResponseMessage(status, response);
    gameServerWorker_.sendDataToGameServer(responseData);
}

void
BaseEventHandler::broadcastToAllClients(const std::string &responseData, int excludeClientId)
{
    std::vector<ClientDataStruct> clientDataMap;
    try
    {
        clientDataMap = gameServices_.getClientManager().getClientsList();
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error getting client list for broadcast: " + std::string(ex.what()));
        return;
    }

    for (const auto &clientDataItem : clientDataMap)
    {
        // Skip excluded client
        if (excludeClientId != -1 && clientDataItem.clientId == excludeClientId)
        {
            continue;
        }

        // Get socket for this client using ClientManager
        auto itemSocket = gameServices_.getClientManager().getClientSocket(clientDataItem.clientId);

        // Validate socket before using
        if (itemSocket && itemSocket->is_open())
        {
            try
            {
                networkManager_.sendResponse(itemSocket, responseData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error broadcasting to client " +
                                                   std::to_string(clientDataItem.clientId) + ": " + ex.what());
            }
        }
    }
}

void
BaseEventHandler::sendErrorResponseWithTimestamps(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const TimestampStruct &timestamps,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        gameServices_.getLogger().logError("Cannot send error response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", message)
                                  .setHeader("hash", hash)
                                  .setHeader("clientId", clientId)
                                  .setHeader("eventType", eventType)
                                  .setTimestamps(timestamps)
                                  .setBody("", "")
                                  .build();

    std::string errorData = networkManager_.generateResponseMessage("error", response, timestamps);
    networkManager_.sendResponse(clientSocket, errorData);
}

void
BaseEventHandler::sendSuccessResponseWithTimestamps(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const TimestampStruct &timestamps,
    const std::string &bodyKey,
    const nlohmann::json &bodyValue,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        gameServices_.getLogger().logError("Cannot send success response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    auto builder = ResponseBuilder()
                       .setHeader("message", message)
                       .setHeader("hash", hash)
                       .setHeader("clientId", clientId)
                       .setHeader("eventType", eventType)
                       .setTimestamps(timestamps);

    if (!bodyKey.empty())
    {
        builder.setBody(bodyKey, bodyValue);
    }

    nlohmann::json response = builder.build();
    std::string successData = networkManager_.generateResponseMessage("success", response, timestamps);
    networkManager_.sendResponse(clientSocket, successData);
}

void
BaseEventHandler::broadcastToAllClientsWithTimestamps(
    const std::string &status,
    const nlohmann::json &response,
    const TimestampStruct &timestamps,
    int excludeClientId)
{
    std::string responseData = networkManager_.generateResponseMessage(status, response, timestamps);

    auto clientsList = gameServices_.getClientManager().getClientsList();
    for (const auto &clientDataItem : clientsList)
    {
        if (excludeClientId != -1 && clientDataItem.clientId == excludeClientId)
        {
            continue;
        }

        // Get socket for this client using ClientManager
        auto itemSocket = gameServices_.getClientManager().getClientSocket(clientDataItem.clientId);

        // Validate socket before using
        if (itemSocket && itemSocket->is_open())
        {
            try
            {
                networkManager_.sendResponse(itemSocket, responseData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error broadcasting to client " +
                                                   std::to_string(clientDataItem.clientId) + ": " + ex.what());
            }
        }
    }
}
