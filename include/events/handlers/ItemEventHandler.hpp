#pragma once
#include "events/handlers/BaseEventHandler.hpp"
#include "services/GameServices.hpp"
#include <chrono>
#include <mutex>
#include <unordered_map>

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

    /**
     * @brief Handle inventory update event
     *
     * Pushes the current inventory state to the client whenever
     * an item is added or removed at runtime (e.g. quest rewards,
     * looting). The event payload already contains the pre-built
     * full-inventory JSON assembled by InventoryManager.
     *
     * @param event Event containing characterId (as clientID) + full inventory JSON
     */
    void handleInventoryUpdateEvent(const Event &event);

    /**
     * @brief Handle player drop item event (ITEM_DROP_BY_PLAYER).
     *        Validates ownership, removes from inventory, places on ground and broadcasts.
     */
    void handleItemDropByPlayerEvent(const Event &event);

    /**
     * @brief Handle ITEM_REMOVE broadcast — tell all clients to despawn items
     *        that were cleaned up server-side.
     */
    void handleItemRemoveEvent(const Event &event);

    /**
     * @brief Handle USE_ITEM event — player uses a potion/scroll/food.
     *        Applies instant heal/mana or adds timed ActiveEffect.
     */
    void handleUseItemEvent(const Event &event);

    /**
     * @brief Send the current world ground-items snapshot to a single client.
     *        Called on character join so the client can spawn existing world items.
     * @param clientId  Target client ID
     * @param socket    Target socket
     */
    void sendGroundItemsToClient(int clientId,
        std::shared_ptr<boost::asio::ip::tcp::socket> socket);

  private:
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;

    // Per-character item-use cooldown tracker.
    // Key: characterId → (itemId → time when cooldown expires)
    // Protected by itemCooldownMutex_ — handleUseItemEvent may be called
    // from concurrent event-processing threads.
    mutable std::mutex itemCooldownMutex_;
    std::unordered_map<int, std::unordered_map<int, std::chrono::steady_clock::time_point>> itemCooldowns_;

    /**
     * @brief Check and set item use cooldown atomically.
     * @return true if the item can be used (cooldown set), false if still on cooldown.
     */
    bool trySetItemCooldown(int characterId, int itemId, int cooldownSeconds);

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
