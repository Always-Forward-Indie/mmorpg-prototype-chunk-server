#pragma once

#include "data/DataStructs.hpp"
#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for all vendor, blacksmith (repair), and P2P trade events.
 *
 * Handles:
 *  Game-server → chunk-server (static data load):
 *    SET_VENDOR_DATA, VENDOR_STOCK_UPDATE
 *  Client → chunk-server (NPC vendor):
 *    OPEN_VENDOR_SHOP, BUY_ITEM, SELL_ITEM
 *  Client → chunk-server (blacksmith repair):
 *    OPEN_REPAIR_SHOP, REPAIR_ITEM, REPAIR_ALL
 *  Client → chunk-server (P2P trade):
 *    TRADE_REQUEST, TRADE_ACCEPT, TRADE_DECLINE,
 *    TRADE_OFFER_UPDATE, TRADE_CONFIRM, TRADE_CANCEL
 */
class VendorEventHandler : public BaseEventHandler
{
  public:
    VendorEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    // ── Static data events ───────────────────────────────────────────────────

    void handleSetVendorDataEvent(const Event &event);
    void handleVendorStockUpdateEvent(const Event &event);

    // ── NPC vendor events ────────────────────────────────────────────────────

    void handleOpenVendorShopEvent(const Event &event);
    void handleBuyItemEvent(const Event &event);
    void handleSellItemEvent(const Event &event);
    void handleBuyItemBatchEvent(const Event &event);
    void handleSellItemBatchEvent(const Event &event);

    // ── Repair shop events ───────────────────────────────────────────────────

    void handleOpenRepairShopEvent(const Event &event);
    void handleRepairItemEvent(const Event &event);
    void handleRepairAllEvent(const Event &event);

    // ── P2P trade events ─────────────────────────────────────────────────────

    void handleTradeRequestEvent(const Event &event);
    void handleTradeAcceptEvent(const Event &event);
    void handleTradeDeclineEvent(const Event &event);
    void handleTradeOfferUpdateEvent(const Event &event);
    void handleTradeConfirmEvent(const Event &event);
    void handleTradeCancelEvent(const Event &event);

  private:
    // ── Helpers ──────────────────────────────────────────────────────────────

    /** Check Euclidean distance between player and NPC. */
    bool isPlayerInRange(const PositionStruct &player, const PositionStruct &npc, float radius) const;

    /** Compute per-item repair cost (gold) based on missing durability. */
    int computeRepairCost(const ItemDataStruct &item, int durabilityCurrent) const;

    /**
     * @brief Return the buy-price discount fraction for a character based on their
     *        reputation with the NPC's faction.
     *
     * Returns 0.0 if the NPC has no faction, the character data is unavailable,
     * or the reputation is below the configured threshold.
     * Configured via game_config keys:
     *   - "reputation.vendor_discount_threshold" (default 200)
     *   - "reputation.vendor_discount_pct"       (default 0.05, i.e. 5%)
     */
    float getReputationDiscountPct(int characterId, const std::string &factionSlug) const;

    /**
     * @brief Execute repair for one inventory slot.
     * @return {success, goldCost}
     */
    struct RepairOneResult
    {
        bool success;
        int goldCost;
        std::string reason;
    };
    RepairOneResult repairOne(
        int characterId, int goldItemId, int npcId, PlayerInventoryItemStruct &invSlot);

    /** Build the list of repairable equipped items for the repair shop window.
     *  Sets outTotalCost to the sum of all repair costs. */
    nlohmann::json buildRepairableItemsJson(int characterId, int &outTotalCost) const;

    /** Send TRADE_STATE packet to one participant. */
    void sendTradeState(
        int clientId,
        const TradeSessionStruct &session,
        bool isSideA) const;

    /** Send TRADE_CANCELLED to both participants and close the session. */
    void cancelAndNotify(
        const std::string &sessionId,
        int clientAId,
        int clientBId,
        const std::string &reason);

    /** Persist a durability change for one item back to game-server. */
    void saveDurabilityChange(int characterId, int inventoryItemId, int newDurability);

    /** Persist a currency transaction back to game-server. */
    void saveCurrencyTransaction(
        int characterId, int npcId, int itemId, int quantity, int totalPrice, const std::string &transactionType);
};
