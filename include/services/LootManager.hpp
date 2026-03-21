#pragma once
#include "data/DataStructs.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <random>
#include <shared_mutex>

// Forward declarations
class EventQueue;
class InventoryManager;
class PityManager;
class GameServices;

class LootManager
{
  public:
    LootManager(ItemManager &itemManager, Logger &logger);

    /**
     * @brief Set event queue for sending loot events to clients
     * @param eventQueue Event queue to send ITEM_DROP events
     */
    void setEventQueue(EventQueue *eventQueue);

    /**
     * @brief Set inventory manager for adding items to player inventories
     * @param inventoryManager InventoryManager to add picked up items
     */
    void setInventoryManager(InventoryManager *inventoryManager);

    /**
     * @brief Set callback to nullify item owner when a player drops an item onto the ground.
     *        Sends nullifyItemOwner event to game server (SET character_id = NULL).
     */
    void setNullifyItemOwnerCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Set callback to delete a ground item row when it expires without being picked up.
     *        Sends deleteInventoryItem event to game server.
     */
    void setDeleteInventoryItemCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Set callback to transfer item ownership to a new character on pickup.
     *        Sends transferInventoryItem event to game server.
     */
    void setTransferInventoryItemCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Set PityManager reference (enables pity-based drop chance modification).
     */
    void setPityManager(PityManager *pityManager);

    /**
     * @brief Set GameServices pointer (used to read config and send notifications).
     */
    void setGameServices(GameServices *gameServices);

    /**
     * @brief Generate loot when a mob dies.
     * @param mobId    Template mob ID (from MobDataStruct.id)
     * @param mobUID   Unique mob instance UID
     * @param position Position where mob died
     * @param killerId Character ID of the player who dealt the killing blow (0 = no player)
     * @return Vector of dropped items
     */
    std::vector<DroppedItemStruct> generateLootOnMobDeath(int mobId, int mobUID, const PositionStruct &position, int killerId = 0);

    /**
     * @brief Drop an inventory item from a player onto the ground.
     *        The item is NOT reserved — it is immediately free for all to pick up.
     *        If inventoryItemId > 0, the DB row is kept alive with character_id = NULL
     *        (nullifyItemOwner event fires). On pickup, character_id is restored.
     *        On expiry, the row is deleted.
     * @param characterId Character dropping the item
     * @param inventoryItemId player_inventory.id of the source row (0 = fresh item)
     * @param itemId Item ID to drop
     * @param quantity Quantity to drop
     * @param position Position of the player
     * @return The created DroppedItemStruct (uid == 0 on failure)
     */
    DroppedItemStruct dropItemByPlayer(int characterId, int inventoryItemId, int itemId, int quantity, const PositionStruct &position, int durabilityCurrent = 0);

    /**
     * @brief Get all dropped items in the world
     * @return Map of item UID to DroppedItemStruct
     */
    std::map<int, DroppedItemStruct> getAllDroppedItems() const;

    /**
     * @brief Get dropped items near a position
     * @param position Center position
     * @param radius Search radius
     * @return Vector of nearby dropped items
     */
    std::vector<DroppedItemStruct> getDroppedItemsNearPosition(const PositionStruct &position, float radius = 200.0f) const;

    /**
     * @brief Pick up a dropped item
     * @param itemUID Unique dropped item UID
     * @param characterId ID of character picking up the item
     * @param playerPosition Current position of the player
     * @return true if successfully picked up, false otherwise
     */
    bool pickupDroppedItem(int itemUID, int characterId, const PositionStruct &playerPosition);

    /**
     * @brief Clean up old dropped items
     * @param maxAgeSeconds Maximum age in seconds before cleanup
     */
    void cleanupOldDroppedItems(int maxAgeSeconds = 300); // 5 minutes default

    /**
     * @brief Get dropped item by UID
     * @param itemUID Unique dropped item UID
     * @return DroppedItemStruct or empty struct if not found
     */
    DroppedItemStruct getDroppedItemByUID(int itemUID) const;

  private:
    ItemManager &itemManager_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Event queue for sending loot events to clients
    EventQueue *eventQueue_;

    // Inventory manager for adding items to player inventories
    InventoryManager *inventoryManager_;

    // Optional: Pity manager for modified rare-drop chances
    PityManager *pityManager_ = nullptr;

    // Optional: GameServices for reading config and sending notifications
    GameServices *gameServices_ = nullptr;

    // Callbacks for game-server persistence of item-instance ownership
    std::function<void(const std::string &)> nullifyItemOwnerCallback_;
    std::function<void(const std::string &)> deleteInventoryItemCallback_;
    std::function<void(const std::string &)> transferInventoryItemCallback_;

    // Store all dropped items (itemUID -> DroppedItemStruct)
    std::map<int, DroppedItemStruct> droppedItems_;

    // Thread safety
    mutable std::shared_mutex droppedItemsMutex_;

    // Random number generation for loot drops
    std::random_device randomDevice_;
    mutable std::mt19937 randomGenerator_;

    // Generate unique UID for dropped items
    int generateDroppedItemUID();
    static int nextDroppedItemUID_;

    /**
     * @brief Calculate distance between two positions
     */
    float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2) const;
};
