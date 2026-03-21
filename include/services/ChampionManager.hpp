#pragma once

#include "data/DataStructs.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace spdlog
{
class logger;
}
class GameServices;

/**
 * @brief Unified authority for all champion-type mob events (Stage 3).
 *
 * Responsibilities:
 *  - Threshold Champion: tracks zone kill counters; spawns a champion when the
 *    threshold is reached; resets counter on kill or despawn.
 *  - Survival Champion: periodically checks all living mobs; those that have
 *    been alive longer than the configured threshold are evolved in-place.
 *  - Timed Champion: spawns named world bosses on a periodic schedule loaded
 *    from timed_champion_templates; notifies the game-server on kill so the DB
 *    record (next_spawn_at) can be updated.
 *  - Shared despawn timer for all champion types.
 *
 * Thread safety: all public methods are safe to call from multiple threads.
 * The class uses two independent mutexes:
 *  - counterMutex_  — for zoneKillCounters_
 *  - activeMutex_   — for active_ and timedTemplates_
 */
class ChampionManager
{
  public:
    explicit ChampionManager(GameServices *gs);

    // ── Threshold Champion ───────────────────────────────────────────────────

    /**
     * @brief Record a mob kill in a game zone for Threshold Champion tracking.
     *
     * If the kill count for (gameZoneId, mobTemplateId) reaches the configured
     * threshold AND no champion of that type is already active in the zone, a
     * Threshold Champion is spawned and the counter is reset.
     *
     * Call from CombatSystem::handleMobDeath (skip if mob.isChampion == true).
     *
     * @param gameZoneId      zones.id of the zone where the kill happened
     * @param mobTemplateId   mob_templates.id of the killed mob
     */
    void recordMobKill(int gameZoneId, int mobTemplateId);

    // ── Timed Champion ───────────────────────────────────────────────────────

    /**
     * @brief Load (or replace) timed champion templates received from game-server.
     *
     * Called from ZoneEventHandler::handleSetTimedChampionTemplatesEvent.
     */
    void loadTimedChampions(const std::vector<TimedChampionTemplate> &templates);

    /**
     * @brief Tick: check timed champion schedules and spawn/pre-announce.
     *
     * Should be called every 30 seconds from ChunkServer scheduler.
     */
    void tickTimedChampions();

    // ── Survival Champion ────────────────────────────────────────────────────

    /**
     * @brief Tick: scan all living mobs and evolve those that have survived
     *        longer than survival_champion.evolve_hours.
     *
     * Should be called every 5 minutes from ChunkServer scheduler.
     */
    void tickSurvivalEvolution();

    // ── Shared champion lifecycle ────────────────────────────────────────────

    /**
     * @brief Notify ChampionManager that a champion was killed.
     *
     * Removes the instance from active_, resets/halves the kill counter for
     * Threshold champions, and broadcasts a zone notification.
     * For Timed Champions (slug != "") also sends a TIMED_CHAMPION_KILLED
     * event to the game-server so next_spawn_at is updated in the DB.
     *
     * Call from CombatSystem::handleMobDeath when mob.isChampion == true.
     *
     * @param champUid      MobDataStruct::uid of the killed champion
     * @param killerCharId  characterId of the player that scored the kill
     * @param champSlug     Non-empty only for Timed Champions
     */
    void onChampionKilled(int champUid, int killerCharId, const std::string &champSlug = "");

    /// Set the callback used to push messages to the game-server (e.g. timedChampionKilled).
    /// Must be called from ChunkServer constructor before any tick.
    void setSendToGameServerCallback(std::function<void(const std::string &)> cb);

    // ── Internal spawn helper (also callable by tickTimedChampions) ──────────

    /**
     * @brief Spawn a champion instance and register it in the active list.
     *
     * @param mobTemplateId  Base mob template ID
     * @param gameZoneId     Game zone to spawn in
     * @param namePrefix     Displayed before the base mob name ("[Чемпион] ")
     * @param lootMult       lootMultiplier applied to the spawned instance
     * @param slug           Non-empty only for Timed Champions
     * @return uid of the spawned mob, or 0 on failure
     */
    int spawnChampion(int mobTemplateId,
        int gameZoneId,
        const std::string &namePrefix,
        float lootMult,
        const std::string &slug = "");

  private:
    GameServices *gs_;
    std::shared_ptr<spdlog::logger> log_;

    // ── Threshold kill counters ───────────────────────────────────────────────
    // gameZoneId → (mobTemplateId → killCount)
    std::unordered_map<int, std::unordered_map<int, int>> zoneKillCounters_;
    mutable std::mutex counterMutex_;

    // ── Active champion instances ─────────────────────────────────────────────
    struct ChampionInstance
    {
        int uid = 0;
        int gameZoneId = 0;
        int baseTemplateId = 0;
        std::string slug; ///< Non-empty for Timed Champions
        std::chrono::steady_clock::time_point spawnedAt;
        std::chrono::steady_clock::time_point despawnAt;
    };
    std::vector<ChampionInstance> active_;
    mutable std::mutex activeMutex_;

    // ── Timed champion runtime state ─────────────────────────────────────────
    struct TimedChampionState
    {
        TimedChampionTemplate tmpl;
        bool preAnnounceSent = false; ///< Reset each spawn cycle
        bool spawned = false;         ///< True while champion instance is alive
    };
    std::vector<TimedChampionState> timedStates_;
    mutable std::mutex timedMutex_;

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// Remove all champion instances whose despawnAt has passed.
    void checkDespawnedChampions();

    /// Evolve a single living mob into a Survival Champion.
    void evolveSurvivalMob(int mobUid);

    /// Resolve a world spawn position within the given game zone.
    PositionStruct resolveChampionSpawnPoint(int gameZoneId) const;

    /// Send a worldNotification to all players in a game zone.
    void broadcastToGameZone(int gameZoneId,
        const std::string &type,
        const nlohmann::json &data = nlohmann::json::object(),
        const std::string &priority = "high",
        const std::string &channel = "screen_center");

    /// Tell the game-server that a timed champion was killed.
    void sendTimedChampionKilledToGameServer(const std::string &slug, int killerCharId);

    /// Callback wired to GameServerWorker::sendDataToGameServer
    std::function<void(const std::string &)> sendToGameServerCb_;
};
