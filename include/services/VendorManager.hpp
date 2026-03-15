#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

class ItemManager;

/**
 * @brief In-memory vendor NPC store.
 *
 * Holds the inventory of every vendor/blacksmith NPC.  Data is populated
 * once at chunk startup via setVendorData() and updated by the game-server
 * restock scheduler via updateStockCount().  All public methods are thread-safe.
 */
class VendorManager
{
  public:
    VendorManager(ItemManager &itemManager, Logger &logger);

    // ── Data loading ─────────────────────────────────────────────────────────

    /** Replace the full vendor dataset (called once on chunk startup). */
    void setVendorData(const std::vector<VendorNPCDataStruct> &vendors);

    /** Apply a single stock-count update from the restock scheduler. */
    void updateStockCount(int npcId, int itemId, int newStock);

    // ── Shop queries ─────────────────────────────────────────────────────────

    /**
     * @brief Build the vendor shop JSON for the client.
     * @param npcId  Vendor NPC id
     * @param buyMarkupPct  Global markup applied to vendorPriceBuy (0 = no markup)
     * @return nullptr if vendor not found
     */
    nlohmann::json buildShopJson(int npcId, float buyMarkupPct) const;

    // ── Transactions ─────────────────────────────────────────────────────────

    /**
     * @brief Attempt to buy one or more items from a vendor.
     *
     * Validates: vendor exists, item in vendor stock, sufficient stock,
     * player has enough gold.  If all pass, decrements stock and removes
     * gold from player's inventory via addItemToInventory / removeItemFromInventory.
     *
     * @return {success, reason_string, totalPrice}
     */
    struct BuyResult
    {
        bool success = false;
        std::string reason;
        int totalPrice = 0;
    };
    BuyResult buyItem(
        int characterId,
        int npcId,
        int itemId,
        int quantity,
        int goldItemId,
        class InventoryManager &inventoryMgr,
        float buyMarkupPct);

    /**
     * @brief Attempt to sell an item to the vendor.
     *
     * Validates: item is tradable, not equipped, player has it.
     * If all pass, removes item and adds gold.
     *
     * @param inventoryItemId  player_inventory.id of the slot to sell
     * @return {success, reason_string, goldReceived}
     */
    struct SellResult
    {
        bool success = false;
        std::string reason;
        int goldReceived = 0;
    };
    SellResult sellItem(
        int characterId,
        int npcId,
        int inventoryItemId,
        int quantity,
        int goldItemId,
        class InventoryManager &inventoryMgr,
        float sellTaxPct);

    // ── Batch transactions ────────────────────────────────────────────────────

    /** Maximum number of items allowed in a single batch buy/sell request. */
    static constexpr int MAX_VENDOR_BATCH_SIZE = 20;

    /** Per-item result entry for a batch buy. */
    struct BuyBatchItemResult
    {
        int itemId = 0;
        int quantity = 0;
        int totalPrice = 0;
    };

    /**
     * @brief Atomically buy multiple items from a vendor in one request.
     *
     * All items are validated first (stock, gold); if any check fails the
     * whole batch is rejected.  Stock and gold are only deducted once every
     * validation check passes, so the operation is all-or-nothing.
     *
     * @return success=false + reason on first validation error;
     *         success=true + items/totalGoldSpent on full success.
     */
    struct BuyBatchResult
    {
        bool success = false;
        std::string reason;
        int totalGoldSpent = 0;
        std::vector<BuyBatchItemResult> items;
    };
    BuyBatchResult buyBatch(
        int characterId,
        int npcId,
        const std::vector<BuyBatchItemEntry> &entries,
        int goldItemId,
        class InventoryManager &inventoryMgr,
        float buyMarkupPct);

    /** Per-item result entry for a batch sell. */
    struct SellBatchItemResult
    {
        int inventoryItemId = 0;
        int quantity = 0;
        int goldReceived = 0;
    };

    /**
     * @brief Atomically sell multiple items to a vendor in one request.
     *
     * All items are validated first; the whole batch is rejected if any
     * item fails validation.  Gold is only credited once all checks pass.
     *
     * @return success=false + reason on first validation error;
     *         success=true + items/totalGoldReceived on full success.
     */
    struct SellBatchResult
    {
        bool success = false;
        std::string reason;
        int totalGoldReceived = 0;
        std::vector<SellBatchItemResult> items;
    };
    SellBatchResult sellBatch(
        int characterId,
        int npcId,
        const std::vector<SellBatchItemEntry> &entries,
        int goldItemId,
        class InventoryManager &inventoryMgr,
        float sellTaxPct);

  private:
    ItemManager &itemManager_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<int, VendorNPCDataStruct> vendors_; ///< npcId → data
};
