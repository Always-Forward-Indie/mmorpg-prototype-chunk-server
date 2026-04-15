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
     */
    void handleSetAllNPCsListEvent(const Event &event);

    /**
     * @brief Handle SET_ALL_NPCS_ATTRIBUTES event
     */
    void handleSetAllNPCsAttributesEvent(const Event &event);

    /**
     * @brief Handle SET_NPC_AMBIENT_SPEECH event.
     *        Stores ambient speech configs in AmbientSpeechManager.
     */
    void handleSetNPCAmbientSpeechEvent(const Event &event);

    /**
     * @brief Send NPC spawn data to a specific client
     */
    void sendNPCSpawnDataToClient(int clientId, const PositionStruct &playerPosition, float spawnRadius = 1000.0f);

    /**
     * @brief Build and send NPC_AMBIENT_POOLS packet to a client.
     *        Called on playerReady and whenever player context changes.
     *
     * @param clientId     Target client.
     * @param characterId  Character whose quest/flag context is used for filtering.
     * @param playerPosition  Player position used to determine visible NPCs.
     * @param spawnRadius  Radius matching NPC visibility (same as sendNPCSpawnDataToClient).
     */
    void sendAmbientPoolsToClient(int clientId, int characterId, const PositionStruct &playerPosition, float spawnRadius = 50000.0f);

  private:
    nlohmann::json convertNPCToSpawnJson(const NPCDataStruct &npc, int characterId);
};