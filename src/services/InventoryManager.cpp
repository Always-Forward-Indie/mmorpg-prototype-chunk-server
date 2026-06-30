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

    // Find existing item in inventory by template itemId.
    auto itemIt = findInventoryItem(characterId, itemId);

    // Never stack items with stackMax=1 (e.g. weapons, armour).
    // These are equippable/unique-per-instance and each must be a separate
    // row with its own durability and killCount. Stacking them would merge
    // independent instances and break equip/unequip logic.
    const bool canStack = (itemInfo.stackMax > 1);

    if (canStack && itemIt != playerInventories_[characterId].end())
    {
        // Item already exists and is stackable, increase quantity
        itemIt->quantity += quantity;
        logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.slug +
                    " to character " + std::to_string(characterId) +
                    " (new total: " + std::to_string(itemIt->quantity) + ")");
    }
    else
    {
        // New item (or non-stackable duplicate), add separate entry
        PlayerInventoryItemStruct newItem;
        newItem.characterId = characterId;
        newItem.itemId = itemId;
        newItem.quantity = quantity;
        // ID will be set when syncing with database later

        playerInventories_[characterId].push_back(newItem);
        // Re-find itemIt — push_back may invalidate iterators, so locate the
        // freshly inserted element. For non-stackable items (stackMax=1) there
        // can be multiple entries with the same template itemId; back() always
        // returns the instance we just added.
        auto &inv = playerInventories_[characterId];
        itemIt = (inv.size() >= 1) ? inv.end() - 1 : inv.end();
        logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.slug +
                    " to character " + std::to_string(characterId) + " (new item)");
    }

    // Persist item change to game server DB immediately
    if (saveInventoryCallback_)
    {
        if (itemIt != playerInventories_[characterId].end())
        {
            nlohmann::json savePacket;
            savePacket["header"]["eventType"] = "saveInventoryChange";
            savePacket["header"]["clientId"] = 0;
            savePacket["header"]["hash"] = "";
            savePacket["body"]["characterId"] = characterId;
            savePacket["body"]["itemId"] = itemId;
            savePacket["body"]["quantity"] = itemIt->quantity;
            savePacket["body"]["inventoryItemId"] = itemIt->id; // 0 = new row (INSERT), >0 = UPDATE
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
        savePacket["body"]["inventoryItemId"] = inventoryItemId; // UPDATE by exact DB row id
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

    auto mapIt = playerInventories_.find(characterId);
    if (mapIt == playerInventories_.end())
    {
        logger_.logError("Attempted to remove item " + std::to_string(itemId) + " from character " +
                         std::to_string(characterId) + " but inventory is empty");
        return false;
    }

    auto &inventory = mapIt->second;

    // Count total across all stacks of this item
    int totalQty = 0;
    for (const auto &item : inventory)
        if (item.itemId == itemId)
            totalQty += item.quantity;

    if (totalQty < quantity)
    {
        logger_.logError("Attempted to remove " + std::to_string(quantity) + " of item " +
                         std::to_string(itemId) + " from character " + std::to_string(characterId) +
                         " but only has " + std::to_string(totalQty));
        return false;
    }

    ItemDataStruct itemInfo = itemManager_.getItemById(itemId);
    int remainingToRemove = quantity;
    int lastInvItemId = 0;
    int finalQty = 0;

    // Remove from multiple stacks if needed
    for (auto it = inventory.begin(); it != inventory.end() && remainingToRemove > 0; )
    {
        if (it->itemId != itemId)
        {
            ++it;
            continue;
        }

        lastInvItemId = it->id;
        int deduct = std::min(it->quantity, remainingToRemove);
        it->quantity -= deduct;
        remainingToRemove -= deduct;
        finalQty = it->quantity;

        if (it->quantity == 0)
        {
            it = inventory.erase(it);
        }
        else
        {
            ++it;
        }
    }

    log_->info("[INVENTORY] Removed " + std::to_string(quantity) + "x " + itemInfo.slug +
               " from character " + std::to_string(characterId) +
               " (remaining total: " + std::to_string(totalQty - quantity) + ")");

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
        savePacket["body"]["inventoryItemId"] = lastInvItemId; // >0 = UPDATE by id; 0 = fallback DELETE by itemId
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

    auto mapIt = playerInventories_.find(characterId);
    if (mapIt == playerInventories_.end())
        return 0;

    int total = 0;
    for (const auto &item : mapIt->second)
    {
        if (item.itemId == itemId)
            total += item.quantity;
    }
    return total;
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
        itemJson["itemTypeSlug"] = itemData.itemTypeSlug;
        itemJson["isContainer"] = itemData.isContainer;
        itemJson["isDurable"] = itemData.isDurable;
        itemJson["isTradable"] = itemData.isTradable;
        itemJson["isEquippable"] = itemData.isEquippable;
        itemJson["isUsable"] = itemData.isUsable;
        itemJson["isTwoHanded"] = itemData.isTwoHanded;
        itemJson["weight"] = itemData.weight;
        itemJson["rarityId"] = itemData.rarityId;
        itemJson["raritySlug"] = itemData.raritySlug;
        itemJson["stackMax"] = itemData.stackMax;
        itemJson["durabilityMax"] = itemData.durabilityMax;
        itemJson["durabilityCurrent"] = (item.durabilityCurrent > 0) ? item.durabilityCurrent : itemData.durabilityMax;
        itemJson["isEquipped"] = item.isEquipped;
        itemJson["vendorPriceBuy"] = itemData.vendorPriceBuy;
        itemJson["vendorPriceSell"] = itemData.vendorPriceSell;
        itemJson["equipSlot"] = itemData.equipSlot;
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
                {"slug", attribute.slug},
                {"value", attribute.value}};
            itemJson["attributes"].push_back(attributeJson);
        }

        // Add use effects (what happens when the player uses this item)
        itemJson["useEffects"] = nlohmann::json::array();
        for (const auto &eff : itemData.useEffects)
        {
            nlohmann::json effJson = {
                {"effectSlug", eff.effectSlug},
                {"attributeSlug", eff.attributeSlug},
                {"value", eff.value},
                {"isInstant", eff.isInstant},
                {"durationSeconds", eff.durationSeconds},
                {"tickMs", eff.tickMs},
                {"cooldownSeconds", eff.cooldownSeconds}};
            itemJson["useEffects"].push_back(effJson);
        }

        // Mastery and Item Soul
        itemJson["masterySlug"] = itemData.masterySlug;
        itemJson["killCount"] = item.killCount;
    }
    else
    {
        log_->error("[INVENTORY] Item data not found for ID " + std::to_string(item.itemId));
        // Set default values for missing item data
        itemJson["slug"] = "unknown";
        itemJson["attributes"] = nlohmann::json::array();
    }

    return itemJson;
}

void
InventoryManager::loadPlayerInventory(int characterId, const std::vector<PlayerInventoryItemStruct> &items)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    playerInventories_[characterId] = items;
    consolidateInventory(characterId);
    log_->info("[INVENTORY] Loaded " + std::to_string(items.size()) +
               " items from DB for character " + std::to_string(characterId));
}

void
InventoryManager::consolidateInventory(int characterId)
{
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return;

    auto &inventory = it->second;

    // Group items by template itemId so we can decide per-group whether to
    // merge (stackable) or keep every instance (non-stackable).
    std::map<int, std::vector<PlayerInventoryItemStruct>> groups;
    for (auto &item : inventory)
        groups[item.itemId].push_back(std::move(item));

    std::vector<PlayerInventoryItemStruct> consolidated;
    for (auto &[itemId, items] : groups)
    {
        ItemDataStruct itemInfo = itemManager_.getItemById(itemId);
        // stackMax > 1: stackable (consumables, materials) — merge into one row.
        // stackMax ≤ 1 or item not found: non-stackable (weapons, armour) —
        // every instance has its own durability, killCount and equip state.
        const bool canStack = (itemInfo.id != 0 && itemInfo.stackMax > 1);

        if (canStack)
        {
            auto &first = items.front();
            for (size_t i = 1; i < items.size(); ++i)
                first.quantity += items[i].quantity;
            consolidated.push_back(std::move(first));
        }
        else
        {
            for (auto &item : items)
                consolidated.push_back(std::move(item));
        }
    }
    inventory = std::move(consolidated);
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
        if (itemData.equipSlotSlug == "main_hand" || itemData.equipSlotSlug == "two_hand")
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

    // Broadcast updated inventory so client UI reflects the picked-up / traded item.
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
    bool updated = false;
    for (auto &item : it->second)
    {
        // Only update items with placeholder id==0 (awaiting DB sync).
        // Non-stackable items (stackMax=1) can have multiple entries per
        // template itemId; the sync always corresponds to the most recently
        // inserted row, which still has id==0.
        if (item.itemId == itemId && item.id == 0)
        {
            item.id = static_cast<int>(newId);
            updated = true;
            log_->info("[INVENTORY] Updated in-memory id for char=" + std::to_string(characterId) +
                       " itemId=" + std::to_string(itemId) + " newId=" + std::to_string(newId));
            break;
        }
    }
    if (!updated)
        return;

    // Send INVENTORY_UPDATE to client so it learns the correct inventoryItemId.
    // Without this, the client retains the placeholder id=0 and equip/lookup
    // requests fail with ITEM_NOT_IN_INVENTORY because the server-side id no
    // longer matches what the client cached from the initial INVENTORY_UPDATE.
    if (eventQueue_)
    {
        nlohmann::json inventoryJson;
        inventoryJson["characterId"] = characterId;
        inventoryJson["items"] = nlohmann::json::array();
        for (const auto &item : it->second)
            inventoryJson["items"].push_back(inventoryItemToJson(item));
        Event inventoryUpdateEvent(Event::INVENTORY_UPDATE, characterId, inventoryJson);
        lock.unlock();
        eventQueue_->push(std::move(inventoryUpdateEvent));
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
