#include "events/EventDispatcher.hpp"
#include "utils/JSONParser.hpp"
#include <boost/asio.hpp>

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
}

void
EventDispatcher::dispatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
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
    else if (context.eventType == "getSpawnZones")
    {
        handleGetSpawnZones(context, socket);
    }
    else if (context.eventType == "getConnectedCharacters")
    {
        handleGetConnectedClients(context, socket);
    }
    else if (context.eventType == "PLAYER_ATTACK")
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
    else
    {
        gameServices_.getLogger().logError("Unknown event type: " + context.eventType, RED);
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

        gameServices_.getLogger().log("Cleared eventsBatch_ vector", BLUE);
    }

    // Reserve space for next batch to avoid frequent reallocations
    if (eventsBatch_.capacity() < BATCH_SIZE)
    {
        eventsBatch_.reserve(BATCH_SIZE);
    }

    // Log the size of eventsBatch_ for monitoring memory usage
    gameServices_.getLogger().log("eventsBatch_ size: " + std::to_string(eventsBatch_.size()), GREEN);

    // Log the capacity of eventsBatch_ for monitoring memory usage
    gameServices_.getLogger().log("eventsBatch_ capacity: " + std::to_string(eventsBatch_.capacity()), GREEN);
}

void
EventDispatcher::handleJoinGameClient(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Create client data safely without copying invalid socket references
    ClientDataStruct clientData;
    clientData.clientId = context.clientData.clientId;
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
        gameServices_.getLogger().log("Skipping join client event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
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
            Event joinCharacterEvent(Event::JOIN_CHARACTER, context.clientData.clientId, EventData{std::in_place_type<CharacterDataStruct>, characterData});
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
        gameServices_.getLogger().log("Skipping join character event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
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

    // Log character data for debugging
    gameServices_.getLogger().log("Creating MOVE_CHARACTER event with movement data:", GREEN);
    gameServices_.getLogger().log("Client ID: " + std::to_string(movementData.clientId), GREEN);
    gameServices_.getLogger().log("Character ID: " + std::to_string(movementData.characterId), GREEN);
    gameServices_.getLogger().log("Position: " + std::to_string(movementData.position.positionX) + ", " + std::to_string(movementData.position.positionY), GREEN);

    // Validate character data
    if (movementData.characterId <= 0)
    {
        gameServices_.getLogger().logError("Invalid character data for MOVE_CHARACTER event", RED);
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
            Event moveEvent(Event::MOVE_CHARACTER, context.clientData.clientId, EventData{std::in_place_type<MovementDataStruct>, std::move(movementData)});

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
        gameServices_.getLogger().log("Skipping move character event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
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
            gameServices_.getLogger().log("Skipping ping event for unauthenticated client (logged every 100th occurrence)", GREEN);
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
            Event pingEvent(Event::PING_CLIENT, context.clientData.clientId, std::move(clientData));
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
        gameServices_.getLogger().log("Skipping ping event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
    }
}

void
EventDispatcher::handleGetSpawnZones(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!chunkServer_)
    {
        gameServices_.getLogger().logError("ChunkServer is nullptr in EventDispatcher!", RED);
        return;
    }

    // Validate the socket parameter before creating events
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

    // Only create and queue events if we have a valid socket
    if (validSocket)
    {
        // Get ALL spawn zones instead of just zone ID 1
        auto allSpawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();

        gameServices_.getLogger().log("Sending " + std::to_string(allSpawnZones.size()) +
                                          " spawn zones to client " + std::to_string(context.clientData.clientId),
            BLUE);

        // Send each spawn zone to the client
        for (const auto &[zoneId, spawnZone] : allSpawnZones)
        {
            Event getSpawnZonesEvent(Event::SPAWN_MOBS_IN_ZONE, context.clientData.clientId, spawnZone);
            eventsBatch_.push_back(getSpawnZonesEvent);

            if (eventsBatch_.size() >= BATCH_SIZE)
            {
                eventQueue_.pushBatch(eventsBatch_);
                eventsBatch_.clear();
            }
        }

        // Flush any remaining events in batch
        if (!eventsBatch_.empty())
        {
            eventQueue_.pushBatch(eventsBatch_);
            eventsBatch_.clear();
        }
    }
    else
    {
        // Log that we're skipping the event for a disconnected client
        gameServices_.getLogger().log("Skipping get spawn zones event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
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
        Event getConnectedClientsEvent(Event::GET_CONNECTED_CHARACTERS, context.clientData.clientId, requestType);
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
        gameServices_.getLogger().log("Skipping get connected clients event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
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
            gameServices_.getLogger().log("EventDispatcher handlePlayerAttack - Full message: " + fullMessage, GREEN);

            // Parse combat action data from the full message
            JSONParser jsonParser;
            nlohmann::json attackData = jsonParser.parseCombatActionData(fullMessage.c_str(), fullMessage.length());
            gameServices_.getLogger().log("EventDispatcher handlePlayerAttack - Parsed attack data: " + attackData.dump(), GREEN);

            Event playerAttackEvent(Event::PLAYER_ATTACK, context.clientData.clientId, EventData{std::in_place_type<nlohmann::json>, attackData});
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
        gameServices_.getLogger().log("Skipping player attack event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
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
            gameServices_.getLogger().log("EventDispatcher handlePickupDroppedItem - Full message: " + fullMessage, GREEN);

            // Parse JSON to extract pickup data
            nlohmann::json j = nlohmann::json::parse(fullMessage);
            nlohmann::json pickupData = j["body"];

            gameServices_.getLogger().log("EventDispatcher handlePickupDroppedItem - Parsed pickup data: " + pickupData.dump(), GREEN);

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
                gameServices_.getLogger().logError("EventDispatcher handlePickupDroppedItem - Missing characterId in client request", RED);
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
            Event pickupEvent(Event::ITEM_PICKUP, context.clientData.clientId, pickupRequest);
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
        gameServices_.getLogger().log("Skipping item pickup event for disconnected client ID: " + std::to_string(context.clientData.clientId), GREEN);
    }
}

void
EventDispatcher::handleGetPlayerInventory(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        gameServices_.getLogger().log("EventDispatcher handleGetPlayerInventory - Character ID: " + std::to_string(context.characterData.characterId), GREEN);

        try
        {
            // Create request data
            nlohmann::json requestData;
            requestData["characterId"] = context.characterData.characterId;

            // Create get player inventory event
            Event getInventoryEvent(Event::GET_PLAYER_INVENTORY, context.clientData.clientId, requestData);
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
