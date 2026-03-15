#include "services/InventoryManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "services/QuestManager.hpp"
#include <spdlog/logger.h>

InventoryManager::InventoryManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger), eventQueue_(nullptr)
{
    log_ = logger.getSystem("inventory");
}

void
InventoryManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
}

void
InventoryManager::setQuestManager(QuestManager *questManager)
{
    questManager_ = questManager;
}

void
InventoryManager::setSaveInventoryCallback(std::function<void(const std::string &)> callback)
{
    saveInventoryCallback_ = std::move(callback);
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
        log_->error("Attempted to add non-existent item ID: " + std::to_string(itemId));
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);

    // Find existing item in inventory
    auto itemIt = findInventoryItem(characterId, itemId);

    if (itemIt != playerInventories_[characterId].end())
    {
        // Item already exists, increase quantity
        itemIt->quantity += quantity;
        logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.slug +
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
        logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.slug +
                    " to character " + std::to_string(characterId) + " (new item)");
    }

    // Persist item change to game server DB immediately
    if (saveInventoryCallback_)
    {
        const auto savedIt = findInventoryItem(characterId, itemId);
        if (savedIt != playerInventories_[characterId].end())
        {
            nlohmann::json savePacket;
            savePacket["header"]["eventType"] = "saveInventoryChange";
            savePacket["header"]["clientId"] = 0;
            savePacket["header"]["hash"] = "";
            savePacket["body"]["characterId"] = characterId;
            savePacket["body"]["itemId"] = itemId;
            savePacket["body"]["quantity"] = savedIt->quantity;
            saveInventoryCallback_(savePacket.dump() + "\n");
        }
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
        log_->info("[INVENTORY] Sent INVENTORY_UPDATE event for character " + std::to_string(characterId));
    }

    // Release the write lock BEFORE calling into QuestManager.
    // advanceStep() (called transitively via onItemObtained → checkStepCompletion)
    // calls getPlayerInventory() which takes a shared lock on the same mutex.
    // Holding the exclusive lock here would cause a same-thread deadlock.
    lock.unlock();

    // Quest trigger: item obtained
    if (questManager_)
    {
        try
        {
            questManager_->onItemObtained(characterId, itemId, quantity);
        }
        catch (...)
        {
        }
    }

    return true;
}

bool
InventoryManager::removeItemFromInventoryById(int characterId, int inventoryItemId, int quantity)
{
    if (quantity <= 0)
        return false;

    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);

    auto mapIt = playerInventories_.find(characterId);
    if (mapIt == playerInventories_.end())
        return false;

    auto &inv = mapIt->second;
    auto itemIt = std::find_if(inv.begin(), inv.end(), [inventoryItemId](const PlayerInventoryItemStruct &s)
        { return s.id == inventoryItemId; });

    if (itemIt == inv.end())
        return false;
    if (itemIt->quantity < quantity)
        return false;

    const int itemId = itemIt->itemId;
    itemIt->quantity -= quantity;
    const int finalQty = itemIt->quantity;
    if (finalQty == 0)
        inv.erase(itemIt);

    // Persist
    if (saveInventoryCallback_)
    {
        nlohmann::json savePacket;
        savePacket["header"]["eventType"] = "saveInventoryChange";
        savePacket["header"]["clientId"] = 0;
        savePacket["header"]["hash"] = "";
        savePacket["body"]["characterId"] = characterId;
        savePacket["body"]["itemId"] = itemId;
        savePacket["body"]["quantity"] = finalQty;
        saveInventoryCallback_(savePacket.dump() + "\n");
    }

    // Broadcast inventory update
    if (eventQueue_)
    {
        nlohmann::json inventoryJson;
        inventoryJson["characterId"] = characterId;
        inventoryJson["items"] = nlohmann::json::array();
        auto it2 = playerInventories_.find(characterId);
        if (it2 != playerInventories_.end())
            for (const auto &item : it2->second)
                inventoryJson["items"].push_back(inventoryItemToJson(item));
        Event inventoryUpdateEvent(Event::INVENTORY_UPDATE, characterId, inventoryJson);
        lock.unlock();
        eventQueue_->push(std::move(inventoryUpdateEvent));
    }

    log_->info("[INVENTORY] removeById: char=" + std::to_string(characterId) +
               " invItemId=" + std::to_string(inventoryItemId) +
               " qty=" + std::to_string(quantity));
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
    const int finalQty = itemIt->quantity;

    if (itemIt->quantity == 0)
    {
        // Remove item completely
        playerInventories_[characterId].erase(itemIt);
        log_->info("[INVENTORY] Removed all " + itemInfo.slug + " from character " + std::to_string(characterId));
    }
    else
    {
        logger_.log("[INVENTORY] Removed " + std::to_string(quantity) + "x " + itemInfo.slug +
                    " from character " + std::to_string(characterId) +
                    " (remaining: " + std::to_string(itemIt->quantity) + ")");
    }

    // Persist item change to game server DB immediately
    if (saveInventoryCallback_)
    {
        nlohmann::json savePacket;
        savePacket["header"]["eventType"] = "saveInventoryChange";
        savePacket["header"]["clientId"] = 0;
        savePacket["header"]["hash"] = "";
        savePacket["body"]["characterId"] = characterId;
        savePacket["body"]["itemId"] = itemId;
        savePacket["body"]["quantity"] = finalQty;
        saveInventoryCallback_(savePacket.dump() + "\n");
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
        log_->info("[INVENTORY] Sent INVENTORY_UPDATE event for character " + std::to_string(characterId));
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

    auto mapIt = playerInventories_.find(characterId);
    if (mapIt == playerInventories_.end())
        return 0;

    if (itemIt != mapIt->second.end())
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
    log_->info("[INVENTORY] Cleared inventory for character " + std::to_string(characterId));
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
    itemJson["slotIndex"] = item.slotIndex;

    // Get full item data
    auto itemData = itemManager_.getItemById(item.itemId);
    if (itemData.id > 0) // Check if item was found
    {
        // Add all ItemDataStruct fields
        itemJson["slug"] = itemData.slug;
        itemJson["isQuestItem"] = itemData.isQuestItem;
        itemJson["itemType"] = itemData.itemType;
        itemJson["itemTypeName"] = itemData.itemTypeName;
        itemJson["itemTypeSlug"] = itemData.itemTypeSlug;
        itemJson["isContainer"] = itemData.isContainer;
        itemJson["isDurable"] = itemData.isDurable;
        itemJson["isTradable"] = itemData.isTradable;
        itemJson["isEquippable"] = itemData.isEquippable;
        itemJson["isUsable"] = itemData.isUsable;
        itemJson["isTwoHanded"] = itemData.isTwoHanded;
        itemJson["weight"] = itemData.weight;
        itemJson["rarityId"] = itemData.rarityId;
        itemJson["rarityName"] = itemData.rarityName;
        itemJson["raritySlug"] = itemData.raritySlug;
        itemJson["stackMax"] = itemData.stackMax;
        itemJson["durabilityMax"] = itemData.durabilityMax;
        itemJson["durabilityCurrent"] = (item.durabilityCurrent > 0) ? item.durabilityCurrent : itemData.durabilityMax;
        itemJson["isEquipped"] = item.isEquipped;
        itemJson["vendorPriceBuy"] = itemData.vendorPriceBuy;
        itemJson["vendorPriceSell"] = itemData.vendorPriceSell;
        itemJson["equipSlot"] = itemData.equipSlot;
        itemJson["equipSlotName"] = itemData.equipSlotName;
        itemJson["equipSlotSlug"] = itemData.equipSlotSlug;
        itemJson["levelRequirement"] = itemData.levelRequirement;
        itemJson["setId"] = itemData.setId;
        itemJson["setSlug"] = itemData.setSlug;

        // allowedClassIds: empty array means no class restriction
        itemJson["allowedClassIds"] = itemData.allowedClassIds;

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
        log_->error("[INVENTORY] Item data not found for ID " + std::to_string(item.itemId));
        // Set default values for missing item data
        itemJson["name"] = "Unknown Item";
        itemJson["slug"] = "unknown";
        itemJson["description"] = "Item data not found";
        itemJson["attributes"] = nlohmann::json::array();
    }

    return itemJson;
}

void
InventoryManager::loadPlayerInventory(int characterId, const std::vector<PlayerInventoryItemStruct> &items)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    playerInventories_[characterId] = items;
    log_->info("[INVENTORY] Loaded " + std::to_string(items.size()) +
               " items from DB for character " + std::to_string(characterId));
}

std::vector<PlayerInventoryItemStruct>
InventoryManager::getEquippedItems(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return {};
    std::vector<PlayerInventoryItemStruct> result;
    for (const auto &item : it->second)
        if (item.isEquipped)
            result.push_back(item);
    return result;
}

std::optional<PlayerInventoryItemStruct>
InventoryManager::getEquippedWeapon(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return std::nullopt;
    for (const auto &item : it->second)
    {
        if (!item.isEquipped)
            continue;
        const auto &itemData = itemManager_.getItemById(item.itemId);
        if (itemData.equipSlotSlug == "weapon")
            return item;
    }
    return std::nullopt;
}

void
InventoryManager::updateDurability(int characterId, int inventoryItemId, int newDurability)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return;
    for (auto &item : it->second)
    {
        if (item.id == inventoryItemId)
        {
            item.durabilityCurrent = newDurability;
            return;
        }
    }
}

void
InventoryManager::updateItemKillCount(int characterId, int inventoryItemId, int newKillCount)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return;
    for (auto &item : it->second)
    {
        if (item.id == inventoryItemId)
        {
            item.killCount = newKillCount;
            return;
        }
    }
}

void
InventoryManager::setItemEquipped(int characterId, int inventoryItemId, bool equipped)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return;
    for (auto &item : it->second)
    {
        if (item.id == inventoryItemId)
        {
            item.isEquipped = equipped;
            return;
        }
    }
}

void
InventoryManager::addInstancedItemToInventory(int characterId, const PlayerInventoryItemStruct &inst)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto &inv = playerInventories_[characterId];
    // Remove any stale entry with the same id (shouldn't exist, but be safe)
    inv.erase(std::remove_if(inv.begin(), inv.end(), [&inst](const PlayerInventoryItemStruct &s)
                  { return s.id == inst.id; }),
        inv.end());
    inv.push_back(inst);
}

void
InventoryManager::evictFromMemory(int characterId, int inventoryItemId)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto mapIt = playerInventories_.find(characterId);
    if (mapIt == playerInventories_.end())
        return;
    auto &inv = mapIt->second;
    inv.erase(std::remove_if(inv.begin(), inv.end(), [inventoryItemId](const PlayerInventoryItemStruct &s)
                  { return s.id == inventoryItemId; }),
        inv.end());

    // Broadcast updated inventory to notify clients (no DB save)
    if (eventQueue_)
    {
        nlohmann::json inventoryJson;
        inventoryJson["characterId"] = characterId;
        inventoryJson["items"] = nlohmann::json::array();
        for (const auto &item : inv)
            inventoryJson["items"].push_back(inventoryItemToJson(item));
        Event inventoryUpdateEvent(Event::INVENTORY_UPDATE, characterId, inventoryJson);
        lock.unlock();
        eventQueue_->push(std::move(inventoryUpdateEvent));
    }
}

float
InventoryManager::getTotalWeight(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return 0.0f;

    float total = 0.0f;
    for (const auto &slot : it->second)
    {
        try
        {
            const auto &itemData = itemManager_.getItemById(slot.itemId);
            total += itemData.weight * static_cast<float>(slot.quantity > 0 ? slot.quantity : 1);
        }
        catch (...)
        { /* item not found — skip */
        }
    }
    return total;
}

void
InventoryManager::updateInventoryItemId(int characterId, int itemId, int64_t newId)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return;
    for (auto &item : it->second)
    {
        if (item.itemId == itemId)
        {
            item.id = static_cast<int>(newId);
            log_->info("[INVENTORY] Updated in-memory id for char=" + std::to_string(characterId) +
                       " itemId=" + std::to_string(itemId) + " newId=" + std::to_string(newId));
            return;
        }
    }
}

int
InventoryManager::getGoldAmount(int characterId) const
{
    const ItemDataStruct *goldItem = itemManager_.getItemBySlug("gold_coin");
    if (!goldItem)
        return 0;
    return getItemQuantity(characterId, goldItem->id);
}
