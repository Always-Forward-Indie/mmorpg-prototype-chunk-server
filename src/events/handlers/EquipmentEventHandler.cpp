#include "events/handlers/EquipmentEventHandler.hpp"
#include "events/EventData.hpp"
#include "services/EquipmentManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <spdlog/logger.h>

EquipmentEventHandler::EquipmentEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "equipment")
{
}

// ─── Private helpers ──────────────────────────────────────────────────────────

void
EquipmentEventHandler::sendEquipmentState(int clientId, int characterId, const TimestampStruct &ts)
{
    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket || !socket->is_open())
        return;

    nlohmann::json slotsJson = gameServices_.getEquipmentManager().buildEquipmentStateJson(characterId);

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", "success")
                                  .setHeader("eventType", "EQUIPMENT_STATE")
                                  .setHeader("clientId", clientId)
                                  .setTimestamps(ts)
                                  .setBody("characterId", characterId)
                                  .setBody("slots", slotsJson)
                                  .build();

    std::string responseData = networkManager_.generateResponseMessage("success", response, ts);
    networkManager_.sendResponse(socket, responseData);
}

void
EquipmentEventHandler::sendWeightStatus(int clientId, int characterId)
{
    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket || !socket->is_open())
        return;

    float currentWeight = gameServices_.getInventoryManager().getTotalWeight(characterId);
    float weightLimit = gameServices_.getEquipmentManager().getCarryWeightLimit(characterId);

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", "success")
                                  .setHeader("eventType", "WEIGHT_STATUS")
                                  .setHeader("clientId", clientId)
                                  .setBody("characterId", characterId)
                                  .setBody("currentWeight", currentWeight)
                                  .setBody("weightLimit", weightLimit)
                                  .setBody("isOverweight", currentWeight > weightLimit)
                                  .build();

    networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", response));
}

void
EquipmentEventHandler::broadcastEquipmentUpdate(int characterId, int excludeClientId)
{
    nlohmann::json slotsJson = gameServices_.getEquipmentManager().buildEquipmentStateJson(characterId);

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", "success")
                                  .setHeader("eventType", "PLAYER_EQUIPMENT_UPDATE")
                                  .setHeader("clientId", 0)
                                  .setBody("characterId", characterId)
                                  .setBody("slots", slotsJson)
                                  .build();

    float ex = 0.0f, ey = 0.0f;
    if (charPos(characterId, ex, ey))
    {
        // Interest v2 (phase 4): gear changes are visible nearby; the
        // owner always sees them via participants.
        broadcastPositional("success", response, ex, ey,
            {clientForCharacter(characterId)}, excludeClientId);
    }
    else
    {
        std::string responseData = networkManager_.generateResponseMessage("success", response);
        broadcastToAllClients(responseData, excludeClientId);
    }
}

void
EquipmentEventHandler::saveEquipmentChange(
    int characterId,
    const std::string &action,
    int inventoryItemId,
    const std::string &slotSlug,
    int itemId,
    uint64_t syncToken)
{
    nlohmann::json pkt;
    pkt["header"]["eventType"] = "saveEquipmentChange";
    pkt["header"]["clientId"] = 0;
    pkt["header"]["hash"] = "";
    pkt["body"]["characterId"] = characterId;
    pkt["body"]["action"] = action;
    pkt["body"]["inventoryItemId"] = inventoryItemId;
    pkt["body"]["equipSlotSlug"] = slotSlug;
    // Identity for the game-side fallback: a save for a row that still
    // awaits its DB id (syncToken != 0) is resolved by (character, token)
    // instead of being silently dropped.
    pkt["body"]["itemId"] = itemId;
    pkt["body"]["syncToken"] = syncToken;
    gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
}

void
EquipmentEventHandler::sendPendingEquipSave(int characterId,
    const std::string &action, const std::string &slotSlug,
    int inventoryItemId, int itemId)
{
    // Flushed saves always carry real ids (filled at sync time); the token
    // already served its purpose, so no deferral check is needed here.
    saveEquipmentChange(characterId, action, inventoryItemId, slotSlug, itemId, 0);
}

void
EquipmentEventHandler::persistEquipSave(int characterId,
    const std::string &action, const std::string &slotSlug,
    int inventoryItemId, int itemId, uint64_t syncToken)
{
    if (syncToken != 0)
    {
        // Row has no DB id yet: stashing beats sending — the game drops
        // id==0 saves silently. Flushed with the real id on the sync echo.
        gameServices_.getInventoryManager().deferEquipSave(
            syncToken, characterId, action, slotSlug, itemId);
        return;
    }
    saveEquipmentChange(characterId, action, inventoryItemId, slotSlug, itemId, 0);
}

void
EquipmentEventHandler::triggerAttributesRefresh(int characterId, int clientId)
{
    nlohmann::json req;
    req["header"]["eventType"] = "getCharacterAttributes";
    req["header"]["clientId"] = clientId;
    req["header"]["hash"] = "";
    req["body"]["characterId"] = characterId;
    gameServerWorker_.sendDataToGameServer(req.dump() + "\n");
}

// ─── Event handlers ───────────────────────────────────────────────────────────

void
EquipmentEventHandler::handleEquipItemEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<EquipItemRequestStruct>(data))
        {
            log_->error("[EquipmentEH] handleEquipItemEvent: unexpected data type");
            return;
        }

        const auto &req = std::get<EquipItemRequestStruct>(data);
        const int characterId = req.characterId;
        const int clientId = event.getClientID();

        auto socket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!socket)
            return;

        auto result = gameServices_.getEquipmentManager().equipItem(characterId, req.inventoryItemId);

        if (result.error != EquipmentManager::EquipError::NONE)
        {
            std::string reason;
            switch (result.error)
            {
            case EquipmentManager::EquipError::ITEM_NOT_IN_INVENTORY:
                reason = "ITEM_NOT_IN_INVENTORY";
                break;
            case EquipmentManager::EquipError::ITEM_NOT_EQUIPPABLE:
                reason = "ITEM_NOT_EQUIPPABLE";
                break;
            case EquipmentManager::EquipError::LEVEL_REQUIREMENT_NOT_MET:
                reason = "LEVEL_REQUIREMENT_NOT_MET";
                break;
            case EquipmentManager::EquipError::CLASS_RESTRICTION:
                reason = "CLASS_RESTRICTION";
                break;
            case EquipmentManager::EquipError::SLOT_BLOCKED_BY_TWO_HANDED:
                reason = "SLOT_BLOCKED_BY_TWO_HANDED";
                break;
            default:
                reason = "EQUIP_FAILED";
                break;
            }

            sendErrorResponseWithTimestamps(socket, reason, "EQUIP_RESULT", clientId, req.timestamps);
            return;
        }

        // ── Persist equip ────────────────────────────────────────────────────
        // Deferred when the row still awaits its DB id (syncToken != 0):
        // the game silently drops id==0 saves.
        persistEquipSave(characterId, "equip", result.equipSlotSlug,
            req.inventoryItemId, result.itemId, result.syncToken);

        // If a swap-out happened persist unequip too (both swap paths now
        // report identity, including the two-handed off_hand auto-swap that
        // previously persisted nothing).
        if (result.swappedOutInventoryItemId != 0 || !result.swappedOutSlotSlug.empty())
        {
            persistEquipSave(characterId, "unequip", result.swappedOutSlotSlug,
                result.swappedOutInventoryItemId, result.swappedOutItemTemplateId,
                result.swappedOutSyncToken);
        }

        // ── Trigger attribute refresh ────────────────────────────────────────
        triggerAttributesRefresh(characterId, clientId);

        // ── Send EQUIP_RESULT ────────────────────────────────────────────────
        nlohmann::json resultBody;
        resultBody["action"] = "equip";
        resultBody["inventoryItemId"] = req.inventoryItemId;
        resultBody["equipSlotSlug"] = result.equipSlotSlug;
        resultBody["swappedOutInventoryItemId"] =
            result.swappedOutInventoryItemId != 0
                ? nlohmann::json(result.swappedOutInventoryItemId)
                : nlohmann::json(nullptr);

        {
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "success")
                                          .setHeader("eventType", "EQUIP_RESULT")
                                          .setHeader("clientId", clientId)
                                          .setTimestamps(req.timestamps)
                                          .setBody("equip", resultBody)
                                          .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response, req.timestamps);
            networkManager_.sendResponse(socket, responseData);
        }

        // ── Send updated EQUIPMENT_STATE ─────────────────────────────────────
        sendEquipmentState(clientId, characterId, req.timestamps);
        sendWeightStatus(clientId, characterId);

        // NOTE: no immediate sendStatsUpdate here on purpose. Stored attrs
        // are still pre-refresh (the game round-trip below recomputes them);
        // a now-packet would broadcast stale effective values. The refresh
        // echo (SET_CHARACTER_ATTRIBUTES_REFRESH) replaces stored and pushes
        // a fresh stats_update. EQUIP_RESULT + EQUIPMENT_STATE above stay
        // instant (ack + visuals must not wait a round-trip).

        // ── Broadcast gear change to all other clients (visual update) ─────────
        broadcastEquipmentUpdate(characterId, clientId);

        log_->info("[EquipmentEH] Equipped invItemId=" + std::to_string(req.inventoryItemId) +
                   " slot=" + result.equipSlotSlug +
                   " char=" + std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        log_->error("[EquipmentEH] handleEquipItemEvent exception: " + std::string(e.what()));
    }
}

void
EquipmentEventHandler::handleUnequipItemEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<UnequipItemRequestStruct>(data))
        {
            log_->error("[EquipmentEH] handleUnequipItemEvent: unexpected data type");
            return;
        }

        const auto &req = std::get<UnequipItemRequestStruct>(data);
        const int characterId = req.characterId;
        const int clientId = event.getClientID();

        auto socket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!socket)
            return;

        auto result = gameServices_.getEquipmentManager().unequipItem(characterId, req.equipSlotSlug);

        if (result.error != EquipmentManager::UnequipError::NONE)
        {
            sendErrorResponseWithTimestamps(socket, "SLOT_EMPTY", "EQUIP_RESULT", clientId, req.timestamps);
            return;
        }

        // ── Persist unequip ──────────────────────────────────────────────────
        persistEquipSave(characterId, "unequip", req.equipSlotSlug,
            result.inventoryItemId, result.itemId, result.syncToken);

        // ── Trigger attribute refresh ────────────────────────────────────────
        triggerAttributesRefresh(characterId, clientId);

        // ── Send EQUIP_RESULT ────────────────────────────────────────────────
        nlohmann::json resultBody;
        resultBody["action"] = "unequip";
        resultBody["inventoryItemId"] = result.inventoryItemId;
        resultBody["equipSlotSlug"] = req.equipSlotSlug;
        resultBody["swappedOutInventoryItemId"] = nullptr;

        {
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "success")
                                          .setHeader("eventType", "EQUIP_RESULT")
                                          .setHeader("clientId", clientId)
                                          .setTimestamps(req.timestamps)
                                          .setBody("unequip", resultBody)
                                          .build();
            std::string responseData = networkManager_.generateResponseMessage("success", response, req.timestamps);
            networkManager_.sendResponse(socket, responseData);
        }

        // ── Send updated EQUIPMENT_STATE ─────────────────────────────────────
        sendEquipmentState(clientId, characterId, req.timestamps);
        sendWeightStatus(clientId, characterId);

        // NOTE: no immediate sendStatsUpdate here on purpose (same reason
        // as equip: stored attrs are pre-refresh; the echo pushes fresh).

        // ── Broadcast gear change to all other clients (visual update) ─────────
        broadcastEquipmentUpdate(characterId, clientId);

        log_->info("[EquipmentEH] Unequipped slot=" + req.equipSlotSlug +
                   " invItemId=" + std::to_string(result.inventoryItemId) +
                   " char=" + std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        log_->error("[EquipmentEH] handleUnequipItemEvent exception: " + std::string(e.what()));
    }
}

void
EquipmentEventHandler::handleGetEquipmentEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<GetEquipmentRequestStruct>(data))
        {
            log_->error("[EquipmentEH] handleGetEquipmentEvent: unexpected data type");
            return;
        }

        const auto &req = std::get<GetEquipmentRequestStruct>(data);
        sendEquipmentState(event.getClientID(), req.characterId, req.timestamps);
    }
    catch (const std::exception &e)
    {
        log_->error("[EquipmentEH] handleGetEquipmentEvent exception: " + std::string(e.what()));
    }
}
