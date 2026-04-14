#include "events/handlers/VendorEventHandler.hpp"
#include "events/EventData.hpp"
#include "services/GameServices.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/TradeSessionManager.hpp"
#include "services/VendorManager.hpp"
#include "utils/ResponseBuilder.hpp"
#include <algorithm>
#include <cmath>
#include <spdlog/logger.h>

VendorEventHandler::VendorEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "vendor")
{
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

bool
VendorEventHandler::isPlayerInRange(
    const PositionStruct &player,
    const PositionStruct &npc,
    float radius) const
{
    float dx = player.positionX - npc.positionX;
    float dy = player.positionY - npc.positionY;
    float dz = player.positionZ - npc.positionZ;
    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

int
VendorEventHandler::computeRepairCost(const ItemDataStruct &item, int durabilityCurrent) const
{
    if (!item.isDurable || item.durabilityMax <= 0)
        return 0;
    int missing = item.durabilityMax - durabilityCurrent;
    if (missing <= 0)
        return 0;
    // cost = vendorPriceBuy * (missing / durabilityMax)  (rounded up)
    return static_cast<int>(std::ceil(
        static_cast<float>(item.vendorPriceBuy) * missing / item.durabilityMax));
}

float
VendorEventHandler::getReputationDiscountPct(int characterId, const std::string &factionSlug) const
{
    if (factionSlug.empty() || characterId <= 0)
        return 0.0f;
    const int threshold = gameServices_.getGameConfigService().getInt("reputation.vendor_discount_threshold", 200);
    const int rep = gameServices_.getReputationManager().getReputation(characterId, factionSlug);
    if (rep < threshold)
        return 0.0f;
    return gameServices_.getGameConfigService().getFloat("reputation.vendor_discount_pct", 0.05f);
}

void
VendorEventHandler::saveDurabilityChange(int characterId, int inventoryItemId, int newDurability)
{
    nlohmann::json pkt;
    pkt["header"]["eventType"] = "saveDurabilityChange";
    pkt["header"]["clientId"] = 0;
    pkt["header"]["hash"] = "";
    pkt["body"]["characterId"] = characterId;
    pkt["body"]["inventoryItemId"] = inventoryItemId;
    pkt["body"]["durabilityCurrent"] = newDurability;
    gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
}

void
VendorEventHandler::saveCurrencyTransaction(
    int characterId, int npcId, int itemId, int quantity, int totalPrice, const std::string &transactionType)
{
    nlohmann::json pkt;
    pkt["header"]["eventType"] = "saveCurrencyTransaction";
    pkt["header"]["clientId"] = 0;
    pkt["header"]["hash"] = "";
    pkt["body"]["characterId"] = characterId;
    pkt["body"]["npcId"] = npcId;
    pkt["body"]["itemId"] = itemId;
    pkt["body"]["quantity"] = quantity;
    pkt["body"]["totalPrice"] = totalPrice;
    pkt["body"]["transactionType"] = transactionType;
    gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
}

void
VendorEventHandler::sendTradeState(
    int clientId,
    const TradeSessionStruct &session,
    bool isSideA) const
{
    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket)
        return;

    auto &invMgr = gameServices_.getInventoryManager();
    auto &itemMgr = gameServices_.getItemManager();

    auto itemsToJson = [&](const std::vector<TradeOfferItemStruct> &offer, int charId)
    {
        nlohmann::json arr = nlohmann::json::array();
        const auto inv = invMgr.getPlayerInventory(charId);
        for (const auto &o : offer)
        {
            auto it = std::find_if(inv.begin(), inv.end(), [&](const PlayerInventoryItemStruct &s)
                { return s.id == o.inventoryItemId; });
            if (it != inv.end())
            {
                nlohmann::json entry = invMgr.inventoryItemToJson(*it);
                // trade quantity may be a partial split; override slot quantity
                entry["quantity"] = o.quantity;
                arr.push_back(entry);
            }
            else
            {
                // fallback: minimal data if inventory slot not found in memory
                const ItemDataStruct item = itemMgr.getItemById(o.itemId);
                nlohmann::json entry;
                entry["inventoryItemId"] = o.inventoryItemId;
                entry["itemId"] = o.itemId;
                entry["quantity"] = o.quantity;
                entry["slug"] = item.slug;
                arr.push_back(entry);
            }
        }
        return arr;
    };

    const int myCharId = isSideA ? session.charAId : session.charBId;
    const int theirCharId = isSideA ? session.charBId : session.charAId;

    nlohmann::json body;
    body["sessionId"] = session.sessionId;
    body["myGold"] = isSideA ? session.goldA : session.goldB;
    body["theirGold"] = isSideA ? session.goldB : session.goldA;
    body["myGoldBalance"] = invMgr.getGoldAmount(myCharId);
    body["myItems"] = itemsToJson(isSideA ? session.offerA : session.offerB, myCharId);
    body["theirItems"] = itemsToJson(isSideA ? session.offerB : session.offerA, theirCharId);
    body["myConfirmed"] = isSideA ? session.confirmedA : session.confirmedB;
    body["theirConfirmed"] = isSideA ? session.confirmedB : session.confirmedA;

    ResponseBuilder builder;
    std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "tradeState").setHeader("status", "success").setHeader("clientId", clientId).setHeader("hash", "").setBody("trade", body).build());
    networkManager_.sendResponse(socket, msg);
}

void
VendorEventHandler::cancelAndNotify(
    const std::string &sessionId,
    int clientAId,
    int clientBId,
    const std::string &reason)
{
    for (int cid : {clientAId, clientBId})
    {
        auto socket = gameServices_.getClientManager().getClientSocket(cid);
        if (!socket)
            continue;
        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("cancelled", builder.setHeader("eventType", "tradeCancelled").setHeader("status", "cancelled").setHeader("clientId", cid).setHeader("hash", "").setBody("sessionId", sessionId).setBody("reason", reason).build());
        networkManager_.sendResponse(socket, msg);
    }
    gameServices_.getTradeSessionManager().closeSession(sessionId);
}

// ─── Static data events ───────────────────────────────────────────────────────

void
VendorEventHandler::handleSetVendorDataEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("[VendorEventHandler] SET_VENDOR_DATA: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        if (!body.contains("vendors") || !body["vendors"].is_array())
        {
            log_->error("[VendorEventHandler] SET_VENDOR_DATA: missing 'vendors' array");
            return;
        }

        std::vector<VendorNPCDataStruct> vendors;
        for (const auto &vj : body["vendors"])
        {
            VendorNPCDataStruct vendor;
            vendor.npcId = vj.value("npcId", 0);
            if (vj.contains("items") && vj["items"].is_array())
            {
                for (const auto &ij : vj["items"])
                {
                    VendorInventoryItemStruct item;
                    item.itemId = ij.value("itemId", 0);
                    item.stockCurrent = ij.value("stockCurrent", -1);
                    item.stockMax = ij.value("stockMax", -1);
                    item.restockAmount = ij.value("restockAmount", 0);
                    item.restockIntervalSec = ij.value("restockIntervalSec", 3600);
                    item.priceOverrideBuy = ij.value("priceOverrideBuy", 0);
                    item.priceOverrideSell = ij.value("priceOverrideSell", 0);
                    vendor.items.push_back(std::move(item));
                }
            }
            vendors.push_back(std::move(vendor));
        }

        gameServices_.getVendorManager().setVendorData(vendors);
        log_->info("[VendorEventHandler] Loaded vendor data for {} NPCs", vendors.size());
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleSetVendorDataEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleVendorStockUpdateEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<VendorStockUpdateStruct>(data))
        {
            log_->error("[VendorEventHandler] VENDOR_STOCK_UPDATE: unexpected data type");
            return;
        }
        const auto &upd = std::get<VendorStockUpdateStruct>(data);
        gameServices_.getVendorManager().updateStockCount(upd.npcId, upd.itemId, upd.newStock);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleVendorStockUpdateEvent: {}", ex.what());
    }
}

// ─── NPC vendor events ────────────────────────────────────────────────────────

void
VendorEventHandler::handleOpenVendorShopEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<OpenVendorShopRequestStruct>(data))
            return;

        const auto &req = std::get<OpenVendorShopRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "npc_not_found", "vendorShop", req.clientId, req.timestamps);
            return;
        }
        if (!isPlayerInRange(req.playerPosition, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "vendorShop", req.clientId, req.timestamps);
            return;
        }

        float markup = gameServices_.getGameConfigService().getFloat("economy.vendor_buy_markup_pct", 0.0f);
        markup -= getReputationDiscountPct(req.characterId, npc.factionSlug);
        nlohmann::json shopJson = gameServices_.getVendorManager().buildShopJson(req.npcId, markup);
        if (shopJson.is_null())
        {
            sendErrorResponseWithTimestamps(socket, "vendor_no_inventory", "vendorShop", req.clientId, req.timestamps);
            return;
        }

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "vendorShop").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("goldBalance", gameServices_.getInventoryManager().getGoldAmount(req.characterId)).setBody("items", shopJson).build());
        networkManager_.sendResponse(socket, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleOpenVendorShopEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleBuyItemEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<BuyItemRequestStruct>(data))
            return;

        const auto &req = std::get<BuyItemRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "vendor_not_found", "buyItem", req.clientId, req.timestamps);
            return;
        }
        if (!isPlayerInRange(req.playerPosition, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "buyItem", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            log_->error("[VendorEventHandler] gold_coin item not found");
            sendErrorResponseWithTimestamps(socket, "server_error", "buyItem", req.clientId, req.timestamps);
            return;
        }

        float markup = gameServices_.getGameConfigService().getFloat("economy.vendor_buy_markup_pct", 0.0f);
        markup -= getReputationDiscountPct(req.characterId, npc.factionSlug);
        auto result = gameServices_.getVendorManager().buyItem(
            req.characterId, req.npcId, req.itemId, req.quantity, goldItem->id, gameServices_.getInventoryManager(), markup);

        if (!result.success)
        {
            sendErrorResponseWithTimestamps(socket, result.reason, "buyItem", req.clientId, req.timestamps);
            return;
        }

        saveCurrencyTransaction(req.characterId, req.npcId, req.itemId, req.quantity, result.totalPrice, "buy");

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "buyItemResult").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("itemId", req.itemId).setBody("quantity", req.quantity).setBody("totalPrice", result.totalPrice).build());
        networkManager_.sendResponse(socket, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleBuyItemEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleSellItemEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<SellItemRequestStruct>(data))
            return;

        const auto &req = std::get<SellItemRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "vendor_not_found", "sellItem", req.clientId, req.timestamps);
            return;
        }
        if (!isPlayerInRange(req.playerPosition, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "sellItem", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            sendErrorResponseWithTimestamps(socket, "server_error", "sellItem", req.clientId, req.timestamps);
            return;
        }

        float tax = gameServices_.getGameConfigService().getFloat("economy.vendor_sell_tax_pct", 0.0f);
        tax = std::max(0.0f, tax - getReputationDiscountPct(req.characterId, npc.factionSlug));
        auto result = gameServices_.getVendorManager().sellItem(
            req.characterId, req.npcId, req.inventoryItemId, req.quantity, goldItem->id, gameServices_.getInventoryManager(), tax);

        if (!result.success)
        {
            sendErrorResponseWithTimestamps(socket, result.reason, "sellItem", req.clientId, req.timestamps);
            return;
        }

        // Find item id for the transaction log (from the inventory slot we just sold)
        // goldReceived is already logged inside VendorManager; persist here
        // Note: itemId not available after sell, use inventoryItemId as placeholder
        saveCurrencyTransaction(req.characterId, req.npcId, 0, req.quantity, result.goldReceived, "sell");

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "sellItemResult").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("goldReceived", result.goldReceived).build());
        networkManager_.sendResponse(socket, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleSellItemEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleBuyItemBatchEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<BuyBatchRequestStruct>(data))
            return;

        const auto &req = std::get<BuyBatchRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "vendor_not_found", "buyItemBatch", req.clientId, req.timestamps);
            return;
        }
        if (!isPlayerInRange(req.playerPosition, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "buyItemBatch", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            log_->error("[VendorEventHandler] gold_coin item not found");
            sendErrorResponseWithTimestamps(socket, "server_error", "buyItemBatch", req.clientId, req.timestamps);
            return;
        }

        float markup = gameServices_.getGameConfigService().getFloat("economy.vendor_buy_markup_pct", 0.0f);
        markup -= getReputationDiscountPct(req.characterId, npc.factionSlug);
        auto result = gameServices_.getVendorManager().buyBatch(
            req.characterId, req.npcId, req.items, goldItem->id, gameServices_.getInventoryManager(), markup);

        if (!result.success)
        {
            sendErrorResponseWithTimestamps(socket, result.reason, "buyItemBatch", req.clientId, req.timestamps);
            return;
        }

        for (const auto &r : result.items)
            saveCurrencyTransaction(req.characterId, req.npcId, r.itemId, r.quantity, r.totalPrice, "buy");

        nlohmann::json itemsJson = nlohmann::json::array();
        for (const auto &r : result.items)
            itemsJson.push_back({{"itemId", r.itemId}, {"quantity", r.quantity}, {"totalPrice", r.totalPrice}});

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "buyItemBatchResult").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("totalGoldSpent", result.totalGoldSpent).setBody("items", itemsJson).build());
        networkManager_.sendResponse(socket, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleBuyItemBatchEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleSellItemBatchEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<SellBatchRequestStruct>(data))
            return;

        const auto &req = std::get<SellBatchRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "vendor_not_found", "sellItemBatch", req.clientId, req.timestamps);
            return;
        }
        if (!isPlayerInRange(req.playerPosition, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "sellItemBatch", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            sendErrorResponseWithTimestamps(socket, "server_error", "sellItemBatch", req.clientId, req.timestamps);
            return;
        }

        float tax = gameServices_.getGameConfigService().getFloat("economy.vendor_sell_tax_pct", 0.0f);
        tax = std::max(0.0f, tax - getReputationDiscountPct(req.characterId, npc.factionSlug));
        auto result = gameServices_.getVendorManager().sellBatch(
            req.characterId, req.npcId, req.items, goldItem->id, gameServices_.getInventoryManager(), tax);

        if (!result.success)
        {
            sendErrorResponseWithTimestamps(socket, result.reason, "sellItemBatch", req.clientId, req.timestamps);
            return;
        }

        for (const auto &r : result.items)
            saveCurrencyTransaction(req.characterId, req.npcId, 0, r.quantity, r.goldReceived, "sell");

        nlohmann::json itemsJson = nlohmann::json::array();
        for (const auto &r : result.items)
            itemsJson.push_back({{"inventoryItemId", r.inventoryItemId}, {"quantity", r.quantity}, {"goldReceived", r.goldReceived}});

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "sellItemBatchResult").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("totalGoldReceived", result.totalGoldReceived).setBody("items", itemsJson).build());
        networkManager_.sendResponse(socket, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleSellItemBatchEvent: {}", ex.what());
    }
}

// ─── Repair shop events ───────────────────────────────────────────────────────

VendorEventHandler::RepairOneResult
VendorEventHandler::repairOne(
    int characterId, int goldItemId, int npcId, PlayerInventoryItemStruct &invSlot)
{
    auto &inventoryMgr = gameServices_.getInventoryManager();
    auto &itemMgr = gameServices_.getItemManager();

    const ItemDataStruct item = itemMgr.getItemById(invSlot.itemId);
    if (!item.isDurable || item.durabilityMax <= 0)
        return {false, 0, "not_durable"};

    int current = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : item.durabilityMax;
    if (current >= item.durabilityMax)
        return {false, 0, "already_full"};

    int cost = computeRepairCost(item, current);
    int playerGold = inventoryMgr.getItemQuantity(characterId, goldItemId);
    if (playerGold < cost)
        return {false, 0, "insufficient_gold"};

    // Update durability in-memory FIRST so the INVENTORY_UPDATE snapshot
    // fired by removeItemFromInventory already contains the correct durability.
    inventoryMgr.updateDurability(characterId, invSlot.id, item.durabilityMax);

    if (!inventoryMgr.removeItemFromInventory(characterId, goldItemId, cost))
    {
        // Roll back durability on gold-removal failure
        inventoryMgr.updateDurability(characterId, invSlot.id, current);
        return {false, 0, "gold_deduct_failed"};
    }

    // Persist
    saveDurabilityChange(characterId, invSlot.id, item.durabilityMax);
    saveCurrencyTransaction(characterId, npcId, invSlot.itemId, 1, cost, "repair");

    return {true, cost, ""};
}

nlohmann::json
VendorEventHandler::buildRepairableItemsJson(int characterId, int &outTotalCost) const
{
    auto &inventoryMgr = gameServices_.getInventoryManager();
    auto &itemMgr = gameServices_.getItemManager();
    // Use full inventory — repair shop shows ALL durable items, not just equipped ones
    const auto inventory = inventoryMgr.getPlayerInventory(characterId);

    nlohmann::json items = nlohmann::json::array();
    outTotalCost = 0;
    for (const auto &invSlot : inventory)
    {
        const ItemDataStruct item = itemMgr.getItemById(invSlot.itemId);
        if (!item.isDurable || item.durabilityMax <= 0)
            continue;
        int current = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : item.durabilityMax;
        if (current >= item.durabilityMax)
            continue;
        int cost = computeRepairCost(item, current);
        outTotalCost += cost;
        nlohmann::json entry;
        entry["inventoryItemId"] = invSlot.id;
        entry["itemId"] = item.id;
        entry["itemName"] = item.slug; // field name matches openRepairShop dialogue path
        entry["durabilityMax"] = item.durabilityMax;
        entry["durabilityCurrent"] = current;
        entry["repairCost"] = cost;
        items.push_back(std::move(entry));
    }
    return items;
}

void
VendorEventHandler::handleOpenRepairShopEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<OpenRepairShopRequestStruct>(data))
            return;

        const auto &req = std::get<OpenRepairShopRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "npc_not_found", "repairShop", req.clientId, req.timestamps);
            return;
        }
        const auto playerPos = gameServices_.getCharacterManager().getCharacterPosition(req.characterId);
        if (!isPlayerInRange(playerPos, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "repairShop", req.clientId, req.timestamps);
            return;
        }

        int totalRepairCost = 0;
        nlohmann::json repairableItems = buildRepairableItemsJson(req.characterId, totalRepairCost);

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "repairShop").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("goldBalance", gameServices_.getInventoryManager().getGoldAmount(req.characterId)).setBody("items", repairableItems).setBody("totalRepairCost", totalRepairCost).build());
        networkManager_.sendResponse(socket, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleOpenRepairShopEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleRepairItemEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<RepairItemRequestStruct>(data))
            return;

        const auto &req = std::get<RepairItemRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "npc_not_found", "repairItem", req.clientId, req.timestamps);
            return;
        }
        const auto playerPos = gameServices_.getCharacterManager().getCharacterPosition(req.characterId);
        if (!isPlayerInRange(playerPos, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "repairItem", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            sendErrorResponseWithTimestamps(socket, "server_error", "repairItem", req.clientId, req.timestamps);
            return;
        }

        // Find the inventory slot
        auto inventory = gameServices_.getInventoryManager().getPlayerInventory(req.characterId);
        PlayerInventoryItemStruct *invSlot = nullptr;
        for (auto &s : inventory)
            if (s.id == req.inventoryItemId)
            {
                invSlot = &s;
                break;
            }

        if (!invSlot)
        {
            sendErrorResponseWithTimestamps(socket, "item_not_found", "repairItem", req.clientId, req.timestamps);
            return;
        }

        auto result = repairOne(req.characterId, goldItem->id, req.npcId, *invSlot);
        if (!result.success)
        {
            sendErrorResponseWithTimestamps(socket, result.reason, "repairItem", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct item = gameServices_.getItemManager().getItemById(invSlot->itemId);
        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "repairItemResult").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("inventoryItemId", req.inventoryItemId).setBody("durabilityCurrent", item.durabilityMax).setBody("goldSpent", result.goldCost).build());
        networkManager_.sendResponse(socket, msg);

        // Send refreshed repair shop list so the client window updates
        int updatedTotalCost = 0;
        nlohmann::json updatedItems = buildRepairableItemsJson(req.characterId, updatedTotalCost);
        ResponseBuilder shopBuilder;
        std::string shopMsg = networkManager_.generateResponseMessage("success", shopBuilder.setHeader("eventType", "repairShop").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("goldBalance", gameServices_.getInventoryManager().getGoldAmount(req.characterId)).setBody("items", updatedItems).setBody("totalRepairCost", updatedTotalCost).build());
        networkManager_.sendResponse(socket, shopMsg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleRepairItemEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleRepairAllEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<RepairAllRequestStruct>(data))
            return;

        const auto &req = std::get<RepairAllRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "npc_not_found", "repairAll", req.clientId, req.timestamps);
            return;
        }
        const auto playerPos = gameServices_.getCharacterManager().getCharacterPosition(req.characterId);
        if (!isPlayerInRange(playerPos, npc.position, npc.radius + 2.0f))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "repairAll", req.clientId, req.timestamps);
            return;
        }

        const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            sendErrorResponseWithTimestamps(socket, "server_error", "repairAll", req.clientId, req.timestamps);
            return;
        }

        auto inventory = gameServices_.getInventoryManager().getPlayerInventory(req.characterId);
        int totalGoldSpent = 0;
        nlohmann::json repairedItems = nlohmann::json::array();

        for (auto &invSlot : inventory)
        {
            const ItemDataStruct item = gameServices_.getItemManager().getItemById(invSlot.itemId);
            if (!item.isDurable || item.durabilityMax <= 0)
                continue;
            int current = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : item.durabilityMax;
            if (current >= item.durabilityMax)
                continue;

            auto result = repairOne(req.characterId, goldItem->id, req.npcId, invSlot);
            if (!result.success)
                break; // probably out of gold — stop repairing
            totalGoldSpent += result.goldCost;
            nlohmann::json entry;
            entry["inventoryItemId"] = invSlot.id;
            entry["durabilityCurrent"] = item.durabilityMax;
            repairedItems.push_back(entry);
        }

        ResponseBuilder builder;
        std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "repairAllResult").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("repairedItems", repairedItems).setBody("totalGoldSpent", totalGoldSpent).build());
        networkManager_.sendResponse(socket, msg);

        // Send refreshed repair shop list (should be empty after all repairs)
        int updatedTotalCost = 0;
        nlohmann::json updatedItems = buildRepairableItemsJson(req.characterId, updatedTotalCost);
        ResponseBuilder shopBuilder;
        std::string shopMsg = networkManager_.generateResponseMessage("success", shopBuilder.setHeader("eventType", "repairShop").setHeader("status", "success").setHeader("clientId", req.clientId).setHeader("hash", "").setBody("npcId", req.npcId).setBody("npcSlug", npc.slug).setBody("goldBalance", gameServices_.getInventoryManager().getGoldAmount(req.characterId)).setBody("items", updatedItems).setBody("totalRepairCost", updatedTotalCost).build());
        networkManager_.sendResponse(socket, shopMsg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleRepairAllEvent: {}", ex.what());
    }
}

// ─── P2P Trade events ─────────────────────────────────────────────────────────

void
VendorEventHandler::handleTradeRequestEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<TradeRequestStruct>(data))
            return;

        const auto &req = std::get<TradeRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        if (!isPlayerAlive(req.characterId))
        {
            sendErrorResponseWithTimestamps(socket, "cannot_trade_while_dead", "tradeRequest", req.clientId, req.timestamps);
            return;
        }

        // Validate initiator has no active session
        if (gameServices_.getTradeSessionManager().getSessionByCharacter(req.characterId))
        {
            sendErrorResponseWithTimestamps(socket, "already_in_trade", "tradeRequest", req.clientId, req.timestamps);
            return;
        }

        const auto &targetChar = gameServices_.getCharacterManager().getCharacterData(req.targetCharacterId);
        if (targetChar.characterId == 0)
        {
            sendErrorResponseWithTimestamps(socket, "target_not_found", "tradeRequest", req.clientId, req.timestamps);
            return;
        }

        // Range check
        float tradeRange = gameServices_.getGameConfigService().getFloat("economy.trade_range", 5.0f);
        const auto &myPos = req.playerPosition;
        const auto &theirPos = targetChar.characterPosition;
        if (!isPlayerInRange(myPos, theirPos, tradeRange))
        {
            sendErrorResponseWithTimestamps(socket, "out_of_range", "tradeRequest", req.clientId, req.timestamps);
            return;
        }

        // Target busy?
        if (gameServices_.getTradeSessionManager().getSessionByCharacter(req.targetCharacterId))
        {
            sendErrorResponseWithTimestamps(socket, "target_busy", "tradeRequest", req.clientId, req.timestamps);
            return;
        }

        // Send invite to target character
        auto targetSocket = gameServices_.getClientManager().getClientSocket(targetChar.clientId);
        if (!targetSocket)
        {
            sendErrorResponseWithTimestamps(socket, "target_offline", "tradeRequest", req.clientId, req.timestamps);
            return;
        }

        const auto &myChar = gameServices_.getCharacterManager().getCharacterData(req.characterId);
        ResponseBuilder inviteBuilder;
        std::string inviteMsg = networkManager_.generateResponseMessage("pending", inviteBuilder.setHeader("eventType", "tradeInvite").setHeader("status", "pending").setHeader("clientId", targetChar.clientId).setHeader("hash", "").setBody("fromCharacterId", req.characterId).setBody("fromCharacterName", myChar.characterName).build());
        networkManager_.sendResponse(targetSocket, inviteMsg);

        // Ack to initiator
        sendSuccessResponseWithTimestamps(socket, "invite_sent", "tradeRequest", req.clientId, req.timestamps, "targetCharacterId", req.targetCharacterId);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleTradeRequestEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleTradeAcceptEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<TradeRespondStruct>(data))
            return;

        const auto &req = std::get<TradeRespondStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        // For accept we receive the initiator's characterId in sessionId field
        // Actually, the client should send fromCharacterId; re-read plan:
        // The accepter replies with fromCharacterId to the server
        // We store the initiator char ID in the sessionId field of TradeRespondStruct
        int initiatorCharId = 0;
        try
        {
            initiatorCharId = std::stoi(req.sessionId);
        }
        catch (...)
        {
        }

        if (initiatorCharId == 0)
        {
            sendErrorResponseWithTimestamps(socket, "invalid_session", "tradeAccept", req.clientId, req.timestamps);
            return;
        }

        const auto &initiatorChar = gameServices_.getCharacterManager().getCharacterData(initiatorCharId);
        if (initiatorChar.characterId == 0)
        {
            sendErrorResponseWithTimestamps(socket, "initiator_not_found", "tradeAccept", req.clientId, req.timestamps);
            return;
        }

        // Create session: initiator=A, accepter=B
        auto &session = gameServices_.getTradeSessionManager().createSession(
            initiatorChar.clientId, initiatorCharId, req.clientId, req.characterId);

        // Notify both
        sendTradeState(session.clientAId, session, true);
        sendTradeState(session.clientBId, session, false);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleTradeAcceptEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleTradeDeclineEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<TradeRespondStruct>(data))
            return;

        const auto &req = std::get<TradeRespondStruct>(data);

        int initiatorCharId = 0;
        try
        {
            initiatorCharId = std::stoi(req.sessionId);
        }
        catch (...)
        {
        }

        if (initiatorCharId > 0)
        {
            const auto &initiatorChar = gameServices_.getCharacterManager().getCharacterData(initiatorCharId);
            auto initiatorSocket = gameServices_.getClientManager().getClientSocket(initiatorChar.clientId);
            if (initiatorSocket)
            {
                const auto &declinerChar = gameServices_.getCharacterManager().getCharacterData(req.characterId);
                ResponseBuilder builder;
                std::string msg = networkManager_.generateResponseMessage("declined", builder.setHeader("eventType", "tradeDeclined").setHeader("status", "declined").setHeader("clientId", initiatorChar.clientId).setHeader("hash", "").setBody("byCharacterName", declinerChar.characterName).build());
                networkManager_.sendResponse(initiatorSocket, msg);
            }
        }
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleTradeDeclineEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleTradeOfferUpdateEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<TradeOfferUpdateStruct>(data))
            return;

        const auto &req = std::get<TradeOfferUpdateStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        TradeSessionStruct *session = gameServices_.getTradeSessionManager().getSession(req.sessionId);
        if (!session)
        {
            sendErrorResponseWithTimestamps(socket, "session_not_found", "tradeOfferUpdate", req.clientId, req.timestamps);
            return;
        }

        bool isA = (session->charAId == req.characterId);
        bool isB = (session->charBId == req.characterId);
        if (!isA && !isB)
        {
            sendErrorResponseWithTimestamps(socket, "not_in_session", "tradeOfferUpdate", req.clientId, req.timestamps);
            return;
        }

        // Validate offered items exist in player's non-equipped inventory
        auto &inventoryMgr = gameServices_.getInventoryManager();
        for (const auto &oi : req.items)
        {
            auto inventory = inventoryMgr.getPlayerInventory(req.characterId);
            bool found = false;
            for (const auto &s : inventory)
                if (s.id == oi.inventoryItemId && s.quantity >= oi.quantity && !s.isEquipped)
                {
                    const ItemDataStruct item = gameServices_.getItemManager().getItemById(s.itemId);
                    if (item.isTradable)
                    {
                        found = true;
                        break;
                    }
                }
            if (!found)
            {
                sendErrorResponseWithTimestamps(socket, "invalid_offer_item", "tradeOfferUpdate", req.clientId, req.timestamps);
                return;
            }
        }

        // Apply offer update
        if (isA)
        {
            session->offerA = req.items;
            session->goldA = req.gold;
            session->confirmedA = false;
        }
        else
        {
            session->offerB = req.items;
            session->goldB = req.gold;
            session->confirmedB = false;
        }
        session->lastActivity = std::chrono::steady_clock::now();

        // Notify both sides
        sendTradeState(session->clientAId, *session, true);
        sendTradeState(session->clientBId, *session, false);
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleTradeOfferUpdateEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleTradeConfirmEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<TradeConfirmCancelStruct>(data))
            return;

        const auto &req = std::get<TradeConfirmCancelStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        TradeSessionStruct *session = gameServices_.getTradeSessionManager().getSession(req.sessionId);
        if (!session)
        {
            sendErrorResponseWithTimestamps(socket, "session_not_found", "tradeConfirm", req.clientId, req.timestamps);
            return;
        }

        bool isA = (session->charAId == req.characterId);
        bool isB = (session->charBId == req.characterId);
        if (!isA && !isB)
        {
            sendErrorResponseWithTimestamps(socket, "not_in_session", "tradeConfirm", req.clientId, req.timestamps);
            return;
        }

        if (isA)
            session->confirmedA = true;
        else
            session->confirmedB = true;

        session->lastActivity = std::chrono::steady_clock::now();

        // Both confirmed → execute trade
        if (session->confirmedA && session->confirmedB)
        {
            auto &inventoryMgr = gameServices_.getInventoryManager();
            const ItemDataStruct *goldItem = gameServices_.getItemManager().getItemBySlug("gold_coin");
            if (!goldItem)
            {
                cancelAndNotify(req.sessionId, session->clientAId, session->clientBId, "server_error");
                return;
            }

            // Final validation: both players still have all offered items and gold
            auto validateOfferor = [&](int charId, const std::vector<TradeOfferItemStruct> &offer, int gold) -> bool
            {
                for (const auto &oi : offer)
                {
                    auto inv = inventoryMgr.getPlayerInventory(charId);
                    bool ok = false;
                    for (const auto &s : inv)
                        if (s.id == oi.inventoryItemId && s.quantity >= oi.quantity && !s.isEquipped)
                        {
                            ok = true;
                            break;
                        }
                    if (!ok)
                        return false;
                }
                if (gold > 0 && inventoryMgr.getItemQuantity(charId, goldItem->id) < gold)
                    return false;
                return true;
            };

            if (!validateOfferor(session->charAId, session->offerA, session->goldA) ||
                !validateOfferor(session->charBId, session->offerB, session->goldB))
            {
                cancelAndNotify(req.sessionId, session->clientAId, session->clientBId, "offer_no_longer_valid");
                return;
            }

            // Execute: transfer all offered items from sender to receiver, preserving instances
            auto executeTransfer = [&](
                                       int fromChar, int toChar, const std::vector<TradeOfferItemStruct> &offer, int gold)
            {
                for (const auto &oi : offer)
                {
                    auto inv = inventoryMgr.getPlayerInventory(fromChar);
                    for (const auto &s : inv)
                    {
                        if (s.id == oi.inventoryItemId)
                        {
                            // Remove instanced item from sender's memory without a DB delete
                            inventoryMgr.evictFromMemory(fromChar, oi.inventoryItemId);

                            // Add the same instance to receiver's memory without a DB upsert
                            PlayerInventoryItemStruct transferred = s;
                            transferred.characterId = toChar;
                            transferred.slotIndex = -1;
                            transferred.isEquipped = false;
                            inventoryMgr.addInstancedItemToInventory(toChar, transferred);

                            // Persist ownership change to game server DB
                            nlohmann::json pkt;
                            pkt["header"]["eventType"] = "transferInventoryItem";
                            pkt["header"]["clientId"] = 0;
                            pkt["header"]["hash"] = "";
                            pkt["body"]["fromCharId"] = fromChar;
                            pkt["body"]["toCharId"] = toChar;
                            pkt["body"]["inventoryItemId"] = oi.inventoryItemId;
                            gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
                            break;
                        }
                    }
                }
                if (gold > 0)
                {
                    inventoryMgr.removeItemFromInventory(fromChar, goldItem->id, gold);
                    inventoryMgr.addItemToInventory(toChar, goldItem->id, gold);
                }
            };

            executeTransfer(session->charAId, session->charBId, session->offerA, session->goldA);
            executeTransfer(session->charBId, session->charAId, session->offerB, session->goldB);

            // Notify both: trade complete
            for (auto [cid, charId] : std::vector<std::pair<int, int>>{
                     {session->clientAId, session->charAId},
                     {session->clientBId, session->charBId}})
            {
                auto s = gameServices_.getClientManager().getClientSocket(cid);
                if (!s)
                    continue;
                ResponseBuilder builder;
                std::string msg = networkManager_.generateResponseMessage("success", builder.setHeader("eventType", "tradeComplete").setHeader("status", "success").setHeader("clientId", cid).setHeader("hash", "").setBody("sessionId", req.sessionId).build());
                networkManager_.sendResponse(s, msg);
            }

            gameServices_.getTradeSessionManager().closeSession(req.sessionId);
        }
        else
        {
            // Only one side confirmed — notify both of updated state
            sendTradeState(session->clientAId, *session, true);
            sendTradeState(session->clientBId, *session, false);
        }
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleTradeConfirmEvent: {}", ex.what());
    }
}

void
VendorEventHandler::handleTradeCancelEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<TradeConfirmCancelStruct>(data))
            return;

        const auto &req = std::get<TradeConfirmCancelStruct>(data);

        TradeSessionStruct *session = gameServices_.getTradeSessionManager().getSession(req.sessionId);
        if (!session)
            return;

        cancelAndNotify(req.sessionId, session->clientAId, session->clientBId, "cancelled_by_player");
    }
    catch (const std::exception &ex)
    {
        log_->error("[VendorEventHandler] handleTradeCancelEvent: {}", ex.what());
    }
}
