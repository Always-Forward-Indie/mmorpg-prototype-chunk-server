#include "chunk_server/ChunkServer.hpp"
#include <chrono>
#include <fstream>
#include <unordered_map>
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
    // Set EventQueue for MobMovementManager to send combat events
    gameServices_.getMobMovementManager().setEventQueue(&eventQueueGameServer_);

    // Set EventQueue for MobInstanceManager to send loot generation events
    gameServices_.getMobInstanceManager().setEventQueue(&eventQueueChunkServer_);

    // Set EventQueue for LootManager to send item drop events to clients
    gameServices_.getLootManager().setEventQueue(&eventQueueGameServer_);

    // Set InventoryManager for LootManager to add items to player inventories
    gameServices_.getLootManager().setInventoryManager(&gameServices_.getInventoryManager());

    // Set EventQueue for InventoryManager to send inventory update events to clients
    gameServices_.getInventoryManager().setEventQueue(&eventQueueGameServer_);

    // Set EventQueue for HarvestManager to send harvest events to clients
    gameServices_.getHarvestManager().setEventQueue(&eventQueueGameServer_);

    // Set manager references for HarvestManager to broadcast harvest events
    gameServices_.getHarvestManager().setManagerReferences(&gameServices_.getClientManager(), &networkManager_);

    // Set CombatSystem for MobMovementManager to handle mob attacks
    gameServices_.getMobMovementManager().setCombatSystem(eventHandler_.getCombatEventHandler().getCombatSystem());
}

void
ChunkServer::sendSpawnEventsToClients(const SpawnZoneStruct &zone)
{
    auto connectedClients = gameServices_.getClientManager().getClientsListReadOnly();
    for (const auto &client : connectedClients)
    {
        if (client.clientId <= 0)
            continue;

        Event spawnMobsInZoneEvent(Event::SPAWN_MOBS_IN_ZONE, client.clientId, zone);
        eventQueueGameServer_.push(std::move(spawnMobsInZoneEvent));
    }
}

void
ChunkServer::mainEventLoopCH()
{
    gameServices_.getLogger().log("Add Tasks To Game Server Scheduler...", YELLOW);
    constexpr int BATCH_SIZE = 10;

    // Perform initial mob spawn directly instead of using scheduler to avoid infinite loop
    {
        gameServices_.getLogger().log("Starting initial mob spawn for all zones...", YELLOW);

        auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
        if (spawnZones.empty())
        {
            gameServices_.getLogger().logError("No spawn zones found for initial spawn!", RED);
        }
        else
        {
            int totalInitialSpawns = 0;
            for (const auto &zone : spawnZones)
            {
                gameServices_.getLogger().log("[DEBUG] Checking zone " + std::to_string(zone.second.zoneId) +
                                              " - spawnEnabled: " + (zone.second.spawnEnabled ? "true" : "false") +
                                              ", spawnMobId: " + std::to_string(zone.second.spawnMobId));

                if (zone.second.spawnEnabled && zone.second.spawnMobId > 0)
                {
                    auto spawnedMobs = gameServices_.getSpawnZoneManager().spawnMobsInZone(zone.second.zoneId);
                    if (!spawnedMobs.empty())
                    {
                        totalInitialSpawns += spawnedMobs.size();
                        gameServices_.getLogger().log("[INITIAL_SPAWN] Zone " + std::to_string(zone.second.zoneId) +
                                                      ": spawned " + std::to_string(spawnedMobs.size()) + " mobs");

                        // Send initial spawn events to all connected clients
                        sendSpawnEventsToClients(zone.second);
                    }
                    else
                    {
                        gameServices_.getLogger().log("[DEBUG] Zone " + std::to_string(zone.second.zoneId) +
                                                      " - no mobs spawned (zone may be full or mob template not loaded)");
                    }
                }
                else
                {
                    gameServices_.getLogger().log("[DEBUG] Zone " + std::to_string(zone.second.zoneId) +
                                                  " - skipped (spawn disabled or no mob ID)");
                }
            }

            gameServices_.getLogger().log("[INITIAL_SPAWN] Completed! Total mobs spawned: " + std::to_string(totalInitialSpawns), GREEN);
        }
    }

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

            bool anyMobsSpawned = false;
            int totalMobsSpawned = 0;

            // go through all spawn zones and spawn mobs
            for (const auto &zone : spawnZones)
            {
                if (zone.second.spawnEnabled && zone.second.spawnMobId > 0)
                {
                    // Check if zone needs mobs before attempting spawn
                    int currentMobCount = gameServices_.getMobInstanceManager().getAliveMobCountInZone(zone.second.zoneId);

                    if (currentMobCount < zone.second.spawnCount)
                    {
                        // Spawn mobs in this zone - only if needed
                        auto spawnedMobs = gameServices_.getSpawnZoneManager().spawnMobsInZone(zone.second.zoneId);

                        if (!spawnedMobs.empty())
                        {
                            anyMobsSpawned = true;
                            totalMobsSpawned += spawnedMobs.size();
                            gameServices_.getLogger().log("[INFO] Spawned " + std::to_string(spawnedMobs.size()) +
                                                          " mobs in zone: " + std::to_string(zone.second.zoneId) +
                                                          " (total alive: " + std::to_string(currentMobCount + spawnedMobs.size()) + "/" +
                                                          std::to_string(zone.second.spawnCount) + ")");

                            // Send spawn zone updates to all connected clients - only when mobs actually spawned
                            sendSpawnEventsToClients(zone.second);
                        }
                    }
                    else
                    {
                        // Zone is full - log occasionally (not every time)
                        static std::chrono::steady_clock::time_point lastFullZoneLog;
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::minutes>(now - lastFullZoneLog).count() >= 5)
                        {
                            gameServices_.getLogger().log("[DEBUG] Zone " + std::to_string(zone.second.zoneId) +
                                                          " is full (" + std::to_string(currentMobCount) + "/" +
                                                          std::to_string(zone.second.spawnCount) + ")");
                            lastFullZoneLog = now;
                        }
                    }
                }
                else
                {
                    gameServices_.getLogger().log("[DEBUG] Skipping zone " + std::to_string(zone.second.zoneId) +
                                                  " - spawn disabled or no mob ID set");
                }
            }

            // Log summary only if mobs were spawned
            if (anyMobsSpawned)
            {
                gameServices_.getLogger().log("[SPAWN_SUMMARY] Total mobs spawned this cycle: " + std::to_string(totalMobsSpawned));
            }
        },
        300, // Increased to 5 minutes (300 seconds) - less frequent safety checks
        std::chrono::system_clock::now(),
        1 // unique task ID
    );

    scheduler_.scheduleTask(spawnMobInZoneTask);

    // Task for respawn-time based mob spawning (more frequent than safety check)
    Task respawnMobTask(
        [&]
        {
            auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
            if (spawnZones.empty())
                return;

            for (const auto &zone : spawnZones)
            {
                if (!zone.second.spawnEnabled || zone.second.spawnMobId <= 0)
                    continue;

                int currentMobCount = gameServices_.getMobInstanceManager().getAliveMobCountInZone(zone.second.zoneId);

                // Only spawn if zone is not full
                if (currentMobCount < zone.second.spawnCount)
                {
                    auto spawnedMobs = gameServices_.getSpawnZoneManager().spawnMobsInZone(zone.second.zoneId);

                    if (!spawnedMobs.empty())
                    {
                        gameServices_.getLogger().log("[RESPAWN] Zone " + std::to_string(zone.second.zoneId) +
                                                      ": respawned " + std::to_string(spawnedMobs.size()) + " mobs");

                        // Send respawn events to all connected clients
                        sendSpawnEventsToClients(zone.second);
                    }
                }
            }
        },
        30,                                                          // Every 30 seconds - more frequent than safety check but respects respawn times
        std::chrono::system_clock::now() + std::chrono::seconds(10), // Start after initial spawn
        8                                                            // unique task ID
    );

    scheduler_.scheduleTask(respawnMobTask);

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

                    if (anyMobMoved)
                    {
                        // Get mobs that should send updates (moved significantly)
                        auto movedMobs = gameServices_.getMobInstanceManager().getMobInstancesInZone(zone.second.zoneId);
                        std::vector<MobDataStruct> mobsToSend;

                        for (const auto &mob : movedMobs)
                        {
                            if (gameServices_.getMobMovementManager().shouldSendMobUpdate(mob.uid, mob.position))
                            {
                                mobsToSend.push_back(mob);
                            }
                        }

                        // Only send updates if there are mobs that moved significantly
                        if (!mobsToSend.empty())
                        {
                            // send move mobs updates to all connected clients
                            auto connectedClients = gameServices_.getClientManager().getClientsListReadOnly();
                            for (const auto &client : connectedClients)
                            {
                                if (client.clientId <= 0) // Strict validation
                                    continue;
                                auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
                                // Send specific moved mobs instead of just zone ID
                                Event moveMobsInZoneEvent(Event::SPAWN_ZONE_MOVE_MOBS, client.clientId, mobsToSend);
                                eventQueueGameServer_.push(std::move(moveMobsInZoneEvent)); // Use move
                            }
                        }
                    }
                }
            }
        },
        1, // Increased frequency - every 1 second instead of 3
        std::chrono::system_clock::now(),
        2 // unique task ID
    );

    scheduler_.scheduleTask(moveMobInZoneTask);

    // Task for aggressive mob movement (faster update for mobs with targets)
    Task aggroMobMovementTask(
        [&]
        {
            // Get all spawn zones
            auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
            if (spawnZones.empty())
            {
                return;
            }

            // Track individual mobs that need position updates
            std::unordered_map<int, int> mobsToUpdate; // mobUID -> zoneId
            static auto lastBroadcastTime = std::chrono::steady_clock::now();

            // Check for mobs with active targets and move them more frequently
            for (const auto &zone : spawnZones)
            {
                if (zone.second.spawnEnabled && zone.second.spawnedMobsCount > 0)
                {
                    auto mobsInZone = gameServices_.getMobInstanceManager().getMobInstancesInZone(zone.second.zoneId);
                    int aggroMobsMovedCount = 0;

                    for (const auto &mob : mobsInZone)
                    {
                        if (mob.isDead || mob.currentHealth <= 0)
                            continue;

                        auto movementData = gameServices_.getMobMovementManager().getMobMovementData(mob.uid);

                        // If mob has a target or is returning to spawn, try to move it (respecting timing)
                        if (movementData.targetPlayerId > 0 || movementData.isReturningToSpawn)
                        {
                            // Move single mob (will respect timing constraints internally)
                            bool moved = gameServices_.getMobMovementManager().moveSingleMob(mob.uid, zone.second.zoneId);

                            if (moved)
                            {
                                aggroMobsMovedCount++;

                                // Get updated mob position after movement
                                auto updatedMob = gameServices_.getMobInstanceManager().getMobInstance(mob.uid);
                                if (updatedMob.uid > 0) // Valid mob
                                {
                                    PositionStruct currentPos = updatedMob.position;
                                    if (gameServices_.getMobMovementManager().shouldSendMobUpdate(mob.uid, currentPos))
                                    {
                                        mobsToUpdate[mob.uid] = zone.second.zoneId;
                                    }
                                }
                            }
                        }
                    }

                    if (aggroMobsMovedCount > 0)
                    {
                        // Removed excessive logging
                    }
                }
            }

            // Send position updates only for mobs that moved significantly
            // And limit update frequency to avoid client spam
            auto currentTime = std::chrono::steady_clock::now();
            auto timeSinceLastBroadcast = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastBroadcastTime);

            // Only send updates every 50ms minimum to avoid overwhelming clients
            if (!mobsToUpdate.empty() && timeSinceLastBroadcast.count() >= 50)
            {
                // Group mobs by zone for efficient broadcasting
                std::unordered_map<int, std::vector<int>> mobsByZone;
                for (const auto &[mobUID, zoneId] : mobsToUpdate)
                {
                    mobsByZone[zoneId].push_back(mobUID);
                }

                for (const auto &[zoneId, mobUIDs] : mobsByZone)
                {
                    // Get actual mob data for the moved mobs
                    std::vector<MobDataStruct> movedMobs;
                    for (int mobUID : mobUIDs)
                    {
                        auto mobData = gameServices_.getMobInstanceManager().getMobInstance(mobUID);
                        if (mobData.uid > 0) // Valid mob
                        {
                            movedMobs.push_back(mobData);
                        }
                    }

                    if (!movedMobs.empty())
                    {
                        // Send zone update events to all connected clients with specific moved mobs
                        auto connectedClients = gameServices_.getClientManager().getClientsListReadOnly();
                        for (const auto &client : connectedClients)
                        {
                            if (client.clientId <= 0)
                                continue;

                            // Send event with vector of moved mobs instead of just zoneId
                            Event mobUpdateEvent(Event::SPAWN_ZONE_MOVE_MOBS, client.clientId, movedMobs);
                            eventQueueGameServer_.push(std::move(mobUpdateEvent));
                        }
                    }
                }

                lastBroadcastTime = currentTime;
            }
        },
        0.05, // Very fast frequency - every 50ms for very smooth chase movement
        std::chrono::system_clock::now(),
        7 // unique task ID
    );

    scheduler_.scheduleTask(aggroMobMovementTask);

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

    // Task for updating harvest progress and completing harvests
    Task harvestUpdateTask(
        [&]
        {
            try
            {
                // Update harvest progress and complete any ready harvests
                gameServices_.getHarvestManager().updateHarvestProgress();

                // Clean up old corpses that can no longer be harvested
                gameServices_.getHarvestManager().cleanupOldCorpses(600); // 10 minutes
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error updating harvest progress: " + std::string(ex.what()));
            }
        },
        1, // Run every 1 second for responsive harvest updates
        std::chrono::system_clock::now(),
        7 // unique task ID
    );

    scheduler_.scheduleTask(harvestUpdateTask);

    // Task for cleaning up dead mobs and triggering respawn
    Task deadMobCleanupTask(
        [&]
        {
            auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
            if (spawnZones.empty())
                return;

            int totalDeadMobsRemoved = 0;
            std::vector<std::pair<int, int>> deadMobsToNotify; // mobUID, zoneId pairs

            for (const auto &zone : spawnZones)
            {
                if (!zone.second.spawnEnabled)
                    continue;

                // Get all mobs in zone from MobInstanceManager
                auto mobsInZone = gameServices_.getMobInstanceManager().getMobInstancesInZone(zone.second.zoneId);
                std::vector<int> deadMobUIDs;

                // Find dead mobs and generate loot
                for (const auto &mob : mobsInZone)
                {
                    if (mob.isDead || mob.currentHealth <= 0)
                    {
                        deadMobUIDs.push_back(mob.uid);
                        deadMobsToNotify.emplace_back(mob.uid, zone.second.zoneId);

                        // Note: Loot generation is now handled immediately in MobInstanceManager::updateMobHealth()
                        // when mob dies, not during cleanup task
                    }
                }

                // Remove dead mobs and trigger respawn logic
                for (int mobUID : deadMobUIDs)
                {
                    gameServices_.getSpawnZoneManager().mobDied(zone.second.zoneId, mobUID);
                    totalDeadMobsRemoved++;
                    gameServices_.getLogger().log("[CLEANUP] Removed dead mob UID " + std::to_string(mobUID) +
                                                  " from zone " + std::to_string(zone.second.zoneId));
                }
            }

            // Send death notifications AFTER cleanup is complete to avoid any potential deadlocks
            if (!deadMobsToNotify.empty())
            {
                for (const auto &[mobUID, zoneId] : deadMobsToNotify)
                {
                    try
                    {
                        std::pair<int, int> mobDeathData = std::make_pair(mobUID, zoneId);
                        Event deathEvent(Event::MOB_DEATH, 0, mobDeathData);
                        eventQueueGameServer_.push(std::move(deathEvent));
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError("Failed to send death notification for mob " +
                                                           std::to_string(mobUID) + ": " + ex.what());
                    }
                }
                gameServices_.getLogger().log("[CLEANUP] Sent " + std::to_string(deadMobsToNotify.size()) +
                                              " death notifications to clients");
            }

            if (totalDeadMobsRemoved > 0)
            {
                gameServices_.getLogger().log("[CLEANUP] Cleaned up " + std::to_string(totalDeadMobsRemoved) +
                                              " dead mobs across all zones");
            }
        },
        60,                                                          // Every 60 seconds - cleanup dead mobs to allow respawning
        std::chrono::system_clock::now() + std::chrono::seconds(30), // Start after 30 seconds
        9                                                            // unique task ID
    );

    scheduler_.scheduleTask(deadMobCleanupTask);

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
