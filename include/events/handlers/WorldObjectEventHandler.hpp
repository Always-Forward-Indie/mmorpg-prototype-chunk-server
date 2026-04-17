#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <string>
#include <vector>

/**
 * @brief Handles World Interactive Object (WIO) events on the chunk server.
 *
 * Responsibilities:
 *  - Receive static definitions from game-server (SET_ALL_WORLD_OBJECTS)
 *  - Process player WORLD_OBJECT_INTERACT requests:
 *      examine     → open dialogue session (npcId = -objectId)
 *      search      → roll & send loot, deplete global-scope objects
 *      activate    → set global state, broadcast update
 *      use_with_item → check inventory, consume item, trigger effect
 *      channeled   → start channel session, complete / cancel
 *  - Process WORLD_OBJECT_CHANNEL_CANCEL
 *  - Broadcast worldObjectStateUpdate to all clients in zone
 *  - Tick respawns (called from ZoneEventManager or main tick loop)
 */
class WorldObjectEventHandler : public BaseEventHandler
{
  public:
    WorldObjectEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    // ── Incoming events ────────────────────────────────────────────────────

    /// SET_ALL_WORLD_OBJECTS — store definitions in WorldObjectManager.
    void handleSetAllWorldObjectsEvent(const Event &event);

    /// WORLD_OBJECT_INTERACT — client wants to interact.
    void handleInteractEvent(const Event &event);

    /// WORLD_OBJECT_CHANNEL_CANCEL — client cancels channeled interaction.
    void handleChannelCancelEvent(const Event &event);

    // ── Outgoing helpers ───────────────────────────────────────────────────

    /// Send spawnWorldObjects packet (full list with current state) to a client on join.
    void sendWorldObjectsToClient(int clientId, int zoneId);

    /// Broadcast a worldObjectStateUpdate packet to all clients in the same zone.
    void broadcastStateUpdate(int objectId, const std::string &newState, int respawnSec);

    /// Called periodically to process completed channel sessions and respawn timers.
    void tick();

  private:
    // ── Type-specific interaction helpers ──────────────────────────────────
    void processExamine(int clientId, int characterId, const WorldObjectDataStruct &obj, PlayerContextStruct &ctx);
    void processSearch(int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct &ctx);
    void processActivate(int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct &ctx);
    void processUseWithItem(int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct &ctx);
    void processChanneledStart(int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct &ctx);

    /// Complete a finished channeled interaction (called from tick()).
    void completeChannel(int characterId, int objectId);

    /// Build the JSON payload for a single world object (including current state).
    nlohmann::json buildObjectJson(const WorldObjectDataStruct &obj) const;

    /// Send a worldObjectInteractResult packet to the client.
    void sendInteractResult(int clientId, int objectId, bool success, const std::string &errorCode = "", const nlohmann::json &extra = nullptr);

    std::shared_ptr<spdlog::logger> log_;
};
