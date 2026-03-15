#include "events/handlers/BaseEventHandler.hpp"
#include "utils/TimestampUtils.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

BaseEventHandler::BaseEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices,
    const std::string &loggerSubsystem)
    : networkManager_(networkManager),
      gameServerWorker_(gameServerWorker),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem(loggerSubsystem);
}

bool
BaseEventHandler::isPlayerAlive(int characterId)
{
    try
    {
        const auto &charData = gameServices_.getCharacterManager().getCharacterData(characterId);
        return charData.characterCurrentHealth > 0;
    }
    catch (...)
    {
        return false;
    }
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
        log_->error("Cannot send error response: invalid or closed socket for client " + std::to_string(clientId));
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
        log_->error("Cannot send success response: invalid or closed socket for client " + std::to_string(clientId));
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
    // CRITICAL-8 fix:
    // 1. ONE shared_ptr allocation for the whole broadcast (not N string copies)
    // 2. ONE shared_lock acquisition instead of N individual getClientSocket() calls
    // At 2000 clients x 100 broadcasts/s: 100 lock acquisitions vs previous 200,000
    auto sharedData = std::make_shared<const std::string>(responseData);
    auto sockets = gameServices_.getClientManager().getActiveSockets(excludeClientId);

    for (auto &sock : sockets)
    {
        if (sock && sock->is_open())
        {
            try
            {
                networkManager_.sendResponse(sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error broadcasting to client: " + std::string(ex.what()));
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
        log_->error("Cannot send error response: invalid or closed socket for client " + std::to_string(clientId));
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
        log_->error("Cannot send success response: invalid or closed socket for client " + std::to_string(clientId));
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
    // CRITICAL-8 fix: one allocation + one shared_lock acquisition for the whole broadcast
    std::string rawData = networkManager_.generateResponseMessage(status, response, timestamps);
    auto sharedData = std::make_shared<const std::string>(rawData);
    auto sockets = gameServices_.getClientManager().getActiveSockets(excludeClientId);

    for (auto &sock : sockets)
    {
        if (sock && sock->is_open())
        {
            try
            {
                networkManager_.sendResponse(sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error in broadcastWithTimestamps: " + std::string(ex.what()));
            }
        }
    }
}
