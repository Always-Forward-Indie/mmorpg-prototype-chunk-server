#include "events/handlers/ItemEventHandler.hpp"
#include "events/Event.hpp"
#include "utils/ResponseBuilder.hpp"
#include <spdlog/logger.h>

ItemEventHandler::ItemEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "item"), gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("item");
}

void
ItemEventHandler::handleSetItemsListEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<ItemDataStruct>>(data))
        {
            std::vector<ItemDataStruct> itemsList = std::get<std::vector<ItemDataStruct>>(data);

            // Debug items
            for (const auto &item : itemsList)
            {
                gameServices_.getLogger().log("Item ID: " + std::to_string(item.id) +
                                              ", Name: " + item.slug +
                                              ", Type: " + item.itemTypeName +
                                              ", Attributes: " + std::to_string(item.attributes.size()));
            }

            gameServices_.getItemManager().setItemsList(itemsList);
        }
        else
        {
            log_->error("Error with extracting items list data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing items list event: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleSetMobLootInfoEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<MobLootInfoStruct>>(data))
        {
            std::vector<MobLootInfoStruct> mobLootInfo = std::get<std::vector<MobLootInfoStruct>>(data);

            // Debug loot info
            for (const auto &loot : mobLootInfo)
            {
                gameServices_.getLogger().log("Loot - Mob ID: " + std::to_string(loot.mobId) +
                                              ", Item ID: " + std::to_string(loot.itemId) +
                                              ", Drop Chance: " + std::to_string(loot.dropChance));
            }

            gameServices_.getItemManager().setMobLootInfo(mobLootInfo);
        }
        else
        {
            log_->error("Error with extracting mob loot info data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob loot info event: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleItemDropEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<DroppedItemStruct>>(data))
        {
            std::vector<DroppedItemStruct> droppedItems = std::get<std::vector<DroppedItemStruct>>(data);

            gameServices_.getLogger().log("[ITEM_DROP_EVENT] Broadcasting " +
                                          std::to_string(droppedItems.size()) + " dropped items");

            // Build response with all dropped items
            nlohmann::json droppedItemsArray = nlohmann::json::array();

            for (const auto &droppedItem : droppedItems)
            {
                droppedItemsArray.push_back(droppedItemToJson(droppedItem));
            }

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Items dropped")
                                          .setHeader("hash", "")
                                          .setHeader("eventType", "itemDrop")
                                          .setBody("droppedItems", droppedItemsArray)
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);

            log_->info("[ITEM_DROP_EVENT] Sending to clients: " + responseData);

            broadcastToAllClients(responseData);
        }
        else
        {
            log_->error("Invalid data format for ITEM_DROP event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing item drop event: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleItemPickupEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<ItemPickupRequestStruct>(data))
        {
            ItemPickupRequestStruct pickupRequest = std::get<ItemPickupRequestStruct>(data);

            if (!isPlayerAlive(pickupRequest.characterId))
            {
                log_->warn("[ITEM_PICKUP_EVENT] Dead character {} attempted pickup", pickupRequest.characterId);
                return;
            }

            // Use server-side position instead of client-provided to prevent desync false negatives
            pickupRequest.playerPosition = gameServices_.getCharacterManager().getCharacterPosition(pickupRequest.characterId);

            gameServices_.getLogger().log("[ITEM_PICKUP_EVENT] Processing pickup request - Character: " +
                                          std::to_string(pickupRequest.characterId) +
                                          ", Player ID (verified): " + std::to_string(pickupRequest.playerId) +
                                          ", Item UID: " + std::to_string(pickupRequest.droppedItemUID));

            // Try to pickup the item
            bool success = gameServices_.getLootManager().pickupDroppedItem(
                pickupRequest.droppedItemUID,
                pickupRequest.characterId,
                pickupRequest.playerPosition);

            if (success)
            {
                log_->info("[ITEM_PICKUP_EVENT] Item successfully picked up");

                // Notify clients that item was picked up
                nlohmann::json response = ResponseBuilder()
                                              .setHeader("message", "Item picked up")
                                              .setHeader("hash", "")
                                              .setHeader("eventType", "itemPickup")
                                              .setBody("success", true)
                                              .setBody("characterId", pickupRequest.characterId)
                                              .setBody("droppedItemUID", pickupRequest.droppedItemUID)
                                              .build();

                // Broadcast to all clients in the area
                std::string responseData = networkManager_.generateResponseMessage("success", response);
                broadcastToAllClients(responseData);

                // Update carry weight for the player who picked up the item
                auto charData = gameServices_.getCharacterManager().getCharacterData(pickupRequest.characterId);
                if (charData.clientId > 0)
                {
                    auto weightSocket = gameServices_.getClientManager().getClientSocket(charData.clientId);
                    if (weightSocket && weightSocket->is_open())
                    {
                        float currentWeight = gameServices_.getInventoryManager().getTotalWeight(pickupRequest.characterId);
                        float weightLimit = gameServices_.getEquipmentManager().getCarryWeightLimit(pickupRequest.characterId);
                        nlohmann::json weightResponse = ResponseBuilder()
                                                            .setHeader("message", "success")
                                                            .setHeader("eventType", "WEIGHT_STATUS")
                                                            .setHeader("clientId", charData.clientId)
                                                            .setBody("characterId", pickupRequest.characterId)
                                                            .setBody("currentWeight", currentWeight)
                                                            .setBody("weightLimit", weightLimit)
                                                            .setBody("isOverweight", currentWeight > weightLimit)
                                                            .build();
                        networkManager_.sendResponse(weightSocket, networkManager_.generateResponseMessage("success", weightResponse));
                    }
                }
            }
            else
            {
                log_->error("[ITEM_PICKUP_EVENT] Failed to pickup item");

                // Send failure response to all clients (TODO: optimize to send only to requesting client)
                nlohmann::json response = ResponseBuilder()
                                              .setHeader("message", "Item pickup failed")
                                              .setHeader("hash", "")
                                              .setHeader("eventType", "itemPickup")
                                              .setBody("success", false)
                                              .setBody("characterId", pickupRequest.characterId)
                                              .setBody("droppedItemUID", pickupRequest.droppedItemUID)
                                              .build();

                std::string responseData = networkManager_.generateResponseMessage("error", response);
                broadcastToAllClients(responseData);
            }
        }
        else
        {
            log_->error("[ITEM_PICKUP_EVENT] Invalid event data type for pickup request");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("[ITEM_PICKUP_EVENT] Error processing pickup event: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[ITEM_PICKUP_EVENT] Exception during pickup processing: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleGetNearbyItemsEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<PositionStruct>(data))
        {
            PositionStruct playerPosition = std::get<PositionStruct>(data);

            gameServices_.getLogger().log("[GET_NEARBY_ITEMS_EVENT] Getting items near position: " +
                                          std::to_string(playerPosition.positionX) + "," + std::to_string(playerPosition.positionY));

            // Get all dropped items (TODO: implement getNearbyItems in LootManager)
            auto allDroppedItems = gameServices_.getLootManager().getAllDroppedItems();
            std::vector<DroppedItemStruct> nearbyItems;

            // Convert map to vector for now (TODO: filter by distance)
            for (const auto &item : allDroppedItems)
            {
                nearbyItems.push_back(item.second);
            }

            // Build response with nearby items
            nlohmann::json nearbyItemsArray = nlohmann::json::array();

            for (const auto &droppedItem : nearbyItems)
            {
                nearbyItemsArray.push_back(droppedItemToJson(droppedItem));
            }

            nlohmann::json playerPositionJson = {
                {"x", playerPosition.positionX},
                {"y", playerPosition.positionY},
                {"z", playerPosition.positionZ}};

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Nearby items")
                                          .setHeader("hash", "")
                                          .setHeader("eventType", "nearbyItems")
                                          .setBody("items", nearbyItemsArray)
                                          .setBody("playerPosition", playerPositionJson)
                                          .build();

            gameServices_.getLogger().log("[GET_NEARBY_ITEMS_EVENT] Found " + std::to_string(nearbyItems.size()) + " nearby items");

            // Send response to requesting client (we'll need to determine which client later)
            // For now, just log that we have the data ready
            std::string responseStr = response.dump();
            log_->info("[GET_NEARBY_ITEMS_EVENT] Response prepared: " + responseStr.substr(0, 200) + "...");
        }
        else
        {
            log_->error("[GET_NEARBY_ITEMS_EVENT] Invalid event data type - expected PositionStruct");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("[GET_NEARBY_ITEMS_EVENT] Error processing nearby items event: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[GET_NEARBY_ITEMS_EVENT] Exception during nearby items processing: " + std::string(ex.what()));
    }
}

nlohmann::json
ItemEventHandler::droppedItemToJson(const DroppedItemStruct &droppedItem)
{
    // Get item info
    ItemDataStruct itemInfo = gameServices_.getItemManager().getItemById(droppedItem.itemId);

    // Calculate seconds remaining on reservation (0 if free for all)
    int64_t reservationSecondsLeft = 0;
    if (droppedItem.reservedForCharacterId != 0)
    {
        auto now = std::chrono::steady_clock::now();
        if (now < droppedItem.reservationExpiry)
        {
            reservationSecondsLeft = std::chrono::duration_cast<std::chrono::seconds>(
                droppedItem.reservationExpiry - now)
                                         .count();
        }
    }

    nlohmann::json droppedItemJson = {
        {"uid", droppedItem.uid},
        {"itemId", droppedItem.itemId},
        {"quantity", droppedItem.quantity},
        {"canBePickedUp", droppedItem.canBePickedUp},
        {"droppedByMobUID", droppedItem.droppedByMobUID},
        {"droppedByCharacterId", droppedItem.droppedByCharacterId},
        {"reservedForCharacterId", droppedItem.reservedForCharacterId},
        {"reservationSecondsLeft", reservationSecondsLeft},
        {"position", {{"x", droppedItem.position.positionX}, {"y", droppedItem.position.positionY}, {"z", droppedItem.position.positionZ}, {"rotationZ", droppedItem.position.rotationZ}}},
        {"item", itemToJson(itemInfo)}};

    return droppedItemJson;
}

nlohmann::json
ItemEventHandler::itemToJson(const ItemDataStruct &itemData)
{
    nlohmann::json itemJson = {
        {"id", itemData.id},
        {"slug", itemData.slug},
        {"isQuestItem", itemData.isQuestItem},
        {"itemType", itemData.itemType},
        {"itemTypeName", itemData.itemTypeName},
        {"itemTypeSlug", itemData.itemTypeSlug},
        {"isContainer", itemData.isContainer},
        {"isDurable", itemData.isDurable},
        {"isTradable", itemData.isTradable},
        {"isEquippable", itemData.isEquippable},
        {"weight", itemData.weight},
        {"rarityId", itemData.rarityId},
        {"rarityName", itemData.rarityName},
        {"raritySlug", itemData.raritySlug},
        {"stackMax", itemData.stackMax},
        {"durabilityMax", itemData.durabilityMax},
        {"vendorPriceBuy", itemData.vendorPriceBuy},
        {"vendorPriceSell", itemData.vendorPriceSell},
        {"equipSlot", itemData.equipSlot},
        {"equipSlotName", itemData.equipSlotName},
        {"equipSlotSlug", itemData.equipSlotSlug},
        {"levelRequirement", itemData.levelRequirement},
        {"attributes", nlohmann::json::array()},
        {"isUsable", itemData.isUsable},
        {"useEffects", nlohmann::json::array()}};

    // Add attributes
    for (const auto &attribute : itemData.attributes)
    {
        nlohmann::json attributeJson = {
            {"id", attribute.id},
            {"item_id", attribute.item_id},
            {"name", attribute.name},
            {"slug", attribute.slug},
            {"value", attribute.value}};
        itemJson["attributes"].push_back(attributeJson);
    }

    // Add use effects
    for (const auto &ue : itemData.useEffects)
    {
        itemJson["useEffects"].push_back({{"effectSlug", ue.effectSlug},
            {"attributeSlug", ue.attributeSlug},
            {"value", ue.value},
            {"isInstant", ue.isInstant},
            {"durationSeconds", ue.durationSeconds},
            {"tickMs", ue.tickMs},
            {"cooldownSeconds", ue.cooldownSeconds}});
    }

    return itemJson;
}

void
ItemEventHandler::handleMobLootGenerationEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json mobLootData = std::get<nlohmann::json>(data);

            // Extract mob death data
            int mobId = mobLootData["mobId"];
            int mobUID = mobLootData["mobUID"];
            float posX = mobLootData["positionX"];
            float posY = mobLootData["positionY"];
            float posZ = mobLootData["positionZ"];
            int zoneId = mobLootData["zoneId"];

            // Create position struct
            PositionStruct position;
            position.positionX = posX;
            position.positionY = posY;
            position.positionZ = posZ;

            gameServices_.getLogger().log("[LOOT_EVENT] Processing loot generation for mob ID " +
                                          std::to_string(mobId) + " (UID " + std::to_string(mobUID) +
                                          ") at position (" + std::to_string(posX) + ", " +
                                          std::to_string(posY) + ")");

            // Generate loot using LootManager
            int killerId = mobLootData.value("killerId", 0);
            gameServices_.getLootManager().generateLootOnMobDeath(mobId, mobUID, position, killerId);

            // Register corpse for harvesting
            gameServices_.getHarvestManager().registerCorpse(mobUID, mobId, position);
            gameServices_.getLogger().log("[HARVEST] Registered corpse for harvesting - mob ID " +
                                          std::to_string(mobId) + " (UID " + std::to_string(mobUID) + ")");
        }
        else
        {
            log_->error("Error with extracting mob loot generation data!");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing mob loot generation event: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleGetPlayerInventoryEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json requestData = std::get<nlohmann::json>(data);

            // Extract character ID from request
            if (requestData.contains("characterId"))
            {
                int characterId = requestData["characterId"];

                log_->info("[INVENTORY] Processing inventory request for character " + std::to_string(characterId));

                // Get client data to extract hash
                ClientDataStruct clientData = gameServices_.getClientManager().getClientData(clientID);

                // Get player's inventory
                auto inventory = gameServices_.getInventoryManager().getPlayerInventory(characterId);

                gameServices_.getLogger().log("[INVENTORY] Found " + std::to_string(inventory.size()) + " items in inventory");

                // Build response
                nlohmann::json itemsArray = nlohmann::json::array();

                for (const auto &item : inventory)
                {
                    gameServices_.getLogger().log("[INVENTORY] Processing item ID " + std::to_string(item.itemId) + " quantity " + std::to_string(item.quantity));

                    // Use InventoryManager's method to get full item data
                    nlohmann::json itemJson = gameServices_.getInventoryManager().inventoryItemToJson(item);
                    itemsArray.push_back(itemJson);
                }

                gameServices_.getLogger().log("[INVENTORY] Built inventory response with " + std::to_string(itemsArray.size()) + " items");

                // Create response using ResponseBuilder
                int goldAmount = gameServices_.getInventoryManager().getGoldAmount(characterId);

                nlohmann::json response = ResponseBuilder()
                                              .setHeader("message", "Inventory retrieved successfully!")
                                              .setHeader("hash", clientData.hash)
                                              .setHeader("clientId", clientID)
                                              .setHeader("eventType", "getPlayerInventory")
                                              .setBody("characterId", characterId)
                                              .setBody("items", itemsArray)
                                              .setBody("gold", goldAmount)
                                              .build();

                std::string responseData = networkManager_.generateResponseMessage("success", response);
                networkManager_.sendResponse(clientSocket, responseData);

                gameServices_.getLogger().log("[INVENTORY] Sent inventory to client for character " +
                                              std::to_string(characterId) + " (" +
                                              std::to_string(inventory.size()) + " items)");

                // Send carry weight so the client can display currentWeight/weightLimit
                // in the inventory UI immediately when the inventory panel is opened.
                {
                    float curW = gameServices_.getInventoryManager().getTotalWeight(characterId);
                    float limW = gameServices_.getEquipmentManager().getCarryWeightLimit(characterId);
                    nlohmann::json weightResponse = ResponseBuilder()
                                                        .setHeader("message", "success")
                                                        .setHeader("eventType", "WEIGHT_STATUS")
                                                        .setHeader("clientId", clientID)
                                                        .setBody("characterId", characterId)
                                                        .setBody("currentWeight", curW)
                                                        .setBody("weightLimit", limW)
                                                        .setBody("isOverweight", curW > limW)
                                                        .build();
                    networkManager_.sendResponse(clientSocket,
                        networkManager_.generateResponseMessage("success", weightResponse));
                }
            }
            else
            {
                log_->error("[INVENTORY] characterId not found in GET_PLAYER_INVENTORY request");

                // Get client data for hash
                ClientDataStruct clientData = gameServices_.getClientManager().getClientData(clientID);

                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "characterId not found in request!")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "getPlayerInventory")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
        }
        else
        {
            log_->error("Error with extracting get player inventory data!");

            // Get client data for hash
            ClientDataStruct clientData = gameServices_.getClientManager().getClientData(clientID);

            nlohmann::json errorResponse = ResponseBuilder()
                                               .setHeader("message", "Invalid request data format!")
                                               .setHeader("hash", clientData.hash)
                                               .setHeader("clientId", clientID)
                                               .setHeader("eventType", "getPlayerInventory")
                                               .build();

            std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
            networkManager_.sendResponse(clientSocket, responseData);
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing get player inventory event: " + std::string(ex.what()));
    }
}

void
ItemEventHandler::handleInventoryUpdateEvent(const Event &event)
{
    const auto &data = event.getData();
    int characterId = event.getClientID(); // InventoryManager stores characterId in the clientID field

    if (!std::holds_alternative<nlohmann::json>(data))
    {
        log_->error("[INVENTORY] handleInventoryUpdateEvent: unexpected data type for character " +
                    std::to_string(characterId));
        return;
    }

    const nlohmann::json &inventoryData = std::get<nlohmann::json>(data);

    // Resolve the client socket for this character
    auto charData = gameServices_.getCharacterManager().getCharacterData(characterId);
    int clientId = charData.clientId;
    if (clientId <= 0)
        return;

    auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!clientSocket)
        return;

    auto clientData = gameServices_.getClientManager().getClientData(clientId);

    // Use the same eventType the client already handles for inventory display.
    // The client only processes "getPlayerInventory" responses to refresh its
    // inventory panel — "inventoryUpdate" is silently ignored on the client side.
    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", "Inventory retrieved successfully!")
                                  .setHeader("hash", clientData.hash)
                                  .setHeader("clientId", clientId)
                                  .setHeader("eventType", "getPlayerInventory")
                                  .build();
    response["body"] = inventoryData;
    response["body"]["gold"] = gameServices_.getInventoryManager().getGoldAmount(characterId);

    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));

    log_->info("[INVENTORY] Sent inventoryUpdate to client " + std::to_string(clientId) +
               " for character " + std::to_string(characterId));

    // Keep carry weight in sync after any inventory change (harvest, quest reward, loot, etc.).
    {
        float curW = gameServices_.getInventoryManager().getTotalWeight(characterId);
        float limW = gameServices_.getEquipmentManager().getCarryWeightLimit(characterId);
        nlohmann::json weightResponse = ResponseBuilder()
                                            .setHeader("message", "success")
                                            .setHeader("eventType", "WEIGHT_STATUS")
                                            .setHeader("clientId", clientId)
                                            .setBody("characterId", characterId)
                                            .setBody("currentWeight", curW)
                                            .setBody("weightLimit", limW)
                                            .setBody("isOverweight", curW > limW)
                                            .build();
        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", weightResponse));
    }
}

// ---------------------------------------------------------------------------
// sendGroundItemsToClient — snapshot of all dropped items to one socket
// ---------------------------------------------------------------------------
void
ItemEventHandler::sendGroundItemsToClient(int clientId,
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    auto allDropped = gameServices_.getLootManager().getAllDroppedItems();

    nlohmann::json itemsArray = nlohmann::json::array();
    for (const auto &[uid, drop] : allDropped)
        itemsArray.push_back(droppedItemToJson(drop));

    ClientDataStruct clientData = gameServices_.getClientManager().getClientData(clientId);

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", "success")
                                  .setHeader("eventType", "itemDrop")
                                  .setHeader("hash", clientData.hash)
                                  .setHeader("clientId", clientId)
                                  .setBody("items", itemsArray)
                                  .build();

    networkManager_.sendResponse(clientSocket,
        networkManager_.generateResponseMessage("success", response));

    log_->info("[LOOT] Sent " + std::to_string(itemsArray.size()) +
               " ground items snapshot to client " + std::to_string(clientId));
}

// ---------------------------------------------------------------------------
// handleItemDropByPlayerEvent — player drops item from inventory onto the ground
// ---------------------------------------------------------------------------
void
ItemEventHandler::handleItemDropByPlayerEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientId = event.getClientID();

    try
    {
        if (!std::holds_alternative<ItemDropByPlayerRequestStruct>(data))
        {
            log_->error("[DROP] handleItemDropByPlayerEvent: unexpected data type for client " +
                        std::to_string(clientId));
            return;
        }

        const auto &req = std::get<ItemDropByPlayerRequestStruct>(data);
        int characterId = req.characterId;
        int itemId = req.itemId;
        int quantity = req.quantity;

        if (quantity <= 0)
        {
            log_->warn("[DROP] Character " + std::to_string(characterId) +
                       " requested invalid quantity " + std::to_string(quantity));
            return;
        }

        // Validate item exists and is tradable / not quest-only
        ItemDataStruct itemInfo = gameServices_.getItemManager().getItemById(itemId);
        if (itemInfo.id == 0)
        {
            log_->warn("[DROP] Unknown itemId=" + std::to_string(itemId) +
                       " from character " + std::to_string(characterId));
            return;
        }
        if (itemInfo.isQuestItem || !itemInfo.isTradable)
        {
            log_->warn("[DROP] Character " + std::to_string(characterId) +
                       " tried to drop non-droppable item " + itemInfo.slug);
            return;
        }

        // Check the inventory contains enough quantity
        int inInventory = gameServices_.getInventoryManager().getItemQuantity(characterId, itemId);
        if (inInventory < quantity)
        {
            log_->warn("[DROP] Character " + std::to_string(characterId) +
                       " has only " + std::to_string(inInventory) + " of item " +
                       std::to_string(itemId) + ", cannot drop " + std::to_string(quantity));
            return;
        }

        // ── Auto-unequip if the item being dropped is currently equipped ────
        auto equippedSlot = gameServices_.getEquipmentManager().findSlotForItemId(characterId, itemId);
        if (equippedSlot.has_value())
        {
            const auto &[slotSlug, inventoryItemId] = equippedSlot.value();

            // Unequip from in-memory state
            gameServices_.getEquipmentManager().unequipItem(characterId, slotSlug);

            // Persist unequip to game server DB
            nlohmann::json unequipPkt;
            unequipPkt["header"]["eventType"] = "saveEquipmentChange";
            unequipPkt["header"]["clientId"] = 0;
            unequipPkt["header"]["hash"] = "";
            unequipPkt["body"]["characterId"] = characterId;
            unequipPkt["body"]["action"] = "unequip";
            unequipPkt["body"]["inventoryItemId"] = inventoryItemId;
            unequipPkt["body"]["equipSlotSlug"] = slotSlug;
            gameServerWorker_.sendDataToGameServer(unequipPkt.dump() + "\n");

            // Notify client of updated equipment state
            auto socket = gameServices_.getClientManager().getClientSocket(clientId);
            if (socket && socket->is_open())
            {
                nlohmann::json slotsJson = gameServices_.getEquipmentManager().buildEquipmentStateJson(characterId);
                nlohmann::json eqResponse = ResponseBuilder()
                                                .setHeader("message", "success")
                                                .setHeader("eventType", "EQUIPMENT_STATE")
                                                .setHeader("clientId", clientId)
                                                .setBody("characterId", characterId)
                                                .setBody("slots", slotsJson)
                                                .build();
                networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", eqResponse));
            }

            // Recalculate stats — gear bonus no longer applies
            gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

            log_->info("[DROP] Auto-unequipped slot=" + slotSlug +
                       " invItemId=" + std::to_string(inventoryItemId) +
                       " for char=" + std::to_string(characterId) + " before drop");
        }

        // Grab the inventory item's DB id and current durability before removing.
        // Use the instanced path (evict + nullify owner) ONLY for non-stackable items
        // (stackMax == 1, e.g. equipment) — for those we want to preserve the exact DB row.
        // Stackable items always use the quantity-reduction path with inventoryItemId = 0,
        // otherwise evictFromMemory removes the entire stack regardless of how many we drop.
        int droppingInvItemId = 0;
        int droppingDurability = 0;
        {
            auto inv = gameServices_.getInventoryManager().getPlayerInventory(characterId);
            for (const auto &s : inv)
            {
                if (s.itemId == itemId)
                {
                    droppingInvItemId = s.id;
                    droppingDurability = s.durabilityCurrent;
                    break;
                }
            }
        }

        // Instanced path applies only to non-stackable unique items (e.g. equipment).
        bool isInstanced = (itemInfo.stackMax == 1) && (droppingInvItemId > 0);

        if (isInstanced)
        {
            // Non-stackable item: remove from in-memory only (no DB delete).
            // The DB row will be nullified (character_id = NULL) by LootManager.
            gameServices_.getInventoryManager().evictFromMemory(characterId, droppingInvItemId);
        }
        else
        {
            // Stackable item: reduce quantity by the dropped amount.
            // Do NOT link the drop to the original DB row — create a new ground pile.
            bool removed = gameServices_.getInventoryManager().removeItemFromInventory(characterId, itemId, quantity);
            if (!removed)
            {
                log_->error("[DROP] Failed to remove item " + std::to_string(itemId) +
                            " from character " + std::to_string(characterId));
                return;
            }
            droppingInvItemId = 0; // ground item is a new pile, not tied to any DB row
        }

        // Use server-tracked position (client does not send coords in dropItem)
        PositionStruct dropPosition = gameServices_.getCharacterManager().getCharacterPosition(characterId);

        // Spawn on the ground (LootManager fires nullifyItemOwner if inventoryItemId > 0)
        gameServices_.getLootManager().dropItemByPlayer(characterId, droppingInvItemId, itemId, quantity, dropPosition, droppingDurability);

        log_->info("[DROP] Character " + std::to_string(characterId) + " dropped " +
                   std::to_string(quantity) + "x item " + std::to_string(itemId));
    }
    catch (const std::exception &ex)
    {
        log_->error("[DROP] Exception: " + std::string(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// handleItemRemoveEvent — broadcast despawn of one or more ground items
// ---------------------------------------------------------------------------
void
ItemEventHandler::handleItemRemoveEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (!std::holds_alternative<std::vector<int>>(data))
        {
            log_->error("[ITEM_REMOVE] Unexpected data type");
            return;
        }

        const auto &uids = std::get<std::vector<int>>(data);
        if (uids.empty())
            return;

        nlohmann::json uidsJson = nlohmann::json::array();
        for (int uid : uids)
            uidsJson.push_back(uid);

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "success")
                                      .setHeader("eventType", "itemRemove")
                                      .setBody("uids", uidsJson)
                                      .build();

        std::string msg = networkManager_.generateResponseMessage("success", response);
        broadcastToAllClients(msg);

        log_->info("[ITEM_REMOVE] Broadcast removal of " + std::to_string(uids.size()) + " ground items");
    }
    catch (const std::exception &ex)
    {
        log_->error("[ITEM_REMOVE] Exception: " + std::string(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// handleUseItemEvent — potion / scroll / food use
// ---------------------------------------------------------------------------

bool
ItemEventHandler::trySetItemCooldown(int characterId, int itemId, int cooldownSeconds)
{
    if (cooldownSeconds <= 0)
        return true; // no cooldown defined — always allow
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(itemCooldownMutex_);
    auto &entry = itemCooldowns_[characterId][itemId];
    if (now < entry)
        return false; // still on cooldown
    entry = now + std::chrono::seconds(cooldownSeconds);
    return true;
}

void
ItemEventHandler::handleUseItemEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientId = event.getClientID();

    try
    {
        if (!std::holds_alternative<ItemUseRequestStruct>(data))
        {
            log_->error("[USE_ITEM] Unexpected data type for client " + std::to_string(clientId));
            return;
        }

        const auto &req = std::get<ItemUseRequestStruct>(data);
        int characterId = req.characterId;
        int itemId = req.itemId;

        // Character must be alive
        {
            const auto ch = gameServices_.getCharacterManager().getCharacterData(characterId);
            if (ch.characterCurrentHealth <= 0)
            {
                log_->warn("[USE_ITEM] Character " + std::to_string(characterId) + " is dead, ignoring use request");
                return;
            }
        }

        // Must have the item
        int qty = gameServices_.getInventoryManager().getItemQuantity(characterId, itemId);
        if (qty <= 0)
        {
            log_->warn("[USE_ITEM] Character " + std::to_string(characterId) +
                       " has no item " + std::to_string(itemId));
            return;
        }

        // Fetch item definition
        ItemDataStruct itemInfo = gameServices_.getItemManager().getItemById(itemId);
        if (!itemInfo.isUsable || itemInfo.useEffects.empty())
        {
            log_->warn("[USE_ITEM] Item " + itemInfo.slug + " is not usable");
            return;
        }

        // Determine the cooldown from the first use-effect definition
        // (items typically share a single cooldown across all their effects)
        const int cooldownSec = itemInfo.useEffects.empty() ? 0 : itemInfo.useEffects[0].cooldownSeconds;
        if (!trySetItemCooldown(characterId, itemId, cooldownSec))
        {
            log_->warn("[USE_ITEM] Character " + std::to_string(characterId) +
                       " tried to use '" + itemInfo.slug + "' while still on cooldown");
            return;
        }

        // Consume one from inventory
        gameServices_.getInventoryManager().removeItemFromInventory(characterId, itemId, 1);

        // Unix timestamp (seconds) — used for expiresAt in ActiveEffectStruct
        const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
                                   .count();

        int totalHeal = 0;
        int finalHp = 0;

        // Apply each use-effect
        for (const auto &ue : itemInfo.useEffects)
        {
            if (ue.isInstant)
            {
                if (ue.attributeSlug == "hp")
                {
                    finalHp = gameServices_.getCharacterManager().applyHealToCharacter(
                                                                     characterId, static_cast<int>(ue.value))
                                  .newHealth;
                    totalHeal += static_cast<int>(ue.value);
                }
                else if (ue.attributeSlug == "mp")
                {
                    gameServices_.getCharacterManager().restoreManaToCharacter(
                        characterId, static_cast<int>(ue.value));
                }
                // Other instant slugs: value is applied directly via stat modifier
                // and reflected in the stats_update packet sent below.
            }
            else
            {
                // Timed buff / HoT / debuff — routed through the existing
                // ActiveEffect pipeline (CombatSystem::tickEffects ticks hot/dot,
                // CharacterStatsNotificationService includes all effects in
                // the stats_update packet for the buff bar).
                ActiveEffectStruct effect;
                effect.effectSlug = ue.effectSlug;
                effect.attributeSlug = ue.attributeSlug;
                effect.value = ue.value;
                effect.tickMs = ue.tickMs > 0 ? ue.tickMs : 1000;
                effect.expiresAt = nowSec + ue.durationSeconds; // Unix seconds

                // Derive effectTypeSlug so CombatSystem handles ticking correctly
                if (ue.attributeSlug == "hp" && ue.value > 0)
                    effect.effectTypeSlug = "hot";
                else if (ue.attributeSlug == "hp" && ue.value < 0)
                    effect.effectTypeSlug = "dot";
                else
                    effect.effectTypeSlug = "buff"; // stat modifier (mp regen, str, …)

                // Prime the first tick
                effect.nextTickAt = std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(effect.tickMs);

                gameServices_.getCharacterManager().addActiveEffect(characterId, effect);
            }
        }

        // Broadcast heal number to all clients (floating combat text).
        // Uses the same "healingResult" eventType as heal skills — client
        // needs no special handling for potion heals.
        if (totalHeal > 0)
        {
            nlohmann::json skillResult;
            skillResult["casterId"] = characterId;
            skillResult["targetId"] = characterId;
            skillResult["skillName"] = itemInfo.slug;
            skillResult["skillSlug"] = itemInfo.slug;
            skillResult["skillEffectType"] = "heal";
            skillResult["healing"] = totalHeal;
            skillResult["finalTargetHealth"] = finalHp;
            skillResult["success"] = true;

            nlohmann::json healResponse = ResponseBuilder()
                                              .setHeader("message", "success")
                                              .setHeader("eventType", "healingResult")
                                              .setBody("skillResult", skillResult)
                                              .build();
            broadcastToAllClients(
                networkManager_.generateResponseMessage("success", healResponse));
        }

        // One full stats_update to the owner — contains updated hp/mp, effective
        // attributes (stat buffs already factored in), and the activeEffects list
        // (buff bar display). This is the same packet used for equipment, respawn,
        // level-up etc., so the client needs no special handling.
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        log_->info("[USE_ITEM] Character " + std::to_string(characterId) +
                   " used item " + itemInfo.slug);
    }
    catch (const std::exception &ex)
    {
        log_->error("[USE_ITEM] Exception: " + std::string(ex.what()));
    }
}
