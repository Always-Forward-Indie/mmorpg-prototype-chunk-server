#pragma once
#include "data/DataStructs.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>

// Forward declaration
class EventQueue;
class QuestManager;

class InventoryManager
{
  public:
    InventoryManager(ItemManager &itemManager, Logger &logger);

    /**
     * @brief Set event queue for sending inventory update events
     * @param eventQueue Event queue to send INVENTORY_UPDATE events
     */
    void setEventQueue(EventQueue *eventQueue);
    void setQuestManager(QuestManager *questManager);

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
    /** Remove a specific inventory row by its player_inventory.id (inventoryItemId). Avoids ambiguity when two rows share the same itemId. */
    bool removeItemFromInventoryById(int characterId, int inventoryItemId, int quantity);

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

    /**
     * @brief Set callback for immediately persisting inventory changes to game server DB
     * @param callback Function that sends serialized packet to game server
     */
    void setSaveInventoryCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Load player inventory from DB response (called on character login).
     *        Replaces any in-memory inventory for the character without triggering saves.
     * @param characterId Character ID
     * @param items Items loaded from DB
     */
    void loadPlayerInventory(int characterId, const std::vector<PlayerInventoryItemStruct> &items);

    /**
     * @brief Get all equipped items for a character
     */
    std::vector<PlayerInventoryItemStruct> getEquippedItems(int characterId) const;

    /**
     * @brief Get the equipped weapon (equipSlotSlug == "weapon"), if any
     */
    std::optional<PlayerInventoryItemStruct> getEquippedWeapon(int characterId) const;

    /**
     * @brief Update durability in-memory (does NOT persist — caller must save separately)
     */
    void updateDurability(int characterId, int inventoryItemId, int newDurability);

    /**
     * @brief Update Item Soul kill_count in-memory (does NOT persist — caller must save separately)
     */
    void updateItemKillCount(int characterId, int inventoryItemId, int newKillCount);

    /**
     * @brief Mark an inventory item as equipped or unequipped in-memory
     */
    void setItemEquipped(int characterId, int inventoryItemId, bool equipped);

    /**
     * @brief Insert a fully-populated instanced item into the in-memory inventory without
     *        triggering a DB upsert. Used when picking up a player-dropped item that already
     *        has a player_inventory row (character_id was NULL while on ground).
     */
    void addInstancedItemToInventory(int characterId, const PlayerInventoryItemStruct &inst);

    /**
     * @brief Remove an item from in-memory inventory without sending any DB save event.
     *        Used when dropping an instanced item: the DB row stays alive (character_id
     *        will be nullified separately by LootManager).
     */
    void evictFromMemory(int characterId, int inventoryItemId);

    /**
     * @brief Calculate total weight of all items (equipped + unequipped) in character's inventory.
     * @return Sum of (item.weight * quantity) for every inventory slot.
     */
    float getTotalWeight(int characterId) const;

    /**
     * @brief Update the in-memory DB row ID for an inventory item after server-side upsert.
     *        Called when the game server responds with the assigned player_inventory.id.
     */
    void updateInventoryItemId(int characterId, int itemId, int64_t newId);

    /**
     * @brief Get the current gold (gold_coin) amount for a character.
     * @return Quantity of "gold_coin" items; 0 if none.
     */
    int getGoldAmount(int characterId) const;

  private:
    ItemManager &itemManager_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Event queue for sending inventory update events
    EventQueue *eventQueue_;
    QuestManager *questManager_ = nullptr;
    std::function<void(const std::string &)> saveInventoryCallback_;

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
