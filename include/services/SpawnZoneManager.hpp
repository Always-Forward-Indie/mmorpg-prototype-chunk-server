#pragma once

#include "data/DataStructs.hpp"
#include "services/MobManager.hpp"
#include "utils/Logger.hpp"
#include "utils/TimeConverter.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <random>
#include <shared_mutex>
#include <unordered_map>

// Forward declaration to avoid circular dependency
class MobInstanceManager;
class GameZoneManager;
class IStatsNotifier;

class SpawnZoneManager
{
  public:
    SpawnZoneManager(MobManager &mobManager, Logger &logger);

    // Set mob instance manager for registration of spawned mobs
    void setMobInstanceManager(MobInstanceManager *mobInstanceManager);

    // Set game-zone manager for exclusionGameZoneId filtering (nullable:
    // without it exclusion is silently ignored — same posture as the
    // optional MobInstanceManager above).
    void setGameZoneManager(GameZoneManager *gameZoneManager);

    // Optional announcer for rare spawns (nullable: without it rares still
    // spawn, only the rare_mob_spawned toast is skipped).
    void setStatsNotifier(IStatsNotifier *statsNotify);

    void loadMobSpawnZones(std::vector<SpawnZoneStruct> selectSpawnZones);
    void loadMobsInSpawnZones(std::vector<MobDataStruct> selectMobs);

    std::map<int, SpawnZoneStruct> getMobSpawnZones();
    SpawnZoneStruct getMobSpawnZoneByID(int zoneId);
    std::vector<MobDataStruct> getMobsInZone(int zoneId);

    std::vector<MobDataStruct> spawnMobsInZone(int zoneId);
    void mobDied(int zoneId, int mobUID);

    // Deprecated: Use MobInstanceManager instead
    void removeMobByUID(int mobUID);       // Public method for zone cleanup

  private:
    void removeMobByUIDInternal(int mobUID); // Internal method (assumes mutex is locked)

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    MobManager &mobManager_;
    MobInstanceManager *mobInstanceManager_; // Pointer to avoid circular dependency
    GameZoneManager *gameZoneManager_ = nullptr; // Optional exclusion filter
    IStatsNotifier *statsNotify_ = nullptr; // Optional rare-spawn announcer

    // Store the mob spawn zones in memory with zoneId as key
    std::map<int, SpawnZoneStruct> mobSpawnZones_;

    // Last death per (zoneId, mobId) for per-entry respawnTime gating.
    // Key: (int64_t(zoneId) << 32) | mobId. Empty = no deaths yet (spawn freely).
    std::unordered_map<int64_t, std::chrono::steady_clock::time_point> lastDeathByType_;

    // Mutex for the mob spawn zones map
    mutable std::shared_mutex mutex_;
};