#pragma once
#include "data/DataStructs.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <shared_mutex>

// Forward declaration
class EventQueue;

class InventoryManager
{
  public:
    InventoryManager(ItemManager &itemManager, Logger &logger);

    /**
     * @brief Set event queue for sending inventory update events
     * @param eventQueue Event queue to send INVENTORY_UPDATE events
     */
    void setEventQueue(EventQueue *eventQueue);

    /**
     * @brief Add item to player's inventory
     * @param characterId Character ID
     * @param itemId Item ID to add
     * @param quantity Quantity to add
     * @return true if successfully added, false otherwise
     */
    bool addItemToInventory(int characterId, int itemId, int quantity = 1);

    /**
     * @brief Remove item from player's inventory
     * @param characterId Character ID
     * @param itemId Item ID to remove
     * @param quantity Quantity to remove
     * @return true if successfully removed, false otherwise
     */
    bool removeItemFromInventory(int characterId, int itemId, int quantity = 1);

    /**
     * @brief Get player's inventory
     * @param characterId Character ID
     * @return Vector of PlayerInventoryItemStruct
     */
    std::vector<PlayerInventoryItemStruct> getPlayerInventory(int characterId) const;

    /**
     * @brief Check if player has item
     * @param characterId Character ID
     * @param itemId Item ID to check
     * @param requiredQuantity Required quantity (default 1)
     * @return true if player has enough of the item
     */
    bool hasItem(int characterId, int itemId, int requiredQuantity = 1) const;

    /**
     * @brief Get item quantity in inventory
     * @param characterId Character ID
     * @param itemId Item ID
     * @return Quantity of item (0 if not found)
     */
    int getItemQuantity(int characterId, int itemId) const;

    /**
     * @brief Clear player's inventory (for testing or character deletion)
     * @param characterId Character ID
     */
    void clearPlayerInventory(int characterId);

    /**
     * @brief Get total number of different items in inventory
     * @param characterId Character ID
     * @return Number of different item types
     */
    int getInventoryItemCount(int characterId) const;

    /**
     * @brief Convert inventory item to full JSON with complete item data
     * @param item Player inventory item
     * @return JSON representation with full item data
     */
    nlohmann::json inventoryItemToJson(const PlayerInventoryItemStruct &item) const;

  private:
    ItemManager &itemManager_;
    Logger &logger_;

    // Event queue for sending inventory update events
    EventQueue *eventQueue_;

    // Store inventories (characterId -> vector of inventory items)
    std::map<int, std::vector<PlayerInventoryItemStruct>> playerInventories_;

    // Thread safety
    mutable std::shared_mutex inventoryMutex_;

    /**
     * @brief Find inventory item entry for character and item
     * @param characterId Character ID
     * @param itemId Item ID
     * @return Iterator to the item or end() if not found
     */
    std::vector<PlayerInventoryItemStruct>::iterator findInventoryItem(int characterId, int itemId);
    std::vector<PlayerInventoryItemStruct>::const_iterator findInventoryItem(int characterId, int itemId) const;
};
