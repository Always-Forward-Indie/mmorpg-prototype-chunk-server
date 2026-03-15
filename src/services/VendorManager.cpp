#include "services/VendorManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include <cmath>
#include <spdlog/logger.h>

VendorManager::VendorManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger)
{
    log_ = logger.getSystem("vendor");
}

// ── Data loading ──────────────────────────────────────────────────────────────

void
VendorManager::setVendorData(const std::vector<VendorNPCDataStruct> &vendors)
{
    std::unique_lock lock(mutex_);
    vendors_.clear();
    for (const auto &v : vendors)
        vendors_[v.npcId] = v;
    log_->info("[VendorManager] Loaded {} vendor NPCs", vendors.size());
}

void
VendorManager::updateStockCount(int npcId, int itemId, int newStock)
{
    std::unique_lock lock(mutex_);
    auto npcIt = vendors_.find(npcId);
    if (npcIt == vendors_.end())
        return;
    for (auto &slot : npcIt->second.items)
    {
        if (slot.itemId == itemId)
        {
            slot.stockCurrent = newStock;
            log_->debug("[VendorManager] Restock NPC {} item {} -> {}", npcId, itemId, newStock);
            return;
        }
    }
}

// ── Shop queries ──────────────────────────────────────────────────────────────

nlohmann::json
VendorManager::buildShopJson(int npcId, float buyMarkupPct) const
{
    std::shared_lock lock(mutex_);
    auto npcIt = vendors_.find(npcId);
    if (npcIt == vendors_.end())
        return nullptr;

    nlohmann::json shop = nlohmann::json::array();
    for (const auto &slot : npcIt->second.items)
    {
        const ItemDataStruct item = itemManager_.getItemById(slot.itemId);
        if (item.id == 0)
            continue;

        int priceBuy = (slot.priceOverrideBuy > 0)
                           ? slot.priceOverrideBuy
                           : static_cast<int>(std::ceil(item.vendorPriceBuy * (1.0f + buyMarkupPct)));

        int priceSell = (slot.priceOverrideSell > 0)
                            ? slot.priceOverrideSell
                            : item.vendorPriceSell;

        nlohmann::json entry;
        entry["itemId"] = item.id;
        entry["name"] = item.slug;
        entry["slug"] = item.slug;
        entry["itemTypeSlug"] = item.itemTypeSlug;
        entry["raritySlug"] = item.raritySlug;
        entry["stackMax"] = item.stackMax;
        entry["isDurable"] = item.isDurable;
        entry["isTradable"] = item.isTradable;
        entry["priceBuy"] = priceBuy;
        entry["priceSell"] = priceSell;
        entry["stockCurrent"] = slot.stockCurrent;
        entry["stockMax"] = slot.stockMax;
        shop.push_back(entry);
    }
    return shop;
}

// ── Transactions ──────────────────────────────────────────────────────────────

VendorManager::BuyResult
VendorManager::buyItem(
    int characterId,
    int npcId,
    int itemId,
    int quantity,
    int goldItemId,
    InventoryManager &inventoryMgr,
    float buyMarkupPct)
{
    if (quantity <= 0)
        return {false, "invalid_quantity", 0};

    std::unique_lock lock(mutex_);

    auto npcIt = vendors_.find(npcId);
    if (npcIt == vendors_.end())
        return {false, "vendor_not_found", 0};

    VendorInventoryItemStruct *slot = nullptr;
    for (auto &s : npcIt->second.items)
    {
        if (s.itemId == itemId)
        {
            slot = &s;
            break;
        }
    }
    if (!slot)
        return {false, "item_not_sold_here", 0};

    if (slot->stockCurrent != -1 && slot->stockCurrent < quantity)
        return {false, "insufficient_stock", 0};

    const ItemDataStruct item = itemManager_.getItemById(itemId);
    if (item.id == 0)
        return {false, "item_not_found", 0};

    int unitPrice = (slot->priceOverrideBuy > 0)
                        ? slot->priceOverrideBuy
                        : static_cast<int>(std::ceil(item.vendorPriceBuy * (1.0f + buyMarkupPct)));
    int totalPrice = unitPrice * quantity;

    // Gold check (lock-free on inventory mutex — InventoryManager is separately locked)
    lock.unlock(); // Release vendor lock before touching InventoryManager
    int playerGold = inventoryMgr.getItemQuantity(characterId, goldItemId);
    if (playerGold < totalPrice)
        return {false, "insufficient_gold", 0};

    // Deduct gold
    if (!inventoryMgr.removeItemFromInventory(characterId, goldItemId, totalPrice))
        return {false, "gold_deduct_failed", 0};

    // Give item
    if (!inventoryMgr.addItemToInventory(characterId, itemId, quantity))
    {
        // Rollback gold
        inventoryMgr.addItemToInventory(characterId, goldItemId, totalPrice);
        return {false, "inventory_add_failed", 0};
    }

    // Decrement stock
    lock.lock();
    npcIt = vendors_.find(npcId); // Re-find after unlock
    if (npcIt != vendors_.end())
    {
        for (auto &s : npcIt->second.items)
        {
            if (s.itemId == itemId && s.stockCurrent != -1)
            {
                s.stockCurrent = std::max(0, s.stockCurrent - quantity);
                break;
            }
        }
    }

    log_->info("[VendorManager] char {} bought {}x item {} from NPC {} for {} gold",
        characterId,
        quantity,
        itemId,
        npcId,
        totalPrice);
    return {true, "", totalPrice};
}

VendorManager::SellResult
VendorManager::sellItem(
    int characterId,
    int npcId,
    int inventoryItemId,
    int quantity,
    int goldItemId,
    InventoryManager &inventoryMgr,
    float sellTaxPct)
{
    if (quantity <= 0)
        return {false, "invalid_quantity", 0};

    // Check vendor exists
    {
        std::shared_lock lock(mutex_);
        if (vendors_.find(npcId) == vendors_.end())
            return {false, "vendor_not_found", 0};
    }

    // Find item in player inventory by inventoryItemId
    const auto inventory = inventoryMgr.getPlayerInventory(characterId);
    const PlayerInventoryItemStruct *invSlot = nullptr;
    for (const auto &s : inventory)
        if (s.id == inventoryItemId)
        {
            invSlot = &s;
            break;
        }

    if (!invSlot)
        return {false, "item_not_in_inventory", 0};
    if (invSlot->isEquipped)
        return {false, "item_is_equipped", 0};
    if (invSlot->quantity < quantity)
        return {false, "insufficient_quantity", 0};

    const ItemDataStruct item = itemManager_.getItemById(invSlot->itemId);
    if (!item.isTradable)
        return {false, "item_not_tradable", 0};

    int unitSell = item.vendorPriceSell;
    int goldReceived = static_cast<int>(std::floor(unitSell * quantity * (1.0f - sellTaxPct)));

    if (!inventoryMgr.removeItemFromInventory(characterId, invSlot->itemId, quantity))
        return {false, "item_remove_failed", 0};

    inventoryMgr.addItemToInventory(characterId, goldItemId, goldReceived);

    log_->info("[VendorManager] char {} sold {}x item {} to NPC {} for {} gold",
        characterId,
        quantity,
        invSlot->itemId,
        npcId,
        goldReceived);
    return {true, "", goldReceived};
}

// ── Batch transactions ────────────────────────────────────────────────────────

VendorManager::BuyBatchResult
VendorManager::buyBatch(
    int characterId,
    int npcId,
    const std::vector<BuyBatchItemEntry> &entries,
    int goldItemId,
    InventoryManager &inventoryMgr,
    float buyMarkupPct)
{
    if (entries.empty())
        return {false, "empty_batch", 0, {}};
    if (static_cast<int>(entries.size()) > MAX_VENDOR_BATCH_SIZE)
        return {false, "batch_too_large", 0, {}};

    // ── Phase 1: validation + price calculation (hold shared lock) ────────────
    std::vector<BuyBatchItemResult> resultItems;
    resultItems.reserve(entries.size());
    int totalGold = 0;

    {
        std::shared_lock lock(mutex_);

        auto npcIt = vendors_.find(npcId);
        if (npcIt == vendors_.end())
            return {false, "vendor_not_found", 0, {}};

        for (const auto &entry : entries)
        {
            if (entry.quantity <= 0)
                return {false, "invalid_quantity", 0, {}};

            const VendorInventoryItemStruct *slot = nullptr;
            for (const auto &s : npcIt->second.items)
            {
                if (s.itemId == entry.itemId)
                {
                    slot = &s;
                    break;
                }
            }
            if (!slot)
                return {false, "item_not_sold_here", 0, {}};
            if (slot->stockCurrent != -1 && slot->stockCurrent < entry.quantity)
                return {false, "insufficient_stock", 0, {}};

            const ItemDataStruct item = itemManager_.getItemById(entry.itemId);
            if (item.id == 0)
                return {false, "item_not_found", 0, {}};

            int unitPrice = (slot->priceOverrideBuy > 0)
                                ? slot->priceOverrideBuy
                                : static_cast<int>(std::ceil(item.vendorPriceBuy * (1.0f + buyMarkupPct)));
            int lineTotal = unitPrice * entry.quantity;
            totalGold += lineTotal;
            resultItems.push_back({entry.itemId, entry.quantity, lineTotal});
        }
    }

    // ── Phase 2: gold check ───────────────────────────────────────────────────
    int playerGold = inventoryMgr.getItemQuantity(characterId, goldItemId);
    if (playerGold < totalGold)
        return {false, "insufficient_gold", 0, {}};

    // ── Phase 3: execute (deduct gold first, then add items one by one) ───────
    if (!inventoryMgr.removeItemFromInventory(characterId, goldItemId, totalGold))
        return {false, "gold_deduct_failed", 0, {}};

    int goldRolledBack = 0;
    for (int i = 0; i < static_cast<int>(resultItems.size()); ++i)
    {
        const auto &r = resultItems[i];
        if (!inventoryMgr.addItemToInventory(characterId, r.itemId, r.quantity))
        {
            // Partial failure: rollback gold and all items already added
            inventoryMgr.addItemToInventory(characterId, goldItemId, totalGold);
            for (int j = 0; j < i; ++j)
                inventoryMgr.removeItemFromInventory(characterId, resultItems[j].itemId, resultItems[j].quantity);
            return {false, "inventory_add_failed", 0, {}};
        }
        goldRolledBack += r.totalPrice;
    }

    // ── Phase 4: decrement stocks ─────────────────────────────────────────────
    {
        std::unique_lock lock(mutex_);
        auto npcIt = vendors_.find(npcId);
        if (npcIt != vendors_.end())
        {
            for (const auto &r : resultItems)
            {
                for (auto &s : npcIt->second.items)
                {
                    if (s.itemId == r.itemId && s.stockCurrent != -1)
                    {
                        s.stockCurrent = std::max(0, s.stockCurrent - r.quantity);
                        break;
                    }
                }
            }
        }
    }

    log_->info("[VendorManager] char {} batch-bought {} items from NPC {} for {} gold total",
        characterId,
        resultItems.size(),
        npcId,
        totalGold);
    return {true, "", totalGold, std::move(resultItems)};
}

VendorManager::SellBatchResult
VendorManager::sellBatch(
    int characterId,
    int npcId,
    const std::vector<SellBatchItemEntry> &entries,
    int goldItemId,
    InventoryManager &inventoryMgr,
    float sellTaxPct)
{
    if (entries.empty())
        return {false, "empty_batch", 0, {}};
    if (static_cast<int>(entries.size()) > MAX_VENDOR_BATCH_SIZE)
        return {false, "batch_too_large", 0, {}};

    // ── Phase 1: validate vendor exists ──────────────────────────────────────
    {
        std::shared_lock lock(mutex_);
        if (vendors_.find(npcId) == vendors_.end())
            return {false, "vendor_not_found", 0, {}};
    }

    // ── Phase 2: validate all items + compute totals ──────────────────────────
    const auto inventory = inventoryMgr.getPlayerInventory(characterId);

    std::vector<SellBatchItemResult> resultItems;
    resultItems.reserve(entries.size());
    int totalGold = 0;

    for (const auto &entry : entries)
    {
        if (entry.quantity <= 0)
            return {false, "invalid_quantity", 0, {}};

        const PlayerInventoryItemStruct *invSlot = nullptr;
        for (const auto &s : inventory)
        {
            if (s.id == entry.inventoryItemId)
            {
                invSlot = &s;
                break;
            }
        }
        if (!invSlot)
            return {false, "item_not_in_inventory", 0, {}};
        if (invSlot->isEquipped)
            return {false, "item_is_equipped", 0, {}};
        if (invSlot->quantity < entry.quantity)
            return {false, "insufficient_quantity", 0, {}};

        const ItemDataStruct item = itemManager_.getItemById(invSlot->itemId);
        if (!item.isTradable)
            return {false, "item_not_tradable", 0, {}};

        int lineGold = static_cast<int>(std::floor(item.vendorPriceSell * entry.quantity * (1.0f - sellTaxPct)));
        totalGold += lineGold;
        resultItems.push_back({entry.inventoryItemId, entry.quantity, lineGold});
    }

    // ── Phase 3: execute removes (fail-fast with gold rollback) ──────────────
    int goldCredited = 0;
    for (int i = 0; i < static_cast<int>(resultItems.size()); ++i)
    {
        const auto &r = resultItems[i];
        // Re-look up the actual itemId for removal
        const PlayerInventoryItemStruct *invSlot = nullptr;
        for (const auto &s : inventory)
        {
            if (s.id == r.inventoryItemId)
            {
                invSlot = &s;
                break;
            }
        }
        if (!invSlot)
        {
            // Inventory changed under us; rollback gold already credited
            if (goldCredited > 0)
                inventoryMgr.removeItemFromInventory(characterId, goldItemId, goldCredited);
            return {false, "item_not_in_inventory", 0, {}};
        }
        if (!inventoryMgr.removeItemFromInventory(characterId, invSlot->itemId, r.quantity))
        {
            if (goldCredited > 0)
                inventoryMgr.removeItemFromInventory(characterId, goldItemId, goldCredited);
            return {false, "item_remove_failed", 0, {}};
        }
        inventoryMgr.addItemToInventory(characterId, goldItemId, r.goldReceived);
        goldCredited += r.goldReceived;
    }

    log_->info("[VendorManager] char {} batch-sold {} items to NPC {} for {} gold total",
        characterId,
        resultItems.size(),
        npcId,
        totalGold);
    return {true, "", totalGold, std::move(resultItems)};
}
