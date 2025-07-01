#include "events/EventDispatcher.hpp"

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
    // TODO - Analyze code for events and refactor it
    if (context.eventType == "joinGame")
    {
        handleJoinGame(context, socket);
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
    else
    {
        gameServices_.getLogger().logError("Unknown event type: " + context.eventType, RED);
    }

    // Push the batch of events to the queue
    if (!eventsBatch_.empty())
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handleJoinGame(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event joinEvent(Event::JOIN_CLIENT, context.clientData.clientId, context.clientData, socket);
    eventsBatch_.push_back(joinEvent);
    if (eventsBatch_.size() >= BATCH_SIZE)
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handleMoveCharacter(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event moveEvent(Event::MOVE_CHARACTER, context.clientData.clientId, context.characterData, socket);
    eventsBatch_.push_back(moveEvent);
    if (eventsBatch_.size() >= BATCH_SIZE)
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handleDisconnect(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event disconnectEvent(Event::DISCONNECT_CLIENT, context.clientData.clientId, context.clientData, socket);

    eventsBatch_.push_back(disconnectEvent);

    if (eventsBatch_.size() >= BATCH_SIZE)
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handlePing(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event pingEvent(Event::PING_CLIENT, context.clientData.clientId, context.clientData, socket);
    eventQueuePing_.push(pingEvent);
}

void
EventDispatcher::handleGetSpawnZones(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!chunkServer_)
    {
        gameServices_.getLogger().logError("ChunkServer is nullptr in EventDispatcher!", RED);
        return;
    }

    gameServices_.getSpawnZoneManager().spawnMobsInZone(1);
    SpawnZoneStruct spawnZone = gameServices_.getSpawnZoneManager().getMobSpawnZoneByID(1);

    Event getSpawnZonesEvent(Event::SPAWN_MOBS_IN_ZONE, context.clientData.clientId, spawnZone, socket);
    eventsBatch_.push_back(getSpawnZonesEvent);

    if (eventsBatch_.size() >= BATCH_SIZE)
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}

void
EventDispatcher::handleGetConnectedClients(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    Event getConnectedClientsEvent(Event::GET_CONNECTED_CHARACTERS, context.clientData.clientId, gameServices_.getClientManager().getClientsList(), socket);
    eventsBatch_.push_back(getConnectedClientsEvent);

    if (eventsBatch_.size() >= BATCH_SIZE)
    {
        eventQueue_.pushBatch(eventsBatch_);
        eventsBatch_.clear();
    }
}
