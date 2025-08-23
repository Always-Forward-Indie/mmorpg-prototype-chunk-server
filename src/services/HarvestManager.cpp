#include "services/HarvestManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "network/NetworkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/InventoryManager.hpp"
#include "utils/ResponseBuilder.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

HarvestManager::HarvestManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger), eventQueue_(nullptr),
      inventoryManager_(nullptr), clientManager_(nullptr), networkManager_(nullptr), randomGenerator_(randomDevice_())
{
}

void
HarvestManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
}

void
HarvestManager::setInventoryManager(InventoryManager *inventoryManager)
{
    inventoryManager_ = inventoryManager;
}

void
HarvestManager::setManagerReferences(ClientManager *clientManager, NetworkManager *networkManager)
{
    clientManager_ = clientManager;
    networkManager_ = networkManager;
}

void
HarvestManager::registerCorpse(int mobUID, int mobId, const PositionStruct &position)
{
    std::unique_lock<std::shared_mutex> lock(corpsesMutex_);

    HarvestableCorpseStruct corpse;
    corpse.mobUID = mobUID;
    corpse.mobId = mobId;
    corpse.position = position;
    corpse.deathTime = std::chrono::steady_clock::now();
    corpse.hasBeenHarvested = false;
    corpse.harvestedByCharacterId = 0;
    corpse.currentHarvesterCharacterId = 0;
    corpse.interactionRadius = DEFAULT_INTERACTION_RADIUS;

    harvestableCorpses_[mobUID] = corpse;

    logger_.log("[HARVEST] Registered corpse for mobUID: " + std::to_string(mobUID) +
                " at position (" + std::to_string(position.positionX) + ", " +
                std::to_string(position.positionY) + ", " + std::to_string(position.positionZ) + ")");
}

bool
HarvestManager::startHarvest(int characterId, int corpseUID, const PositionStruct &playerPosition)
{
    // Validate harvest prerequisites
    auto validation = validateHarvest(characterId, corpseUID, playerPosition);
    if (!validation.isValid)
    {
        logger_.logError("[HARVEST] Harvest validation failed for character " +
                         std::to_string(characterId) + ": " + validation.failureReason);
        return false;
    }

    // Check if character is already harvesting
    {
        std::shared_lock<std::shared_mutex> harvestLock(harvestsMutex_);
        if (activeHarvests_.find(characterId) != activeHarvests_.end())
        {
            logger_.logError("[HARVEST] Character " + std::to_string(characterId) +
                             " is already harvesting");
            return false;
        }
    }

    // Check if corpse is already being harvested by someone else
    {
        std::unique_lock<std::shared_mutex> corpseLock(corpsesMutex_);
        auto corpseIt = harvestableCorpses_.find(corpseUID);
        if (corpseIt != harvestableCorpses_.end())
        {
            if (corpseIt->second.currentHarvesterCharacterId != 0 &&
                corpseIt->second.currentHarvesterCharacterId != characterId)
            {
                logger_.logError("[HARVEST] Corpse " + std::to_string(corpseUID) +
                                 " is already being harvested by character " +
                                 std::to_string(corpseIt->second.currentHarvesterCharacterId));
                return false;
            }

            // Set current harvester
            corpseIt->second.currentHarvesterCharacterId = characterId;
        }
    }

    // Start harvest
    {
        std::unique_lock<std::shared_mutex> harvestLock(harvestsMutex_);

        HarvestProgressStruct harvest;
        harvest.characterId = characterId;
        harvest.corpseUID = corpseUID;
        harvest.startTime = std::chrono::steady_clock::now();
        harvest.harvestDuration = DEFAULT_HARVEST_DURATION;
        harvest.isActive = true;
        harvest.startPosition = playerPosition;
        harvest.maxMoveDistance = DEFAULT_MAX_MOVE_DISTANCE;

        activeHarvests_[characterId] = harvest;

        logger_.log("[HARVEST] Added active harvest for character " + std::to_string(characterId) +
                    " on corpse " + std::to_string(corpseUID) +
                    ". Active harvests count: " + std::to_string(activeHarvests_.size()));
    }

    // Broadcast harvest start to all clients
    try
    {
        logger_.log("[HARVEST] Attempting to broadcast harvest start", GREEN);
        broadcastHarvestStart(characterId, corpseUID, playerPosition);
        logger_.log("[HARVEST] Successfully broadcasted harvest start", GREEN);
    }
    catch (const std::exception &e)
    {
        logger_.logError("[HARVEST] Exception in broadcastHarvestStart call: " + std::string(e.what()));
    }

    logger_.log("[HARVEST] Started harvest for character " + std::to_string(characterId) +
                " on corpse " + std::to_string(corpseUID));

    return true;
}

void
HarvestManager::updateHarvestProgress()
{
    std::vector<int> toComplete;
    std::vector<int> toCancel;

    // Check all active harvests
    {
        std::shared_lock<std::shared_mutex> lock(harvestsMutex_);
        auto now = std::chrono::steady_clock::now();

        for (const auto &[characterId, harvest] : activeHarvests_)
        {
            if (!harvest.isActive)
                continue;

            auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(
                now - harvest.startTime)
                               .count();

            if (elapsed >= harvest.harvestDuration)
            {
                toComplete.push_back(characterId);
            }
            // Прогресс клиент считает сам, отправляем только при завершении
        }
    }

    // Complete harvests that are ready
    for (int characterId : toComplete)
    {
        std::unique_lock<std::shared_mutex> lock(harvestsMutex_);
        auto it = activeHarvests_.find(characterId);
        if (it != activeHarvests_.end())
        {
            int corpseUID = it->second.corpseUID;

            // Mark harvest as completed but don't remove it yet - it will be removed in completeHarvestAndGenerateLoot
            it->second.isActive = false;

            logger_.log("[HARVEST] Marking harvest as completed for character " + std::to_string(characterId) +
                        " on corpse " + std::to_string(corpseUID));

            // Send harvest complete event instead of calling completeHarvest directly
            if (eventQueue_)
            {
                HarvestCompleteStruct completeData;
                completeData.playerId = characterId;
                completeData.corpseId = corpseUID;

                Event completeEvent(Event::HARVEST_COMPLETE, characterId, completeData);
                eventQueue_->push(completeEvent);

                logger_.log("[HARVEST] Sent HARVEST_COMPLETE event for player " +
                            std::to_string(characterId) + " on corpse " + std::to_string(corpseUID));
            }
        }
    }
}

void
HarvestManager::cancelHarvest(int characterId, const std::string &reason)
{
    int corpseUID = 0;

    // Get corpse UID and remove harvest
    {
        std::unique_lock<std::shared_mutex> lock(harvestsMutex_);
        auto it = activeHarvests_.find(characterId);

        if (it != activeHarvests_.end())
        {
            corpseUID = it->second.corpseUID;
            logger_.log("[HARVEST] Cancelled harvest for character " + std::to_string(characterId) +
                        (reason.empty() ? "" : " - Reason: " + reason));
            activeHarvests_.erase(it);
        }
    }

    // Clear current harvester from corpse
    if (corpseUID != 0)
    {
        std::unique_lock<std::shared_mutex> corpseLock(corpsesMutex_);
        auto corpseIt = harvestableCorpses_.find(corpseUID);
        if (corpseIt != harvestableCorpses_.end() &&
            corpseIt->second.currentHarvesterCharacterId == characterId)
        {
            corpseIt->second.currentHarvesterCharacterId = 0;
        }

        // Broadcast harvest cancellation to all clients
        broadcastHarvestCancel(characterId, corpseUID, reason);
    }
}

bool
HarvestManager::isCharacterHarvesting(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(harvestsMutex_);
    auto it = activeHarvests_.find(characterId);
    return it != activeHarvests_.end() && it->second.isActive;
}

HarvestProgressStruct
HarvestManager::getHarvestProgress(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(harvestsMutex_);
    auto it = activeHarvests_.find(characterId);
    return (it != activeHarvests_.end()) ? it->second : HarvestProgressStruct{};
}

std::vector<HarvestableCorpseStruct>
HarvestManager::getHarvestableCorpsesNearPosition(
    const PositionStruct &position, float radius) const
{
    std::vector<HarvestableCorpseStruct> nearbyCorpses;
    std::shared_lock<std::shared_mutex> lock(corpsesMutex_);

    for (const auto &[uid, corpse] : harvestableCorpses_)
    {
        if (!corpse.hasBeenHarvested &&
            calculateDistance(position, corpse.position) <= radius)
        {
            nearbyCorpses.push_back(corpse);
        }
    }

    return nearbyCorpses;
}

HarvestableCorpseStruct
HarvestManager::getCorpseByUID(int corpseUID) const
{
    std::shared_lock<std::shared_mutex> lock(corpsesMutex_);
    auto it = harvestableCorpses_.find(corpseUID);
    return (it != harvestableCorpses_.end()) ? it->second : HarvestableCorpseStruct{};
}

void
HarvestManager::cleanupOldCorpses(int maxAgeSeconds)
{
    std::vector<int> corpsesToCleanup;

    // Собираем список трупов для очистки
    {
        std::unique_lock<std::shared_mutex> lock(corpsesMutex_);
        auto now = std::chrono::steady_clock::now();
        auto maxAge = std::chrono::seconds(maxAgeSeconds);

        auto it = harvestableCorpses_.begin();
        while (it != harvestableCorpses_.end())
        {
            if (now - it->second.deathTime > maxAge)
            {
                corpsesToCleanup.push_back(it->first);
                logger_.log("[HARVEST] Cleaned up old corpse: " + std::to_string(it->first));
                it = harvestableCorpses_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Очищаем лут для удаленных трупов
    if (!corpsesToCleanup.empty())
    {
        std::unique_lock<std::shared_mutex> lootLock(lootMutex_);
        for (int corpseUID : corpsesToCleanup)
        {
            auto lootIt = corpseLoot_.find(corpseUID);
            if (lootIt != corpseLoot_.end())
            {
                logger_.log("[HARVEST] Cleaned up loot for corpse: " + std::to_string(corpseUID));
                corpseLoot_.erase(lootIt);
            }
        }
    }
}

HarvestManager::HarvestValidationResult
HarvestManager::validateHarvest(
    int characterId, int corpseUID, const PositionStruct &playerPosition) const
{
    HarvestValidationResult result;

    // Check if corpse exists
    std::shared_lock<std::shared_mutex> corpselock(corpsesMutex_);
    auto corpseIt = harvestableCorpses_.find(corpseUID);
    if (corpseIt == harvestableCorpses_.end())
    {
        result.failureReason = "Corpse not found";
        return result;
    }

    const auto &corpse = corpseIt->second;

    // Check if corpse has already been harvested
    if (corpse.hasBeenHarvested)
    {
        result.failureReason = "Corpse has already been harvested";
        return result;
    }

    // Check distance
    float distance = calculateDistance(playerPosition, corpse.position);
    if (distance > corpse.interactionRadius)
    {
        result.failureReason = "Too far from corpse (distance: " +
                               std::to_string(distance) + ", max: " +
                               std::to_string(corpse.interactionRadius) + ")";
        return result;
    }

    result.isValid = true;
    return result;
}

std::vector<std::pair<int, int>>
HarvestManager::generateHarvestLoot(int mobId)
{
    std::vector<std::pair<int, int>> loot;

    // Get all loot info for this mob
    std::vector<MobLootInfoStruct> allLootInfo = itemManager_.getLootForMob(mobId);

    for (const auto &lootInfo : allLootInfo)
    {
        // Get item info to check if it's a harvest-only item
        ItemDataStruct itemInfo = itemManager_.getItemById(lootInfo.itemId);

        // Skip items that are not harvest-only items
        if (!itemInfo.isHarvest)
            continue;

        // Roll for drop chance using the item's specific drop chance
        std::uniform_real_distribution<float> dis(0.0f, 1.0f);
        float roll = dis(randomGenerator_);

        if (roll <= lootInfo.dropChance)
        {
            // Determine quantity (could be enhanced with quantity ranges)
            int quantity = 1; // For now, always 1
            loot.emplace_back(lootInfo.itemId, quantity);

            logger_.log("[HARVEST] Generated harvest loot: " + std::to_string(quantity) +
                        "x " + itemInfo.name + " (chance: " +
                        std::to_string(lootInfo.dropChance * 100) + "%, roll: " +
                        std::to_string(roll * 100) + "%)");
        }
        else
        {
            logger_.log("[HARVEST] Failed to generate loot for " + itemInfo.name +
                        " (chance: " + std::to_string(lootInfo.dropChance * 100) +
                        "%, roll: " + std::to_string(roll * 100) + "%)");
        }
    }

    return loot;
}

std::vector<std::pair<int, int>>
HarvestManager::completeHarvestAndGenerateLoot(int characterId)
{
    logger_.log("[HARVEST] Attempting to complete harvest for character " + std::to_string(characterId));

    std::vector<std::pair<int, int>> harvestLoot;

    // Получаем информацию о текущем харвесте
    HarvestProgressStruct harvestProgress;
    {
        std::shared_lock<std::shared_mutex> lock(harvestsMutex_);

        logger_.log("[HARVEST] Checking active harvests. Total active harvests: " + std::to_string(activeHarvests_.size()));

        auto it = activeHarvests_.find(characterId);
        if (it == activeHarvests_.end())
        {
            logger_.logError("[HARVEST] No harvest record found for character " + std::to_string(characterId));

            // Log all active harvests for debugging
            for (const auto &[charId, harvest] : activeHarvests_)
            {
                logger_.log("[HARVEST] Harvest record found for character " + std::to_string(charId) +
                            ", isActive: " + (harvest.isActive ? "true" : "false"));
            }

            return harvestLoot;
        }

        if (it->second.isActive)
        {
            logger_.logError("[HARVEST] Harvest for character " + std::to_string(characterId) + " is still active (not completed yet)");
            return harvestLoot;
        }

        harvestProgress = it->second;

        logger_.log("[HARVEST] Found completed harvest for character " + std::to_string(characterId) +
                    " on corpse " + std::to_string(harvestProgress.corpseUID));
    }

    // Получаем информацию о трупе
    auto corpse = getCorpseByUID(harvestProgress.corpseUID);
    if (corpse.mobUID == 0)
    {
        logger_.logError("[HARVEST] Corpse not found for completion: " + std::to_string(harvestProgress.corpseUID));
        return harvestLoot;
    }

    // Генерируем лут
    harvestLoot = generateHarvestLoot(corpse.mobId);

    // Помечаем труп как заха рвещенный и очищаем текущего харвестера
    {
        std::unique_lock<std::shared_mutex> corpselock(corpsesMutex_);
        auto corpseIt = harvestableCorpses_.find(harvestProgress.corpseUID);
        if (corpseIt != harvestableCorpses_.end())
        {
            corpseIt->second.hasBeenHarvested = true;
            corpseIt->second.harvestedByCharacterId = characterId;
            corpseIt->second.currentHarvesterCharacterId = 0; // Clear current harvester
        }
    }

    // Сохраняем лут для трупа
    {
        std::unique_lock<std::shared_mutex> lootLock(lootMutex_);
        CorpseLootStruct corpseLootData;
        corpseLootData.corpseUID = harvestProgress.corpseUID;
        corpseLootData.availableLoot = harvestLoot;
        corpseLootData.generatedTime = std::chrono::steady_clock::now();

        corpseLoot_[harvestProgress.corpseUID] = corpseLootData;
    }

    // Удаляем активный харвест
    {
        std::unique_lock<std::shared_mutex> harvestLock(harvestsMutex_);
        activeHarvests_.erase(characterId);
    }

    // Broadcast harvest completion to all clients
    broadcastHarvestComplete(characterId, harvestProgress.corpseUID, harvestProgress.startPosition);

    logger_.log("[HARVEST] Completed harvest for character " + std::to_string(characterId) +
                " on corpse " + std::to_string(harvestProgress.corpseUID) +
                ", generated " + std::to_string(harvestLoot.size()) + " loot items");

    // Remove the completed harvest from active harvests
    activeHarvests_.erase(characterId);
    logger_.log("[HARVEST] Removed completed harvest for character " + std::to_string(characterId));

    return harvestLoot;
}

std::vector<std::pair<int, int>>
HarvestManager::getExpectedHarvestLoot(int mobId) const
{
    return const_cast<HarvestManager *>(this)->generateHarvestLoot(mobId);
}

std::pair<bool, std::vector<std::pair<int, int>>>
HarvestManager::pickupCorpseLoot(int characterId, int corpseUID, const std::vector<std::pair<int, int>> &requestedItems, const PositionStruct &playerPosition)
{
    logger_.log("[HARVEST] pickupCorpseLoot called for character " + std::to_string(characterId) +
                " corpse " + std::to_string(corpseUID));

    // Логируем все запрошенные предметы
    for (const auto &[itemId, quantity] : requestedItems)
    {
        logger_.log("[HARVEST] Requested item: " + std::to_string(itemId) + " quantity: " + std::to_string(quantity));
    }

    std::vector<std::pair<int, int>> successfulPickups;

    // Валидируем что труп существует и игрок рядом
    auto corpse = getCorpseByUID(corpseUID);
    if (corpse.mobUID == 0)
    {
        logger_.logError("[HARVEST] Corpse not found for loot pickup: " + std::to_string(corpseUID));
        return {false, successfulPickups};
    }

    // Проверяем дистанцию
    float distance = calculateDistance(playerPosition, corpse.position);
    if (distance > corpse.interactionRadius)
    {
        logger_.logError("[HARVEST] Player too far from corpse for loot pickup: " +
                         std::to_string(distance) + " > " + std::to_string(corpse.interactionRadius));
        return {false, successfulPickups};
    }

    // Проверяем что труп был заха рвещен
    if (!corpse.hasBeenHarvested)
    {
        logger_.logError("[HARVEST] Cannot pickup loot from non-harvested corpse: " + std::to_string(corpseUID));
        return {false, successfulPickups};
    }

    // Проверяем что игрок является владельцем харвеста
    if (corpse.harvestedByCharacterId != characterId)
    {
        logger_.logError("[HARVEST] Player " + std::to_string(characterId) +
                         " tried to pickup loot from corpse " + std::to_string(corpseUID) +
                         " harvested by player " + std::to_string(corpse.harvestedByCharacterId));
        return {false, successfulPickups};
    }

    // Получаем доступный лут для трупа
    std::unique_lock<std::shared_mutex> lootLock(lootMutex_);
    auto lootIt = corpseLoot_.find(corpseUID);
    if (lootIt == corpseLoot_.end())
    {
        logger_.logError("[HARVEST] No loot data found for corpse: " + std::to_string(corpseUID));
        return {false, successfulPickups};
    }

    auto &availableLoot = lootIt->second.availableLoot;

    // Валидируем каждый запрашиваемый айтем
    for (const auto &[requestedItemId, requestedQuantity] : requestedItems)
    {
        if (requestedQuantity <= 0)
        {
            logger_.logError("[HARVEST] Invalid quantity requested: " + std::to_string(requestedQuantity));
            continue;
        }

        // Ищем айтем в доступном луте
        auto lootItemIt = std::find_if(availableLoot.begin(), availableLoot.end(), [requestedItemId](const auto &lootItem)
            { return lootItem.first == requestedItemId; });

        if (lootItemIt == availableLoot.end())
        {
            logger_.logError("[HARVEST] Requested item not found in corpse loot: " + std::to_string(requestedItemId));
            continue;
        }

        // Проверяем что запрашиваемое количество не превышает доступное
        int availableQuantity = lootItemIt->second;
        int quantityToTake = std::min(requestedQuantity, availableQuantity);

        if (quantityToTake <= 0)
        {
            logger_.logError("[HARVEST] No quantity available for item: " + std::to_string(requestedItemId));
            continue;
        }

        // Пытаемся добавить в инвентарь
        if (inventoryManager_ && inventoryManager_->addItemToInventory(characterId, requestedItemId, quantityToTake))
        {
            // Успешно добавили в инвентарь - убираем из лута трупа
            lootItemIt->second -= quantityToTake;
            successfulPickups.emplace_back(requestedItemId, quantityToTake);

            // Если айтем полностью забрали, удаляем из лута
            if (lootItemIt->second <= 0)
            {
                availableLoot.erase(lootItemIt);
            }

            logger_.log("[HARVEST] Player " + std::to_string(characterId) +
                        " picked up " + std::to_string(quantityToTake) +
                        "x item " + std::to_string(requestedItemId) +
                        " from corpse " + std::to_string(corpseUID));
        }
        else
        {
            logger_.logError("[HARVEST] Failed to add item to inventory: " + std::to_string(requestedItemId));
        }
    }

    // Если лут закончился, удаляем запись о луте
    if (availableLoot.empty())
    {
        corpseLoot_.erase(lootIt);
        logger_.log("[HARVEST] All loot picked up from corpse: " + std::to_string(corpseUID));
    }

    // Логируем результат
    logger_.log("[HARVEST] pickupCorpseLoot completed with " + std::to_string(successfulPickups.size()) + " successful pickups");
    for (const auto &[itemId, quantity] : successfulPickups)
    {
        logger_.log("[HARVEST] Successfully picked up item: " + std::to_string(itemId) + " quantity: " + std::to_string(quantity));
    }

    return {!successfulPickups.empty(), successfulPickups};
}

std::vector<std::pair<int, int>>
HarvestManager::getCorpseLoot(int corpseUID) const
{
    std::shared_lock<std::shared_mutex> lootLock(lootMutex_);
    auto lootIt = corpseLoot_.find(corpseUID);

    if (lootIt != corpseLoot_.end())
    {
        return lootIt->second.availableLoot;
    }

    return {};
}

bool
HarvestManager::corpseHasLoot(int corpseUID) const
{
    std::shared_lock<std::shared_mutex> lootLock(lootMutex_);
    auto lootIt = corpseLoot_.find(corpseUID);

    return lootIt != corpseLoot_.end() && lootIt->second.hasRemainingLoot();
}

void
HarvestManager::broadcastHarvestStart(int characterId, int corpseUID, const PositionStruct &playerPosition)
{
    logger_.log("[HARVEST] Starting broadcastHarvestStart", GREEN);

    if (!clientManager_ || !networkManager_)
    {
        logger_.logError("[HARVEST] Cannot broadcast harvest start - managers not set");
        return;
    }

    try
    {
        logger_.log("[HARVEST] Getting clients list", GREEN);
        auto clientsList = clientManager_->getClientsListReadOnly();
        logger_.log("[HARVEST] Got " + std::to_string(clientsList.size()) + " clients", GREEN);

        logger_.log("[HARVEST] Creating broadcast message JSON", GREEN);

        // Создаем правильную структуру для generateResponseMessage
        nlohmann::json broadcastMessage;

        // Header для broadcast сообщения
        broadcastMessage["header"]["eventType"] = "harvestStartBroadcast";
        broadcastMessage["header"]["message"] = "Player started harvesting";

        // Body с данными о харвестинге
        broadcastMessage["body"]["type"] = "HARVEST_START_BROADCAST";
        broadcastMessage["body"]["characterId"] = characterId;
        broadcastMessage["body"]["corpseUID"] = corpseUID;

        // Создаем объект position
        nlohmann::json position;
        position["x"] = playerPosition.positionX;
        position["y"] = playerPosition.positionY;
        position["z"] = playerPosition.positionZ;
        broadcastMessage["body"]["position"] = position;

        broadcastMessage["body"]["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
                                                    .count();
        logger_.log("[HARVEST] Created broadcast message JSON", GREEN);

        logger_.log("[HARVEST] Generating response message", GREEN);
        std::string messageData;
        try
        {
            messageData = networkManager_->generateResponseMessage("success", broadcastMessage);
            logger_.log("[HARVEST] Generated response message successfully", GREEN);
        }
        catch (const std::exception &e)
        {
            logger_.logError("[HARVEST] Exception in generateResponseMessage: " + std::string(e.what()));
            return;
        }

        logger_.log("[HARVEST] Starting to send messages to clients", GREEN);
        for (const auto &client : clientsList)
        {
            logger_.log("[HARVEST] Processing client " + std::to_string(client.clientId), GREEN);
            auto clientSocket = clientManager_->getClientSocket(client.clientId);
            if (clientSocket)
            {
                logger_.log("[HARVEST] Sending message to client " + std::to_string(client.clientId), GREEN);
                networkManager_->sendResponse(clientSocket, messageData);
                logger_.log("[HARVEST] Sent message to client " + std::to_string(client.clientId), GREEN);
            }
            else
            {
                logger_.log("[HARVEST] No socket for client " + std::to_string(client.clientId), GREEN);
            }
        }

        logger_.log("[HARVEST] Broadcasted harvest start to " + std::to_string(clientsList.size()) + " clients");
    }
    catch (const std::exception &e)
    {
        logger_.logError("[HARVEST] Exception in broadcastHarvestStart: " + std::string(e.what()));
    }
}

void
HarvestManager::broadcastHarvestComplete(int characterId, int corpseUID, const PositionStruct &playerPosition)
{
    if (!clientManager_ || !networkManager_)
    {
        logger_.logError("[HARVEST] Cannot broadcast harvest complete - managers not set");
        return;
    }

    try
    {
        auto clientsList = clientManager_->getClientsListReadOnly();

        // Создаем правильную структуру для generateResponseMessage
        nlohmann::json broadcastMessage;

        // Header для broadcast сообщения
        broadcastMessage["header"]["eventType"] = "harvestCompleteBroadcast";
        broadcastMessage["header"]["message"] = "Player completed harvesting";

        // Body с данными о завершении харвестинга
        broadcastMessage["body"]["type"] = "HARVEST_COMPLETE_BROADCAST";
        broadcastMessage["body"]["characterId"] = characterId;
        broadcastMessage["body"]["corpseUID"] = corpseUID;

        // Создаем объект position
        nlohmann::json position;
        position["x"] = playerPosition.positionX;
        position["y"] = playerPosition.positionY;
        position["z"] = playerPosition.positionZ;
        broadcastMessage["body"]["position"] = position;

        broadcastMessage["body"]["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
                                                    .count();

        std::string messageData = networkManager_->generateResponseMessage("success", broadcastMessage);

        for (const auto &client : clientsList)
        {
            auto clientSocket = clientManager_->getClientSocket(client.clientId);
            if (clientSocket)
            {
                networkManager_->sendResponse(clientSocket, messageData);
            }
        }

        logger_.log("[HARVEST] Broadcasted harvest complete to " + std::to_string(clientsList.size()) + " clients");
    }
    catch (const std::exception &e)
    {
        logger_.logError("[HARVEST] Exception in broadcastHarvestComplete: " + std::string(e.what()));
    }
}

void
HarvestManager::broadcastHarvestCancel(int characterId, int corpseUID, const std::string &reason)
{
    if (!clientManager_ || !networkManager_)
    {
        logger_.logError("[HARVEST] Cannot broadcast harvest cancel - managers not set");
        return;
    }

    try
    {
        auto clientsList = clientManager_->getClientsListReadOnly();

        // Создаем правильную структуру для generateResponseMessage
        nlohmann::json broadcastMessage;

        // Header для broadcast сообщения
        broadcastMessage["header"]["eventType"] = "harvestCancelBroadcast";
        broadcastMessage["header"]["message"] = "Player cancelled harvesting";

        // Body с данными об отмене харвестинга
        broadcastMessage["body"]["type"] = "HARVEST_CANCEL_BROADCAST";
        broadcastMessage["body"]["characterId"] = characterId;
        broadcastMessage["body"]["corpseUID"] = corpseUID;
        broadcastMessage["body"]["reason"] = reason;
        broadcastMessage["body"]["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
                                                    .count();

        std::string messageData = networkManager_->generateResponseMessage("success", broadcastMessage);

        for (const auto &client : clientsList)
        {
            auto clientSocket = clientManager_->getClientSocket(client.clientId);
            if (clientSocket)
            {
                networkManager_->sendResponse(clientSocket, messageData);
            }
        }

        logger_.log("[HARVEST] Broadcasted harvest cancel to " + std::to_string(clientsList.size()) + " clients");
    }
    catch (const std::exception &e)
    {
        logger_.logError("[HARVEST] Exception in broadcastHarvestCancel: " + std::string(e.what()));
    }
}

float
HarvestManager::calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2) const
{
    float dx = pos1.positionX - pos2.positionX;
    float dy = pos1.positionY - pos2.positionY;
    float dz = pos1.positionZ - pos2.positionZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
