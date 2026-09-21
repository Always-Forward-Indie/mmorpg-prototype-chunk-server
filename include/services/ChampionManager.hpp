#pragma once

#include "data/DataStructs.hpp"
#include "services/CharacterManager.hpp"
#include "services/CharacterStatsNotificationService.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Logger.hpp"
#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace spdlog
{
class logger;
}

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
    /// Explicit dependencies (no GameServices). statsNotify is optional and
    /// may be null (zone announcements are then skipped) — same pattern as
    /// LootManager::gameServices_. Tests pass nullptr; production passes
    /// &statsNotificationService_.
    ChampionManager(GameZoneManager &gameZones,
        GameConfigService &gameConfig,
        MobInstanceManager &mobInstances,
        CharacterManager &characters,
        MobManager &mobs,
        SpawnZoneManager &spawnZones,
        CharacterStatsNotificationService *statsNotify,
        Logger &logger);

    // ── Threshold Champion ───────────────────────────────────────────────────

    /**
     * @brief Record a mob kill in a game zone for Threshold Champion tracking.
     *
     * Attribution is by ORIGIN (spawn zone), not death position: a mob that
     * flees across a boundary (or dies in unzoned space) still credits the
     * zone it spawned in — otherwise kiting voids threshold progress. When
     * the spawn zone is unknown (spawnZoneId 0 or unmapped), falls back to
     * the position-resolved gameZoneId.
     *
     * If the kill count for (gameZoneId, mobTemplateId) reaches the configured
     * threshold AND no champion of that type is already active in the zone, a
     * Threshold Champion is spawned and the counter is reset — subject to two
     * Wave-6 gates: the post-threshold chance roll
     * (champion.spawn_chance_pct, default 100 = legacy) and the per-zone
     * simultaneous cap (champion.max_active_per_zone, default 3). A refused
     * spawn still resets the counter (natural retry next threshold).
     *
     * Call from CombatSystem::handleMobDeath (skip if mob.isChampion == true).
     *
     * @param gameZoneId      position-resolved zones.id (fallback, 0 = none)
     * @param mobTemplateId   mob_templates.id of the killed mob
     * @param spawnZoneId     mob's spawn zone id (mob.zoneId, 0 = unknown)
     */
    void recordMobKill(int gameZoneId, int mobTemplateId, int spawnZoneId = 0);

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

    /// Set the callback fired after every successful champion spawn (timed,
    /// threshold, survival-evolved materializations go through spawnChampion).
    /// Production wires it to push a real spawn list to the spawn cell's
    /// subscribers — thin deltas carry no slug/name, so without this clients
    /// never learn server-spawned mobs until an opportunistic snapshot.
    /// Null by default (unit tests opt in).
    void setSpawnNotifyCallback(std::function<void(const MobDataStruct &)> cb)
    {
        spawnNotifyCb_ = std::move(cb);
    }

    // ── Test seam: injectable clocks ──────────────────────────────────────────
    // Defaults are the wall clocks; unit tests override them to drive
    // timed/despawn/survival paths in milliseconds without DB or bots.
    // Pure seam: production behavior is bit-for-bit identical.
    void setEpochSecFn(std::function<int64_t()> fn) { epochSecFn_ = std::move(fn); }
    void setSteadyFn(std::function<std::chrono::steady_clock::time_point()> fn)
    {
        steadyFn_ = std::move(fn);
    }

    // ── Admin-RPC time travel (DEV only) ────────────────────────────────────
    // Advances BOTH injected clocks by `seconds` (composing onto any
    // previously injected fns, including unit-test fakes) and runs both
    // public ticks immediately. Reuses tick logic — no duplicated paths.
    // seconds must be > 0 (backwards travel breaks despawn/evolve
    // monotonicity); non-positive values are ignored.
    void skipTime(int64_t seconds);

    // ── Admin-RPC hermetic reset (DEV only) ─────────────────────────────────
    // Clears threshold kill counters, unregisters all ACTIVE champion
    // instances (releases the spawn block), and re-arms timed schedules
    // (spawned=false; next_spawn_at untouched — skipTime drives the rest).
    // Timed schedules need no DB reset (kill reschedules, skip refires);
    // per-character state is solved by ephemeral test bots, not scrubbing.
    // Call between tests only: no evict broadcast is sent, live sessions
    // would keep ghosts (fresh joins snapshot clean state).
    void resetThresholdState();

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
    // Single source of truth for the survival-evolution fallback (Wave 2.5).
    // Live value comes from game_config (survival_champion.evolve_hours).
    static constexpr int kDefaultEvolveHours = 12;
    // Post-threshold spawn chance (Wave 6): live value from game_config
    // (champion.spawn_chance_pct). 100 = legacy always-spawn.
    static constexpr float kDefaultSpawnChancePct = 100.0f;
    // Simultaneous-champion flood cap per zone (Wave 6): live value from
    // game_config (champion.max_active_per_zone).
    static constexpr int kDefaultMaxActivePerZone = 3;
    int evolveHours() const
    {
        return gameConfig_.getInt("survival_champion.evolve_hours", kDefaultEvolveHours);
    }

    GameZoneManager &gameZones_;
    GameConfigService &gameConfig_;
    // Injectable clocks (test seam; defaults = wall clocks, see setters above).
    std::function<int64_t()> epochSecFn_ = [] { return static_cast<int64_t>(std::time(nullptr)); };
    std::function<std::chrono::steady_clock::time_point()> steadyFn_ = [] { return std::chrono::steady_clock::now(); };    MobInstanceManager &mobInstances_;
    CharacterManager &characters_;
    MobManager &mobs_;
    SpawnZoneManager &spawnZones_;
    CharacterStatsNotificationService *statsNotify_; // optional, may be null
    Logger &logger_;
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

    /// Resolve the attributing game zone for a kill (lock-free entry:
    /// all lookups below synchronize internally).
    /// Order: pushed spawn-zone mapping (validated against known game
    /// zones — stale/garbage ids are ignored, never trusted blindly) →
    /// live containment of the spawn zone center → caller fallback
    /// (death position). Returns <= 0 when nothing attributes.
    int resolveGameZone(int spawnZoneId, int fallbackZoneId) const;

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

    /// Spawn-visibility callback (wired by EventHandler to push a real spawn
    /// list to subscribers). Null in unit tests unless set.
    std::function<void(const MobDataStruct &)> spawnNotifyCb_;
};
