#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for client-related events
 *
 * Handles all events related to client connections, authentication,
 * ping/pong, and disconnections.
 */
class ClientEventHandler : public BaseEventHandler
{
  public:
    ClientEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle ping event from client
     *
     * Responds with pong message to maintain connection
     *
     * @param event Event containing client data
     */
    void handlePingClientEvent(const Event &event);

    /**
     * @brief Handle client join event
     *
     * Authenticates client and adds to client manager,
     * broadcasts join notification to all clients
     *
     * @param event Event containing client authentication data
     */
    void handleJoinClientEvent(const Event &event);

    /**
     * @brief Handle get connected clients request
     *
     * Returns list of all currently connected clients
     *
     * @param event Event containing client request
     */
    void handleGetConnectedClientsEvent(const Event &event);

    /**
     * @brief Handle client disconnect event
     *
     * Removes client from manager and broadcasts disconnect notification
     *
     * @param event Event containing client data to disconnect
     */
    void handleDisconnectClientEvent(const Event &event);

  private:
    /**
     * @brief Validate client authentication data
     *
     * @param clientData Client data structure to validate
     * @return true if authentication is valid, false otherwise
     */
    bool validateClientAuthentication(const ClientDataStruct &clientData);
};
