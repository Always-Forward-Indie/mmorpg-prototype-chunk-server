#include "chunk_server/ChunkServer.hpp"
#include <unordered_set>

ChunkServer::ChunkServer(GameServices &gameServices,
                        EventHandler &eventHandler,
                        EventQueue &eventQueueGameServer,
                        EventQueue &eventQueueChunkServer,
                        EventQueue &eventQueueGameServerPing,
                        Scheduler &scheduler,
                        GameServerWorker &gameServerWorker
                    )
    :
      eventQueueGameServer_(eventQueueGameServer),
      eventQueueChunkServer_(eventQueueChunkServer),
      eventQueueGameServerPing_(eventQueueGameServerPing),
      eventHandler_(eventHandler),
      scheduler_(scheduler),
      gameServices_(gameServices)
{
    
}

void ChunkServer::mainEventLoopCH()
{
    gameServices_.getLogger().log("Add Tasks To Game Server Scheduler...", YELLOW);
    constexpr int BATCH_SIZE = 10;
    
    // Task for spawning mobs in the zone
    Task spawnMobInZoneTask(
        [&]
        {
            gameServices_.getSpawnZoneManager().spawnMobsInZone(1);
            SpawnZoneStruct spawnZone = gameServices_.getSpawnZoneManager().getMobSpawnZoneByID(1);

            auto connectedClients = gameServices_.getClientManager().getClientsList();
            for (const auto &client : connectedClients)
            {
                if (client.clientId == 0)
                    continue;

                auto clientSocket = client.socket;
                Event spawnMobsInZoneEvent(Event::SPAWN_MOBS_IN_ZONE, client.clientId, spawnZone, clientSocket);
                eventQueueGameServer_.push(spawnMobsInZoneEvent);
            }
        },
        15,
        std::chrono::system_clock::now(),
        1 // unique task ID
    );

    scheduler_.scheduleTask(spawnMobInZoneTask); 

    // Task for moving mobs in the zone
    Task moveMobInZoneTask(
        [&]
        {
            gameServices_.getSpawnZoneManager().moveMobsInZone(1);
            std::vector<MobDataStruct> mobsList = gameServices_.getSpawnZoneManager().getMobsInZone(1);

            auto connectedClients = gameServices_.getClientManager().getClientsList();
            for (const auto &client : connectedClients)
            {
                if (client.clientId == 0)
                    continue;

                auto clientSocket = client.socket;
                Event moveMobsInZoneEvent(Event::SPAWN_ZONE_MOVE_MOBS, client.clientId, mobsList, clientSocket);
                eventQueueGameServer_.push(moveMobsInZoneEvent);
            }
        },
        3,
        std::chrono::system_clock::now(),
        2 // unique task ID
    );

    scheduler_.scheduleTask(moveMobInZoneTask); 

    try
    {
        gameServices_.getLogger().log("Starting Game Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueGameServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(e.what(), RED);
    }
}



void ChunkServer::mainEventLoopGS()
{
    constexpr int BATCH_SIZE = 10;
    gameServices_.getLogger().log("Add Tasks To Chunk Server Scheduler...", YELLOW);

    try
    {
        gameServices_.getLogger().log("Starting Chunk Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueChunkServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(e.what(), RED);
    }
}

void ChunkServer::mainEventLoopPing()
{
    constexpr int BATCH_SIZE = 1; // Ping обрабатывай сразу

    gameServices_.getLogger().log("Starting Ping Event Loop...", YELLOW);

    try
    {
        while (running_)
        {
            std::vector<Event> pingEvents;
            if (eventQueueGameServerPing_.popBatch(pingEvents, BATCH_SIZE))
            {
                processPingBatch(pingEvents);
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Ping Event Loop Error: " + std::string(e.what()), RED);
    }
}

void ChunkServer::processPingBatch(const std::vector<Event>& pingEvents)
{
    for (const auto& event : pingEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error processing PING_EVENT: " + std::string(e.what()));
            }
        });
    }

    eventCondition.notify_all();
}

void ChunkServer::processBatch(const std::vector<Event>& eventsBatch)
{
    std::vector<Event> priorityEvents;
    std::vector<Event> normalEvents;

    // Separate ping events from other events
    for (const auto& event : eventsBatch)
    {
        // if (event.PING_CLIENT == Event::PING_CLIENT)
        //     priorityEvents.push_back(event);
        // else
            normalEvents.push_back(event);
    }

    // Process priority ping events first
    for (const auto& event : priorityEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error processing priority dispatchEvent: " + std::string(e.what()));
            }
        });
    }

    // Process normal events
    for (const auto& event : normalEvents)
    {
        threadPool_.enqueueTask([this, event] {
            try
            {
                eventHandler_.dispatchEvent(event);
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error in normal dispatchEvent: " + std::string(e.what()));
            }
        });
    }

    eventCondition.notify_all();
}

void ChunkServer::startMainEventLoop()
{
    if (event_game_server_thread_.joinable() || event_chunk_server_thread_.joinable())
    {
        gameServices_.getLogger().log("Chunk server event loops are already running!", RED);
        return;
    }

    event_game_server_thread_ = std::thread(&ChunkServer::mainEventLoopGS, this);
    event_chunk_server_thread_ = std::thread(&ChunkServer::mainEventLoopCH, this);
    event_ping_thread_ = std::thread(&ChunkServer::mainEventLoopPing, this);
}

void ChunkServer::stop()
{
    running_ = false;
    scheduler_.stop();
    eventCondition.notify_all();
}

ChunkServer::~ChunkServer()
{
    gameServices_.getLogger().log("Shutting down Game Server...", YELLOW);
    
    stop();  

    if (event_game_server_thread_.joinable())
        event_game_server_thread_.join();

    if (event_chunk_server_thread_.joinable())
        event_chunk_server_thread_.join();

    if (event_ping_thread_.joinable())
        event_ping_thread_.join();
}
