#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for NPC-related events
 *
 * Handles all events related to NPCs such as receiving NPC data
 * from game server and managing NPC information for clients.
 */
class NPCEventHandler : public BaseEventHandler
{
  public:
    NPCEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle SET_ALL_NPCS_LIST event
     *
     * Receives and processes NPC data list from game server
     *
     * @param event Event containing NPC data list
     */
    void handleSetAllNPCsListEvent(const Event &event);

    /**
     * @brief Handle SET_ALL_NPCS_ATTRIBUTES event
     *
     * Receives and processes NPC attributes from game server
     *
     * @param event Event containing NPC attributes data
     */
    void handleSetAllNPCsAttributesEvent(const Event &event);

    /**
     * @brief Send NPC spawn data to a specific client
     *
     * Sends NPC spawn information to client when they enter a zone
     *
     * @param clientId The client ID to send data to
     * @param playerPosition Player's current position for area filtering
     * @param spawnRadius Radius around player to spawn NPCs
     */
    void sendNPCSpawnDataToClient(int clientId, const PositionStruct &playerPosition, float spawnRadius = 1000.0f);

  private:
    /**
     * @brief Convert NPC data to JSON format for client transmission
     *
     * @param npc The NPC data to convert
     * @return JSON object ready for client transmission
     */
    nlohmann::json convertNPCToSpawnJson(const NPCDataStruct &npc) const;
};