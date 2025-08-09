#include "events/handlers/ItemEventHandler.hpp"
#include "events/Event.hpp"
#include "utils/ResponseBuilder.hpp"

ItemEventHandler::ItemEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices), gameServices_(gameServices)
{
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
                                              ", Name: " + item.name +
                                              ", Type: " + item.itemTypeName +
                                              ", Attributes: " + std::to_string(item.attributes.size()));
            }

            gameServices_.getItemManager().setItemsList(itemsList);
        }
        else
        {
            gameServices_.getLogger().logError("Error with extracting items list data!");
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
            gameServices_.getLogger().logError("Error with extracting mob loot info data!");
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

            gameServices_.getLogger().log("[ITEM_DROP_EVENT] Sending to clients: " + responseData);

            broadcastToAllClients(responseData);
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data format for ITEM_DROP event");
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

            gameServices_.getLogger().log("[ITEM_PICKUP_EVENT] Processing pickup request - Character: " +
                                          std::to_string(pickupRequest.characterId) +
                                          ", Item UID: " + std::to_string(pickupRequest.droppedItemUID));

            // Try to pickup the item
            bool success = gameServices_.getLootManager().pickupDroppedItem(
                pickupRequest.droppedItemUID,
                pickupRequest.characterId,
                pickupRequest.playerPosition);

            if (success)
            {
                gameServices_.getLogger().log("[ITEM_PICKUP_EVENT] Item successfully picked up");

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
            }
            else
            {
                gameServices_.getLogger().logError("[ITEM_PICKUP_EVENT] Failed to pickup item");

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
            gameServices_.getLogger().logError("[ITEM_PICKUP_EVENT] Invalid event data type for pickup request");
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
            gameServices_.getLogger().log("[GET_NEARBY_ITEMS_EVENT] Response prepared: " + responseStr.substr(0, 200) + "...");
        }
        else
        {
            gameServices_.getLogger().logError("[GET_NEARBY_ITEMS_EVENT] Invalid event data type - expected PositionStruct");
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

    nlohmann::json droppedItemJson = {
        {"uid", droppedItem.uid},
        {"itemId", droppedItem.itemId},
        {"quantity", droppedItem.quantity},
        {"canBePickedUp", droppedItem.canBePickedUp},
        {"droppedByMobUID", droppedItem.droppedByMobUID},
        {"position", {{"x", droppedItem.position.positionX}, {"y", droppedItem.position.positionY}, {"z", droppedItem.position.positionZ}, {"rotationZ", droppedItem.position.rotationZ}}},
        {"item", itemToJson(itemInfo)}};

    return droppedItemJson;
}

nlohmann::json
ItemEventHandler::itemToJson(const ItemDataStruct &itemData)
{
    nlohmann::json itemJson = {
        {"id", itemData.id},
        {"name", itemData.name},
        {"slug", itemData.slug},
        {"description", itemData.description},
        {"isQuestItem", itemData.isQuestItem},
        {"itemType", itemData.itemType},
        {"itemTypeName", itemData.itemTypeName},
        {"itemTypeSlug", itemData.itemTypeSlug},
        {"attributes", nlohmann::json::array()}};

    // Add attributes
    for (const auto &attribute : itemData.attributes)
    {
        nlohmann::json attributeJson = {
            {"id", attribute.id},
            {"name", attribute.name},
            {"slug", attribute.slug},
            {"value", attribute.value}};
        itemJson["attributes"].push_back(attributeJson);
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
            gameServices_.getLootManager().generateLootOnMobDeath(mobId, mobUID, position);
        }
        else
        {
            gameServices_.getLogger().logError("Error with extracting mob loot generation data!");
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

    try
    {
        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json requestData = std::get<nlohmann::json>(data);

            // Extract character ID from request
            if (requestData.contains("characterId"))
            {
                int characterId = requestData["characterId"];

                // Get player's inventory
                auto inventory = gameServices_.getInventoryManager().getPlayerInventory(characterId);

                // Build response
                nlohmann::json inventoryResponse;
                inventoryResponse["characterId"] = characterId;
                inventoryResponse["items"] = nlohmann::json::array();

                for (const auto &item : inventory)
                {
                    nlohmann::json itemJson;
                    itemJson["itemId"] = item.itemId;
                    itemJson["quantity"] = item.quantity;

                    // Get item data for additional info
                    auto itemData = gameServices_.getItemManager().getItemById(item.itemId);
                    if (itemData.id > 0) // Check if item was found
                    {
                        itemJson["name"] = itemData.name;
                        itemJson["description"] = itemData.description;
                        itemJson["itemType"] = itemData.itemTypeName;
                        itemJson["attributes"] = nlohmann::json::array();

                        for (const auto &attr : itemData.attributes)
                        {
                            nlohmann::json attrJson;
                            attrJson["name"] = attr.name;
                            attrJson["value"] = attr.value;
                            itemJson["attributes"].push_back(attrJson);
                        }
                    }

                    inventoryResponse["items"].push_back(itemJson);
                }

                // Send response to client using networkManager
                std::string response = networkManager_.generateResponseMessage("success", inventoryResponse);

                gameServices_.getLogger().log("[INVENTORY] Sending inventory to client for character " +
                                              std::to_string(characterId) + " (" +
                                              std::to_string(inventory.size()) + " items)");
            }
            else
            {
                gameServices_.getLogger().logError("[INVENTORY] characterId not found in GET_PLAYER_INVENTORY request");
            }
        }
        else
        {
            gameServices_.getLogger().logError("Error with extracting get player inventory data!");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing get player inventory event: " + std::string(ex.what()));
    }
}
