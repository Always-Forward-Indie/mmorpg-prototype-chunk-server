#pragma once
#include "data/DataStructs.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"

/**
 * @brief Event handler for harvest-related events
 *
 * Handles:
 * - Harvest start requests from clients
 * - Getting nearby harvestable corpses
 * - Managing harvest progress and completion
 */
class HarvestEventHandler : public BaseEventHandler
{
  public:
    HarvestEventHandler(NetworkManager &networkManager, GameServerWorker &gameServerWorker, GameServices &gameServices);

    /**
     * @brief Handle harvest start request event
     *
     * Processes client request to start harvesting a corpse
     *
     * @param event Event containing harvest request data
     */
    void handleHarvestStartRequest(const Event &event);

    /**
     * @brief Handle get nearby corpses request
     *
     * Responds with list of harvestable corpses near player
     *
     * @param event Event containing position data
     */
    void handleGetNearbyCorpses(const Event &event);

    /**
     * @brief Handle harvest cancellation request
     *
     * Cancels active harvest for a player
     *
     * @param event Event containing cancellation data
     */
    void handleHarvestCancel(const Event &event);

    /**
     * @brief Handle harvest completion
     *
     * Called when harvest is completed, sends harvest results to client
     *
     * @param playerId ID of player who completed harvest
     * @param corpseId ID of harvested corpse
     */
    void handleHarvestComplete(int playerId, int corpseId);

    /**
     * @brief Handle corpse loot pickup request
     *
     * Called when client wants to pickup specific items from corpse loot
     *
     * @param pickupRequest Request with details about what to pickup
     */
    void handleCorpseLootPickup(const CorpseLootPickupRequestStruct &pickupRequest);
    void handleCorpseLootInspect(const CorpseLootInspectRequestStruct &inspectRequest);

  private:
    GameServices &gameServices_;
};
