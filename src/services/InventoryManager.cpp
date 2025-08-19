#include "services/InventoryManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"

InventoryManager::InventoryManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger), eventQueue_(nullptr)
{
}

void
InventoryManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
}

bool
InventoryManager::addItemToInventory(int characterId, int itemId, int quantity)
{
    if (quantity <= 0)
    {
        logger_.logError("Attempted to add invalid quantity (" + std::to_string(quantity) + ") of item " + std::to_string(itemId));
        return false;
    }

    // Verify item exists
    ItemDataStruct itemInfo = itemManager_.getItemById(itemId);
    if (itemInfo.id == 0)
    {
        logger_.logError("Attempted to add non-existent item ID: " + std::to_string(itemId));
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);

    // Find existing item in inventory
    auto itemIt = findInventoryItem(characterId, itemId);

    if (itemIt != playerInventories_[characterId].end())
    {
        // Item already exists, increase quantity
        itemIt->quantity += quantity;
        logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.name +
                    " to character " + std::to_string(characterId) +
                    " (new total: " + std::to_string(itemIt->quantity) + ")");
    }
    else
    {
        // New item, add to inventory
        PlayerInventoryItemStruct newItem;
        newItem.characterId = characterId;
        newItem.itemId = itemId;
        newItem.quantity = quantity;
        // ID will be set when syncing with database later

        playerInventories_[characterId].push_back(newItem);
        logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.name +
                    " to character " + std::to_string(characterId) + " (new item)");
    }

    // Send inventory update event to notify clients
    if (eventQueue_)
    {
        // Get updated inventory and convert to JSON (use direct access since we already have the lock)
        nlohmann::json inventoryJson;
        inventoryJson["characterId"] = characterId;
        inventoryJson["items"] = nlohmann::json::array();

        // Direct access to avoid recursive locking
        auto it = playerInventories_.find(characterId);
        if (it != playerInventories_.end())
        {
            for (const auto &item : it->second)
            {
                inventoryJson["items"].push_back(inventoryItemToJson(item));
            }
        }

        Event inventoryUpdateEvent(Event::INVENTORY_UPDATE, characterId, inventoryJson);
        eventQueue_->push(std::move(inventoryUpdateEvent));
        logger_.log("[INVENTORY] Sent INVENTORY_UPDATE event for character " + std::to_string(characterId));
    }

    return true;
}

bool
InventoryManager::removeItemFromInventory(int characterId, int itemId, int quantity)
{
    if (quantity <= 0)
    {
        logger_.logError("Attempted to remove invalid quantity (" + std::to_string(quantity) + ") of item " + std::to_string(itemId));
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);

    auto itemIt = findInventoryItem(characterId, itemId);

    if (itemIt == playerInventories_[characterId].end())
    {
        logger_.logError("Attempted to remove item " + std::to_string(itemId) + " from character " +
                         std::to_string(characterId) + " but item not found in inventory");
        return false;
    }

    if (itemIt->quantity < quantity)
    {
        logger_.logError("Attempted to remove " + std::to_string(quantity) + " of item " +
                         std::to_string(itemId) + " from character " + std::to_string(characterId) +
                         " but only has " + std::to_string(itemIt->quantity));
        return false;
    }

    ItemDataStruct itemInfo = itemManager_.getItemById(itemId);

    itemIt->quantity -= quantity;

    if (itemIt->quantity == 0)
    {
        // Remove item completely
        playerInventories_[characterId].erase(itemIt);
        logger_.log("[INVENTORY] Removed all " + itemInfo.name + " from character " + std::to_string(characterId));
    }
    else
    {
        logger_.log("[INVENTORY] Removed " + std::to_string(quantity) + "x " + itemInfo.name +
                    " from character " + std::to_string(characterId) +
                    " (remaining: " + std::to_string(itemIt->quantity) + ")");
    }

    // Send inventory update event to notify clients
    if (eventQueue_)
    {
        // Get updated inventory and convert to JSON (use direct access since we already have the lock)
        nlohmann::json inventoryJson;
        inventoryJson["characterId"] = characterId;
        inventoryJson["items"] = nlohmann::json::array();

        // Direct access to avoid recursive locking
        auto it = playerInventories_.find(characterId);
        if (it != playerInventories_.end())
        {
            for (const auto &item : it->second)
            {
                inventoryJson["items"].push_back(inventoryItemToJson(item));
            }
        }

        Event inventoryUpdateEvent(Event::INVENTORY_UPDATE, characterId, inventoryJson);
        eventQueue_->push(std::move(inventoryUpdateEvent));
        logger_.log("[INVENTORY] Sent INVENTORY_UPDATE event for character " + std::to_string(characterId));
    }

    return true;
}

std::vector<PlayerInventoryItemStruct>
InventoryManager::getPlayerInventory(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(inventoryMutex_);

    auto it = playerInventories_.find(characterId);
    if (it != playerInventories_.end())
    {
        return it->second;
    }

    return std::vector<PlayerInventoryItemStruct>();
}

bool
InventoryManager::hasItem(int characterId, int itemId, int requiredQuantity) const
{
    return getItemQuantity(characterId, itemId) >= requiredQuantity;
}

int
InventoryManager::getItemQuantity(int characterId, int itemId) const
{
    std::shared_lock<std::shared_mutex> lock(inventoryMutex_);

    auto itemIt = findInventoryItem(characterId, itemId);

    if (itemIt != playerInventories_.at(characterId).end())
    {
        return itemIt->quantity;
    }

    return 0;
}

void
InventoryManager::clearPlayerInventory(int characterId)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);

    playerInventories_[characterId].clear();
    logger_.log("[INVENTORY] Cleared inventory for character " + std::to_string(characterId));
}

int
InventoryManager::getInventoryItemCount(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(inventoryMutex_);

    auto it = playerInventories_.find(characterId);
    if (it != playerInventories_.end())
    {
        return it->second.size();
    }

    return 0;
}

std::vector<PlayerInventoryItemStruct>::iterator
InventoryManager::findInventoryItem(int characterId, int itemId)
{
    auto &inventory = playerInventories_[characterId];
    return std::find_if(inventory.begin(), inventory.end(), [itemId](const PlayerInventoryItemStruct &item)
        { return item.itemId == itemId; });
}

std::vector<PlayerInventoryItemStruct>::const_iterator
InventoryManager::findInventoryItem(int characterId, int itemId) const
{
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
    {
        static std::vector<PlayerInventoryItemStruct> empty;
        return empty.end();
    }

    const auto &inventory = it->second;
    return std::find_if(inventory.begin(), inventory.end(), [itemId](const PlayerInventoryItemStruct &item)
        { return item.itemId == itemId; });
}

nlohmann::json
InventoryManager::inventoryItemToJson(const PlayerInventoryItemStruct &item) const
{
    nlohmann::json itemJson;

    // Basic inventory data
    itemJson["id"] = item.id;
    itemJson["characterId"] = item.characterId;
    itemJson["itemId"] = item.itemId;
    itemJson["quantity"] = item.quantity;

    // Get full item data
    auto itemData = itemManager_.getItemById(item.itemId);
    if (itemData.id > 0) // Check if item was found
    {
        // Add all ItemDataStruct fields
        itemJson["name"] = itemData.name;
        itemJson["slug"] = itemData.slug;
        itemJson["description"] = itemData.description;
        itemJson["isQuestItem"] = itemData.isQuestItem;
        itemJson["itemType"] = itemData.itemType;
        itemJson["itemTypeName"] = itemData.itemTypeName;
        itemJson["itemTypeSlug"] = itemData.itemTypeSlug;
        itemJson["isContainer"] = itemData.isContainer;
        itemJson["isDurable"] = itemData.isDurable;
        itemJson["isTradable"] = itemData.isTradable;
        itemJson["weight"] = itemData.weight;
        itemJson["rarityId"] = itemData.rarityId;
        itemJson["rarityName"] = itemData.rarityName;
        itemJson["raritySlug"] = itemData.raritySlug;
        itemJson["stackMax"] = itemData.stackMax;
        itemJson["durabilityMax"] = itemData.durabilityMax;
        itemJson["durabilityCurrent"] = itemData.durabilityMax;
        itemJson["vendorPriceBuy"] = itemData.vendorPriceBuy;
        itemJson["vendorPriceSell"] = itemData.vendorPriceSell;
        itemJson["equipSlot"] = itemData.equipSlot;
        itemJson["equipSlotName"] = itemData.equipSlotName;
        itemJson["equipSlotSlug"] = itemData.equipSlotSlug;
        itemJson["levelRequirement"] = itemData.levelRequirement;

        // Add attributes
        itemJson["attributes"] = nlohmann::json::array();
        for (const auto &attribute : itemData.attributes)
        {
            nlohmann::json attributeJson = {
                {"id", attribute.id},
                {"name", attribute.name},
                {"slug", attribute.slug},
                {"value", attribute.value}};
            itemJson["attributes"].push_back(attributeJson);
        }
    }
    else
    {
        logger_.logError("[INVENTORY] Item data not found for ID " + std::to_string(item.itemId));
        // Set default values for missing item data
        itemJson["name"] = "Unknown Item";
        itemJson["slug"] = "unknown";
        itemJson["description"] = "Item data not found";
        itemJson["attributes"] = nlohmann::json::array();
    }

    return itemJson;
}
