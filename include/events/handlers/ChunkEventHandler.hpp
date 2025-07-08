#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for chunk-related events
 *
 * Handles all events related to chunk operations such as
 * initialization, joining chunk, and chunk disconnection.
 */
class ChunkEventHandler : public BaseEventHandler
{
  public:
    ChunkEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle chunk initialization event
     *
     * Initializes chunk data and sends response to game server
     *
     * @param event Event containing chunk information
     */
    void handleInitChunkEvent(const Event &event);

    /**
     * @brief Handle join chunk event
     *
     * Handles client joining a chunk, validates authentication
     *
     * @param event Event containing client data for chunk join
     */
    void handleJoinChunkEvent(const Event &event);

    /**
     * @brief Handle disconnect chunk event
     *
     * Handles chunk disconnection and notifies game server
     *
     * @param event Event containing client data for disconnection
     */
    void handleDisconnectChunkEvent(const Event &event);

  private:
    /**
     * @brief Validate chunk authentication
     *
     * @param chunkId Chunk ID to validate
     * @return true if valid, false otherwise
     */
    bool validateChunkAuthentication(int chunkId);

    /**
     * @brief Validate client authentication for chunk operations
     *
     * @param clientData Client data structure to validate
     * @return true if authentication is valid, false otherwise
     */
    bool validateClientAuthentication(const ClientDataStruct &clientData);
};
