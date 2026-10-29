#include "services/InventoryManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "services/QuestManager.hpp"
#include <algorithm>
#include <random>
#include <spdlog/logger.h>

namespace
{
// Next token: monotonic, random start per process (never the reserved 0).
// Masked to int63: tokens cross JSON numbers and a pqxx int64 param, and
// the game keys idempotency on them — sign flips would still work, but
// staying positive keeps every layer boring.
uint64_t mintSyncToken(uint64_t &counter, bool &seeded)
{
    if (!seeded)
    {
        std::random_device rd;
        counter = ((static_cast<uint64_t>(rd()) << 32) | rd()) & 0x7FFFFFFFFFFFFFFFULL;
        seeded = true;
    }
    do
    {
        ++counter;
    } while (counter == 0);
    return counter;
}
} // namespace

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
    uint64_t tok = 0;
    int rowId = 0;
    if (!addItemToInventoryNoPersist(characterId, itemId, quantity, tok, rowId))
        return false;

    // Persist item change to game server DB immediately (non-claim path only;
    // claim path uses addItemToInventoryNoPersist directly + single claim pkt).
    if (saveInventoryCallback_)
    {
        std::shared_lock<std::shared_mutex> lock(inventoryMutex_);
        auto mapIt = playerInventories_.find(characterId);
        if (mapIt != playerInventories_.end())
        {
            auto itemIt = mapIt->second.end();
            if (tok != 0)
            {
                for (auto it = mapIt->second.begin(); it != mapIt->second.end(); ++it)
                    if (it->syncToken == tok)
                    {
                        itemIt = it;
                        break;
                    }
            }
            else
            {
                itemIt = findInventoryItem(characterId, itemId);
                if (itemIt == mapIt->second.end())
                    itemIt = mapIt->second.end();
            }
            if (itemIt != mapIt->second.end())
            {
                nlohmann::json savePacket;
                savePacket["header"]["eventType"] = "saveInventoryChange";
                savePacket["header"]["clientId"] = 0;
                savePacket["header"]["hash"] = "";
                savePacket["body"]["characterId"] = characterId;
                savePacket["body"]["itemId"] = itemId;
                savePacket["body"]["quantity"] = itemIt->quantity;
                savePacket["body"]["inventoryItemId"] = itemIt->id;
                savePacket["body"]["syncToken"] = itemIt->syncToken;
                // Copy payload while holding the shared lock, send outside it.
                std::string wire = savePacket.dump() + "\n";
                lock.unlock();
                saveInventoryCallback_(wire);
                return true;
            }
        }
    }
    return true;
}

bool
InventoryManager::addItemToInventoryNoPersist(int characterId, int itemId, int quantity,
    uint64_t &outSyncToken, int &outRowId)
{
    outSyncToken = 0;
    outRowId = 0;
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

    PlayerInventoryItemStruct touched;
    {
        std::unique_lock<std::shared_mutex> lock(inventoryMutex_);

        // Find existing item in inventory by template itemId.
        auto itemIt = findInventoryItem(characterId, itemId);

        // Never stack items with stackMax=1 (e.g. weapons, armour).
        const bool canStack = (itemInfo.stackMax > 1);

        if (canStack && itemIt != playerInventories_[characterId].end())
        {
            itemIt->quantity += quantity;
            logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.slug +
                        " to character " + std::to_string(characterId) +
                        " (new total: " + std::to_string(itemIt->quantity) + ")");
        }
        else
        {
            PlayerInventoryItemStruct newItem;
            newItem.characterId = characterId;
            newItem.itemId = itemId;
            newItem.quantity = quantity;
            newItem.syncToken = mintSyncToken(nextSyncToken_, syncTokenSeeded_);
            newItem.syncNextMs = 0;
            playerInventories_[characterId].push_back(newItem);
            auto &inv = playerInventories_[characterId];
            itemIt = (inv.size() >= 1) ? inv.end() - 1 : inv.end();
            logger_.log("[INVENTORY] Added " + std::to_string(quantity) + "x " + itemInfo.slug +
                        " to character " + std::to_string(characterId) + " (new item)");
        }

        outSyncToken = itemIt->syncToken;
        outRowId = itemIt->id;
        touched = *itemIt;
    }

    // S2.3 delta: only the touched row travels; statics resolve client-side.
    if (eventQueue_)
        emitInventoryDelta(characterId, {touched}, {});

    // Quest trigger: item obtained. Best-effort: never fail the pickup itself.
    if (questManager_)
    {
        try
        {
            questManager_->onItemObtained(characterId, itemId, quantity);
        }
        catch (const std::exception &e)
        {
            log_->debug("[INVENTORY] onItemObtained hook failed for char={} item={} ({}), pickup kept",
                characterId, itemId, e.what());
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

    // S2.3 delta: reduced row, or the erased id when qty hit 0.
    PlayerInventoryItemStruct touchedRow;
    const bool rowErased = (finalQty == 0);
    if (!rowErased)
        touchedRow = *itemIt;
    if (eventQueue_)
    {
        lock.unlock();
        if (rowErased)
            emitInventoryDelta(characterId, {}, {inventoryItemId});
        else
            emitInventoryDelta(characterId, {touchedRow}, {});
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

    // Remove from multiple stacks if needed, remembering every touched
    // stack: each needs its own save packet, otherwise the game only learns
    // about the last stack and resurrects the others on the next login.
    struct TouchedStack
    {
        int inventoryItemId = 0;
        int finalQty = 0;
    };
    std::vector<TouchedStack> touched;

    // Remove from multiple stacks if needed
    for (auto it = inventory.begin(); it != inventory.end() && remainingToRemove > 0; )
    {
        if (it->itemId != itemId)
        {
            ++it;
            continue;
        }

        const int stackId = it->id;
        int deduct = std::min(it->quantity, remainingToRemove);
        it->quantity -= deduct;
        remainingToRemove -= deduct;

        if (it->quantity == 0)
        {
            touched.push_back({stackId, 0});
            it = inventory.erase(it);
        }
        else
        {
            touched.push_back({stackId, it->quantity});
            ++it;
        }
    }

    log_->info("[INVENTORY] Removed " + std::to_string(quantity) + "x " + itemInfo.slug +
               " from character " + std::to_string(characterId) +
               " (remaining total: " + std::to_string(totalQty - quantity) + ")");

    // Persist item change to game server DB immediately
    if (saveInventoryCallback_)
    {
        for (const auto &t : touched)
        {
            nlohmann::json savePacket;
            savePacket["header"]["eventType"] = "saveInventoryChange";
            savePacket["header"]["clientId"] = 0;
            savePacket["header"]["hash"] = "";
            savePacket["body"]["characterId"] = characterId;
            savePacket["body"]["itemId"] = itemId;
            savePacket["body"]["quantity"] = t.finalQty;
            savePacket["body"]["inventoryItemId"] = t.inventoryItemId; // >0 = by id; 0 = fallback DELETE by itemId
            savePacket["body"]["syncToken"] = uint64_t(0);
            saveInventoryCallback_(savePacket.dump() + "\n");
        }
    }

    // S2.3 delta: re-resolve touched stacks (erased ones go to removed).
    if (eventQueue_)
    {
        std::vector<PlayerInventoryItemStruct> changed;
        std::vector<int64_t> removed;
        auto it = playerInventories_.find(characterId);
        for (const auto &t : touched)
        {
            bool found = false;
            if (it != playerInventories_.end())
                for (const auto &item : it->second)
                    if (item.id == t.inventoryItemId)
                    {
                        changed.push_back(item);
                        found = true;
                        break;
                    }
            if (!found)
                removed.push_back(t.inventoryItemId);
        }
        emitInventoryDelta(characterId, changed, removed);
        log_->info("[INVENTORY] Sent INVENTORY_UPDATE event for character " + std::to_string(characterId));
    }

    return true;
}

bool
InventoryManager::removeItemFromInventoryNoPersist(int characterId, int itemId, int quantity)
{
    if (quantity <= 0)
        return false;
    {
        std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
        auto mapIt = playerInventories_.find(characterId);
        if (mapIt == playerInventories_.end())
            return false;
        auto &inventory = mapIt->second;
        int totalQty = 0;
        for (const auto &item : inventory)
            if (item.itemId == itemId)
                totalQty += item.quantity;
        if (totalQty < quantity)
            return false;
        int remainingToRemove = quantity;
        for (auto it = inventory.begin(); it != inventory.end() && remainingToRemove > 0; )
        {
            if (it->itemId != itemId)
            {
                ++it;
                continue;
            }
            int deduct = std::min(it->quantity, remainingToRemove);
            it->quantity -= deduct;
            remainingToRemove -= deduct;
            if (it->quantity == 0)
                it = inventory.erase(it);
            else
                ++it;
        }
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
        // Canonical wire order (single source with the DB snapshot path,
        // which is ORDER BY id): synced rows ascending by instance id,
        // unsynced pendings (id==0) last in arrival order. Without this,
        // consecutive full snapshots reshuffle the client grid and stale
        // tooltips no longer match the slots underneath.
        std::vector<PlayerInventoryItemStruct> out = it->second;
        std::stable_sort(out.begin(), out.end(),
            [](const PlayerInventoryItemStruct &a, const PlayerInventoryItemStruct &b)
            {
                const bool aPending = (a.id == 0);
                const bool bPending = (b.id == 0);
                if (aPending != bPending)
                    return bPending; // synced rows first, pendings last
                return a.id < b.id;
            });
        return out;
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

        // Add attributes (slug+value only: names resolve client-side via slug)
        itemJson["attributes"] = nlohmann::json::array();
        for (const auto &attribute : itemData.attributes)
        {
            nlohmann::json attributeJson = {
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

nlohmann::json
InventoryManager::inventoryDeltaToJson(const PlayerInventoryItemStruct &item) const
{
    // S2.3 slim delta row: instance state only. Statics (slug, prices,
    // attributes, effects) come from the client's versioned itemCatalog.
    return {
        {"id", item.id},
        {"itemId", item.itemId},
        {"quantity", item.quantity},
        {"durabilityCurrent", item.durabilityCurrent},
        {"isEquipped", item.isEquipped},
        {"slotIndex", item.slotIndex},
        {"killCount", item.killCount}};
}

void
InventoryManager::emitInventoryDelta(int characterId,
    const std::vector<PlayerInventoryItemStruct> &changed,
    const std::vector<int64_t> &removedIds)
{
    if (!eventQueue_)
        return;
    nlohmann::json delta;
    delta["characterId"] = characterId;
    // S15a: every client-visible change carries the new generation. Only
    // published changes bump (queue present) — the version describes what
    // the client can see, so a queue-less (test) manager stays at 0.
    delta["version"] = bumpInventoryVersion(characterId);
    delta["changed"] = nlohmann::json::array();
    for (const auto &item : changed)
        delta["changed"].push_back(inventoryDeltaToJson(item));
    delta["removed"] = nlohmann::json::array();
    for (int64_t id : removedIds)
        delta["removed"].push_back(id);
    Event inventoryUpdateEvent(Event::INVENTORY_UPDATE, characterId, delta);
    eventQueue_->push(std::move(inventoryUpdateEvent));
    // S15b: retain for reconnect replay (bounded; oldest falls off).
    // Version read BEFORE the move — argument evaluation order would
    // otherwise race the move-from state.
    const uint64_t publishedVersion = delta["version"].get<uint64_t>();
    {
        std::lock_guard<std::mutex> lock(versionMutex_);
        auto &log = versionLog_[characterId];
        log.push_back(VersionedBody{publishedVersion, false, std::move(delta)});
        while (log.size() > kDeltaLogDepth)
            log.erase(log.begin());
    }
    log_->info("[INVENTORY] Sent delta update for character " + std::to_string(characterId) +
               " (changed=" + std::to_string(changed.size()) +
               " removed=" + std::to_string(removedIds.size()) + ")");
}

void
InventoryManager::loadPlayerInventory(int characterId, const std::vector<PlayerInventoryItemStruct> &items)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    // JOIN audit (convergence): keep in-memory rows still awaiting their DB
    // id (save in flight, sync echo not yet seen). A blind replace would drop
    // a just-picked-up item until some later mutation re-sends it.
    std::vector<PlayerInventoryItemStruct> pending;
    auto memIt = playerInventories_.find(characterId);
    if (memIt != playerInventories_.end())
        for (const auto &row : memIt->second)
            if (row.id == 0 && row.syncToken != 0)
                pending.push_back(row);
    playerInventories_[characterId] = items;
    auto &inv = playerInventories_[characterId];
    for (auto &p : pending)
    {
        // Max-wins: memory holds the newest absolute quantity; the DB push
        // may predate the in-flight save. Never sum (that double-counts).
        // Non-stackable instances are never absorbed (each is a new row).
        ItemDataStruct info = itemManager_.getItemById(p.itemId);
        const bool canStack = (info.id != 0 && info.stackMax > 1);
        bool absorbed = false;
        if (canStack)
        {
            for (auto &row : inv)
            {
                if (row.itemId != p.itemId || row.id == 0)
                    continue;
                if (row.quantity < p.quantity)
                    row.quantity = p.quantity;
                absorbed = true;
                break; // (character_id, item_id) is unique in DB: one row max
            }
        }
        if (!absorbed)
            inv.push_back(std::move(p));
    }
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

    // S2.3 delta: the instanced row just added.
    if (eventQueue_)
    {
        emitInventoryDelta(characterId, {inst}, {});
        lock.unlock();
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

    // S2.3 delta: single erased row id.
    if (eventQueue_)
    {
        emitInventoryDelta(characterId, {}, {inventoryItemId});
        lock.unlock();
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
InventoryManager::updateInventoryItemId(int characterId, int itemId, int64_t newId, uint64_t syncToken)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    auto it = playerInventories_.find(characterId);
    if (it == playerInventories_.end())
        return;
    bool updated = false;
    // Whichever token got cleared by this echo owns stashed equip saves:
    // fill them with the real id now (drained by the caller afterwards).
    // Covers the zero-token echo case too.
    uint64_t clearedToken = 0;
    if (syncToken != 0)
    {
        // Exact-instance match: the sync echo carries the token stamped at
        // pickup, so two pending rows with the same itemId never cross.
        for (auto &item : it->second)
        {
            if (item.itemId == itemId && item.id == 0 && item.syncToken == syncToken)
            {
                item.id = static_cast<int>(newId);
                item.syncToken = 0;
                item.syncNextMs = 0;
                clearedToken = syncToken;
                log_->info("[INVENTORY] Updated in-memory id for char=" + std::to_string(characterId) +
                           " itemId=" + std::to_string(itemId) + " newId=" + std::to_string(newId));
                updated = true;
                break;
            }
        }
        if (!updated)
            return; // stale/duplicate echo for an already-synced row: ignore
    }
    else
    {
        for (auto &item : it->second)
        {
            // Legacy token-less echo: only update items with placeholder
            // id==0 (awaiting DB sync). Non-stackable items (stackMax=1) can
            // have multiple entries per template itemId; the sync always
            // corresponds to the most recently inserted row, which still
            // has id==0.
            if (item.itemId == itemId && item.id == 0)
            {
                item.id = static_cast<int>(newId);
                clearedToken = item.syncToken;
                item.syncToken = 0;
                item.syncNextMs = 0;
                log_->info("[INVENTORY] Updated in-memory id for char=" + std::to_string(characterId) +
                           " itemId=" + std::to_string(itemId) + " newId=" + std::to_string(newId));
                updated = true;
                break;
            }
        }
        if (!updated)
            return;
    }
    if (clearedToken != 0)
    {
        auto pendIt = pendingEquipSaves_.find(clearedToken);
        if (pendIt != pendingEquipSaves_.end())
            for (auto &s : pendIt->second)
                s.inventoryItemId = static_cast<int>(newId);
    }

    // Send INVENTORY_UPDATE to client so it learns the correct inventoryItemId.
    // Without this, the client retains the placeholder id=0 and equip/lookup
    // requests fail with ITEM_NOT_IN_INVENTORY because the server-side id no
    // longer matches what the client cached from the initial INVENTORY_UPDATE.
    // S2.3 delta: only the newly-synced row.
    if (eventQueue_)
    {
        std::vector<PlayerInventoryItemStruct> changed;
        for (const auto &item : it->second)
            if (item.itemId == itemId && item.id == static_cast<int>(newId))
            {
                changed.push_back(item);
                break;
            }
        emitInventoryDelta(characterId, changed, {});
        lock.unlock();
    }
}

void
InventoryManager::deferEquipSave(uint64_t syncToken, int characterId,
    const std::string &action, const std::string &slotSlug, int itemId)
{
    if (syncToken == 0)
        return;
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    PendingEquipSave s;
    s.characterId = characterId;
    s.action = action;
    s.slotSlug = slotSlug;
    s.inventoryItemId = 0; // filled when the sync echo lands
    s.itemId = itemId;
    s.syncToken = syncToken;
    pendingEquipSaves_[syncToken].push_back(std::move(s));
}

std::vector<InventoryManager::PendingEquipSave>
InventoryManager::drainPendingEquipSaves()
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    std::vector<PendingEquipSave> out;
    for (auto &[token, vec] : pendingEquipSaves_)
    {
        (void)token;
        for (auto &s : vec)
            if (s.inventoryItemId > 0)
                out.push_back(s);
    }
    // Drop flushed buckets; keep still-unsynced ones for the next echo.
    for (auto it = pendingEquipSaves_.begin(); it != pendingEquipSaves_.end();)
    {
        it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                             [](const PendingEquipSave &s) { return s.inventoryItemId > 0; }),
            it->second.end());
        if (it->second.empty())
            it = pendingEquipSaves_.erase(it);
        else
            ++it;
    }
    return out;
}

int
InventoryManager::getGoldAmount(int characterId) const
{
    const ItemDataStruct *goldItem = itemManager_.getItemBySlug("gold_coin");
    if (!goldItem)
        return 0;
    return getItemQuantity(characterId, goldItem->id);
}

int
InventoryManager::getGoldFromSnapshot(const std::vector<PlayerInventoryItemStruct> &items) const
{
    const ItemDataStruct *goldItem = itemManager_.getItemBySlug("gold_coin");
    if (!goldItem)
        return 0;
    int total = 0;
    for (const auto &item : items)
    {
        if (item.itemId == goldItem->id)
            total += item.quantity;
    }
    return total;
}

uint64_t
InventoryManager::getInventoryVersion(int characterId) const
{
    std::lock_guard<std::mutex> lock(versionMutex_);
    auto it = inventoryVersions_.find(characterId);
    return it != inventoryVersions_.end() ? it->second : 0;
}

uint64_t
InventoryManager::bumpInventoryVersion(int characterId)
{
    std::lock_guard<std::mutex> lock(versionMutex_);
    return ++inventoryVersions_[characterId];
}

uint64_t
InventoryManager::logSnapshotPublished(int characterId, const nlohmann::json &body)
{
    std::lock_guard<std::mutex> lock(versionMutex_);
    const uint64_t v = ++inventoryVersions_[characterId];
    auto &log = versionLog_[characterId];
    nlohmann::json stored = body;
    stored["version"] = v;
    stored["fullResync"] = true; // for the replay receiver (live push omits it)
    log.push_back(VersionedBody{v, true, std::move(stored)});
    while (log.size() > kDeltaLogDepth)
        log.erase(log.begin());
    return v;
}

bool
InventoryManager::getEntriesSince(int characterId, uint64_t lastVersion,
                                  std::vector<VersionedBody> &out) const
{
    out.clear();
    std::lock_guard<std::mutex> lock(versionMutex_);
    const auto lit = versionLog_.find(characterId);
    if (lit == versionLog_.end() || lit->second.empty())
    {
        // Nothing retained: covered only when the client is already current.
        const auto vit = inventoryVersions_.find(characterId);
        const uint64_t current = vit != inventoryVersions_.end() ? vit->second : 0;
        return lastVersion >= current;
    }
    const auto &log = lit->second;
    if (lastVersion + 1 < log.front().version)
        return false; // baseline fell out of the window -> live full snapshot
    for (const auto &entry : log)
    {
        if (entry.version > lastVersion)
            out.push_back(entry);
    }
    return true;
}

std::vector<PlayerInventoryItemStruct>
InventoryManager::dueSyncResend(int64_t nowMs)
{
    std::unique_lock<std::shared_mutex> lock(inventoryMutex_);
    std::vector<PlayerInventoryItemStruct> due;
    for (auto &[characterId, inv] : playerInventories_)
    {
        (void)characterId;
        for (auto &item : inv)
        {
            // Only rows still awaiting their DB id (sync echo lost or socket
            // was dead). Synced rows (id>0 or token==0) never resend.
            if (item.id != 0 || item.syncToken == 0 || item.syncNextMs > nowMs)
                continue;
            ++item.syncAttempts;
            if (item.syncAttempts > kSyncMaxAttempts)
            {
                // Give up: token-less matching takes over. One warn — a row
                // unsynced for ~5 min means the save path is broken.
                log_->warn("[INVENTORY] Sync retry budget exhausted for char=" +
                           std::to_string(item.characterId) + " itemId=" +
                           std::to_string(item.itemId) + "; clearing token");
                item.syncToken = 0;
                item.syncNextMs = 0;
                continue;
            }
            item.syncNextMs = nowMs + kSyncRetryMs;
            due.push_back(item);
        }
    }
    return due;
}
