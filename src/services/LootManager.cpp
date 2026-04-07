#include "services/LootManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "services/GameServices.hpp"
#include "services/InventoryManager.hpp"
#include "services/PityManager.hpp"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

// Static member initialization
int LootManager::nextDroppedItemUID_ = 1;

LootManager::LootManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger), eventQueue_(nullptr), inventoryManager_(nullptr), randomGenerator_(randomDevice_())
{
    log_ = logger.getSystem("item");
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

void
LootManager::setPityManager(PityManager *pityManager)
{
    pityManager_ = pityManager;
}

void
LootManager::setGameServices(GameServices *gameServices)
{
    gameServices_ = gameServices;
}

void
LootManager::setNullifyItemOwnerCallback(std::function<void(const std::string &)> callback)
{
    nullifyItemOwnerCallback_ = std::move(callback);
}

void
LootManager::setDeleteInventoryItemCallback(std::function<void(const std::string &)> callback)
{
    deleteInventoryItemCallback_ = std::move(callback);
}

void
LootManager::setTransferInventoryItemCallback(std::function<void(const std::string &)> callback)
{
    transferInventoryItemCallback_ = std::move(callback);
}

std::vector<DroppedItemStruct>
LootManager::generateLootOnMobDeath(int mobId, int mobUID, const PositionStruct &position, int killerId)
{
    std::vector<DroppedItemStruct> droppedItems;

    // Resolve champion/rare loot multiplier from the mob instance (Stage 3)
    float lootMultiplier = 1.0f;
    if (gameServices_ && mobUID > 0)
    {
        auto inst = gameServices_->getMobInstanceManager().getMobInstance(mobUID);
        if (inst.uid != 0)
            lootMultiplier = inst.lootMultiplier;
    }

    // Zone event loot multiplier (Stage 4)
    if (gameServices_)
    {
        auto gameZone = gameServices_->getGameZoneManager().getZoneForPosition(position);
        if (gameZone.has_value())
            lootMultiplier *= gameServices_->getZoneEventManager().getLootMultiplier(gameZone->id);
    }

    try
    {
        // Get loot table for this mob
        std::vector<MobLootInfoStruct> lootTable = itemManager_.getLootForMob(mobId);

        if (lootTable.empty())
        {
            log_->info("[LOOT] No loot table found for mob ID " + std::to_string(mobId));
            return droppedItems;
        }

        logger_.log("[LOOT] Processing loot for mob ID " + std::to_string(mobId) +
                    " (UID: " + std::to_string(mobUID) + ") with " +
                    std::to_string(lootTable.size()) + " possible drops");

        std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

        // Read pity config once outside the loop (avoid repeated map lookups)
        int softPityKills = 300;
        int hardPityKills = 800;
        float softBonusPerKill = 0.00005f;
        int hintThreshold = 500;
        if (gameServices_)
        {
            const auto &cfg = gameServices_->getGameConfigService();
            softPityKills = cfg.getInt("pity.soft_pity_kills", 300);
            hardPityKills = cfg.getInt("pity.hard_pity_kills", 800);
            softBonusPerKill = cfg.getFloat("pity.soft_bonus_per_kill", 0.00005f);
            hintThreshold = cfg.getInt("pity.hint_threshold_kills", 500);
        }

        for (const auto &lootEntry : lootTable)
        {
            // Skip items that are harvest-only (can only be obtained through harvesting).
            // The isHarvestOnly flag on the loot entry is the authoritative source
            // (set from mob_loot_info.is_harvest_only, migration 013).
            if (lootEntry.isHarvestOnly)
            {
                logger_.log("[LOOT] Skipping harvest-only item (ID: " +
                            std::to_string(lootEntry.itemId) + ") from regular drop");
                continue;
            }

            // Retrieve item info for name logging
            ItemDataStruct itemInfo = itemManager_.getItemById(lootEntry.itemId);

            // ── Pity System ────────────────────────────────────────────────────
            // Only apply pity when we have a valid killer and pity manager.
            float effectiveChance = lootEntry.dropChance * lootMultiplier;
            bool forceDrop = false;
            if (pityManager_ && killerId > 0)
            {
                if (pityManager_->isHardPity(killerId, lootEntry.itemId, hardPityKills))
                {
                    forceDrop = true;
                    log_->info("[Pity] Hard pity triggered for char=" +
                               std::to_string(killerId) + " item=" +
                               std::to_string(lootEntry.itemId));
                }
                else
                {
                    float extra = pityManager_->getExtraDropChance(
                        killerId, lootEntry.itemId, softPityKills, softBonusPerKill);
                    effectiveChance = lootEntry.dropChance + extra;
                }
            }
            // ──────────────────────────────────────────────────────────────────

            float randomRoll = distribution(randomGenerator_);

            logger_.log("[LOOT] Item " + std::to_string(lootEntry.itemId) +
                        " - Roll: " + std::to_string(randomRoll) +
                        ", Required: " + std::to_string(effectiveChance) +
                        (forceDrop ? " [HARD PITY]" : ""));

            if (forceDrop || randomRoll <= effectiveChance)
            {
                // Reset pity on successful drop
                if (pityManager_ && killerId > 0)
                    pityManager_->resetCounter(killerId, lootEntry.itemId);
                // Item should drop!
                DroppedItemStruct droppedItem;
                droppedItem.uid = generateDroppedItemUID();
                droppedItem.itemId = lootEntry.itemId;
                // Randomise quantity within [minQuantity, maxQuantity] (migration 014)
                if (lootEntry.maxQuantity > lootEntry.minQuantity)
                {
                    std::uniform_int_distribution<int> qtyDist(lootEntry.minQuantity, lootEntry.maxQuantity);
                    droppedItem.quantity = qtyDist(randomGenerator_);
                }
                else
                {
                    droppedItem.quantity = lootEntry.minQuantity;
                }
                droppedItem.position = position;

                // Add random circular offset around the mob's death position
                const float MAX_DROP_RADIUS = 50.0f; // Maximum radius for item drops
                std::uniform_real_distribution<float> radiusDist(15.0f, MAX_DROP_RADIUS);
                std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);

                float dropRadius = radiusDist(randomGenerator_);
                float dropAngle = angleDist(randomGenerator_);

                // Calculate offset using polar coordinates for more natural distribution
                float offsetX = dropRadius * std::cos(dropAngle);
                float offsetY = dropRadius * std::sin(dropAngle);

                droppedItem.position.positionX += offsetX;
                droppedItem.position.positionY += offsetY;

                droppedItem.dropTime = std::chrono::steady_clock::now();
                droppedItem.droppedByMobUID = mobUID;
                droppedItem.canBePickedUp = true;

                // Store in our dropped items map
                {
                    std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);
                    droppedItems_[droppedItem.uid] = droppedItem;
                }

                droppedItems.push_back(droppedItem);

                // Use already retrieved item info for logging
                logger_.log("[LOOT] DROPPED: " + itemInfo.slug + " (ID: " +
                            std::to_string(lootEntry.itemId) + ", UID: " +
                            std::to_string(droppedItem.uid) + ") at position (" +
                            std::to_string(droppedItem.position.positionX) + ", " +
                            std::to_string(droppedItem.position.positionY) + ", " +
                            std::to_string(droppedItem.position.positionZ) + ")");
            }
            else
            {
                // Item did not drop — increment pity counter, send hint on threshold
                if (pityManager_ && killerId > 0)
                {
                    int cid = killerId;
                    int iid = lootEntry.itemId;
                    pityManager_->incrementCounter(
                        cid, iid, hintThreshold, [this, cid, iid]()
                        {
                            if (gameServices_)
                            {
                                gameServices_->getStatsNotificationService()
                                    .sendWorldNotification(
                                        cid, "pity_hint",
                                        nlohmann::json::object(),
                                        "ambient",
                                        "atmosphere");
                            } });
                }
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
    // First, retrieve the dropped item data (with minimal locking)
    DroppedItemStruct droppedItem;
    bool found = false;

    {
        std::shared_lock<std::shared_mutex> lock(droppedItemsMutex_);

        auto it = droppedItems_.find(itemUID);
        if (it == droppedItems_.end())
        {
            log_->error("Attempted to pickup non-existent dropped item UID: " + std::to_string(itemUID));
            return false;
        }

        if (!it->second.canBePickedUp)
        {
            log_->error("Attempted to pickup item that cannot be picked up, UID: " + std::to_string(itemUID));
            return false;
        }

        // Reservation check: if item is reserved for another player and reservation hasn't expired
        if (it->second.reservedForCharacterId != 0 && it->second.reservedForCharacterId != characterId && std::chrono::steady_clock::now() < it->second.reservationExpiry)
        {
            log_->warn("[LOOT] Character " + std::to_string(characterId) + " cannot pick up item UID " + std::to_string(itemUID) + " — reserved for character " + std::to_string(it->second.reservedForCharacterId));
            return false;
        }

        droppedItem = it->second;
        found = true;
    }

    if (!found)
    {
        return false;
    }

    // Validate distance - player must be within pickup range (no locks needed)
    const float MAX_PICKUP_DISTANCE = 200.0f; // Maximum distance for item pickup
    float distance = calculateDistance(playerPosition, droppedItem.position);

    if (distance > MAX_PICKUP_DISTANCE)
    {
        logger_.logError("Player " + std::to_string(characterId) + " too far from item UID " +
                         std::to_string(itemUID) + " - Distance: " + std::to_string(distance) +
                         ", Max: " + std::to_string(MAX_PICKUP_DISTANCE));
        return false;
    }

    // Get item info for logging (no locks needed for ItemManager read operations)
    ItemDataStruct itemInfo = itemManager_.getItemById(droppedItem.itemId);

    // Add item to player's inventory (InventoryManager handles its own locking)
    bool addedToInventory = false;
    if (inventoryManager_)
    {
        if (droppedItem.inventoryItemId > 0)
        {
            // Instanced item: flip character_id via transferInventoryItem event.
            // Update in-memory inventory manually (no upsert path for this case).
            PlayerInventoryItemStruct inst;
            inst.id = droppedItem.inventoryItemId;
            inst.characterId = characterId;
            inst.itemId = droppedItem.itemId;
            inst.quantity = droppedItem.quantity;
            inst.durabilityCurrent = droppedItem.durabilityCurrent; // restore preserved durability
            inventoryManager_->addInstancedItemToInventory(characterId, inst);

            if (transferInventoryItemCallback_)
            {
                nlohmann::json pkt;
                pkt["header"]["eventType"] = "transferInventoryItem";
                pkt["header"]["clientId"] = 0;
                pkt["header"]["hash"] = "";
                pkt["body"]["fromCharId"] = 0; // NULL owner (on ground)
                pkt["body"]["toCharId"] = characterId;
                pkt["body"]["inventoryItemId"] = droppedItem.inventoryItemId;
                transferInventoryItemCallback_(pkt.dump() + "\n");
            }
            addedToInventory = true;
        }
        else
        {
            // Mob loot or other new item: create a fresh inventory row.
            addedToInventory = inventoryManager_->addItemToInventory(characterId, droppedItem.itemId, droppedItem.quantity);
        }

        if (!addedToInventory)
        {
            logger_.logError("[LOOT] Failed to add item " + std::to_string(droppedItem.itemId) +
                             " to character " + std::to_string(characterId) + " inventory");
            return false;
        }
    }
    else
    {
        log_->error("[LOOT] InventoryManager not set - cannot add item to inventory");
        return false;
    }

    // Only now remove from dropped items (with exclusive lock)
    {
        std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);

        // Double-check that the item is still there
        auto it = droppedItems_.find(itemUID);
        if (it != droppedItems_.end())
        {
            droppedItems_.erase(it);

            logger_.log("[LOOT] Character " + std::to_string(characterId) + " picked up " +
                        itemInfo.slug + " (UID: " + std::to_string(itemUID) + ") from distance " +
                        std::to_string(distance));

            logger_.log("[LOOT] Successfully added item " + std::to_string(droppedItem.itemId) +
                        " (quantity: " + std::to_string(droppedItem.quantity) +
                        ") to character " + std::to_string(characterId) + " inventory");
        }
        else
        {
            // Item was picked up by someone else in the meantime
            log_->error("[LOOT] Item UID " + std::to_string(itemUID) + " was already picked up by another player");

            // We need to remove the item from inventory since it was already taken
            if (inventoryManager_)
            {
                inventoryManager_->removeItemFromInventory(characterId, droppedItem.itemId, droppedItem.quantity);
            }
            return false;
        }
    }

    return true;
}

void
LootManager::cleanupOldDroppedItems(int maxAgeSeconds)
{
    std::vector<int> removedUIDs;
    std::vector<int> expiredInstanceIds; // inventoryItemId of expired player-dropped items

    {
        std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);

        auto currentTime = std::chrono::steady_clock::now();
        auto maxAge = std::chrono::seconds(maxAgeSeconds);

        auto it = droppedItems_.begin();
        while (it != droppedItems_.end())
        {
            auto age = currentTime - it->second.dropTime;

            // Don't remove while still under active reservation
            bool reservationActive = (it->second.reservedForCharacterId != 0 && currentTime < it->second.reservationExpiry);

            if (age > maxAge && !reservationActive)
            {
                log_->info("[LOOT] Cleaning up old dropped item UID: " + std::to_string(it->first));
                removedUIDs.push_back(it->first);
                // Capture inventoryItemId before erasing
                if (it->second.inventoryItemId > 0)
                    expiredInstanceIds.push_back(it->second.inventoryItemId);
                it = droppedItems_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    if (!removedUIDs.empty())
    {
        // For instanced player-dropped items, delete the DB row permanently.
        if (deleteInventoryItemCallback_)
        {
            for (int instanceId : expiredInstanceIds)
            {
                nlohmann::json pkt;
                pkt["header"]["eventType"] = "deleteInventoryItem";
                pkt["header"]["clientId"] = 0;
                pkt["header"]["hash"] = "";
                pkt["body"]["inventoryItemId"] = instanceId;
                deleteInventoryItemCallback_(pkt.dump() + "\n");
            }
        }

        log_->info("[LOOT] Cleaned up " + std::to_string(removedUIDs.size()) + " old dropped items");

        // Broadcast removal to all clients so they can despawn the item visuals
        if (eventQueue_)
        {
            Event removeEvent(Event::ITEM_REMOVE, 0, removedUIDs);
            eventQueue_->push(std::move(removeEvent));
        }
    }
}

DroppedItemStruct
LootManager::dropItemByPlayer(int characterId, int inventoryItemId, int itemId, int quantity, const PositionStruct &position, int durabilityCurrent)
{
    DroppedItemStruct drop;
    drop.uid = generateDroppedItemUID();
    drop.itemId = itemId;
    drop.quantity = quantity;
    drop.inventoryItemId = inventoryItemId;
    drop.durabilityCurrent = durabilityCurrent;
    drop.position = position;
    drop.dropTime = std::chrono::steady_clock::now();
    drop.droppedByCharacterId = characterId;
    drop.droppedByMobUID = 0;
    // Player drops are public immediately — no reservation
    drop.reservedForCharacterId = 0;
    drop.canBePickedUp = true;

    {
        std::unique_lock<std::shared_mutex> lock(droppedItemsMutex_);
        droppedItems_[drop.uid] = drop;
    }

    // If this item has an existing DB row, nullify its owner instead of deleting it.
    if (inventoryItemId > 0 && nullifyItemOwnerCallback_)
    {
        nlohmann::json pkt;
        pkt["header"]["eventType"] = "nullifyItemOwner";
        pkt["header"]["clientId"] = 0;
        pkt["header"]["hash"] = "";
        pkt["body"]["inventoryItemId"] = inventoryItemId;
        pkt["body"]["fromCharId"] = characterId;
        nullifyItemOwnerCallback_(pkt.dump() + "\n");
    }

    log_->info("[LOOT] Character " + std::to_string(characterId) + " dropped item " + std::to_string(itemId) + " x" + std::to_string(quantity) + " (UID " + std::to_string(drop.uid) + ")");

    // Broadcast to all clients so they can spawn the world item
    if (eventQueue_)
    {
        std::vector<DroppedItemStruct> vec{drop};
        Event dropEvent(Event::ITEM_DROP, 0, vec);
        eventQueue_->push(std::move(dropEvent));
    }

    return drop;
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
