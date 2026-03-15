#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace spdlog
{
class logger;
}
class GameServices;

/**
 * @brief ZoneEventManager — temporary world events that modify zone behaviour.
 *
 * Supports three trigger types:
 *   'scheduled' — fires every N hours
 *   'random'    — random chance per tick (30 s cadence)
 *   'manual'    — started externally (admin, invasion finale, etc.)
 *
 * Active events modify:
 *   – LootManager     via getLootMultiplier(gameZoneId)
 *   – MobMovement     via getMobSpeedMultiplier(gameZoneId)
 *   – (future) SpawnZoneManager via getSpawnRateMultiplier(gameZoneId)
 *
 * Hot-path accessors are lock-free: they read an atomic snapshot that is
 * rebuilt only when events start/end.
 *
 * Templates are loaded from DB via SET_ZONE_EVENT_TEMPLATES at startup.
 * tickEventScheduler() must be called every 30 s (registered in ChunkServer).
 */
class ZoneEventManager
{
  public:
    explicit ZoneEventManager(GameServices *gs);
    ~ZoneEventManager() = default;

    // ── Template management ────────────────────────────────────────────────
    struct ZoneEventTemplate
    {
        int id = 0;
        std::string slug;
        int gameZoneId = 0;      ///< 0 = any zone / global
        std::string triggerType; ///< 'scheduled' | 'random' | 'manual'
        int durationSec = 1200;
        float lootMultiplier = 1.0f;
        float spawnRateMultiplier = 1.0f;
        float mobSpeedMultiplier = 1.0f;
        std::string announceKey;
        int intervalHours = 0;
        float randomChancePerHour = 0.0f;
        // Invasion
        bool hasInvasionWave = false;
        int invasionMobTemplateId = 0;
        int invasionWaveCount = 0;
        int invasionChampionTemplateId = 0;
        std::string invasionChampionSlug;
    };

    void loadTemplates(const std::vector<ZoneEventTemplate> &templates);

    // ── Activation / deactivation ──────────────────────────────────────────
    void startEvent(const std::string &slug, int overrideGameZoneId = 0);
    void endEvent(const std::string &slug);

    // ── Ticker (call every 30 s from ChunkServer) ──────────────────────────
    void tickEventScheduler();

    // ── Hot-path accessors (lock-free via atomic snapshot) ─────────────────
    float getLootMultiplier(int gameZoneId) const;
    float getMobSpeedMultiplier(int gameZoneId) const;
    float getSpawnRateMultiplier(int gameZoneId) const;

    // ── Query ──────────────────────────────────────────────────────────────
    bool hasActiveEvent(int gameZoneId) const;

  private:
    struct ActiveZoneEvent
    {
        std::string slug;
        int gameZoneId = 0;
        float lootMultiplier = 1.0f;
        float spawnRateMultiplier = 1.0f;
        float mobSpeedMultiplier = 1.0f;
        std::chrono::steady_clock::time_point endsAt;
    };

    // Snapshot used by hot-path readers (rebuilt on start/end)
    struct EventSnapshot
    {
        // gameZoneId → aggregated multipliers from all active events in that zone
        std::unordered_map<int, ActiveZoneEvent> byZone;
        // 0 → global events that apply to every zone
        std::vector<ActiveZoneEvent> global;
    };

    void rebuildSnapshot();
    void endEventInternal(const std::string &slug);

    // Templates
    std::vector<ZoneEventTemplate> templates_;

    // Active events: slug → event
    std::unordered_map<std::string, ActiveZoneEvent> active_;
    mutable std::shared_mutex mutex_;

    // Snapshot for hot path (updated under mutex_; read via std::atomic_load)
    std::shared_ptr<const EventSnapshot> snapshot_;

    // Scheduler last-trigger timestamps: slug → time_point
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastTriggerTime_;
    std::mt19937 rng_{std::random_device{}()};

    GameServices *gs_;
    std::shared_ptr<spdlog::logger> log_;
};
