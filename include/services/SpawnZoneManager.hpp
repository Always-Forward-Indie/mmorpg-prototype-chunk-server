#pragma once

#include "data/DataStructs.hpp"
#include "services/MobManager.hpp"
#include "utils/Logger.hpp"
#include "utils/TimeConverter.hpp"
#include <map>
#include <random>
#include <shared_mutex>

// Forward declaration to avoid circular dependency
class MobInstanceManager;

class SpawnZoneManager
{
  public:
    SpawnZoneManager(MobManager &mobManager, Logger &logger);

    // Set mob instance manager for registration of spawned mobs
    void setMobInstanceManager(MobInstanceManager *mobInstanceManager);

    void loadMobSpawnZones(std::vector<SpawnZoneStruct> selectSpawnZones);
    void loadMobsInSpawnZones(std::vector<MobDataStruct> selectMobs);

    std::map<int, SpawnZoneStruct> getMobSpawnZones();
    SpawnZoneStruct getMobSpawnZoneByID(int zoneId);
    std::vector<MobDataStruct> getMobsInZone(int zoneId);

    std::vector<MobDataStruct> spawnMobsInZone(int zoneId);
    void mobDied(int zoneId, int mobUID);

    // Deprecated: Use MobInstanceManager instead
    // These methods are kept for backward compatibility and internal zone management
    MobDataStruct getMobByUID(int mobUID); // Delegates to MobInstanceManager
    void removeMobByUID(int mobUID);       // Public method for zone cleanup

  private:
    void removeMobByUIDInternal(int mobUID); // Internal method (assumes mutex is locked)

  private:
    Logger &logger_;
    MobManager &mobManager_;
    MobInstanceManager *mobInstanceManager_; // Pointer to avoid circular dependency

    // Store the mob spawn zones in memory with zoneId as key
    std::map<int, SpawnZoneStruct> mobSpawnZones_;

    // Mutex for the mob spawn zones map
    mutable std::shared_mutex mutex_;
};