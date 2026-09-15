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
        GameServices &gameServices,
        const std::string &loggerSubsystem = "events");

    virtual ~BaseEventHandler() = default;

  protected:
    // Packet route resolver for broadcastRouted (static: no instance state
    // beyond the handler's own protected lookups).
    static bool resolveRoute(BaseEventHandler &h, const nlohmann::json &packet,
        float &x, float &y, std::vector<int> &participants);

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
     * @brief Send error response to client with timestamps
     *
     * @param clientSocket Client socket to send response to
     * @param message Error message
     * @param eventType Type of event that failed
     * @param clientId Client ID
     * @param timestamps Lag compensation timestamps
     * @param hash Authentication hash (optional)
     */
    void sendErrorResponseWithTimestamps(
        std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
        const std::string &message,
        const std::string &eventType,
        int clientId,
        const TimestampStruct &timestamps,
        const std::string &hash = "");

    /**
     * @brief Send success response to client with timestamps
     *
     * @param clientSocket Client socket to send response to
     * @param message Success message
     * @param eventType Type of event that succeeded
     * @param clientId Client ID
     * @param timestamps Lag compensation timestamps
     * @param bodyKey Key for response body (optional)
     * @param bodyValue Value for response body (optional)
     * @param hash Authentication hash (optional)
     */
    void sendSuccessResponseWithTimestamps(
        std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
        const std::string &message,
        const std::string &eventType,
        int clientId,
        const TimestampStruct &timestamps,
        const std::string &bodyKey = "",
        const nlohmann::json &bodyValue = nlohmann::json{},
        const std::string &hash = "");

    /**
     * @brief Broadcast message to all connected clients with timestamps
     *
     * @param responseData JSON response data to broadcast
     * @param timestamps Lag compensation timestamps
     * @param excludeClientId Client ID to exclude from broadcast (optional)
     */
    void broadcastToAllClientsWithTimestamps(
        const std::string &status,
        const nlohmann::json &response,
        const TimestampStruct &timestamps,
        int excludeClientId = -1);

    /**
     * @brief Send a broadcast payload to an explicit recipient list
     * (interest v2: subscribers + fail-open). Empty list = send to nobody;
     * callers fall back to broadcastToAllClients* when interest is disabled.
     */
    void broadcastToClientIds(
        const std::string &status,
        const nlohmann::json &response,
        const TimestampStruct &timestamps,
        const std::vector<int> &recipientIds);

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
     * @brief Positional broadcast (interest v2): subscribers of the (x, y)
     * cell + explicit participant clientIds + fail-open clients.
     * Falls back to broadcast-all when interest is disabled or the position
     * is unknown. Participants are ALWAYS included (owner must see own
     * actions even across cells).
     */
    void broadcastPositional(const std::string &status, const nlohmann::json &response,
        const TimestampStruct &timestamps, float x, float y,
        const std::vector<int> &participantClientIds, int excludeClientId = -1);
    void broadcastPositional(const std::string &status, const nlohmann::json &response,
        float x, float y, const std::vector<int> &participantClientIds,
        int excludeClientId = -1);

    /**
     * @brief Routed broadcast: inspects packet body for casterId/characterId,
     * resolves position + participants, then broadcastPositional().
     * Unknown shape or unresolvable position => legacy broadcast-all
     * (fail-open, never drops).
     */
    void broadcastRouted(const std::string &status, const nlohmann::json &packet);
    void broadcastRouted(const std::string &status, const nlohmann::json &packet,
        const TimestampStruct &timestamps);

    /**
     * @brief Positional send of an already-encoded payload (wire bytes
     * unchanged): subscribers of (x, y) + participants + fail-open.
     * For legacy raw-string broadcasts (e.g. characterMoved teleports).
     */
    void sendPositionalRaw(const std::string &rawData, float x, float y,
        const std::vector<int> &participantClientIds, int excludeClientId = -1);

    // Position lookups (false when unknown => caller fails open to global).
    bool charPos(int characterId, float &x, float &y);
    bool mobPos(int mobUid, float &x, float &y);
    // clientId for a character, 0 when offline/unknown.
    int clientForCharacter(int characterId);

    /**
     * @brief Broadcast message to all connected clients
     *
     * @param responseData JSON response data to broadcast
     * @param excludeClientId Client ID to exclude from broadcast (optional)
     */
    void broadcastToAllClients(const std::string &responseData, int excludeClientId = -1);

    /**
     * @brief Check whether a character is alive (HP > 0)
     *
     * @param characterId The character to check
     * @return true if alive, false if dead or not found
     */
    bool isPlayerAlive(int characterId);

    // Protected member variables for derived classes
    std::shared_ptr<spdlog::logger> log_;
    NetworkManager &networkManager_;
    GameServerWorker &gameServerWorker_;
    GameServices &gameServices_;
};
