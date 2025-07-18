#include "chunk_server/ChunkServer.hpp"
#include <fstream>
#include <unordered_set>
#ifdef __GLIBC__
#include <malloc.h>
#endif

ChunkServer::ChunkServer(GameServices &gameServices,
    EventHandler &eventHandler,
    EventQueue &eventQueueGameServer,
    EventQueue &eventQueueChunkServer,
    EventQueue &eventQueueGameServerPing,
    Scheduler &scheduler,
    GameServerWorker &gameServerWorker,
    NetworkManager &networkManager)
    : eventQueueGameServer_(eventQueueGameServer),
      eventQueueChunkServer_(eventQueueChunkServer),
      eventQueueGameServerPing_(eventQueueGameServerPing),
      eventHandler_(eventHandler),
      scheduler_(scheduler),
      gameServices_(gameServices),
      networkManager_(networkManager)
{
}

void
ChunkServer::mainEventLoopCH()
{
    gameServices_.getLogger().log("Add Tasks To Game Server Scheduler...", YELLOW);
    constexpr int BATCH_SIZE = 10;

    // Task for spawning mobs in the zone
    Task spawnMobInZoneTask(
        [&]
        {
            // get all spawn zones
            auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
            if (spawnZones.empty())
            {
                gameServices_.getLogger().logError("No spawn zones found, cannot spawn mobs!", RED);
                return;
            }

            // go through all spawn zones and spawn mobs
            for (const auto &zone : spawnZones)
            {
                if (zone.second.spawnEnabled && zone.second.spawnMobId > 0)
                {
                    // Spawn mobs in this zone
                    gameServices_.getSpawnZoneManager().spawnMobsInZone(zone.second.zoneId);
                    gameServices_.getLogger().log("[INFO] Spawned mobs in zone: " + std::to_string(zone.second.zoneId) +
                                                  ", count: " + std::to_string(zone.second.spawnedMobsCount));

                    // send spawn zone updates to all connected clients
                    auto connectedClients = gameServices_.getClientManager().getClientsListReadOnly();
                    for (const auto &client : connectedClients)
                    {
                        if (client.clientId <= 0) // Strict validation
                            continue;

                        auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
                        Event spawnMobsInZoneEvent(Event::SPAWN_MOBS_IN_ZONE, client.clientId, zone.second);
                        eventQueueGameServer_.push(std::move(spawnMobsInZoneEvent)); // Use move
                    }
                }
                else
                {
                    gameServices_.getLogger().log("[DEBUG] Skipping zone " + std::to_string(zone.second.zoneId) +
                                                  " - spawn disabled or no mob ID set");
                }
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
            // get all spawn zones
            auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
            if (spawnZones.empty())
            {
                gameServices_.getLogger().logError("No spawn zones found, cannot move mobs!", RED);
                return;
            }

            // move mobs in all zones

            for (const auto &zone : spawnZones)
            {
                if (zone.second.spawnEnabled && zone.second.spawnedMobsCount > 0)
                {
                    // Move mobs in this zone
                    bool anyMobMoved = gameServices_.getMobMovementManager().moveMobsInZone(zone.second.zoneId);
                    gameServices_.getLogger().log("[INFO] Moved mobs in zone: " + std::to_string(zone.second.zoneId));

                    if (anyMobMoved)
                    {
                        // send move mobs updates to all connected clients
                        auto connectedClients = gameServices_.getClientManager().getClientsListReadOnly();
                        for (const auto &client : connectedClients)
                        {
                            if (client.clientId <= 0) // Strict validation
                                continue;
                            auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
                            // Pass only zone ID instead of full mobs list to prevent memory leaks
                            Event moveMobsInZoneEvent(Event::SPAWN_ZONE_MOVE_MOBS, client.clientId, zone.second.zoneId);
                            eventQueueGameServer_.push(std::move(moveMobsInZoneEvent)); // Use move
                        }
                    }
                    else
                    {
                        gameServices_.getLogger().log("[DEBUG] No mobs moved in zone " + std::to_string(zone.second.zoneId));
                    }
                }
                else if (zone.second.spawnedMobsCount > 0)
                {
                    gameServices_.getLogger().log("[DEBUG] Skipping zone " + std::to_string(zone.second.zoneId) +
                                                  " - movement disabled or no mobs to move");
                }
                else
                {
                    gameServices_.getLogger().log("[DEBUG] Skipping zone " + std::to_string(zone.second.zoneId) +
                                                  " - no mobs to move or movement disabled");
                }
            }
        },
        3,
        std::chrono::system_clock::now(),
        2 // unique task ID
    );

    scheduler_.scheduleTask(moveMobInZoneTask);

    // Task for updating ongoing combat actions
    Task combatUpdateTask(
        [&]
        {
            // Update all ongoing combat actions - this will automatically complete
            // or interrupt actions that have exceeded their duration
            try
            {
                eventHandler_.getCombatEventHandler().updateOngoingActions();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error updating combat actions: " + std::string(ex.what()));
            }
        },
        1, // Run every 1 second - frequent enough for responsive combat
        std::chrono::system_clock::now(),
        6 // unique task ID
    );

    scheduler_.scheduleTask(combatUpdateTask);

    // Task for periodic cleanup of inactive sessions and client data
    Task cleanupTask(
        [&] {                                                                     // Clean up inactive client data
            auto clientsList = gameServices_.getClientManager().getClientsList(); // This automatically cleans up invalid clients

            // More aggressive cleanup - force cleanup of disconnected clients
            std::vector<int> clientsToRemove;
            for (const auto &client : clientsList)
            {
                auto socket = gameServices_.getClientManager().getClientSocket(client.clientId);
                if (!socket)
                {
                    clientsToRemove.push_back(client.clientId);
                }
            }

            for (int clientId : clientsToRemove)
            {
                gameServices_.getClientManager().removeClientData(clientId);
                gameServices_.getLogger().log("Force removed disconnected client: " + std::to_string(clientId), YELLOW);
            }

            // Force cleanup memory in ClientManager
            gameServices_.getClientManager().forceCleanupMemory();

            // Clean up inactive network sessions to prevent memory leaks
            networkManager_.cleanupInactiveSessions();

            // Log memory usage information
            gameServices_.getLogger().log("Active clients: " + std::to_string(clientsList.size()), BLUE);
            gameServices_.getLogger().log("Game Server Queue size: " + std::to_string(eventQueueGameServer_.size()), BLUE);
            gameServices_.getLogger().log("Chunk Server Queue size: " + std::to_string(eventQueueChunkServer_.size()), BLUE);
            gameServices_.getLogger().log("Ping Queue size: " + std::to_string(eventQueueGameServerPing_.size()), BLUE);
            gameServices_.getLogger().log("ThreadPool Queue size: " + std::to_string(threadPool_.getTaskQueueSize()), BLUE);

            // If any queue is getting too large, log a warning
            if (eventQueueGameServer_.size() > 500 || eventQueueChunkServer_.size() > 500 || eventQueueGameServerPing_.size() > 500 || threadPool_.getTaskQueueSize() > 500)
            {
                gameServices_.getLogger().logError("Event queues are getting large - potential memory leak!", RED);
            }
        },
        10, // Run every 10 seconds (more frequent cleanup)
        std::chrono::system_clock::now(),
        3 // unique task ID
    );

    scheduler_.scheduleTask(cleanupTask);

    // Add memory monitoring task
    Task memoryMonitorTask(
        [&]
        {
            // Simple memory monitoring using /proc/self/status
            std::ifstream statusFile("/proc/self/status");
            if (statusFile.is_open())
            {
                std::string line;
                while (std::getline(statusFile, line))
                {
                    if (line.find("VmRSS:") == 0)
                    {
                        gameServices_.getLogger().log("Memory usage: " + line, GREEN);
                        break;
                    }
                }
                statusFile.close();
            }
        },
        5, // Run every 5 seconds
        std::chrono::system_clock::now(),
        4 // unique task ID
    );

    scheduler_.scheduleTask(memoryMonitorTask);

    // Add aggressive memory cleanup task to force memory release
    Task aggressiveCleanupTask(
        [&]
        {
            gameServices_.getLogger().log("Running aggressive memory cleanup...", YELLOW);

            // Force garbage collection by explicitly clearing and shrinking containers
            // Force memory cleanup in all managers
            gameServices_.getClientManager().forceCleanupMemory();
            networkManager_.cleanupInactiveSessions();

            // Force cleanup of event queues when they're empty
            if (eventQueueGameServer_.empty())
            {
                eventQueueGameServer_.forceCleanup();
                gameServices_.getLogger().log("Cleaned up Game Server event queue", BLUE);
            }
            if (eventQueueChunkServer_.empty())
            {
                eventQueueChunkServer_.forceCleanup();
                gameServices_.getLogger().log("Cleaned up Chunk Server event queue", BLUE);
            }
            if (eventQueueGameServerPing_.empty())
            {
                eventQueueGameServerPing_.forceCleanup();
                gameServices_.getLogger().log("Cleaned up Ping event queue", BLUE);
            }

            // Force cleanup of thread pool task queue if possible
            if (threadPool_.getTaskQueueSize() == 0)
            {
                // When there are no tasks, it's safe to shrink internal containers
                gameServices_.getLogger().log("Thread pool is idle, triggering internal cleanup", BLUE);
            }

// Force system to release unused memory back to OS (Linux/glibc specific)
#ifdef __GLIBC__
            if (malloc_trim(0))
            {
                gameServices_.getLogger().log("malloc_trim() released memory back to OS", BLUE);
            }
#endif

            // Log memory status after cleanup
            std::ifstream statusFile("/proc/self/status");
            if (statusFile.is_open())
            {
                std::string line;
                while (std::getline(statusFile, line))
                {
                    if (line.find("VmRSS:") == 0)
                    {
                        gameServices_.getLogger().log("Post-cleanup " + line, YELLOW);
                        break;
                    }
                    if (line.find("VmSize:") == 0)
                    {
                        gameServices_.getLogger().log("Post-cleanup " + line, YELLOW);
                    }
                }
                statusFile.close();
            }
        },
        30, // Run every 30 seconds - aggressive but not too frequent
        std::chrono::system_clock::now(),
        5 // unique task ID
    );

    scheduler_.scheduleTask(aggressiveCleanupTask);

    try
    {
        gameServices_.getLogger().log("Starting Game Server Event Loop...", YELLOW);
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (eventQueueGameServer_.popBatch(eventsBatch, BATCH_SIZE))
            {
                processBatch(eventsBatch);

                // Clear and shrink the batch vector to prevent memory retention
                eventsBatch.clear();
                eventsBatch.shrink_to_fit();
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(e.what(), RED);
    }
}

void
ChunkServer::mainEventLoopGS()
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

                // Clear and shrink the batch vector to prevent memory retention
                eventsBatch.clear();
                eventsBatch.shrink_to_fit();
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(e.what(), RED);
    }
}

void
ChunkServer::mainEventLoopPing()
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

                // Clear and shrink the ping events vector to prevent memory retention
                pingEvents.clear();
                pingEvents.shrink_to_fit();
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Ping Event Loop Error: " + std::string(e.what()), RED);
    }
}

void
ChunkServer::processPingBatch(const std::vector<Event> &pingEvents)
{
    for (const auto &event : pingEvents)
    {
        try
        {
            // Create a copy only when passing to the thread pool
            threadPool_.enqueueTask([this, eventCopy = Event(event)]() mutable
                {
                try
                {
                    eventHandler_.dispatchEvent(std::move(eventCopy));
                }
                catch (const std::exception &e)
                {
                    gameServices_.getLogger().logError("Error processing PING_EVENT: " + std::string(e.what()));
                } });
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Failed to enqueue PING task to ThreadPool: " + std::string(e.what()), RED);
            // Handle ping synchronously if ThreadPool is full
            try
            {
                eventHandler_.dispatchEvent(event);
            }
            catch (const std::exception &sync_e)
            {
                gameServices_.getLogger().logError("Error in synchronous ping handling: " + std::string(sync_e.what()), RED);
            }
        }
    }

    eventCondition.notify_all();
}

void
ChunkServer::processBatch(const std::vector<Event> &eventsBatch)
{
    // Process events directly from the batch without copying to avoid memory retention
    // Process all events as normal events for now to simplify memory management
    for (const auto &event : eventsBatch)
    {
        try
        {
            // Create a copy only when passing to the thread pool
            // Use move construction in the lambda capture to minimize copies
            threadPool_.enqueueTask([this, eventCopy = Event(event)]() mutable
                {
                try
                {
                    eventHandler_.dispatchEvent(std::move(eventCopy));
                }
                catch (const std::exception &e)
                {
                    gameServices_.getLogger().logError("Error in dispatchEvent: " + std::string(e.what()));
                } });
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Failed to enqueue task to ThreadPool: " + std::string(e.what()), RED);
            // Handle the event synchronously if ThreadPool is full
            try
            {
                eventHandler_.dispatchEvent(event);
            }
            catch (const std::exception &sync_e)
            {
                gameServices_.getLogger().logError("Error in synchronous event handling: " + std::string(sync_e.what()), RED);
            }
        }
    }

    eventCondition.notify_all();
}

void
ChunkServer::startMainEventLoop()
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

void
ChunkServer::stop()
{
    running_ = false;
    scheduler_.stop();
    eventCondition.notify_all();
}

ChunkServer::~ChunkServer()
{
    gameServices_.getLogger().log("Shutting down Chunk Server...", YELLOW);

    stop();

    if (event_game_server_thread_.joinable())
        event_game_server_thread_.join();

    if (event_chunk_server_thread_.joinable())
        event_chunk_server_thread_.join();

    if (event_ping_thread_.joinable())
        event_ping_thread_.join();
}
