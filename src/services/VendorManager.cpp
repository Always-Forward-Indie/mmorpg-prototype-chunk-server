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

int
VendorManager::tickRestock(int64_t nowMs)
{
    std::unique_lock lock(mutex_);
    int refilled = 0;
    for (auto &[npcId, vendor] : vendors_)
    {
        for (auto &slot : vendor.items)
        {
            if (slot.restockAmount <= 0 || slot.stockMax <= 0)
                continue; // unlimited or non-restocking
            if (slot.stockCurrent >= slot.stockMax)
            {
                slot.lastRestockMs = nowMs; // full: re-arm the timer
                continue;
            }
            const int64_t intervalMs =
                static_cast<int64_t>(slot.restockIntervalSec) * 1000;
            if (intervalMs <= 0 || nowMs - slot.lastRestockMs < intervalMs)
                continue;
            slot.stockCurrent = std::min(slot.stockMax, slot.stockCurrent + slot.restockAmount);
            slot.lastRestockMs = nowMs;
            ++refilled;
            log_->debug("[VendorManager] Restock NPC {} item {} -> {}/{}",
                npcId, slot.itemId, slot.stockCurrent, slot.stockMax);
        }
    }
    return refilled;
}

// ── Shop queries ──────────────────────────────────────────────────────────────

int
VendorManager::resolveBuyPrice(int priceOverrideBuy, int baseBuyPrice, float buyMarkupPct)
{
    if (priceOverrideBuy > 0)
        return priceOverrideBuy;
    return static_cast<int>(std::ceil(baseBuyPrice * (1.0f + buyMarkupPct)));
}

int
VendorManager::resolveSellLine(int unitSell, int quantity, float sellTaxPct)
{
    if (unitSell <= 0 || quantity <= 0)
        return 0;
    const float tax = std::clamp(sellTaxPct, 0.0f, 1.0f);
    const int gold = static_cast<int>(std::floor(unitSell * quantity * (1.0f - tax)));
    // Valued loot must never sell for nothing: a single 1g bone at 5% tax
    // floors to 0 without this guarantee.
    return gold > 0 ? gold : 1;
}

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

        int priceBuy = resolveBuyPrice(slot.priceOverrideBuy, item.vendorPriceBuy, buyMarkupPct);

        int priceSell = (slot.priceOverrideSell > 0)
                            ? slot.priceOverrideSell
                            : item.vendorPriceSell;

        // S2.4 slim entry: vendor dynamics only. All statics (type, rarity,
        // flags, weight, slots, attrs, effects) resolve client-side from the
        // versioned itemCatalog. slug stays: entries must stay self-identifying
        // (bots/tools/tests match by slug; itemIds are DB-autoincrement).
        nlohmann::json entry;
        entry["itemId"] = item.id;
        entry["slug"] = item.slug;
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

    int unitPrice = resolveBuyPrice(slot->priceOverrideBuy, item.vendorPriceBuy, buyMarkupPct);
    int totalPrice = unitPrice * quantity;

    // Wallet check (110_currency_wallet — lock-free on inventory mutex).
    lock.unlock(); // Release vendor lock before touching InventoryManager
    if (inventoryMgr.getGoldAmount(characterId) < totalPrice)
        return {false, "insufficient_gold", 0};

    // Deduct gold
    if (!inventoryMgr.deductGold(characterId, totalPrice, "vendor_buy"))
        return {false, "gold_deduct_failed", 0};

    // Give item (slot cap: stacking lands, new rows beyond 48 refuse as inventory_full).
    if (!inventoryMgr.addItemToInventory(characterId, itemId, quantity))
    {
        // Rollback gold
        inventoryMgr.addGold(characterId, totalPrice, "vendor_buy_rollback");
        const bool bFull = inventoryMgr.getPlayerInventory(characterId).size() >= 48;
        return {false, bFull ? "inventory_full" : "inventory_add_failed", 0};
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
    int goldReceived = resolveSellLine(unitSell, quantity, sellTaxPct);

    if (!inventoryMgr.removeItemFromInventory(characterId, invSlot->itemId, quantity))
        return {false, "item_remove_failed", 0};

    // Wallet credit (110): sell proceeds land in character_currencies.
    if (goldReceived > 0)
        inventoryMgr.addGold(characterId, goldReceived, "vendor_sell");

    // Buyback: remember the stack at the price paid (FIFO-capped).
    // quantity > 0 is guaranteed by the guard at the top of this function.
    recordBuyback(characterId, invSlot->itemId, item.slug, quantity, goldReceived / quantity);

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
            // Aggregate duplicates: entries for the same item share one stock pile.
            int alreadyNeeded = 0;
            for (const auto &r : resultItems)
                if (r.itemId == entry.itemId)
                    alreadyNeeded += r.quantity;
            if (slot->stockCurrent != -1 && slot->stockCurrent < alreadyNeeded + entry.quantity)
                return {false, "insufficient_stock", 0, {}};

            const ItemDataStruct item = itemManager_.getItemById(entry.itemId);
            if (item.id == 0)
                return {false, "item_not_found", 0, {}};

            int unitPrice = resolveBuyPrice(slot->priceOverrideBuy, item.vendorPriceBuy, buyMarkupPct);
            int lineTotal = unitPrice * entry.quantity;
            totalGold += lineTotal;
            resultItems.push_back({entry.itemId, entry.quantity, lineTotal});
        }
    }

    // ── Phase 2: wallet check ─────────────────────────────────────────────────
    if (inventoryMgr.getGoldAmount(characterId) < totalGold)
        return {false, "insufficient_gold", 0, {}};

    // ── Phase 3: execute (deduct gold first, then add items one by one) ───────
    if (!inventoryMgr.deductGold(characterId, totalGold, "vendor_buy"))
        return {false, "gold_deduct_failed", 0, {}};

    int goldRolledBack = 0;
    for (int i = 0; i < static_cast<int>(resultItems.size()); ++i)
    {
        const auto &r = resultItems[i];
        if (!inventoryMgr.addItemToInventory(characterId, r.itemId, r.quantity))
        {
            // Partial failure: rollback gold and all items already added
            inventoryMgr.addGold(characterId, totalGold, "vendor_buy_rollback");
            for (int j = 0; j < i; ++j)
                inventoryMgr.removeItemFromInventory(characterId, resultItems[j].itemId, resultItems[j].quantity);
            const bool bFull = inventoryMgr.getPlayerInventory(characterId).size() >= 48;
            return {false, bFull ? "inventory_full" : "inventory_add_failed", 0, {}};
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
        // Aggregate duplicates: entries for the same row share one quantity.
        int alreadyNeeded = 0;
        for (const auto &r : resultItems)
            if (r.inventoryItemId == entry.inventoryItemId)
                alreadyNeeded += r.quantity;
        if (invSlot->quantity < alreadyNeeded + entry.quantity)
            return {false, "insufficient_quantity", 0, {}};

        const ItemDataStruct item = itemManager_.getItemById(invSlot->itemId);
        if (!item.isTradable)
            return {false, "item_not_tradable", 0, {}};

        int lineGold = resolveSellLine(item.vendorPriceSell, entry.quantity, sellTaxPct);
        totalGold += lineGold;
        resultItems.push_back({entry.inventoryItemId, entry.quantity, lineGold});
    }

    // ── Phase 3: execute removes (fail-fast with full rollback) ─────────────
    int goldCredited = 0;
    std::vector<std::pair<int, int>> removedItems; // (itemId, quantity) for rollback
    auto rollback = [&]
    {
        if (goldCredited > 0)
            inventoryMgr.deductGold(characterId, goldCredited, "vendor_sell_rollback");
        for (const auto &removed : removedItems)
            inventoryMgr.addItemToInventory(characterId, removed.first, removed.second);
    };
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
            // Inventory changed under us; rollback everything done so far
            rollback();
            return {false, "item_not_in_inventory", 0, {}};
        }
        if (!inventoryMgr.removeItemFromInventory(characterId, invSlot->itemId, r.quantity))
        {
            rollback();
            return {false, "item_remove_failed", 0, {}};
        }
        removedItems.emplace_back(invSlot->itemId, r.quantity);
        if (r.goldReceived > 0)
            inventoryMgr.addGold(characterId, r.goldReceived, "vendor_sell");
        goldCredited += r.goldReceived;
        // Buyback per committed line (slug re-looked-up: the row may be gone).
        if (r.quantity > 0)
        {
            const auto soldItem = itemManager_.getItemById(invSlot->itemId);
            recordBuyback(characterId, invSlot->itemId, soldItem.slug, r.quantity,
                r.goldReceived / r.quantity);
        }
    }

    log_->info("[VendorManager] char {} batch-sold {} items to NPC {} for {} gold total",
        characterId,
        resultItems.size(),
        npcId,
        totalGold);
    return {true, "", totalGold, std::move(resultItems)};
}

// ── Buyback ───────────────────────────────────────────────────────────────────

void
VendorManager::recordBuyback(int characterId, int itemId, const std::string &slug, int quantity, int unitPrice)
{
    if (characterId <= 0 || itemId <= 0 || quantity <= 0)
        return;
    std::unique_lock lock(mutex_);
    auto &list = buyback_[characterId];
    list.push_back(BuybackEntry{itemId, slug, quantity, unitPrice});
    while (list.size() > MAX_BUYBACK)
        list.pop_front();
}

std::vector<VendorManager::BuybackEntry>
VendorManager::getBuyback(int characterId) const
{
    std::shared_lock lock(mutex_);
    auto it = buyback_.find(characterId);
    if (it == buyback_.end())
        return {};
    return std::vector<BuybackEntry>(it->second.begin(), it->second.end());
}

VendorManager::BuybackResult
VendorManager::buyback(
    int characterId,
    size_t slot,
    int quantity,
    InventoryManager &inventoryMgr)
{
    if (quantity <= 0)
        return {false, "invalid_quantity", 0, 0, 0};

    BuybackEntry entry;
    {
        std::shared_lock lock(mutex_);
        auto it = buyback_.find(characterId);
        if (it == buyback_.end() || slot >= it->second.size())
            return {false, "invalid_buyback_slot", 0, 0, 0};
        entry = it->second[slot];
    }
    if (quantity > entry.quantity)
        return {false, "invalid_quantity", 0, 0, 0};

    const int price = entry.unitPrice * quantity;
    if (inventoryMgr.getGoldAmount(characterId) < price)
        return {false, "insufficient_gold", 0, 0, 0};

    if (!inventoryMgr.deductGold(characterId, price, "vendor_buyback"))
        return {false, "gold_remove_failed", 0, 0, 0};
    if (!inventoryMgr.addItemToInventory(characterId, entry.itemId, quantity))
    {
        // Refund on the (unlikely) restore failure — never eat the gold.
        inventoryMgr.addGold(characterId, price, "vendor_buyback_refund");
        return {false, "item_restore_failed", 0, 0, 0};
    }

    {
        std::unique_lock lock(mutex_);
        auto it = buyback_.find(characterId);
        if (it == buyback_.end() || slot >= it->second.size())
        {
            // List changed under us; the item is already restored — drop the
            // whole list rather than corrupt slot accounting.
            buyback_.erase(characterId);
        }
        else if (static_cast<int>(quantity) >= it->second[slot].quantity)
        {
            it->second.erase(it->second.begin() + static_cast<ptrdiff_t>(slot));
        }
        else
        {
            it->second[slot].quantity -= quantity;
        }
        if (it != buyback_.end() && it->second.empty())
            buyback_.erase(it);
    }

    log_->info("[VendorManager] char {} bought back {}x item {} for {} gold",
        characterId, quantity, entry.itemId, price);
    return {true, "", entry.itemId, quantity, price};
}
