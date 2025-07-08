#pragma once

#include "events/Event.hpp"
#include "events/EventData.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <boost/asio.hpp>
#include <memory>

/**
 * @brief Base class for all event handlers
 *
 * Provides common functionality and dependencies for all specialized event handlers.
 * Each handler should inherit from this class and implement specific event handling logic.
 */
class BaseEventHandler
{
  public:
    /**
     * @brief Construct a new Base Event Handler object
     *
     * @param networkManager Reference to network manager for client communication
     * @param gameServerWorker Reference to game server worker for server communication
     * @param gameServices Reference to game services for business logic
     */
    BaseEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    virtual ~BaseEventHandler() = default;

  protected:
    /**
     * @brief Safely get client socket from event
     *
     * Always retrieves socket from ClientManager to prevent use-after-free issues
     * with socket references in Events.
     *
     * @param event The event containing client ID
     * @return std::shared_ptr<boost::asio::ip::tcp::socket> Client socket or nullptr
     */
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket(const Event &event);

    /**
     * @brief Send error response to client
     *
     * @param clientSocket Client socket to send response to
     * @param message Error message
     * @param eventType Type of event that failed
     * @param clientId Client ID
     * @param hash Authentication hash (optional)
     */
    void sendErrorResponse(
        std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
        const std::string &message,
        const std::string &eventType,
        int clientId,
        const std::string &hash = "");

    /**
     * @brief Send success response to client
     *
     * @param clientSocket Client socket to send response to
     * @param message Success message
     * @param eventType Type of event that succeeded
     * @param clientId Client ID
     * @param bodyKey Key for response body (optional)
     * @param bodyValue Value for response body (optional)
     * @param hash Authentication hash (optional)
     */
    void sendSuccessResponse(
        std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
        const std::string &message,
        const std::string &eventType,
        int clientId,
        const std::string &bodyKey = "",
        const nlohmann::json &bodyValue = nlohmann::json{},
        const std::string &hash = "");

    /**
     * @brief Send response to game server
     *
     * @param status Response status ("success" or "error")
     * @param response JSON response data
     */
    void sendGameServerResponse(const std::string &status, const nlohmann::json &response);

    /**
     * @brief Broadcast message to all connected clients
     *
     * @param responseData JSON response data to broadcast
     * @param excludeClientId Client ID to exclude from broadcast (optional)
     */
    void broadcastToAllClients(const std::string &responseData, int excludeClientId = -1);

    // Protected member variables for derived classes
    NetworkManager &networkManager_;
    GameServerWorker &gameServerWorker_;
    GameServices &gameServices_;
};
