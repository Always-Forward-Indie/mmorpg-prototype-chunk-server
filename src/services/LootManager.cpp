#include "services/LootManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "services/InventoryManager.hpp"
#include <algorithm>
#include <cmath>

// Static member initialization
int LootManager::nextDroppedItemUID_ = 1;

LootManager::LootManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger), eventQueue_(nullptr), inventoryManager_(nullptr), randomGenerator_(randomDevice_())
{
}

void
LootManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
}

void
LootManager::setInventoryManager(InventoryManager *inventoryManager)
{
    inventoryManager_ = inventoryManager;
}

std::vector<DroppedItemStruct>
LootManager::generateLootOnMobDeath(int mobId, int mobUID, const PositionStruct &position)
{
    std::vector<DroppedItemStruct> droppedItems;

    try
    {
        // Get loot table for this mob
        std::vector<MobLootInfoStruct> lootTable = itemManager_.getLootForMob(mobId);

        if (lootTable.empty())
        {
            logger_.log("[LOOT] No loot table found for mob ID " + std::to_string(mobId));
            return droppedItems;
        }

        logger_.log("[LOOT] Processing loot for mob ID " + std::to_string(mobId) +
                    " (UID: " + std::to_string(mobUID) + ") with " +
                    std::to_string(lootTable.size()) + " possible drops");

        std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

        for (const auto &lootEntry : lootTable)
        {
            float randomRoll = distribution(randomGenerator_);

            logger_.log("[LOOT] Item " + std::to_string(lootEntry.itemId) +
                        " - Roll: " + std::to_string(randomRoll) +
                        ", Required: " + std::to_string(lootEntry.dropChance));

            if (randomRoll <= lootEntry.dropChance)
            {
                // Item should drop!
                DroppedItemStruct droppedItem;
                droppedItem.uid = generateDroppedItemUID();
                droppedItem.itemId = lootEntry.itemId;
                droppedItem.quantity = 1; // Could be configurable later
                droppedItem.position = position;

                // Add some random offset to prevent items from stacking exactly
                std::uniform_real_distribution<float> offsetDist(-20.0f, 20.0f);
                droppedItem.position.positionX += offsetDist(randomGenerator_);
                droppedItem.position.positionY += offsetDist(randomGenerator_);

                droppedItem.dropTime = std::chrono::steady_clock::now();
                droppedItem.droppedByMobUID = mobUID;
                droppedItem.canBePickedUp = true;

                // Store in our dropped items map
                {
                    std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);
                    droppedItems_[droppedItem.uid] = droppedItem;
                }

                droppedItems.push_back(droppedItem);

                // Get item info for logging
                ItemDataStruct itemInfo = itemManager_.getItemById(lootEntry.itemId);
                logger_.log("[LOOT] DROPPED: " + itemInfo.name + " (ID: " +
                            std::to_string(lootEntry.itemId) + ", UID: " +
                            std::to_string(droppedItem.uid) + ") at position (" +
                            std::to_string(droppedItem.position.positionX) + ", " +
                            std::to_string(droppedItem.position.positionY) + ", " +
                            std::to_string(droppedItem.position.positionZ) + ")");
            }
        }

        // Send ITEM_DROP event for all dropped items at once
        if (!droppedItems.empty() && eventQueue_)
        {
            Event itemDropEvent(Event::ITEM_DROP, 0, droppedItems);
            eventQueue_->push(std::move(itemDropEvent));
            logger_.log("[LOOT] Sent ITEM_DROP event for " + std::to_string(droppedItems.size()) + " dropped items");
        }

        if (droppedItems.empty())
        {
            logger_.log("[LOOT] No items dropped from mob ID " + std::to_string(mobId) + " (bad luck!)");
        }
        else
        {
            logger_.log("[LOOT] Mob ID " + std::to_string(mobId) + " dropped " +
                        std::to_string(droppedItems.size()) + " items");
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error generating loot for mob " + std::to_string(mobId) + ": " + std::string(e.what()));
    }

    return droppedItems;
}

std::map<int, DroppedItemStruct>
LootManager::getAllDroppedItems() const
{
    std::shared_lock<std::shared_mutex> lock(droppedItemsMutex_);
    return droppedItems_;
}

std::vector<DroppedItemStruct>
LootManager::getDroppedItemsNearPosition(const PositionStruct &position, float radius) const
{
    std::vector<DroppedItemStruct> nearbyItems;

    std::shared_lock<std::shared_mutex> lock(droppedItemsMutex_);

    for (const auto &item : droppedItems_)
    {
        if (item.second.canBePickedUp)
        {
            float distance = calculateDistance(position, item.second.position);
            if (distance <= radius)
            {
                nearbyItems.push_back(item.second);
            }
        }
    }

    // Sort by distance (closest first)
    std::sort(nearbyItems.begin(), nearbyItems.end(), [&position, this](const DroppedItemStruct &a, const DroppedItemStruct &b)
        {
                  float distA = calculateDistance(position, a.position);
                  float distB = calculateDistance(position, b.position);
                  return distA < distB; });

    return nearbyItems;
}

bool
LootManager::pickupDroppedItem(int itemUID, int characterId, const PositionStruct &playerPosition)
{
    std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);

    auto it = droppedItems_.find(itemUID);
    if (it == droppedItems_.end())
    {
        logger_.logError("Attempted to pickup non-existent dropped item UID: " + std::to_string(itemUID));
        return false;
    }

    if (!it->second.canBePickedUp)
    {
        logger_.logError("Attempted to pickup item that cannot be picked up, UID: " + std::to_string(itemUID));
        return false;
    }

    // Validate distance - player must be within pickup range
    const float MAX_PICKUP_DISTANCE = 100.0f; // Maximum distance for item pickup
    float distance = calculateDistance(playerPosition, it->second.position);

    if (distance > MAX_PICKUP_DISTANCE)
    {
        logger_.logError("Player " + std::to_string(characterId) + " too far from item UID " +
                         std::to_string(itemUID) + " - Distance: " + std::to_string(distance) +
                         ", Max: " + std::to_string(MAX_PICKUP_DISTANCE));
        return false;
    }

    // Get item info for logging
    ItemDataStruct itemInfo = itemManager_.getItemById(it->second.itemId);

    logger_.log("[LOOT] Character " + std::to_string(characterId) + " picked up " +
                itemInfo.name + " (UID: " + std::to_string(itemUID) + ") from distance " +
                std::to_string(distance));

    // Store item data before removing from dropped items
    DroppedItemStruct droppedItem = it->second;

    // Remove from dropped items
    droppedItems_.erase(it);

    // Add item to player's inventory
    if (inventoryManager_)
    {
        bool addedToInventory = inventoryManager_->addItemToInventory(characterId, droppedItem.itemId, droppedItem.quantity);

        if (addedToInventory)
        {
            logger_.log("[LOOT] Successfully added item " + std::to_string(droppedItem.itemId) +
                        " (quantity: " + std::to_string(droppedItem.quantity) +
                        ") to character " + std::to_string(characterId) + " inventory");
        }
        else
        {
            logger_.logError("[LOOT] Failed to add item " + std::to_string(droppedItem.itemId) +
                             " to character " + std::to_string(characterId) + " inventory");
            return false;
        }
    }
    else
    {
        logger_.logError("[LOOT] InventoryManager not set - cannot add item to inventory");
        return false;
    }

    return true;
}

void
LootManager::cleanupOldDroppedItems(int maxAgeSeconds)
{
    std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);

    auto currentTime = std::chrono::steady_clock::now();
    auto maxAge = std::chrono::seconds(maxAgeSeconds);

    auto it = droppedItems_.begin();
    int cleanedUp = 0;

    while (it != droppedItems_.end())
    {
        auto age = currentTime - it->second.dropTime;
        if (age > maxAge)
        {
            logger_.log("[LOOT] Cleaning up old dropped item UID: " + std::to_string(it->first));
            it = droppedItems_.erase(it);
            cleanedUp++;
        }
        else
        {
            ++it;
        }
    }

    if (cleanedUp > 0)
    {
        logger_.log("[LOOT] Cleaned up " + std::to_string(cleanedUp) + " old dropped items");
    }
}

DroppedItemStruct
LootManager::getDroppedItemByUID(int itemUID) const
{
    std::shared_lock<std::shared_mutex> lock(droppedItemsMutex_);

    auto it = droppedItems_.find(itemUID);
    if (it != droppedItems_.end())
    {
        return it->second;
    }

    return DroppedItemStruct();
}

int
LootManager::generateDroppedItemUID()
{
    return nextDroppedItemUID_++;
}

float
LootManager::calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2) const
{
    float dx = pos1.positionX - pos2.positionX;
    float dy = pos1.positionY - pos2.positionY;
    float dz = pos1.positionZ - pos2.positionZ;

    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
