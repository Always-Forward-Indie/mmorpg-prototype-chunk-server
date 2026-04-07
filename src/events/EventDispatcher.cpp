#include "events/EventDispatcher.hpp"
#include "utils/JSONParser.hpp"
#include <boost/asio.hpp>
#include <spdlog/logger.h>

EventDispatcher::EventDispatcher(
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    ChunkServer *chunkServer,
    GameServices &gameServices)
    : eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      chunkServer_(chunkServer),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("events");
}

void
EventDispatcher::dispatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // CRITICAL-3 fix: eventsBatch_ is a shared member; serialise all dispatch() invocations
    // so concurrent io_context threads cannot race on it.
    std::lock_guard<std::mutex> dispatchLock(dispatchMutex_);

    if (context.eventType == "joinGameClient")
    {
        handleJoinGameClient(context, socket);
    }
    else if (context.eventType == "joinGameCharacter")
    {
        handleJoinGameCharacter(context, socket);
    }
    else if (context.eventType == "moveCharacter")
    {
        handleMoveCharacter(context, socket);
    }
    else if (context.eventType == "disconnectClient")
    {
        handleDisconnect(context, socket);
    }
    else if (context.eventType == "pingClient")
    {
        handlePing(context, socket);
    }
    else if (context.eventType == "getConnectedCharacters")
    {
        handleGetConnectedClients(context, socket);
    }
    else if (context.eventType == "playerAttack")
    {
        handlePlayerAttack(context, socket);
    }
    else if (context.eventType == "itemPickup")
    {
        handlePickupDroppedItem(context, socket);
    }
    else if (context.eventType == "getPlayerInventory")
    {
        handleGetPlayerInventory(context, socket);
    }
    else if (context.eventType == "harvestStart")
    {
        handleHarvestStart(context, socket);
    }
    else if (context.eventType == "harvestCancel")
    {
        handleHarvestCancel(context, socket);
    }
    else if (context.eventType == "getNearbyCorpses")
    {
        handleGetNearbyCorpses(context, socket);
    }
    else if (context.eventType == "corpseLootPickup")
    {
        handleCorpseLootPickup(context, socket);
    }
    else if (context.eventType == "corpseLootInspect")
    {
        handleCorpseLootInspect(context, socket);
    }
    else if (context.eventType == "getCharacterExperience")
    {
        handleGetCharacterExperience(context, socket);
    }
    else if (context.eventType == "npcInteract")
    {
        handleNPCInteract(context, socket);
    }
    else if (context.eventType == "dialogueChoice")
    {
        handleDialogueChoice(context, socket);
    }
    else if (context.eventType == "dialogueClose")
    {
        handleDialogueClose(context, socket);
    }
    else if (context.eventType == "openVendorShop")
    {
        handleOpenVendorShop(context, socket);
    }
    else if (context.eventType == "openSkillShop")
    {
        handleOpenSkillShop(context, socket);
    }
    else if (context.eventType == "requestLearnSkill")
    {
        handleRequestLearnSkill(context, socket);
    }
    else if (context.eventType == "buyItem")
    {
        handleBuyItem(context, socket);
    }
    else if (context.eventType == "sellItem")
    {
        handleSellItem(context, socket);
    }
    else if (context.eventType == "buyItemBatch")
    {
        handleBuyItemBatch(context, socket);
    }
    else if (context.eventType == "sellItemBatch")
    {
        handleSellItemBatch(context, socket);
    }
    else if (context.eventType == "openRepairShop")
    {
        handleOpenRepairShop(context, socket);
    }
    else if (context.eventType == "repairItem")
    {
        handleRepairItem(context, socket);
    }
    else if (context.eventType == "repairAll")
    {
        handleRepairAll(context, socket);
    }
    else if (context.eventType == "tradeRequest")
    {
        handleTradeRequest(context, socket);
    }
    else if (context.eventType == "tradeAccept")
    {
        handleTradeAccept(context, socket);
    }
    else if (context.eventType == "tradeDecline")
    {
        handleTradeDecline(context, socket);
    }
    else if (context.eventType == "tradeOfferUpdate")
    {
        handleTradeOfferUpdate(context, socket);
    }
    else if (context.eventType == "tradeConfirm")
    {
        handleTradeConfirm(context, socket);
    }
    else if (context.eventType == "tradeCancel")
    {
        handleTradeCancel(context, socket);
    }
    else if (context.eventType == "equipItem")
    {
        handleEquipItem(context, socket);
    }
    else if (context.eventType == "unequipItem")
    {
        handleUnequipItem(context, socket);
    }
    else if (context.eventType == "getEquipment")
    {
        handleGetEquipment(context, socket);
    }
    else if (context.eventType == "respawnRequest")
    {
        handleRespawnRequest(context, socket);
    }
    else if (context.eventType == "dropItem")
    {
        handleDropItemByPlayer(context, socket);
    }
    else if (context.eventType == "useItem")
    {
        handleUseItem(context, socket);
    }
    else if (context.eventType == "getBestiaryEntry")
    {
        handleGetBestiaryEntry(context, socket);
    }
    else if (context.eventType == "getBestiaryOverview")
    {
        handleGetBestiaryOverview(context, socket);
    }
    else if (context.eventType == "chatMessage")
    {
        handleChatMessage(context, socket);
    }
    else if (context.eventType == "playerReady")
    {
        handlePlayerReady(context, socket);
    }
    else if (context.eventType == "getTitles")
    {
        handleGetPlayerTitles(context, socket);
    }
    else if (context.eventType == "equipTitle")
    {
        handleEquipTitle(context, socket);
    }
    else
    {
        log_->error("Unknown event type: " + context.eventType);
    }

    // Push the batch of events to the queue
    if (!eventsBatch_.empty())
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();

        // Avoid calling shrink_to_fit immediately after clear to prevent undefined behavior
        if (eventsBatch_.capacity() > BATCH_SIZE)
        {
            eventsBatch_.shrink_to_fit();
        }

        log_->debug("Cleared eventsBatch_ vector");
    }

    // Reserve space for next batch to avoid frequent reallocations
    if (eventsBatch_.capacity() < BATCH_SIZE)
    {
        eventsBatch_.reserve(BATCH_SIZE);
    }

#ifdef DEBUG
    // MEDIUM-5: these per-dispatch size logs were fired on EVERY incoming message;
    //           gate them behind DEBUG to avoid hammering the logger in production.
    gameServices_.getLogger().log("eventsBatch_ size: " + std::to_string(eventsBatch_.size()), GREEN);
    gameServices_.getLogger().log("eventsBatch_ capacity: " + std::to_string(eventsBatch_.capacity()), GREEN);
#endif
}

void
EventDispatcher::handleJoinGameClient(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Create client data safely without copying invalid socket references
    ClientDataStruct clientData;
    clientData.clientId = context.clientData.clientId;
    clientData.accountId = context.clientData.clientId; // clientId == accountId (DB owner_id) in this system
    clientData.hash = context.clientData.hash;
    clientData.characterId = context.clientData.characterId;
    // Socket field removed from ClientDataStruct to prevent socket references in EventData

    // Validate the socket parameter before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Only create and queue the event if we have a valid socket
    if (validSocket)
    {
        // Register the socket in ClientManager for this client
        gameServices_.getClientManager().setClientSocket(context.clientData.clientId, validSocket);
        try
        {
            // Create event safely using in_place construction to avoid variant copying
            Event joinEvent(Event::JOIN_CLIENT, context.clientData.clientId, EventData{std::in_place_type<ClientDataStruct>, clientData});
            eventsBatch_.push_back(joinEvent); // Copy instead of move to avoid corruption
            if (eventsBatch_.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch_);
                eventsBatch_.clear();
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating join client event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        log_->info("Skipping join client event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handleJoinGameCharacter(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Create character data safely, avoiding copying invalid socket references from context
    CharacterDataStruct characterData;
    characterData.clientId = context.clientData.clientId;
    characterData.characterId = context.characterData.characterId;
    characterData.characterLevel = context.characterData.characterLevel;
    characterData.characterExperiencePoints = context.characterData.characterExperiencePoints;
    characterData.characterCurrentHealth = context.characterData.characterCurrentHealth;
    characterData.characterCurrentMana = context.characterData.characterCurrentMana;
    characterData.characterMaxHealth = context.characterData.characterMaxHealth;
    characterData.characterMaxMana = context.characterData.characterMaxMana;
    characterData.expForNextLevel = context.characterData.expForNextLevel;
    characterData.characterName = context.characterData.characterName;
    characterData.characterClass = context.characterData.characterClass;
    characterData.characterRace = context.characterData.characterRace;
    characterData.characterPosition = context.characterData.characterPosition;
    characterData.attributes = context.characterData.attributes;

    // Validate the socket parameter before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Only create and queue the event if we have a valid socket
    if (validSocket)
    {
        // Register the socket in ClientManager for this client (in case it wasn't already registered)
        gameServices_.getClientManager().setClientSocket(context.clientData.clientId, validSocket);
        try
        {
            // Create event safely using in_place construction to avoid variant copying
            Event joinCharacterEvent(Event::JOIN_CHARACTER, context.clientData.clientId, EventData{std::in_place_type<CharacterDataStruct>, characterData}, context.timestamps);
            eventsBatch_.push_back(joinCharacterEvent); // Copy instead of move to avoid corruption
            if (eventsBatch_.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch_);
                eventsBatch_.clear();
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating join character event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        log_->info("Skipping join character event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handleMoveCharacter(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Create lightweight movement data instead of full character data for movement events
    MovementDataStruct movementData;
    movementData.clientId = context.clientData.clientId;
    movementData.characterId = context.characterData.characterId;
    movementData.position = context.positionData;
    movementData.timestamps = context.timestamps; // Add timestamps for lag compensation

    // Log character data for debugging
    log_->info("Creating MOVE_CHARACTER event with movement data:");
    log_->info("Client ID: " + std::to_string(movementData.clientId));
    log_->info("Character ID: " + std::to_string(movementData.characterId));
    gameServices_.getLogger().log("Position: " + std::to_string(movementData.position.positionX) + ", " + std::to_string(movementData.position.positionY), GREEN);

    // Validate character data
    if (movementData.characterId <= 0)
    {
        log_->error("Invalid character data for MOVE_CHARACTER event");
        return;
    }

    // Validate the socket before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Only create and queue the event if we have a valid socket
    if (validSocket)
    {
        try
        {
            // Create event safely using emplace construction with lightweight movement data
            Event moveEvent(Event::MOVE_CHARACTER, context.clientData.clientId, EventData{std::in_place_type<MovementDataStruct>, std::move(movementData)}, context.timestamps);

            // Reserve space in batch to avoid reallocations
            if (eventsBatch_.capacity() < BATCH_SIZE)
            {
                eventsBatch_.reserve(BATCH_SIZE);
            }

            eventsBatch_.emplace_back(std::move(moveEvent)); // Use emplace_back with move to avoid copy
            if (eventsBatch_.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch_);
                eventsBatch_.clear();
                // Don't shrink capacity immediately after clearing to avoid frequent reallocations
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating move character event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        log_->info("Skipping move character event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handleDisconnect(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Create client data safely without copying invalid socket references
    ClientDataStruct clientData;
    clientData.clientId = context.clientData.clientId;
    clientData.hash = context.clientData.hash;
    clientData.characterId = context.characterData.characterId; // Set character ID from context
    // Socket field removed from ClientDataStruct to prevent socket references in EventData

    // Validate the socket parameter before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Always create disconnect event, even with invalid socket, as cleanup is needed
    try
    {
        // Create event safely using in_place construction to avoid variant copying
        Event disconnectEvent(Event::DISCONNECT_CLIENT, context.clientData.clientId, EventData{std::in_place_type<ClientDataStruct>, clientData});
        eventsBatch_.push_back(disconnectEvent); // Copy instead of move to avoid corruption

        if (eventsBatch_.size() >= BATCH_SIZE)
        {
            eventQueue_.pushBatch(eventsBatch_);
            eventsBatch_.clear();
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error creating disconnect event: " + std::string(e.what()), RED);
    }
}

void
EventDispatcher::handlePing(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Only process ping events for authenticated clients (clientId != 0) to prevent memory leaks
    if (context.clientData.clientId == 0)
    {
        static thread_local int logCounter = 0;
        if (++logCounter % 100 == 0) // Log every 100th occurrence
        {
            log_->info("Skipping ping event for unauthenticated client (logged every 100th occurrence)");
        }
        return;
    }

    // Validate the socket parameter before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Only queue ping event if we have a valid socket
    if (validSocket)
    {
        // Create client data safely without copying invalid socket references
        ClientDataStruct clientData;
        clientData.clientId = context.clientData.clientId;
        clientData.hash = context.clientData.hash;
        clientData.characterId = context.clientData.characterId;
        // Socket field removed from ClientDataStruct to prevent socket references in EventData

        try
        {
            Event pingEvent(Event::PING_CLIENT, context.clientData.clientId, std::move(clientData), context.timestamps);
            eventQueuePing_.push(pingEvent);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating ping event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping ping for a disconnected client
        log_->info("Skipping ping event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handleGetConnectedClients(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Validate the socket parameter before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Only create and queue the event if we have a valid socket
    if (validSocket)
    {
        // Pass a simple request instead of full client list to prevent memory leaks
        std::string requestType = "getConnectedClients";
        Event getConnectedClientsEvent(Event::GET_CONNECTED_CHARACTERS, context.clientData.clientId, requestType, context.timestamps);
        eventsBatch_.push_back(getConnectedClientsEvent);

        if (eventsBatch_.size() >= BATCH_SIZE)
        {
            eventQueue_.pushBatch(eventsBatch_);
            eventsBatch_.clear();
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        log_->info("Skipping get connected clients event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handlePlayerAttack(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Validate the socket parameter before creating the event
    std::shared_ptr<boost::asio::ip::tcp::socket> validSocket = nullptr;
    if (socket)
    {
        try
        {
            if (socket->is_open())
            {
                validSocket = socket;
            }
        }
        catch (const std::exception &e)
        {
            // Socket is invalid, use nullptr
            validSocket = nullptr;
        }
    }

    // Only create and queue the event if we have a valid socket
    if (validSocket)
    {
        try
        {
            // Use the full message to parse combat data
            std::string fullMessage = context.fullMessage;
            log_->info("EventDispatcher handlePlayerAttack - Full message: " + fullMessage);

            // Parse the complete JSON message instead of just the body
            JSONParser jsonParser;
            nlohmann::json fullData = nlohmann::json::parse(fullMessage);
            log_->info("EventDispatcher handlePlayerAttack - Parsed full data: " + fullData.dump());

            Event playerAttackEvent(Event::PLAYER_ATTACK, context.clientData.clientId, EventData{std::in_place_type<nlohmann::json>, fullData}, context.timestamps);
            eventsBatch_.push_back(playerAttackEvent);

            if (eventsBatch_.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch_);
                eventsBatch_.clear();
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating player attack event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        log_->info("Skipping player attack event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handlePickupDroppedItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Validate client socket is still connected
    if (socket && socket->is_open() && context.clientData.clientId != 0)
    {
        try
        {
            // Parse pickup data from the full message
            std::string fullMessage = context.fullMessage;
            log_->info("EventDispatcher handlePickupDroppedItem - Full message: " + fullMessage);

            // Parse JSON to extract pickup data
            nlohmann::json j = nlohmann::json::parse(fullMessage);
            nlohmann::json pickupData = j["body"];

            log_->info("EventDispatcher handlePickupDroppedItem - Parsed pickup data: " + pickupData.dump());

            // Create ItemPickupRequestStruct
            ItemPickupRequestStruct pickupRequest;
            pickupRequest.characterId = context.clientData.characterId;
            pickupRequest.droppedItemUID = pickupData["itemUID"];
            pickupRequest.playerPosition = context.positionData;

            // Parse playerId from client message for security verification
            if (pickupData.contains("characterId") && pickupData["characterId"].is_number_integer())
            {
                pickupRequest.playerId = pickupData["characterId"];
            }
            else
            {
                log_->error("EventDispatcher handlePickupDroppedItem - Missing characterId in client request");
                return;
            }

            // Security check: verify that client-provided playerId matches server-side characterId
            if (pickupRequest.playerId != pickupRequest.characterId)
            {
                gameServices_.getLogger().logError("EventDispatcher handlePickupDroppedItem - Security violation: client playerId (" +
                                                       std::to_string(pickupRequest.playerId) + ") does not match server characterId (" +
                                                       std::to_string(pickupRequest.characterId) + ")",
                    RED);
                return;
            }

            gameServices_.getLogger().log("EventDispatcher handlePickupDroppedItem - Character ID: " + std::to_string(pickupRequest.characterId) +
                                              ", Player ID (verified): " + std::to_string(pickupRequest.playerId) +
                                              ", Item UID: " + std::to_string(pickupRequest.droppedItemUID) +
                                              ", Position: " + std::to_string(pickupRequest.playerPosition.positionX) + "," +
                                              std::to_string(pickupRequest.playerPosition.positionY),
                GREEN);

            // Create pickup event
            Event pickupEvent(Event::ITEM_PICKUP, context.clientData.clientId, pickupRequest, context.timestamps);
            eventsBatch_.push_back(pickupEvent);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating item pickup event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        log_->info("Skipping item pickup event for disconnected client ID: " + std::to_string(context.clientData.clientId));
    }
}

void
EventDispatcher::handleGetPlayerInventory(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        log_->info("EventDispatcher handleGetPlayerInventory - Character ID: " + std::to_string(context.characterData.characterId));

        try
        {
            // Create request data
            nlohmann::json requestData;
            requestData["characterId"] = context.characterData.characterId;

            // Create get player inventory event
            Event getInventoryEvent(Event::GET_PLAYER_INVENTORY, context.clientData.clientId, requestData, context.timestamps);
            eventsBatch_.push_back(getInventoryEvent);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating get player inventory event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        // Log that we're skipping the event for invalid character
        gameServices_.getLogger().log("Skipping get player inventory event for invalid character ID: " + std::to_string(context.characterData.characterId) + " (client ID: " + std::to_string(context.clientData.clientId) + ")", GREEN);
    }
}

void
EventDispatcher::handleHarvestStart(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    log_->info("EventDispatcher::handleHarvestStart called");
    log_->info("Character ID in context: " + std::to_string(context.characterData.characterId));
    log_->info("Client ID in context: " + std::to_string(context.clientData.clientId));

    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        log_->info("EventDispatcher handleHarvestStart - Character ID: " + std::to_string(context.characterData.characterId));

        try
        {
            // Parse harvest request from message
            auto messageJson = nlohmann::json::parse(context.fullMessage);

            if (!messageJson.contains("body") || !messageJson["body"].contains("corpseUID"))
            {
                log_->error("Missing corpseUID in harvest start request");
                return;
            }

            int corpseUID = messageJson["body"]["corpseUID"];

            // Create harvest request data
            HarvestRequestStruct harvestRequest;
            harvestRequest.characterId = context.characterData.characterId;
            harvestRequest.playerId = context.clientData.clientId;
            harvestRequest.corpseUID = corpseUID;

            // Create harvest start event
            Event harvestEvent(Event::HARVEST_START_REQUEST, context.clientData.clientId, harvestRequest, context.timestamps);
            eventsBatch_.push_back(harvestEvent);
            gameServices_.getLogger().log("Added HARVEST_START_REQUEST event to batch. Batch size: " + std::to_string(eventsBatch_.size()), GREEN);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating harvest start event: " + std::string(e.what()), RED);
        }
    }
    else
    {
        log_->error("Invalid character ID for harvest start: " + std::to_string(context.characterData.characterId));
    }
}

void
EventDispatcher::handleHarvestCancel(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        log_->info("EventDispatcher handleHarvestCancel - Character ID: " + std::to_string(context.characterData.characterId));

        try
        {
            // Create harvest cancel event
            Event harvestCancelEvent(Event::HARVEST_CANCELLED, context.clientData.clientId, context.characterData, context.timestamps);
            eventsBatch_.push_back(harvestCancelEvent);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating harvest cancel event: " + std::string(e.what()), RED);
        }
    }
}

void
EventDispatcher::handleGetNearbyCorpses(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        log_->info("EventDispatcher handleGetNearbyCorpses - Character ID: " + std::to_string(context.characterData.characterId));

        try
        {
            // Create get nearby corpses event
            Event nearbyCorpsesEvent(Event::GET_NEARBY_CORPSES, context.clientData.clientId, context.characterData, context.timestamps);
            eventsBatch_.push_back(nearbyCorpsesEvent);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating get nearby corpses event: " + std::string(e.what()), RED);
        }
    }
}

void
EventDispatcher::handleCorpseLootPickup(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        log_->info("EventDispatcher handleCorpseLootPickup - Character ID: " + std::to_string(context.characterData.characterId));

        try
        {
            // Parse pickup data from the full message
            std::string fullMessage = context.fullMessage;
            log_->info("EventDispatcher handleCorpseLootPickup - Full message: " + fullMessage);

            // Parse JSON to extract pickup data
            nlohmann::json j = nlohmann::json::parse(fullMessage);
            nlohmann::json bodyData = j["body"];

            log_->info("EventDispatcher handleCorpseLootPickup - Parsed body data: " + bodyData.dump());

            // Create CorpseLootPickupRequestStruct
            CorpseLootPickupRequestStruct pickupRequest;
            pickupRequest.characterId = context.characterData.characterId;

            // Parse playerId from client message for security verification
            if (bodyData.contains("playerId") && bodyData["playerId"].is_number_integer())
            {
                pickupRequest.playerId = bodyData["playerId"];
            }
            else
            {
                log_->error("EventDispatcher handleCorpseLootPickup - Missing playerId in client request");
                return;
            }

            // Security check: verify that client-provided playerId matches server-side characterId
            if (pickupRequest.playerId != pickupRequest.characterId)
            {
                gameServices_.getLogger().logError("EventDispatcher handleCorpseLootPickup - Security violation: client playerId (" +
                                                       std::to_string(pickupRequest.playerId) + ") does not match server characterId (" +
                                                       std::to_string(pickupRequest.characterId) + ")",
                    RED);
                return;
            }

            // Parse corpse UID
            if (bodyData.contains("corpseUID") && bodyData["corpseUID"].is_number_integer())
            {
                pickupRequest.corpseUID = bodyData["corpseUID"];
            }
            else
            {
                log_->error("EventDispatcher handleCorpseLootPickup - Missing or invalid corpseUID in client request");
                return;
            }

            // Parse requested items array
            if (bodyData.contains("requestedItems") && bodyData["requestedItems"].is_array())
            {
                for (const auto &item : bodyData["requestedItems"])
                {
                    if (item.contains("itemId") && item["itemId"].is_number_integer() &&
                        item.contains("quantity") && item["quantity"].is_number_integer())
                    {
                        int itemId = item["itemId"];
                        int quantity = item["quantity"];

                        if (quantity > 0)
                        {
                            pickupRequest.requestedItems.emplace_back(itemId, quantity);
                        }
                    }
                }
            }
            else
            {
                log_->error("EventDispatcher handleCorpseLootPickup - Missing or invalid requestedItems in client request");
                return;
            }

            // Validate that there are items to pickup
            if (pickupRequest.requestedItems.empty())
            {
                log_->error("EventDispatcher handleCorpseLootPickup - No valid items requested for pickup");
                return;
            }

            // Create and queue the event
            Event pickupEvent(Event::CORPSE_LOOT_PICKUP, context.clientData.clientId, pickupRequest, context.timestamps);
            eventsBatch_.push_back(pickupEvent);

            gameServices_.getLogger().log("Created corpse loot pickup event for player " +
                                              std::to_string(pickupRequest.characterId) +
                                              " requesting " + std::to_string(pickupRequest.requestedItems.size()) +
                                              " items from corpse " + std::to_string(pickupRequest.corpseUID),
                GREEN);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating corpse loot pickup event: " + std::string(e.what()), RED);
        }
    }
}

void
EventDispatcher::handleCorpseLootInspect(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        log_->info("EventDispatcher handleCorpseLootInspect - Character ID: " + std::to_string(context.characterData.characterId));

        try
        {
            // Parse inspect data from the full message
            std::string fullMessage = context.fullMessage;
            log_->info("EventDispatcher handleCorpseLootInspect - Full message: " + fullMessage);

            // Parse JSON to extract inspect data
            nlohmann::json j = nlohmann::json::parse(fullMessage);
            nlohmann::json bodyData = j["body"];

            log_->info("EventDispatcher handleCorpseLootInspect - Parsed body data: " + bodyData.dump());

            // Create CorpseLootInspectRequestStruct
            CorpseLootInspectRequestStruct inspectRequest;
            inspectRequest.characterId = context.characterData.characterId;

            // Parse playerId from client message for security verification
            if (bodyData.contains("playerId") && bodyData["playerId"].is_number_integer())
            {
                inspectRequest.playerId = bodyData["playerId"];
            }
            else
            {
                log_->error("EventDispatcher handleCorpseLootInspect - Missing playerId in client request");
                return;
            }

            // Security check: verify that client-provided playerId matches server-side characterId
            if (inspectRequest.playerId != inspectRequest.characterId)
            {
                gameServices_.getLogger().logError("EventDispatcher handleCorpseLootInspect - Security violation: client playerId (" +
                                                       std::to_string(inspectRequest.playerId) + ") does not match server characterId (" +
                                                       std::to_string(inspectRequest.characterId) + ")",
                    RED);
                return;
            }

            // Parse corpse UID
            if (bodyData.contains("corpseUID") && bodyData["corpseUID"].is_number_integer())
            {
                inspectRequest.corpseUID = bodyData["corpseUID"];
            }
            else
            {
                log_->error("EventDispatcher handleCorpseLootInspect - Missing or invalid corpseUID in client request");
                return;
            }

            // Create and queue the event
            Event inspectEvent(Event::CORPSE_LOOT_INSPECT, context.clientData.clientId, inspectRequest, context.timestamps);
            eventsBatch_.push_back(inspectEvent);

            gameServices_.getLogger().log("Created corpse loot inspect event for player " +
                                              std::to_string(inspectRequest.characterId) +
                                              " to inspect corpse " + std::to_string(inspectRequest.corpseUID),
                GREEN);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating corpse loot inspect event: " + std::string(e.what()), RED);
        }
    }
}

void
EventDispatcher::handleGetCharacterExperience(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    try
    {
        log_->info("EventDispatcher handleGetCharacterExperience called");

        // Get character data
        auto characterData = gameServices_.getCharacterManager().getCharacterData(context.characterData.characterId);
        auto &experienceManager = gameServices_.getExperienceManager();

        // Build response packet
        nlohmann::json response;
        response["header"]["event"] = "characterExperience";
        response["header"]["status"] = "success";
        response["header"]["timestamp"] = context.timestamps.serverSendMs;
        response["header"]["requestId"] = context.timestamps.requestId;

        // Character experience data
        response["body"]["characterId"] = characterData.characterId;
        response["body"]["currentLevel"] = characterData.characterLevel;
        response["body"]["currentExperience"] = characterData.characterExperiencePoints;
        response["body"]["expForCurrentLevel"] = experienceManager.getExperienceForLevelFromGameServer(characterData.characterLevel);
        response["body"]["expForNextLevel"] = experienceManager.getExperienceForNextLevel(characterData.characterLevel);

        // Calculate experience progress within current level
        int expForCurrentLevel = experienceManager.getExperienceForLevelFromGameServer(characterData.characterLevel);
        int expForNextLevel = experienceManager.getExperienceForNextLevel(characterData.characterLevel);
        int expInCurrentLevel = characterData.characterExperiencePoints - expForCurrentLevel;
        int expNeededForNextLevel = expForNextLevel - expForCurrentLevel;

        response["body"]["expInCurrentLevel"] = expInCurrentLevel;
        response["body"]["expNeededForNextLevel"] = expNeededForNextLevel;
        response["body"]["progressToNextLevel"] = (expNeededForNextLevel > 0) ? static_cast<double>(expInCurrentLevel) / static_cast<double>(expNeededForNextLevel) : 1.0;

        // Timestamps
        response["timestamps"]["serverRecvMs"] = context.timestamps.serverRecvMs;
        response["timestamps"]["serverSendMs"] = context.timestamps.serverSendMs;
        response["timestamps"]["clientSendMsEcho"] = context.timestamps.clientSendMsEcho;
        response["timestamps"]["requestId"] = context.timestamps.requestId;

        // Send response to client
        if (socket && socket->is_open())
        {
            std::string responseStr = response.dump();
            boost::asio::async_write(*socket, boost::asio::buffer(responseStr), [this, socket](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                    {
                        log_->error("Error sending character experience response: " + ec.message());
                    } });
        }

        log_->info("Sent character experience data for character " +
                   std::to_string(characterData.characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error handling get character experience: " + std::string(e.what()));

        // Send error response
        nlohmann::json errorResponse;
        errorResponse["header"]["event"] = "characterExperience";
        errorResponse["header"]["status"] = "error";
        errorResponse["header"]["message"] = std::string(e.what());
        errorResponse["header"]["timestamp"] = context.timestamps.serverSendMs;
        errorResponse["header"]["requestId"] = context.timestamps.requestId;

        if (socket && socket->is_open())
        {
            std::string responseStr = errorResponse.dump();
            boost::asio::async_write(*socket, boost::asio::buffer(responseStr), [this](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                    {
                        log_->error("Error sending character experience error response: " + ec.message());
                    } });
        }
    }
}

void
EventDispatcher::handleNPCInteract(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
    {
        log_->error("[EventDispatcher] handleNPCInteract: invalid character");
        return;
    }

    try
    {
        auto messageJson = nlohmann::json::parse(context.fullMessage);

        int npcId = messageJson["body"].value("npcId", 0);
        if (npcId <= 0)
        {
            log_->error("[EventDispatcher] handleNPCInteract: invalid npcId");
            return;
        }

        NPCInteractRequestStruct request;
        request.characterId = context.characterData.characterId;
        request.clientId = context.clientData.clientId;
        request.npcId = npcId;
        request.playerId = context.clientData.clientId;
        request.timestamps = context.timestamps;

        Event event(Event::NPC_INTERACT, context.clientData.clientId, request, context.timestamps);
        eventsBatch_.push_back(event);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleNPCInteract exception: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleDialogueChoice(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
    {
        log_->error("[EventDispatcher] handleDialogueChoice: invalid character");
        return;
    }

    try
    {
        auto messageJson = nlohmann::json::parse(context.fullMessage);

        DialogueChoiceRequestStruct request;
        request.characterId = context.characterData.characterId;
        request.clientId = context.clientData.clientId;
        request.sessionId = messageJson["body"].value("sessionId", "");
        request.edgeId = messageJson["body"].value("edgeId", 0);
        request.playerId = context.clientData.clientId;
        request.timestamps = context.timestamps;

        Event event(Event::DIALOGUE_CHOICE, context.clientData.clientId, request, context.timestamps);
        eventsBatch_.push_back(event);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleDialogueChoice exception: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleDialogueClose(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
    {
        log_->error("[EventDispatcher] handleDialogueClose: invalid character");
        return;
    }

    try
    {
        auto messageJson = nlohmann::json::parse(context.fullMessage);

        DialogueCloseRequestStruct request;
        request.characterId = context.characterData.characterId;
        request.clientId = context.clientData.clientId;
        request.sessionId = messageJson["body"].value("sessionId", "");
        request.playerId = context.clientData.clientId;
        request.timestamps = context.timestamps;

        Event event(Event::DIALOGUE_CLOSE, context.clientData.clientId, request, context.timestamps);
        eventsBatch_.push_back(event);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleDialogueClose exception: " + std::string(e.what()));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Vendor / Repair / Trade handlers
// ──────────────────────────────────────────────────────────────────────────────

void
EventDispatcher::handleOpenVendorShop(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        OpenVendorShopRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::OPEN_VENDOR_SHOP, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleOpenVendorShop: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleOpenSkillShop(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        OpenSkillShopRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::OPEN_SKILL_SHOP, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleOpenSkillShop: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleRequestLearnSkill(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        RequestLearnSkillRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.skillSlug = j["body"].value("skillSlug", "");
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        if (req.skillSlug.empty())
        {
            gameServices_.getLogger().logError("[EventDispatcher] handleRequestLearnSkill: missing skillSlug");
            return;
        }
        eventsBatch_.push_back(Event(Event::REQUEST_LEARN_SKILL, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleRequestLearnSkill: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleBuyItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        BuyItemRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.itemId = j["body"].value("itemId", 0);
        req.quantity = j["body"].value("quantity", 1);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::BUY_ITEM, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleBuyItem: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleSellItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        SellItemRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.inventoryItemId = j["body"].value("inventoryItemId", 0);
        req.quantity = j["body"].value("quantity", 1);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::SELL_ITEM, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleSellItem: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleBuyItemBatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        BuyBatchRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;

        if (j["body"].contains("items") && j["body"]["items"].is_array())
        {
            for (const auto &entry : j["body"]["items"])
            {
                BuyBatchItemEntry e;
                e.itemId = entry.value("itemId", 0);
                e.quantity = entry.value("quantity", 1);
                req.items.push_back(e);
            }
        }
        eventsBatch_.push_back(Event(Event::BUY_ITEM_BATCH, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleBuyItemBatch: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleSellItemBatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        SellBatchRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;

        if (j["body"].contains("items") && j["body"]["items"].is_array())
        {
            for (const auto &entry : j["body"]["items"])
            {
                SellBatchItemEntry e;
                e.inventoryItemId = entry.value("inventoryItemId", 0);
                e.quantity = entry.value("quantity", 1);
                req.items.push_back(e);
            }
        }
        eventsBatch_.push_back(Event(Event::SELL_ITEM_BATCH, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleSellItemBatch: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleOpenRepairShop(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        OpenRepairShopRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::OPEN_REPAIR_SHOP, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleOpenRepairShop: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleRepairItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        RepairItemRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.inventoryItemId = j["body"].value("inventoryItemId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::REPAIR_ITEM, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleRepairItem: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleRepairAll(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        RepairAllRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.npcId = j["body"].value("npcId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::REPAIR_ALL, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleRepairAll: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleTradeRequest(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        TradeRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.targetCharacterId = j["body"].value("targetCharacterId", 0);
        req.playerPosition = context.positionData;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::TRADE_REQUEST, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleTradeRequest: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleTradeAccept(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        TradeRespondStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        // sessionId field carries the initiator's characterId as string
        req.sessionId = j["body"].value("fromCharacterId", "");
        req.accept = true;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::TRADE_ACCEPT, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleTradeAccept: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleTradeDecline(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        TradeRespondStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.sessionId = j["body"].value("fromCharacterId", "");
        req.accept = false;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::TRADE_DECLINE, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleTradeDecline: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleTradeOfferUpdate(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        TradeOfferUpdateStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.sessionId = j["body"].value("sessionId", "");
        req.gold = j["body"].value("gold", 0);
        req.timestamps = context.timestamps;

        if (j["body"].contains("items") && j["body"]["items"].is_array())
        {
            for (const auto &item : j["body"]["items"])
            {
                TradeOfferItemStruct oi;
                oi.inventoryItemId = item.value("inventoryItemId", 0);
                oi.itemId = item.value("itemId", 0);
                oi.quantity = item.value("quantity", 1);
                req.items.push_back(oi);
            }
        }
        eventsBatch_.push_back(Event(Event::TRADE_OFFER_UPDATE, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleTradeOfferUpdate: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleTradeConfirm(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        TradeConfirmCancelStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.sessionId = j["body"].value("sessionId", "");
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::TRADE_CONFIRM, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleTradeConfirm: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleTradeCancel(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        TradeConfirmCancelStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.sessionId = j["body"].value("sessionId", "");
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::TRADE_CANCEL, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleTradeCancel: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleEquipItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        EquipItemRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.inventoryItemId = j["body"].value("inventoryItemId", 0);
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::EQUIP_ITEM, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleEquipItem: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleUnequipItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        auto j = nlohmann::json::parse(context.fullMessage);
        UnequipItemRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.equipSlotSlug = j["body"].value("equipSlotSlug", "");
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::UNEQUIP_ITEM, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleUnequipItem: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleGetEquipment(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        GetEquipmentRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::GET_EQUIPMENT, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleGetEquipment: " + std::string(e.what()));
    }
}

void
EventDispatcher::handleRespawnRequest(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;
    try
    {
        RespawnRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.clientId = context.clientData.clientId;
        req.timestamps = context.timestamps;
        eventsBatch_.push_back(Event(Event::PLAYER_RESPAWN, req.clientId, req, context.timestamps));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("[EventDispatcher] handleRespawnRequest: " + std::string(e.what()));
    }
}

// ---------------------------------------------------------------------------
// handleDropItemByPlayer — player drops item from inventory onto the ground
// ---------------------------------------------------------------------------
void
EventDispatcher::handleDropItemByPlayer(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;

    try
    {
        nlohmann::json j = nlohmann::json::parse(context.fullMessage);
        nlohmann::json body = j["body"];

        int itemId = body.value("itemId", 0);
        int quantity = body.value("quantity", 1);

        if (itemId <= 0)
        {
            log_->error("[EventDispatcher] dropItem: missing itemId from client " +
                        std::to_string(context.clientData.clientId));
            return;
        }

        ItemDropByPlayerRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.playerId = context.clientData.clientId;
        req.itemId = itemId;
        req.quantity = quantity;
        req.playerPosition = context.positionData;

        eventsBatch_.push_back(Event(Event::ITEM_DROP_BY_PLAYER, req.playerId, req, context.timestamps));

        log_->info("[EventDispatcher] dropItem queued: character=" +
                   std::to_string(req.characterId) +
                   " itemId=" + std::to_string(itemId) +
                   " qty=" + std::to_string(quantity));
    }
    catch (const std::exception &e)
    {
        log_->error("[EventDispatcher] handleDropItemByPlayer: " + std::string(e.what()));
    }
}

// ---------------------------------------------------------------------------
// handleUseItem — player uses a consumable item (potion, scroll, food…)
// ---------------------------------------------------------------------------
void
EventDispatcher::handleUseItem(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;

    try
    {
        nlohmann::json j = nlohmann::json::parse(context.fullMessage);
        nlohmann::json body = j["body"];

        int itemId = body.value("itemId", 0);

        if (itemId <= 0)
        {
            log_->error("[EventDispatcher] useItem: missing itemId from client " +
                        std::to_string(context.clientData.clientId));
            return;
        }

        ItemUseRequestStruct req;
        req.characterId = context.characterData.characterId;
        req.playerId = context.clientData.clientId;
        req.itemId = itemId;

        eventsBatch_.push_back(Event(Event::USE_ITEM, req.playerId, req, context.timestamps));

        log_->info("[EventDispatcher] useItem queued: character=" +
                   std::to_string(req.characterId) +
                   " itemId=" + std::to_string(itemId));
    }
    catch (const std::exception &e)
    {
        log_->error("[EventDispatcher] handleUseItem: " + std::string(e.what()));
    }
}

// ---------------------------------------------------------------------------
// handleGetBestiaryOverview — client requests list of all discovered mobs with kill counts
// ---------------------------------------------------------------------------
void
EventDispatcher::handleGetBestiaryOverview(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> /*socket*/)
{
    if (context.characterData.characterId <= 0)
        return;

    nlohmann::json payload;
    payload["characterId"] = context.characterData.characterId;
    payload["clientId"] = context.clientData.clientId;

    eventsBatch_.push_back(Event(Event::GET_BESTIARY_OVERVIEW, context.clientData.clientId, payload, context.timestamps));

    log_->info("[EventDispatcher] getBestiaryOverview queued: character=" +
               std::to_string(context.characterData.characterId));
}

// handleGetBestiaryEntry — client requests bestiary data for a mob template
// ---------------------------------------------------------------------------
void
EventDispatcher::handleGetBestiaryEntry(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.characterData.characterId <= 0)
        return;

    try
    {
        nlohmann::json j = nlohmann::json::parse(context.fullMessage);
        std::string mobSlug = j["body"].value("mobSlug", std::string{});

        if (mobSlug.empty())
        {
            log_->error("[EventDispatcher] getBestiaryEntry: missing mobSlug");
            return;
        }

        nlohmann::json payload;
        payload["characterId"] = context.characterData.characterId;
        payload["clientId"] = context.clientData.clientId;
        payload["mobSlug"] = mobSlug;

        eventsBatch_.push_back(Event(Event::GET_BESTIARY_ENTRY, context.clientData.clientId, payload, context.timestamps));

        log_->info("[EventDispatcher] getBestiaryEntry queued: character=" +
                   std::to_string(context.characterData.characterId) +
                   " mob=" + mobSlug);
    }
    catch (const std::exception &e)
    {
        log_->error("[EventDispatcher] handleGetBestiaryEntry: " + std::string(e.what()));
    }
}

// ── Chat message ─────────────────────────────────────────────────────────────
void
EventDispatcher::handleChatMessage(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.clientData.clientId <= 0 || context.characterData.characterId <= 0)
        return;

    try
    {
        nlohmann::json j = nlohmann::json::parse(context.fullMessage);

        std::string channelStr = j["body"].value("channel", std::string{"zone"});
        std::string text = j["body"].value("text", std::string{});
        std::string targetName = j["body"].value("targetName", std::string{});

        // Basic server-side validation
        if (text.empty() || text.size() > 255)
        {
            log_->warn("[EventDispatcher] chatMessage: invalid text length from client " + std::to_string(context.clientData.clientId));
            return;
        }

        ChatChannel channel = ChatChannel::ZONE;
        if (channelStr == "local")
            channel = ChatChannel::LOCAL;
        else if (channelStr == "whisper")
            channel = ChatChannel::WHISPER;

        if (channel == ChatChannel::WHISPER && targetName.empty())
        {
            log_->warn("[EventDispatcher] chatMessage: whisper without targetName from client " + std::to_string(context.clientData.clientId));
            return;
        }

        ChatMessageStruct msg;
        msg.senderClientId = context.clientData.clientId;
        msg.senderCharId = context.characterData.characterId;
        msg.channel = channel;
        msg.text = std::move(text);
        msg.targetName = std::move(targetName);
        msg.timestamps = context.timestamps;

        eventsBatch_.push_back(Event(Event::CHAT_MESSAGE,
            context.clientData.clientId,
            msg,
            context.timestamps));

        log_->debug("[EventDispatcher] chatMessage queued: client=" + std::to_string(context.clientData.clientId) + " channel=" + channelStr);
    }
    catch (const std::exception &e)
    {
        log_->error("[EventDispatcher] handleChatMessage: " + std::string(e.what()));
    }
}

// ── Player Ready (scene loaded, client ACK) ───────────────────────────────────
void
EventDispatcher::handlePlayerReady(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.clientData.clientId <= 0 || context.characterData.characterId <= 0)
    {
        log_->warn("[EventDispatcher] playerReady: invalid clientId or characterId");
        return;
    }

    CharacterDataStruct charData;
    charData.characterId = context.characterData.characterId;

    eventsBatch_.push_back(Event(Event::PLAYER_READY,
        context.clientData.clientId,
        charData,
        context.timestamps));

    log_->info("[EventDispatcher] playerReady queued: client=" +
               std::to_string(context.clientData.clientId) +
               " char=" + std::to_string(context.characterData.characterId));
}

// ── getTitles ──────────────────────────────────────────────────────────────
void
EventDispatcher::handleGetPlayerTitles(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.clientData.clientId <= 0 || context.characterData.characterId <= 0)
    {
        log_->warn("[EventDispatcher] getTitles: invalid clientId or characterId");
        return;
    }

    nlohmann::json body;
    body["characterId"] = context.characterData.characterId;
    body["clientId"] = context.clientData.clientId;

    eventsBatch_.push_back(Event(Event::GET_PLAYER_TITLES,
        context.clientData.clientId,
        body,
        context.timestamps));
}

// ── equipTitle ─────────────────────────────────────────────────────────────
void
EventDispatcher::handleEquipTitle(const EventContext &context,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (context.clientData.clientId <= 0 || context.characterData.characterId <= 0)
    {
        log_->warn("[EventDispatcher] equipTitle: invalid clientId or characterId");
        return;
    }

    std::string titleSlug;
    try
    {
        const auto &jsonData = nlohmann::json::parse(context.rawMessage);
        titleSlug = jsonData.value("body", nlohmann::json::object()).value("titleSlug", "");
    }
    catch (...)
    {
        log_->error("[EventDispatcher] equipTitle: failed to parse rawMessage");
        return;
    }

    EquipTitleRequestStruct req;
    req.characterId = context.characterData.characterId;
    req.clientId = context.clientData.clientId;
    req.titleSlug = titleSlug;
    req.timestamps = context.timestamps;

    eventsBatch_.push_back(Event(Event::EQUIP_TITLE,
        context.clientData.clientId,
        req,
        context.timestamps));
}
