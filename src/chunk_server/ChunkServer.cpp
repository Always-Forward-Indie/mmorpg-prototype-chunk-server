#include "chunk_server/ChunkServer.hpp"
#include "services/CombatSystem.hpp"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#ifdef __GLIBC__
#include <malloc.h>
#include <spdlog/logger.h>
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
      networkManager_(networkManager),
      gameServerWorker_(gameServerWorker)
{
    log_ = gameServices_.getLogger().getSystem("gameloop");
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

    // Set InventoryManager for HarvestManager to add harvested items to player inventories
    gameServices_.getHarvestManager().setInventoryManager(&gameServices_.getInventoryManager());

    // Set manager references for HarvestManager to broadcast harvest events
    gameServices_.getHarvestManager().setManagerReferences(&gameServices_.getClientManager(), &networkManager_);

    // Set CombatSystem for MobMovementManager to handle mob attacks
    gameServices_.getMobMovementManager().setCombatSystem(eventHandler_.getCombatEventHandler().getCombatSystem());

    // Set MobManager so MobAIController can look up skill templates (plan §2.1)
    gameServices_.getMobMovementManager().setMobManager(&gameServices_.getMobManager());

    // Wire up QuestManager → GameServerWorker for persistence
    gameServices_.getQuestManager().setGameServerWorker(&gameServerWorker_);

    // Wire up QuestManager → NetworkManager for sending packets to clients
    gameServices_.getQuestManager().setNetworkManager(&networkManager_);

    // Wire up QuestManager into InventoryManager for onItemObtained triggers
    gameServices_.getInventoryManager().setQuestManager(&gameServices_.getQuestManager());

    // Wire up saveCharacterProgress: immediately persist exp/level to game server DB on grant
    gameServices_.getExperienceManager().setSaveProgressCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up inventory persistence: send item changes to game server DB immediately
    gameServices_.getInventoryManager().setSaveInventoryCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up LootManager callbacks for item instance ownership changes
    gameServices_.getLootManager().setNullifyItemOwnerCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    gameServices_.getLootManager().setDeleteInventoryItemCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    gameServices_.getLootManager().setTransferInventoryItemCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up durability persistence: send durability changes to game server DB immediately
    if (auto *cs = eventHandler_.getCombatEventHandler().getCombatSystem())
    {
        cs->setSaveDurabilityCallback(
            [this](const std::string &data)
            { gameServerWorker_.sendDataToGameServer(data); });

        // Wire up Item Soul kill_count persistence: send kill count changes to game server DB
        cs->setSaveItemKillCountCallback(
            [this](const std::string &data)
            { gameServerWorker_.sendDataToGameServer(data); });

        // Wire up attribute refresh trigger: when durability crosses warning threshold,
        // ask game server to recompute character attributes so the stat penalty applies.
        cs->setRefreshAttributesCallback(
            [this](int characterId)
            {
                nlohmann::json pkt;
                pkt["header"]["eventType"] = "getCharacterAttributes";
                pkt["header"]["clientId"] = 0;
                pkt["header"]["hash"] = "";
                pkt["body"]["characterId"] = characterId;
                gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
            });
    }

    // Wire up CharacterStatsNotificationService direct-send: world_notification packets go only
    // to the specific character's socket instead of being broadcast to every client in zone.
    gameServices_.getStatsNotificationService().setDirectSendCallback(
        [this](int characterId, const nlohmann::json &packet)
        {
            try
            {
                auto clientData = gameServices_.getClientManager().getClientDataByCharacterId(characterId);
                if (clientData.clientId <= 0)
                    return;
                auto sock = gameServices_.getClientManager().getClientSocket(clientData.clientId);
                if (!sock || !sock->is_open())
                    return;
                networkManager_.sendResponse(sock,
                    networkManager_.generateResponseMessage("success", packet));
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError(
                    "[WorldNotif] directSend error: " + std::string(ex.what()));
            }
        });

    // Wire up PityManager persistence callbacks
    gameServices_.getPityManager().setSaveCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up BestiaryManager persistence callbacks
    gameServices_.getBestiaryManager().setSaveCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up BestiaryManager tier-unlock notification
    gameServices_.getBestiaryManager().setNotifyCallback(
        [this](int charId, int mobTemplateId, int tierNum, int newKillCount, const std::string &catSlug)
        {
            try
            {
                const std::string mobSlug = gameServices_.getMobManager().getMobById(mobTemplateId).slug;
                nlohmann::json notifData;
                notifData["mobSlug"] = mobSlug;
                notifData["unlockedTier"] = tierNum;
                notifData["categorySlug"] = catSlug;
                notifData["killCount"] = newKillCount;
                gameServices_.getStatsNotificationService()
                    .sendWorldNotification(charId, "bestiary_tier_unlocked", notifData, "medium", "toast");

                // Title auto-grant: check bestiary conditions
                nlohmann::json titleEvent;
                titleEvent["mobSlug"] = mobSlug;
                titleEvent["tier"] = tierNum;
                gameServices_.getTitleManager().checkAndGrantTitles(charId, "bestiary", titleEvent);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError(
                    "[Bestiary] notifyCallback error: " + std::string(ex.what()));
            }
        });

    // Wire up BestiaryManager kill-count update — pushes updated count to client on every kill
    // so the bestiary overview list stays in sync without a full re-request
    gameServices_.getBestiaryManager().setKillUpdateCallback(
        [this](int charId, int mobTemplateId, int newKillCount)
        {
            try
            {
                const std::string mobSlug = gameServices_.getMobManager().getMobById(mobTemplateId).slug;
                if (mobSlug.empty())
                    return;
                nlohmann::json updateData;
                updateData["mobSlug"] = mobSlug;
                updateData["killCount"] = newKillCount;
                gameServices_.getStatsNotificationService()
                    .sendWorldNotification(charId, "bestiary_kill_update", updateData, "low", "silent");
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError(
                    "[Bestiary] killUpdateCallback error: " + std::string(ex.what()));
            }
        });

    // Wire up ChampionManager → GameServerWorker for timed champion persistence
    gameServices_.getChampionManager().setSendToGameServerCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up ReputationManager persistence callbacks
    gameServices_.getReputationManager().setSaveCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up ReputationManager → client notification on every rep change
    gameServices_.getReputationManager().setClientNotifyCallback(
        [this](int characterId, const std::string &factionSlug, int newValue, const std::string &newTier)
        {
            try
            {
                int clientId = 0;
                auto chars = gameServices_.getCharacterManager().getCharactersList();
                for (const auto &c : chars)
                    if (c.characterId == characterId)
                    {
                        clientId = c.clientId;
                        break;
                    }
                if (clientId <= 0)
                    return;
                auto sock = gameServices_.getClientManager().getClientSocket(clientId);
                if (!sock || !sock->is_open())
                    return;

                nlohmann::json pkt;
                pkt["header"]["eventType"] = "reputation_update";
                pkt["header"]["status"] = "success";
                pkt["body"]["characterId"] = characterId;
                pkt["body"]["factionSlug"] = factionSlug;
                pkt["body"]["value"] = newValue;
                pkt["body"]["tier"] = newTier;
                networkManager_.sendResponse(sock, networkManager_.generateResponseMessage("success", pkt));
            }
            catch (...)
            {
            }
        });

    // Wire up ReputationManager tier-change → world_notification callback
    gameServices_.getReputationManager().setTierChangeCallback(
        [this](int characterId, const std::string &factionSlug, const std::string &newTier, int value)
        {
            try
            {
                gameServices_.getStatsNotificationService().sendWorldNotification(
                    characterId,
                    "reputation_tier_change",
                    nlohmann::json{{"factionSlug", factionSlug}, {"newTier", newTier}, {"value", value}},
                    "high",
                    "toast");

                // Title auto-grant: check reputation conditions
                nlohmann::json titleEvent;
                titleEvent["factionSlug"] = factionSlug;
                titleEvent["tierName"] = newTier;
                gameServices_.getTitleManager().checkAndGrantTitles(characterId, "reputation", titleEvent);
            }
            catch (...)
            {
            }
        });

    // Wire up MasteryManager persistence callbacks
    gameServices_.getMasteryManager().setSaveCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up MasteryManager → client notification on mastery progress flush
    gameServices_.getMasteryManager().setClientNotifyCallback(
        [this](int characterId, const std::string &masterySlug, float newValue, const std::string & /*tierSlug*/)
        {
            try
            {
                int clientId = 0;
                auto chars = gameServices_.getCharacterManager().getCharactersList();
                for (const auto &c : chars)
                    if (c.characterId == characterId)
                    {
                        clientId = c.clientId;
                        break;
                    }
                if (clientId <= 0)
                    return;
                auto sock = gameServices_.getClientManager().getClientSocket(clientId);
                if (!sock || !sock->is_open())
                    return;

                nlohmann::json pkt;
                pkt["header"]["eventType"] = "mastery_update";
                pkt["header"]["status"] = "success";
                pkt["body"]["characterId"] = characterId;
                pkt["body"]["masterySlug"] = masterySlug;
                pkt["body"]["value"] = newValue;
                networkManager_.sendResponse(sock, networkManager_.generateResponseMessage("success", pkt));
            }
            catch (...)
            {
            }
        });

    // Wire up TitleManager persistence callbacks
    gameServices_.getTitleManager().setSaveCallback(
        [this](const std::string &data)
        { gameServerWorker_.sendDataToGameServer(data); });

    // Wire up TitleManager → client direct-send callback
    gameServices_.getTitleManager().setNotifyClientCallback(
        [this](int characterId, const nlohmann::json &pkt)
        {
            try
            {
                int clientId = 0;
                auto chars = gameServices_.getCharacterManager().getCharactersList();
                for (const auto &c : chars)
                    if (c.characterId == characterId)
                    {
                        clientId = c.clientId;
                        break;
                    }
                if (clientId <= 0)
                    return;
                auto sock = gameServices_.getClientManager().getClientSocket(clientId);
                if (!sock || !sock->is_open())
                    return;
                networkManager_.sendResponse(sock, networkManager_.generateResponseMessage("success", pkt));
            }
            catch (...)
            {
            }
        });
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
    log_->info("Add Tasks To Game Server Scheduler...");
    constexpr int BATCH_SIZE = 10;

    // Perform initial mob spawn directly instead of using scheduler to avoid infinite loop
    {
        log_->info("Starting initial mob spawn for all zones...");

        auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
        if (spawnZones.empty())
        {
            log_->error("No spawn zones found for initial spawn!");
        }
        else
        {
            int totalInitialSpawns = 0;
            for (const auto &zone : spawnZones)
            {
                gameServices_.getLogger().log("[DEBUG] Checking zone " + std::to_string(zone.second.zoneId) +
                                              " - spawnEnabled: " + (zone.second.spawnEnabled ? "true" : "false") +
                                              ", hasSpawnConfig: " + (zone.second.hasSpawnConfig() ? "true" : "false"));

                if (zone.second.spawnEnabled && zone.second.hasSpawnConfig())
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

            log_->info("[INITIAL_SPAWN] Completed! Total mobs spawned: " + std::to_string(totalInitialSpawns));
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
                log_->error("No spawn zones found, cannot spawn mobs!");
                return;
            }

            bool anyMobsSpawned = false;
            int totalMobsSpawned = 0;

            // go through all spawn zones and spawn mobs
            for (const auto &zone : spawnZones)
            {
                if (zone.second.spawnEnabled && zone.second.hasSpawnConfig())
                {
                    // Check if zone needs mobs before attempting spawn
                    int currentMobCount = gameServices_.getMobInstanceManager().getAliveMobCountInZone(zone.second.zoneId);

                    if (currentMobCount < zone.second.totalSpawnCount())
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
                                                          std::to_string(zone.second.totalSpawnCount()) + ")");
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
                                                          std::to_string(zone.second.totalSpawnCount()) + ")");
                            lastFullZoneLog = now;
                        }
                    }
                }
                else
                {
                    log_->info("[DEBUG] Skipping zone " + std::to_string(zone.second.zoneId) +
                               " - spawn disabled or no mob entries");
                }
            }

            // Log summary only if mobs were spawned
            if (anyMobsSpawned)
            {
                log_->info("[SPAWN_SUMMARY] Total mobs spawned this cycle: " + std::to_string(totalMobsSpawned));
            }
        },
        300000, // Increased to 5 minutes (300 seconds) - less frequent safety checks
        std::chrono::steady_clock::now(),
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
                if (!zone.second.spawnEnabled || !zone.second.hasSpawnConfig())
                    continue;

                int currentMobCount = gameServices_.getMobInstanceManager().getAliveMobCountInZone(zone.second.zoneId);

                // Only spawn if zone is not full
                if (currentMobCount < zone.second.totalSpawnCount())
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
        30000,                                                                   // Every 30 seconds - more frequent than safety check but respects respawn times
        std::chrono::steady_clock::now() + std::chrono::milliseconds(10 * 1000), // Start after initial spawn
        8                                                                        // unique task ID
    );

    scheduler_.scheduleTask(respawnMobTask);

    // -------------------------------------------------------------------------
    // Unified mob movement + broadcast tick (replaces the old split-brain pair
    // of moveMobInZoneTask@1000ms and aggroMobMovementTask@50ms).
    //
    // Running at 50ms gives a fine-grained scheduling resolution, but the
    // actual movement of each mob is gated internally:
    //   • PATROLLING mobs  — governed by nextMoveTime    (~200-400ms cadence)
    //   • CHASING mobs     — governed by chaseMovementInterval (100ms)
    //   • RETURNING mobs   — governed by returnMovementInterval (150ms)
    //
    // Broadcast frequency is further limited per-mob via lastBroadcastMs:
    //   • PATROLLING     → min 200ms between packets  (client uses waypoint DR)
    //   • combat states  → min 100ms between packets  (Hermite interpolation)
    //
    // forceNextUpdate (triggered by combat state transitions) bypasses the
    // per-mob rate limit so state-change packets are never delayed.
    // -------------------------------------------------------------------------
    Task unifiedMobTickTask(
        [&]
        {
            auto spawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
            if (spawnZones.empty())
                return;

            const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                                      .count();

            // Collect updates for ALL zones, keyed by clientId, so we can push one
            // event per client instead of one per zone — reduces async_write frequency
            // by N_zones and prevents mob-position packets from head-of-line-blocking
            // combat packets in the per-socket write queue.
            auto connectedClients = gameServices_.getClientManager().getClientsListReadOnly();
            std::unordered_map<int, std::vector<MobMoveUpdateStruct>> clientUpdates;
            clientUpdates.reserve(connectedClients.size());

            for (const auto &zone : spawnZones)
            {
                if (!zone.second.spawnEnabled || zone.second.spawnedMobsCount == 0)
                    continue;

                // Single movement tick for ALL mobs in zone.
                // runMobTick (called internally) handles every state: patrol,
                // chase, return, flee. Internal timing guards gate actual movement.
                gameServices_.getMobMovementManager().moveMobsInZone(zone.second.zoneId);

                // Collect mobs that need a broadcast this tick.
                auto mobsInZone = gameServices_.getMobInstanceManager().getMobInstancesInZone(zone.second.zoneId);

                for (const auto &mob : mobsInZone)
                {
                    if (mob.isDead || mob.currentHealth <= 0)
                        continue;

                    auto mvData = gameServices_.getMobMovementManager().getMobMovementData(mob.uid);

                    // Per-mob rate limit: patrol gets 200ms budget, combat 100ms.
                    // forceNextUpdate bypasses the limit for instant state-change delivery.
                    const bool isActiveCombat = (mvData.combatState != MobCombatState::PATROLLING);
                    const int64_t minIntervalMs = isActiveCombat ? 100 : 200;

                    if (!mvData.forceNextUpdate && (nowMs - mvData.lastBroadcastMs) < minIntervalMs)
                        continue;

                    // Skip the shouldSendMobUpdate write-lock for states where the mob
                    // physically cannot move (PREPARING_ATTACK, ATTACKING, ATTACK_COOLDOWN,
                    // EVADING).  forceNextUpdate bypasses this guard so state-change packets
                    // (e.g. CHASING → PREPARING_ATTACK) are still delivered immediately.
                    if (!mvData.forceNextUpdate)
                    {
                        const bool canMobMoveInState =
                            mvData.combatState == MobCombatState::PATROLLING ||
                            mvData.combatState == MobCombatState::CHASING ||
                            mvData.combatState == MobCombatState::RETURNING ||
                            mvData.combatState == MobCombatState::FLEEING;
                        if (!canMobMoveInState)
                        {
                            // Advance the timer so next poll respects the rate-limit instead
                            // of perpetually bypassing it on a stationary mob.
                            gameServices_.getMobMovementManager().updateLastBroadcastMs(mob.uid, nowMs);
                            continue;
                        }
                    }

                    // Distance + forceNextUpdate filter (clears forceNextUpdate on true).
                    if (!gameServices_.getMobMovementManager().shouldSendMobUpdate(mob.uid, mob.position))
                    {
                        // Mob hasn't moved — reset the broadcast timer so the rate-limit
                        // fires at the proper interval next time rather than being bypassed
                        // every tick on a stationary mob.
                        gameServices_.getMobMovementManager().updateLastBroadcastMs(mob.uid, nowMs);
                        continue;
                    }

                    MobMoveUpdateStruct upd;
                    upd.uid = mob.uid;
                    upd.zoneId = mob.zoneId;
                    upd.position = mob.position;
                    upd.dirX = mvData.movementDirectionX;
                    upd.dirY = mvData.movementDirectionY;
                    upd.speed = mvData.currentSpeedUnitsPerSec;
                    upd.combatState = static_cast<int>(mvData.combatState);
                    upd.stepTimestampMs = mvData.lastStepTimestampMs;

                    // Patrol waypoint: lets the client dead-reckon toward the mob's
                    // active waypoint at server speed, so 200ms packets look smooth.
                    upd.hasWaypoint = mvData.hasPatrolTarget &&
                                      (mvData.combatState == MobCombatState::PATROLLING);
                    upd.waypointX = mvData.patrolTargetPoint.positionX;
                    upd.waypointY = mvData.patrolTargetPoint.positionY;

                    gameServices_.getMobMovementManager().updateLastBroadcastMs(mob.uid, nowMs);

                    for (const auto &client : connectedClients)
                    {
                        if (client.clientId <= 0)
                            continue;
                        clientUpdates[client.clientId].push_back(upd);
                    }
                }
            }

            // Push ONE event per connected client containing ALL zone updates.
            // This replaces the old per-zone-per-client approach that produced
            // N_zones × N_clients events per tick.
            for (auto &[clientId, updates] : clientUpdates)
            {
                Event mobUpdateEvent(Event::MOB_MOVE_UPDATE, clientId, std::move(updates));
                eventQueueGameServer_.push(std::move(mobUpdateEvent));
            }
        },
        50, // 50ms scheduling resolution; actual mob movement is gated internally
        std::chrono::steady_clock::now(),
        2 // task ID (reuses slot 2, previously moveMobInZoneTask)
    );

    scheduler_.scheduleTask(unifiedMobTickTask);

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
        100, // Every 100 ms — needed for responsive instant-skill execution and cast-time resolution
        std::chrono::steady_clock::now(),
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
                log_->info("Force removed disconnected client: " + std::to_string(clientId));
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
                log_->error("Event queues are getting large - potential memory leak!");
            }
        },
        10000, // Run every 10 seconds (more frequent cleanup)
        std::chrono::steady_clock::now(),
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
                gameServices_.getHarvestManager().cleanupOldCorpses(30); // 30 seconds
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error updating harvest progress: " + std::string(ex.what()));
            }
        },
        1000, // Run every 1 second for responsive harvest updates
        std::chrono::steady_clock::now(),
        7 // unique task ID
    );

    scheduler_.scheduleTask(harvestUpdateTask);

    // Task for cleaning up expired ground items (player/mob drops older than 5 minutes)
    Task droppedItemCleanupTask(
        [&]
        {
            try
            {
                gameServices_.getLootManager().cleanupOldDroppedItems(300); // 5 minutes
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error cleaning up dropped items: " + std::string(ex.what()));
            }
        },
        60000, // Run every 60 seconds
        std::chrono::steady_clock::now(),
        17 // unique task ID
    );

    scheduler_.scheduleTask(droppedItemCleanupTask);

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

                // Find dead mobs whose corpse timer has expired
                constexpr int64_t CORPSE_DURATION_MS = 30'000; // corpse lies for 30 s
                auto now = std::chrono::steady_clock::now();
                for (const auto &mob : mobsInZone)
                {
                    if (!mob.isDead && mob.currentHealth > 0)
                        continue;

                    // Only remove once the corpse has been visible for the full duration.
                    // deathTimestamp == {} means the mob was marked dead without recording
                    // a timestamp (legacy path) → remove immediately to avoid infinite retention.
                    if (mob.deathTimestamp != std::chrono::steady_clock::time_point{} &&
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - mob.deathTimestamp).count() < CORPSE_DURATION_MS)
                    {
                        continue; // corpse still within display window
                    }

                    deadMobUIDs.push_back(mob.uid);

                    // Skip mobDeath re-broadcast if the corpse was already removed by the
                    // harvest system (mobUID==0), OR if harvest completed but loot hasn't
                    // been picked up yet (hasBeenHarvested==true).  In both cases the client
                    // already knows the mob is dead and must not see a duplicate corpse.
                    auto corpseData = gameServices_.getHarvestManager().getCorpseByUID(mob.uid);
                    bool corpseAlreadyRemoved =
                        (corpseData.mobUID == 0 || corpseData.hasBeenHarvested);
                    if (!corpseAlreadyRemoved)
                    {
                        deadMobsToNotify.emplace_back(mob.uid, zone.second.zoneId);
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
                log_->info("[CLEANUP] Cleaned up " + std::to_string(totalDeadMobsRemoved) +
                           " dead mobs across all zones");
            }
        },
        5000,                                                                    // Every 5 seconds - check if corpse duration has expired
        std::chrono::steady_clock::now() + std::chrono::milliseconds(30 * 1000), // Start after 30 seconds
        9                                                                        // unique task ID
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
                        log_->info("Memory usage: " + line);
                        break;
                    }
                }
                statusFile.close();
            }
        },
        5000, // Run every 5 seconds
        std::chrono::steady_clock::now(),
        4 // unique task ID
    );

    scheduler_.scheduleTask(memoryMonitorTask);

    // Add aggressive memory cleanup task to force memory release
    Task aggressiveCleanupTask(
        [&]
        {
            log_->info("Running aggressive memory cleanup...");

            // Force garbage collection by explicitly clearing and shrinking containers
            // Force memory cleanup in all managers
            gameServices_.getClientManager().forceCleanupMemory();
            networkManager_.cleanupInactiveSessions();

            // Force cleanup of event queues when they're empty
            if (eventQueueGameServer_.empty())
            {
                eventQueueGameServer_.forceCleanup();
                log_->debug("Cleaned up Game Server event queue");
            }
            if (eventQueueChunkServer_.empty())
            {
                eventQueueChunkServer_.forceCleanup();
                log_->debug("Cleaned up Chunk Server event queue");
            }
            if (eventQueueGameServerPing_.empty())
            {
                eventQueueGameServerPing_.forceCleanup();
                log_->debug("Cleaned up Ping event queue");
            }

            // Force cleanup of thread pool task queue if possible
            if (threadPool_.getTaskQueueSize() == 0)
            {
                // When there are no tasks, it's safe to shrink internal containers
                log_->debug("Thread pool is idle, triggering internal cleanup");
            }

// Force system to release unused memory back to OS (Linux/glibc specific)
#ifdef __GLIBC__
            if (malloc_trim(0))
            {
                log_->debug("malloc_trim() released memory back to OS");
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
                        log_->info("Post-cleanup " + line);
                        break;
                    }
                    if (line.find("VmSize:") == 0)
                    {
                        log_->info("Post-cleanup " + line);
                    }
                }
                statusFile.close();
            }
        },
        30000, // Run every 30 seconds - aggressive but not too frequent
        std::chrono::steady_clock::now(),
        5 // unique task ID
    );

    scheduler_.scheduleTask(aggressiveCleanupTask);

    // Periodic task: save all online player positions to the database every 30 seconds
    Task savePositionsTask(
        [this]
        {
            auto charactersList = gameServices_.getCharacterManager().getCharactersList();
            if (charactersList.empty())
                return;

            // Build savePositions JSON packet for the game server
            nlohmann::json charactersArray = nlohmann::json::array();
            for (const auto &character : charactersList)
            {
                if (character.characterId <= 0)
                    continue;

                nlohmann::json entry;
                entry["characterId"] = character.characterId;
                entry["posX"] = character.characterPosition.positionX;
                entry["posY"] = character.characterPosition.positionY;
                entry["posZ"] = character.characterPosition.positionZ;
                entry["rotZ"] = character.characterPosition.rotationZ;
                charactersArray.push_back(entry);
            }

            if (charactersArray.empty())
                return;

            nlohmann::json packet;
            packet["header"]["eventType"] = "savePositions";
            packet["header"]["clientId"] = 0;
            packet["header"]["hash"] = "";
            packet["body"]["characters"] = charactersArray;

            gameServerWorker_.sendDataToGameServer(packet.dump() + "\n");

            gameServices_.getLogger().log(
                "[SAVE_POSITIONS] Sent " + std::to_string(charactersArray.size()) +
                    " position(s) to game server",
                GREEN);
        },
        30000,                                                                   // Every 30 seconds
        std::chrono::steady_clock::now() + std::chrono::milliseconds(30 * 1000), // First run after 30s
        10                                                                       // unique task ID
    );

    scheduler_.scheduleTask(savePositionsTask);

    // Periodic task: clean up expired dialogue sessions every 60 seconds
    Task cleanupDialogueSessionsTask(
        [this]
        {
            gameServices_.getDialogueSessionManager().cleanupExpiredSessions();
        },
        60000,                                                                   // Every 60 seconds
        std::chrono::steady_clock::now() + std::chrono::milliseconds(60 * 1000), // First run after 60s
        11                                                                       // unique task ID
    );
    scheduler_.scheduleTask(cleanupDialogueSessionsTask);

    // Periodic task: flush dirty quest progress + pending flags every 5 seconds
    Task flushQuestProgressTask(
        [this]
        {
            gameServices_.getQuestManager().flushDirtyProgress();
        },
        5000,                                                                   // Every 5 seconds
        std::chrono::steady_clock::now() + std::chrono::milliseconds(5 * 1000), // First run after 5s
        12                                                                      // unique task ID
    );
    scheduler_.scheduleTask(flushQuestProgressTask);

    // CRITICAL-8: Periodic dead-socket cleanup separated from hot broadcast path.
    // broadcastToAllClients no longer removes dead sockets inline;
    // this task does it every 30 seconds under unique_lock.
    Task cleanupDeadSocketsTask(
        [this]
        {
            try
            {
                gameServices_.getClientManager().cleanupDeadSockets();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("[Scheduler] cleanupDeadSockets error: " + std::string(ex.what()));
            }
        },
        30000,                                                                   // Every 30 seconds
        std::chrono::steady_clock::now() + std::chrono::milliseconds(30 * 1000), // First run after 30s
        13                                                                       // unique task ID
    );
    scheduler_.scheduleTask(cleanupDeadSocketsTask);

    // ARCH-4: Periodic task: save all online player HP and Mana to the database every 10 seconds.
    // Prevents loss of current health/mana on unexpected server restart or crash.
    Task saveHpManaTask(
        [this]
        {
            auto charactersList = gameServices_.getCharacterManager().getCharactersList();
            if (charactersList.empty())
                return;

            nlohmann::json charactersArray = nlohmann::json::array();
            for (const auto &character : charactersList)
            {
                if (character.characterId <= 0)
                    continue;

                nlohmann::json entry;
                entry["characterId"] = character.characterId;
                entry["currentHp"] = character.characterCurrentHealth;
                entry["currentMana"] = character.characterCurrentMana;
                charactersArray.push_back(entry);
            }

            if (charactersArray.empty())
                return;

            nlohmann::json packet;
            packet["header"]["eventType"] = "saveHpMana";
            packet["header"]["clientId"] = 0;
            packet["header"]["hash"] = "";
            packet["body"]["characters"] = charactersArray;

            gameServerWorker_.sendDataToGameServer(packet.dump() + "\n");

            gameServices_.getLogger().log(
                "[SAVE_HP_MANA] Saved HP/Mana for " + std::to_string(charactersArray.size()) +
                    " character(s)",
                GREEN);
        },
        10000,                                                                   // Every 10 seconds
        std::chrono::steady_clock::now() + std::chrono::milliseconds(10 * 1000), // First run after 10s
        14                                                                       // unique task ID
    );
    scheduler_.scheduleTask(saveHpManaTask);

    // HP/MP regeneration task — fires every 4 seconds by default.
    // Actual regen amounts are driven by config keys (regen.*) read live each tick.
    // The tick interval itself uses the config value at start-up; if not yet loaded
    // the fallback of 4000 ms applies.
    const int regenIntervalMs = gameServices_.getGameConfigService().getInt("regen.tickIntervalMs", 4000);
    Task regenTickTask(
        [this]
        {
            try
            {
                gameServices_.getRegenManager().tickRegen();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("[Regen] tickRegen error: " + std::string(ex.what()));
            }
        },
        regenIntervalMs,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(regenIntervalMs),
        15 // unique task ID
    );
    scheduler_.scheduleTask(regenTickTask);

    // Timed champion tick — checks spawn windows and sends pre-announcements every 30 s
    Task timedChampionTickTask(
        [this]
        {
            try
            {
                gameServices_.getChampionManager().tickTimedChampions();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("[Champion] tickTimedChampions error: " + std::string(ex.what()));
            }
        },
        30000,
        std::chrono::steady_clock::now() + std::chrono::seconds(30),
        16 // unique task ID
    );
    scheduler_.scheduleTask(timedChampionTickTask);

    // Survival champion evolution tick — checks long-lived mobs every 5 minutes
    Task survivalEvolutionTickTask(
        [this]
        {
            try
            {
                gameServices_.getChampionManager().tickSurvivalEvolution();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("[Champion] tickSurvivalEvolution error: " + std::string(ex.what()));
            }
        },
        300000,
        std::chrono::steady_clock::now() + std::chrono::seconds(300),
        18 // unique task ID
    );
    scheduler_.scheduleTask(survivalEvolutionTickTask);

    // Zone event scheduler tick — checks timed/random event triggers every 30 s
    Task zoneEventTickTask(
        [this]
        {
            try
            {
                gameServices_.getZoneEventManager().tickEventScheduler();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("[ZoneEvent] tickEventScheduler error: " + std::string(ex.what()));
            }
        },
        30000,
        std::chrono::steady_clock::now() + std::chrono::seconds(30),
        19 // unique task ID
    );
    scheduler_.scheduleTask(zoneEventTickTask);

    try
    {
        log_->info("Starting Game Server Event Loop...");
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (!eventQueueGameServer_.popBatch(eventsBatch, BATCH_SIZE))
                break; // queue stopped (HIGH-5)
            processBatch(eventsBatch);

            // Clear and shrink the batch vector to prevent memory retention
            eventsBatch.clear();
            eventsBatch.shrink_to_fit();
        }
    }
    catch (const std::exception &e)
    {
        log_->error(e.what());
    }
}

void
ChunkServer::mainEventLoopGS()
{
    constexpr int BATCH_SIZE = 10;
    log_->info("Add Tasks To Chunk Server Scheduler...");

    try
    {
        log_->info("Starting Chunk Server Event Loop...");
        while (running_)
        {
            std::vector<Event> eventsBatch;
            if (!eventQueueChunkServer_.popBatch(eventsBatch, BATCH_SIZE))
                break; // queue stopped (HIGH-5)
            processBatch(eventsBatch);

            // Clear and shrink the batch vector to prevent memory retention
            eventsBatch.clear();
            eventsBatch.shrink_to_fit();
        }
    }
    catch (const std::exception &e)
    {
        log_->error(e.what());
    }
}

void
ChunkServer::mainEventLoopPing()
{
    constexpr int BATCH_SIZE = 1; // Ping обрабатывай сразу

    log_->info("Starting Ping Event Loop...");

    try
    {
        while (running_)
        {
            std::vector<Event> pingEvents;
            if (!eventQueueGameServerPing_.popBatch(pingEvents, BATCH_SIZE))
                break; // queue stopped (HIGH-5)
            processPingBatch(pingEvents);

            // Clear and shrink the ping events vector to prevent memory retention
            pingEvents.clear();
            pingEvents.shrink_to_fit();
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
        log_->warn("Chunk server event loops are already running!");
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
    // HIGH-5: stop all event queues so blocked popBatch() calls unblock and
    // the event-loop threads can exit cleanly.
    eventQueueGameServer_.stop();
    eventQueueChunkServer_.stop();
    eventQueueGameServerPing_.stop();
    scheduler_.stop();
    eventCondition.notify_all();
}

ChunkServer::~ChunkServer()
{
    log_->info("Shutting down Chunk Server...");

    stop();

    if (event_game_server_thread_.joinable())
        event_game_server_thread_.join();

    if (event_chunk_server_thread_.joinable())
        event_chunk_server_thread_.join();

    if (event_ping_thread_.joinable())
        event_ping_thread_.join();
}
