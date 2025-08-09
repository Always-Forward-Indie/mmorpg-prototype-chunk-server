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
     * @brief Generate loot when a mob dies
     * @param mobId Template mob ID (from MobDataStruct.id)
     * @param mobUID Unique mob instance UID
     * @param position Position where mob died
     * @return Vector of dropped items
     */
    std::vector<DroppedItemStruct> generateLootOnMobDeath(int mobId, int mobUID, const PositionStruct &position);

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

    // Event queue for sending loot events to clients
    EventQueue *eventQueue_;

    // Inventory manager for adding items to player inventories
    InventoryManager *inventoryManager_;

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
