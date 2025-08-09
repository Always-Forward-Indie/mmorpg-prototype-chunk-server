#pragma once
#include "events/handlers/BaseEventHandler.hpp"
#include "services/GameServices.hpp"

/**
 * @brief Handler for item and loot related events
 */
class ItemEventHandler : public BaseEventHandler
{
  public:
    ItemEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle set items list event
     *
     * Loads all items data into the item manager
     *
     * @param event Event containing list of items
     */
    void handleSetItemsListEvent(const Event &event);

    /**
     * @brief Handle set mob loot info event
     *
     * Loads mob loot information into the item manager
     *
     * @param event Event containing mob loot info
     */
    void handleSetMobLootInfoEvent(const Event &event);

    /**
     * @brief Handle item drop event
     *
     * Notifies clients about dropped items
     *
     * @param event Event containing dropped item data
     */
    void handleItemDropEvent(const Event &event);

    /**
     * @brief Handle item pickup event
     *
     * Processes item pickup by a player
     *
     * @param event Event containing pickup data
     */
    void handleItemPickupEvent(const Event &event);

    /**
     * @brief Handle get nearby items event
     *
     * Sends nearby dropped items to requesting client
     *
     * @param event Event containing position and character data
     */
    void handleGetNearbyItemsEvent(const Event &event);

    /**
     * @brief Handle mob loot generation event
     *
     * Generates loot when a mob dies
     *
     * @param event Event containing mob death data for loot generation
     */
    void handleMobLootGenerationEvent(const Event &event);

    /**
     * @brief Handle get player inventory event
     *
     * Sends player's inventory to requesting client
     *
     * @param event Event containing character data
     */
    void handleGetPlayerInventoryEvent(const Event &event);

  private:
    GameServices &gameServices_;

    /**
     * @brief Convert DroppedItemStruct to JSON
     * @param droppedItem The dropped item data
     * @return JSON representation
     */
    nlohmann::json droppedItemToJson(const DroppedItemStruct &droppedItem);

    /**
     * @brief Convert ItemDataStruct to JSON
     * @param itemData The item data
     * @return JSON representation
     */
    nlohmann::json itemToJson(const ItemDataStruct &itemData);
};
