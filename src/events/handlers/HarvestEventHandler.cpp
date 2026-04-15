#include "events/handlers/HarvestEventHandler.hpp"
#include "data/DataStructs.hpp"
#include "events/Event.hpp"
#include "nlohmann/json.hpp"
#include "utils/ResponseBuilder.hpp"
#include <spdlog/logger.h>

HarvestEventHandler::HarvestEventHandler(NetworkManager &networkManager, GameServerWorker &gameServerWorker, GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "harvest"), gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("harvest");
}

void
HarvestEventHandler::handleHarvestStartRequest(const Event &event)
{
    log_->info("HarvestEventHandler::handleHarvestStartRequest called");
    log_->info("Handling harvest start request");

    try
    {
        const auto &data = event.getData();
        log_->info("HarvestEventHandler: Checking event data type");

        if (std::holds_alternative<HarvestRequestStruct>(data))
        {
            log_->info("HarvestEventHandler: Event contains HarvestRequestStruct");
            HarvestRequestStruct request = std::get<HarvestRequestStruct>(data);
            int clientId = event.getClientID();

            gameServices_.getLogger().log("HarvestEventHandler: Request data - characterId: " + std::to_string(request.characterId) +
                                              ", playerId: " + std::to_string(request.playerId) +
                                              ", corpseUID: " + std::to_string(request.corpseUID),
                GREEN);

            // Проверяем что characterId валидный
            if (request.characterId <= 0)
            {
                log_->error("Invalid character ID in harvest request: " + std::to_string(request.characterId));
                return;
            }

            if (!isPlayerAlive(request.characterId))
            {
                log_->warn("[HarvestEventHandler] Dead character {} attempted harvest", request.characterId);
                return;
            }

            log_->info("HarvestEventHandler: Using character ID from request: " + std::to_string(request.characterId));

            // Проверяем существование трупа
            log_->info("HarvestEventHandler: Getting HarvestManager");
            auto &harvestManager = gameServices_.getHarvestManager();
            log_->info("HarvestEventHandler: Getting corpse by UID: " + std::to_string(request.corpseUID));
            auto corpse = harvestManager.getCorpseByUID(request.corpseUID);
            if (corpse.mobUID == 0)
            {
                log_->error("Corpse not harvestable: " + std::to_string(request.corpseUID));

                // Отправляем ошибку клиенту
                auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
                if (clientSocket)
                {
                    auto clientData = gameServices_.getClientManager().getClientData(clientId);

                    nlohmann::json response = ResponseBuilder()
                                                  .setHeader("message", "Corpse not available")
                                                  .setHeader("hash", clientData.hash)
                                                  .setHeader("clientId", std::to_string(clientId))
                                                  .setHeader("eventType", "harvestError")
                                                  .setBody("type", "HARVEST_ERROR")
                                                  .setBody("clientId", clientId)
                                                  .setBody("playerId", request.playerId)
                                                  .setBody("corpseId", request.corpseUID)
                                                  .setBody("errorCode", "CORPSE_NOT_AVAILABLE")
                                                  .setBody("message", "Corpse not available for harvest")
                                                  .build();

                    std::string responseData = networkManager_.generateResponseMessage("error", response);
                    networkManager_.sendResponse(clientSocket, responseData);
                }
                return;
            }

            // Начинаем сбор урожая (позицию берем из данных игрока на сервере)
            PositionStruct playerPosition;
            try
            {
                auto player = gameServices_.getCharacterManager().getCharacterById(request.characterId);
                if (player.characterId != 0)
                {
                    playerPosition = player.characterPosition;
                    log_->info("HarvestEventHandler: Got player position from CharacterManager");
                }
                else
                {
                    // Использовать позицию по умолчанию если игрок не найден
                    playerPosition = {0.0f, 0.0f, 0.0f, 0.0f};
                    log_->info("HarvestEventHandler: Using default position for harvest");
                }
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("HarvestEventHandler: Exception getting player position, using default: " + std::string(e.what()), RED);
                playerPosition = {0.0f, 0.0f, 0.0f, 0.0f};
            }

            bool success = false;
            try
            {
                log_->info("HarvestEventHandler: Calling harvestManager.startHarvest()");
                success = harvestManager.startHarvest(clientId, request.corpseUID, playerPosition);
                log_->info("HarvestEventHandler: startHarvest returned: " + std::string(success ? "true" : "false"));
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("HarvestEventHandler: Exception in startHarvest: " + std::string(e.what()), RED);
                success = false;
            }

            // Отправляем ответ клиенту
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(clientId);

                if (success)
                {
                    nlohmann::json response = ResponseBuilder()
                                                  .setHeader("message", "Harvest started successfully")
                                                  .setHeader("hash", clientData.hash)
                                                  .setHeader("clientId", std::to_string(clientId))
                                                  .setHeader("eventType", "harvestStarted")
                                                  .setBody("type", "HARVEST_STARTED")
                                                  .setBody("clientId", clientId)
                                                  .setBody("playerId", request.playerId)
                                                  .setBody("corpseId", request.corpseUID)
                                                  .setBody("duration", 3000) // 3 секунды
                                                  .setBody("startTime", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())
                                                  .build();

                    std::string responseData = networkManager_.generateResponseMessage("success", response);
                    networkManager_.sendResponse(clientSocket, responseData);

                    gameServices_.getLogger().log("Harvest started for player " + std::to_string(clientId) + " on corpse " + std::to_string(request.corpseUID));
                }
                else
                {
                    nlohmann::json response = ResponseBuilder()
                                                  .setHeader("message", "Failed to start harvest")
                                                  .setHeader("hash", clientData.hash)
                                                  .setHeader("clientId", std::to_string(clientId))
                                                  .setHeader("eventType", "harvestError")
                                                  .setBody("type", "HARVEST_ERROR")
                                                  .setBody("clientId", clientId)
                                                  .setBody("playerId", request.playerId)
                                                  .setBody("corpseId", request.corpseUID)
                                                  .setBody("errorCode", "HARVEST_FAILED")
                                                  .setBody("message", "Failed to start harvest")
                                                  .build();

                    std::string responseData = networkManager_.generateResponseMessage("error", response);
                    networkManager_.sendResponse(clientSocket, responseData);

                    log_->error("Failed to start harvest for player " + std::to_string(clientId));
                }
            }
        }
        else
        {
            log_->error("HarvestEventHandler: Invalid data type for harvest start request - expected HarvestRequestStruct");

            // Попробуем определить какой тип данных у нас есть
            if (std::holds_alternative<nlohmann::json>(data))
            {
                log_->error("HarvestEventHandler: Event contains JSON instead of HarvestRequestStruct");
            }
            else if (std::holds_alternative<CharacterDataStruct>(data))
            {
                log_->error("HarvestEventHandler: Event contains CharacterDataStruct instead of HarvestRequestStruct");
            }
            else
            {
                log_->error("HarvestEventHandler: Event contains unknown data type");
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleHarvestStartRequest: " + std::string(e.what()));
    }
}

void
HarvestEventHandler::handleGetNearbyCorpses(const Event &event)
{
    log_->info("Handling get nearby corpses request");

    try
    {
        int clientId = event.getClientID();

        // Получаем игрока
        auto player = gameServices_.getCharacterManager().getCharacterById(clientId);
        if (player.characterId == 0)
        {
            log_->error("Player not found for nearby corpses request: " + std::to_string(clientId));
            return;
        }

        auto &harvestManager = gameServices_.getHarvestManager();

        // Получаем ближайшие трупы
        PositionStruct playerPosition = player.characterPosition;

        auto nearbyCorpses = harvestManager.getHarvestableCorpsesNearPosition(playerPosition, 5.0f);

        // Отправляем ответ клиенту
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (clientSocket)
        {
            nlohmann::json corpsesArray = nlohmann::json::array();
            for (const auto &corpse : nearbyCorpses)
            {
                nlohmann::json corpseJson;
                corpseJson["id"] = corpse.mobUID;
                corpseJson["mobId"] = corpse.mobId;
                corpseJson["positionX"] = corpse.position.positionX;
                corpseJson["positionY"] = corpse.position.positionY;
                corpseJson["hasBeenHarvested"] = corpse.hasBeenHarvested;
                corpseJson["harvestedByCharacterId"] = corpse.harvestedByCharacterId;
                corpseJson["currentHarvesterCharacterId"] = corpse.currentHarvesterCharacterId;
                corpseJson["isBeingHarvested"] = (corpse.currentHarvesterCharacterId != 0);
                corpsesArray.push_back(corpseJson);
            }

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Nearby corpses retrieved")
                                          .setHeader("clientId", std::to_string(clientId))
                                          .setHeader("eventType", "nearbyCorpsesResponse")
                                          .setBody("corpses", corpsesArray)
                                          .setBody("count", nearbyCorpses.size())
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);
        }

        gameServices_.getLogger().log("Sent " + std::to_string(nearbyCorpses.size()) + " nearby corpses to player " + std::to_string(clientId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleGetNearbyCorpses: " + std::string(e.what()));
    }
}

void
HarvestEventHandler::handleHarvestCancel(const Event &event)
{
    log_->info("Handling harvest cancel request");

    try
    {
        int clientId = event.getClientID();

        // Получаем игрока
        auto player = gameServices_.getCharacterManager().getCharacterById(clientId);
        if (player.characterId == 0)
        {
            log_->error("Player not found for harvest cancel request: " + std::to_string(clientId));
            return;
        }

        auto &harvestManager = gameServices_.getHarvestManager();

        // Получаем информацию о текущем харвестинге для корректного ответа
        auto harvestProgress = harvestManager.getHarvestProgress(clientId);
        int corpseId = harvestProgress.corpseUID;

        harvestManager.cancelHarvest(clientId);

        // Отправляем ответ клиенту
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (clientSocket)
        {
            auto clientData = gameServices_.getClientManager().getClientData(clientId);

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Harvest cancelled")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", std::to_string(clientId))
                                          .setHeader("eventType", "harvestCancelled")
                                          .setBody("type", "HARVEST_CANCELLED")
                                          .setBody("clientId", clientId)
                                          .setBody("corpseId", corpseId)
                                          .setBody("reason", "MANUAL_CANCEL")
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);

            log_->info("Harvest cancelled for player " + std::to_string(clientId));
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleHarvestCancel: " + std::string(e.what()));
    }
}

void
HarvestEventHandler::handleHarvestComplete(int playerId, int corpseId)
{
    log_->info("Handling harvest completion for player " + std::to_string(playerId));

    try
    {
        auto &harvestManager = gameServices_.getHarvestManager();
        auto &itemManager = gameServices_.getItemManager();

        // Завершаем харвест и генерируем лут (без добавления в инвентарь)
        auto harvestLoot = harvestManager.completeHarvestAndGenerateLoot(playerId);

        if (harvestLoot.empty())
        {
            log_->info("No loot generated for harvest completion by player " + std::to_string(playerId));
            // Все равно отправляем ответ о завершении харвеста, даже если лута нет
        }

        // Отправляем результат клиенту с информацией о доступном луте
        auto clientSocket = gameServices_.getClientManager().getClientSocket(playerId);
        if (clientSocket)
        {
            auto clientData = gameServices_.getClientManager().getClientData(playerId);

            nlohmann::json itemsArray = nlohmann::json::array();

            for (const auto &[itemId, quantity] : harvestLoot)
            {
                ItemDataStruct itemInfo = itemManager.getItemById(itemId);
                nlohmann::json itemData;
                itemData["itemId"] = itemId;
                itemData["itemSlug"] = itemInfo.slug;
                itemData["quantity"] = quantity;
                itemData["name"] = itemInfo.slug;
                itemData["description"] = "";
                itemData["rarityId"] = itemInfo.rarityId;
                itemData["rarityName"] = itemInfo.rarityName;
                itemData["itemType"] = itemInfo.itemTypeName;
                itemData["weight"] = itemInfo.weight;
                itemData["addedToInventory"] = false; // Изменили на false - лут не добавлен в инвентарь
                itemData["isHarvestItem"] = itemInfo.isHarvest;
                itemsArray.push_back(itemData);
            }

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Harvest completed - loot available for pickup")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", std::to_string(playerId))
                                          .setHeader("eventType", "harvestComplete")
                                          .setBody("type", "HARVEST_COMPLETE")
                                          .setBody("clientId", playerId)
                                          .setBody("playerId", playerId)
                                          .setBody("corpseId", corpseId)
                                          .setBody("success", true)
                                          .setBody("totalItems", harvestLoot.size())
                                          .setBody("availableLoot", itemsArray) // Переименовали в availableLoot
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);

            gameServices_.getLogger().log("Harvest completed for player " + std::to_string(playerId) +
                                          " on corpse " + std::to_string(corpseId) +
                                          ", generated " + std::to_string(harvestLoot.size()) + " loot items for pickup");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleHarvestComplete: " + std::string(e.what()));
    }
}

void
HarvestEventHandler::handleCorpseLootPickup(const CorpseLootPickupRequestStruct &pickupRequest)
{
    log_->info("Handling corpse loot pickup for player " + std::to_string(pickupRequest.characterId));

    try
    {
        auto &harvestManager = gameServices_.getHarvestManager();
        auto &itemManager = gameServices_.getItemManager();

        // Валидация playerId (сверяем с characterId из сессии)
        if (pickupRequest.playerId != pickupRequest.characterId)
        {
            log_->error("Security violation: playerId mismatch in corpse loot pickup");

            auto clientSocket = gameServices_.getClientManager().getClientSocket(pickupRequest.characterId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(pickupRequest.characterId);
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Security violation: player ID mismatch")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(pickupRequest.characterId))
                                                   .setHeader("eventType", "corpseLootPickup")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "SECURITY_VIOLATION")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Получаем позицию игрока из CharacterManager
        PositionStruct playerPosition;
        try
        {
            playerPosition = gameServices_.getCharacterManager().getCharacterPosition(pickupRequest.characterId);
            gameServices_.getLogger().log("Retrieved player position for character ID: " + std::to_string(pickupRequest.characterId) +
                                          " - X: " + std::to_string(playerPosition.positionX) +
                                          ", Y: " + std::to_string(playerPosition.positionY));
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Failed to get player position for character ID " + std::to_string(pickupRequest.characterId) + ": " + std::string(e.what()));
            // Используем позицию по умолчанию если не удалось получить позицию игрока
            playerPosition = {0.0f, 0.0f, 0.0f, 0.0f};
        }

        // Проверяем существование трупа
        auto corpse = harvestManager.getCorpseByUID(pickupRequest.corpseUID);
        if (corpse.mobUID == 0)
        {
            log_->error("Corpse not found for loot pickup: " + std::to_string(pickupRequest.corpseUID));

            auto clientSocket = gameServices_.getClientManager().getClientSocket(pickupRequest.characterId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(pickupRequest.characterId);
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Corpse not found")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(pickupRequest.characterId))
                                                   .setHeader("eventType", "corpseLootPickup")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "CORPSE_NOT_FOUND")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Пытаемся подобрать лут
        auto [success, pickedUpItems] = harvestManager.pickupCorpseLoot(
            pickupRequest.characterId,
            pickupRequest.corpseUID,
            pickupRequest.requestedItems,
            playerPosition);

        // Отправляем ответ клиенту
        auto clientSocket = gameServices_.getClientManager().getClientSocket(pickupRequest.characterId);
        if (clientSocket)
        {
            auto clientData = gameServices_.getClientManager().getClientData(pickupRequest.characterId);

            if (success && !pickedUpItems.empty())
            {
                // Успешный подбор
                nlohmann::json itemsArray = nlohmann::json::array();
                for (const auto &[itemId, quantity] : pickedUpItems)
                {
                    ItemDataStruct itemInfo = itemManager.getItemById(itemId);
                    nlohmann::json itemData;
                    itemData["itemId"] = itemId;
                    itemData["itemSlug"] = itemInfo.slug;
                    itemData["quantity"] = quantity;
                    itemData["name"] = itemInfo.slug;
                    itemData["description"] = "";
                    itemData["rarityId"] = itemInfo.rarityId;
                    itemData["rarityName"] = itemInfo.rarityName;
                    itemData["itemType"] = itemInfo.itemTypeName;
                    itemData["weight"] = itemInfo.weight;
                    itemsArray.push_back(itemData);
                }

                // Получаем оставшийся лут в трупе
                auto remainingLoot = harvestManager.getCorpseLoot(pickupRequest.corpseUID);
                nlohmann::json remainingLootArray = nlohmann::json::array();
                for (const auto &[itemId, quantity] : remainingLoot)
                {
                    ItemDataStruct itemInfo = itemManager.getItemById(itemId);
                    nlohmann::json itemData;
                    itemData["itemId"] = itemId;
                    itemData["itemSlug"] = itemInfo.slug;
                    itemData["quantity"] = quantity;
                    itemData["name"] = itemInfo.slug;
                    itemData["description"] = "";
                    itemData["rarityId"] = itemInfo.rarityId;
                    itemData["rarityName"] = itemInfo.rarityName;
                    itemData["itemType"] = itemInfo.itemTypeName;
                    itemData["weight"] = itemInfo.weight;
                    remainingLootArray.push_back(itemData);
                }

                nlohmann::json successResponse = ResponseBuilder()
                                                     .setHeader("message", "Items picked up successfully")
                                                     .setHeader("hash", clientData.hash)
                                                     .setHeader("clientId", std::to_string(pickupRequest.characterId))
                                                     .setHeader("eventType", "corpseLootPickup")
                                                     .setBody("success", true)
                                                     .setBody("corpseUID", pickupRequest.corpseUID)
                                                     .setBody("pickedUpItems", itemsArray)
                                                     .setBody("remainingLoot", remainingLootArray)
                                                     .setBody("itemsPickedUp", itemsArray.size())
                                                     .build();

                std::string responseData = networkManager_.generateResponseMessage("success", successResponse);
                networkManager_.sendResponse(clientSocket, responseData);

                // Send item_received notification for each picked up item
                for (const auto &[itemId, quantity] : pickedUpItems)
                {
                    ItemDataStruct itemInfo = itemManager.getItemById(itemId);
                    nlohmann::json notifBody;
                    notifBody["type"] = "item_received";
                    notifBody["itemId"] = itemId;
                    notifBody["item_slug"] = itemInfo.slug;
                    notifBody["quantity"] = quantity;
                    nlohmann::json notifResp = ResponseBuilder().setHeader("eventType", "item_received").build();
                    notifResp["body"] = std::move(notifBody);
                    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", notifResp));
                }

                gameServices_.getLogger().log("Player " + std::to_string(pickupRequest.characterId) +
                                              " picked up " + std::to_string(pickedUpItems.size()) +
                                              " items from corpse " + std::to_string(pickupRequest.corpseUID));
            }
            else
            {
                // Неудачный подбор
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Failed to pickup items")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(pickupRequest.characterId))
                                                   .setHeader("eventType", "corpseLootPickup")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "PICKUP_FAILED")
                                                   .setBody("corpseUID", pickupRequest.corpseUID)
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);

                log_->error("Failed to pickup loot for player " + std::to_string(pickupRequest.characterId));
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleCorpseLootPickup: " + std::string(e.what()));
    }
}

void
HarvestEventHandler::handleCorpseLootInspect(const CorpseLootInspectRequestStruct &inspectRequest)
{
    log_->info("Handling corpse loot inspect for player " + std::to_string(inspectRequest.characterId));

    try
    {
        auto &harvestManager = gameServices_.getHarvestManager();
        auto &itemManager = gameServices_.getItemManager();

        // Валидация playerId (сверяем с characterId из сессии)
        if (inspectRequest.playerId != inspectRequest.characterId)
        {
            log_->error("Security violation: playerId mismatch in corpse loot inspect");

            auto clientSocket = gameServices_.getClientManager().getClientSocket(inspectRequest.characterId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(inspectRequest.characterId);
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Security violation: player ID mismatch")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(inspectRequest.characterId))
                                                   .setHeader("eventType", "corpseLootInspect")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "SECURITY_VIOLATION")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Получаем позицию игрока (временно используем позицию трупа, в реальной реализации нужно получать из ClientManager)
        auto corpse = harvestManager.getCorpseByUID(inspectRequest.corpseUID);
        if (corpse.mobUID == 0)
        {
            log_->error("Corpse not found for loot inspect: " + std::to_string(inspectRequest.corpseUID));

            auto clientSocket = gameServices_.getClientManager().getClientSocket(inspectRequest.characterId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(inspectRequest.characterId);
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Corpse not found")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(inspectRequest.characterId))
                                                   .setHeader("eventType", "corpseLootInspect")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "CORPSE_NOT_FOUND")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Проверяем что труп был заха рвещен
        if (!corpse.hasBeenHarvested)
        {
            log_->error("Cannot inspect loot from non-harvested corpse: " + std::to_string(inspectRequest.corpseUID));

            auto clientSocket = gameServices_.getClientManager().getClientSocket(inspectRequest.characterId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(inspectRequest.characterId);
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Corpse has not been harvested yet")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(inspectRequest.characterId))
                                                   .setHeader("eventType", "corpseLootInspect")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "CORPSE_NOT_HARVESTED")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Проверяем что игрок является владельцем харвеста
        if (corpse.harvestedByCharacterId != inspectRequest.characterId)
        {
            gameServices_.getLogger().logError("Player " + std::to_string(inspectRequest.characterId) +
                                               " tried to inspect loot from corpse " + std::to_string(inspectRequest.corpseUID) +
                                               " harvested by player " + std::to_string(corpse.harvestedByCharacterId));

            auto clientSocket = gameServices_.getClientManager().getClientSocket(inspectRequest.characterId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(inspectRequest.characterId);
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "You can only inspect loot from corpses you harvested")
                                                   .setHeader("hash", clientData.hash)
                                                   .setHeader("clientId", std::to_string(inspectRequest.characterId))
                                                   .setHeader("eventType", "corpseLootInspect")
                                                   .setBody("success", false)
                                                   .setBody("errorCode", "NOT_YOUR_HARVEST")
                                                   .build();

                std::string responseData = networkManager_.generateResponseMessage("error", errorResponse);
                networkManager_.sendResponse(clientSocket, responseData);
            }
            return;
        }

        // Получаем доступный лут в трупе
        auto availableLoot = harvestManager.getCorpseLoot(inspectRequest.corpseUID);

        // Отправляем ответ клиенту
        auto clientSocket = gameServices_.getClientManager().getClientSocket(inspectRequest.characterId);
        if (clientSocket)
        {
            auto clientData = gameServices_.getClientManager().getClientData(inspectRequest.characterId);

            // Формируем список лута
            nlohmann::json lootArray = nlohmann::json::array();
            for (const auto &[itemId, quantity] : availableLoot)
            {
                ItemDataStruct itemInfo = itemManager.getItemById(itemId);
                nlohmann::json itemData;
                itemData["itemId"] = itemId;
                itemData["itemSlug"] = itemInfo.slug;
                itemData["quantity"] = quantity;
                itemData["name"] = itemInfo.slug;
                itemData["description"] = "";
                itemData["rarityId"] = itemInfo.rarityId;
                itemData["rarityName"] = itemInfo.rarityName;
                itemData["itemType"] = itemInfo.itemTypeName;
                itemData["weight"] = itemInfo.weight;
                itemData["isHarvestItem"] = itemInfo.isHarvest;
                lootArray.push_back(itemData);
            }

            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Corpse loot retrieved successfully")
                                                 .setHeader("hash", clientData.hash)
                                                 .setHeader("clientId", std::to_string(inspectRequest.characterId))
                                                 .setHeader("eventType", "corpseLootInspect")
                                                 .setBody("success", true)
                                                 .setBody("corpseUID", inspectRequest.corpseUID)
                                                 .setBody("availableLoot", lootArray)
                                                 .setBody("totalItems", availableLoot.size())
                                                 .setBody("type", "CORPSE_LOOT_INSPECT")
                                                 .build();

            std::string responseData = networkManager_.generateResponseMessage("success", successResponse);
            networkManager_.sendResponse(clientSocket, responseData);

            gameServices_.getLogger().log("Player " + std::to_string(inspectRequest.characterId) +
                                          " inspected loot from corpse " + std::to_string(inspectRequest.corpseUID) +
                                          " - found " + std::to_string(availableLoot.size()) + " items");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleCorpseLootInspect: " + std::string(e.what()));
    }
}
