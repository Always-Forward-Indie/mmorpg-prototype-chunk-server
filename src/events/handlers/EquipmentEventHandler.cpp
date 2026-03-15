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
EquipmentEventHandler::saveEquipmentChange(
    int characterId,
    const std::string &action,
    int inventoryItemId,
    const std::string &slotSlug)
{
    nlohmann::json pkt;
    pkt["header"]["eventType"] = "saveEquipmentChange";
    pkt["header"]["clientId"] = 0;
    pkt["header"]["hash"] = "";
    pkt["body"]["characterId"] = characterId;
    pkt["body"]["action"] = action;
    pkt["body"]["inventoryItemId"] = inventoryItemId;
    pkt["body"]["equipSlotSlug"] = slotSlug;
    gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
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
        saveEquipmentChange(characterId, "equip", req.inventoryItemId, result.equipSlotSlug);

        // If a swap-out happened persist unequip too
        if (result.swappedOutInventoryItemId != 0)
        {
            saveEquipmentChange(characterId, "unequip", result.swappedOutInventoryItemId, result.equipSlotSlug);
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

        // ── Send updated stats (effective attrs change with new gear) ─────────
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

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
        saveEquipmentChange(characterId, "unequip", result.inventoryItemId, req.equipSlotSlug);

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

        // ── Send updated stats (effective attrs change without old gear) ──────
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

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
