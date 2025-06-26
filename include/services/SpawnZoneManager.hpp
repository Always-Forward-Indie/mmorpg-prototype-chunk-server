#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include "utils/TimeConverter.hpp"
#include "services/MobManager.hpp"
#include <random>
#include <shared_mutex>
#include <map>

class SpawnZoneManager
{
public:
    SpawnZoneManager(MobManager& mobManager, Logger& logger);
    void loadMobSpawnZones(std::vector<SpawnZoneStruct> selectSpawnZones);
    void loadMobsInSpawnZones(std::vector<MobDataStruct> selectMobs);

    std::map<int, SpawnZoneStruct> getMobSpawnZones();
    SpawnZoneStruct getMobSpawnZoneByID(int zoneId);
    std::vector<MobDataStruct> getMobsInZone(int zoneId);

    std::vector<MobDataStruct> spawnMobsInZone(int zoneId);
    void moveMobsInZone(int zoneId);
    void mobDied(int zoneId, std::string mobUID);

    MobDataStruct getMobByUID(std::string mobUID);
    void removeMobByUID(std::string mobUID);
    
    void updateMobPosition(std::string mobUID, PositionStruct position);
    void updateMobHealth(std::string mobUID, int health);
    void updateMobMana(std::string mobUID, int mana);


private:
    Logger& logger_;
    MobManager& mobManager_;
    // Store the mob spawn zones in memory with zoneId as key
    std::map<int, SpawnZoneStruct> mobSpawnZones_;

    // Mutex for the mob spawn zones map
    mutable std::shared_mutex mutex_;
};