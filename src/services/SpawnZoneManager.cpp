#include "services/SpawnZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Generators.hpp"
#include <algorithm>

SpawnZoneManager::SpawnZoneManager(
    MobManager &mobManager,
    Logger &logger) : mobManager_(mobManager),
                      logger_(logger),
                      mobInstanceManager_(nullptr)
{
}

void
SpawnZoneManager::setMobInstanceManager(MobInstanceManager *mobInstanceManager)
{
    mobInstanceManager_ = mobInstanceManager;
}

void
SpawnZoneManager::loadMobSpawnZones(
    // array for the spawn zones
    std::vector<SpawnZoneStruct> selectSpawnZones)
{
    try
    {
        if (selectSpawnZones.empty())
        {
            // log that the data is empty
            logger_.logError("No spawn zones found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : selectSpawnZones)
        {
            SpawnZoneStruct spawnZone;
            spawnZone.zoneId = row.zoneId;
            spawnZone.zoneName = row.zoneName;
            spawnZone.posX = row.posX;
            spawnZone.sizeX = row.sizeX;
            spawnZone.posY = row.posY;
            spawnZone.sizeY = row.sizeY;
            spawnZone.posZ = row.posZ;
            spawnZone.sizeZ = row.sizeZ;
            spawnZone.spawnMobId = row.spawnMobId;
            spawnZone.spawnCount = row.spawnCount;
            spawnZone.respawnTime = row.respawnTime;
            spawnZone.spawnEnabled = true;  // Enable spawning by default
            spawnZone.spawnedMobsCount = 0; // Start with 0 spawned mobs

            logger_.log("[LOAD_ZONE] Loaded zone " + std::to_string(spawnZone.zoneId) +
                        " '" + spawnZone.zoneName + "' - spawnMobId: " + std::to_string(spawnZone.spawnMobId) +
                        ", spawnCount: " + std::to_string(spawnZone.spawnCount) +
                        ", spawnEnabled: " + (spawnZone.spawnEnabled ? "true" : "false"));

            mobSpawnZones_[spawnZone.zoneId] = spawnZone;
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading spawn zones: " + std::string(e.what()));
    }
}

// load mobs in the spawn zones
void
SpawnZoneManager::loadMobsInSpawnZones(
    // array for the mobs
    std::vector<MobDataStruct> selectMobs)
{
    try
    {
        if (selectMobs.empty())
        {
            // log that the data is empty
            logger_.logError("No mobs found in the GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        // set the mobs to mobSpawnZones_
        for (const auto &row : selectMobs)
        {
            MobDataStruct mobData;
            mobData.id = row.id;
            mobData.name = row.name;
            mobData.level = row.level;
            mobData.raceName = row.raceName;
            mobData.currentHealth = row.currentHealth;
            mobData.currentMana = row.currentMana;
            mobData.isAggressive = row.isAggressive;
            mobData.isDead = row.isDead;

            mobSpawnZones_[mobData.zoneId].spawnedMobsList.push_back(mobData);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading mobs: " + std::string(e.what()));
    }
}

// get all spawn zones
std::map<int, SpawnZoneStruct>
SpawnZoneManager::getMobSpawnZones()
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return mobSpawnZones_;
}

// get spawn zone by id
SpawnZoneStruct
SpawnZoneManager::getMobSpawnZoneByID(int zoneId)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone != mobSpawnZones_.end())
    {
        return zone->second;
    }
    else
    {
        return SpawnZoneStruct();
    }
}

// get mobs in the zone
std::vector<MobDataStruct>
SpawnZoneManager::getMobsInZone(int zoneId)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone != mobSpawnZones_.end())
    {
        return zone->second.spawnedMobsList;
    }
    else
    {
        return std::vector<MobDataStruct>();
    }
}

// spawn count of mobs in spawn zone wiht random position in zone
std::vector<MobDataStruct>
SpawnZoneManager::spawnMobsInZone(int zoneId)
{
    std::vector<MobDataStruct> mobs;

    logger_.log("[SPAWN_DEBUG] Attempting to spawn mobs in zone " + std::to_string(zoneId));

    {
        std::shared_lock<std::shared_mutex> readLock(mutex_);
        auto zone = mobSpawnZones_.find(zoneId);
        if (zone == mobSpawnZones_.end()) // Проверяем, есть ли зона
        {
            logger_.logError("Spawn zone " + std::to_string(zoneId) + " not found in GS");
            return mobs;
        }

        logger_.log("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) + " found - spawnedMobsCount: " +
                    std::to_string(zone->second.spawnedMobsCount) + ", spawnCount: " +
                    std::to_string(zone->second.spawnCount));
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, std::nextafter(1.0f, 0.0f));
    std::uniform_real_distribution<float> rotDist(0.0f, 360.0f);

    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone == mobSpawnZones_.end())
        return mobs;

    logger_.log("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) + " - checking spawn conditions");

    // Get current alive mob count from MobInstanceManager (more accurate than spawnedMobsCount)
    int currentAliveMobs = 0;
    if (mobInstanceManager_)
    {
        currentAliveMobs = mobInstanceManager_->getAliveMobCountInZone(zoneId);
    }
    else
    {
        // Fallback to counting alive mobs manually
        for (const auto &mob : zone->second.spawnedMobsList)
        {
            if (!mob.isDead && mob.currentHealth > 0)
            {
                currentAliveMobs++;
            }
        }
    }

    logger_.log("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) + " - currentAliveMobs=" +
                std::to_string(currentAliveMobs) + ", spawnCount=" + std::to_string(zone->second.spawnCount) +
                ", spawnedMobsCount=" + std::to_string(zone->second.spawnedMobsCount) + " (legacy)");

    if (currentAliveMobs < zone->second.spawnCount)
    {
        int mobsToSpawn = zone->second.spawnCount - currentAliveMobs;
        logger_.log("[SPAWN_DEBUG] Need to spawn " + std::to_string(mobsToSpawn) + " mobs in zone " + std::to_string(zoneId));

        for (int i = 0; i < mobsToSpawn; i++)
        {
            MobDataStruct mob = mobManager_.getMobById(zone->second.spawnMobId);

            // Debug: Log what we got from MobManager
            logger_.log("[DEBUG] Template mob from MobManager - ID: " + std::to_string(mob.id) +
                        ", isDead: " + (mob.isDead ? "true" : "false") +
                        ", currentHealth: " + std::to_string(mob.currentHealth) +
                        ", maxHealth: " + std::to_string(mob.maxHealth) +
                        ", name: " + mob.name);

            // CRITICAL CHECK: Don't spawn if mob template is invalid (not loaded yet)
            if (mob.id == 0 || mob.name.empty())
            {
                logger_.log("[SPAWN_DELAY] Mob template ID " + std::to_string(zone->second.spawnMobId) +
                            " not loaded yet, delaying spawn");
                return mobs; // Exit early, try again later
            }

            mob.zoneId = zoneId;

            // Центр зоны и размеры (правильные)
            float centerX = zone->second.posX;
            float centerY = zone->second.posY;
            float centerZ = zone->second.posZ;
            float sizeX = zone->second.sizeX; // Ширина бокса
            float sizeY = zone->second.sizeY; // Длина бокса
            float sizeZ = zone->second.sizeZ; // Высота бокса

            // Правильные границы зоны
            float minX = centerX - (sizeX / 2.0f);
            float maxX = centerX + (sizeX / 2.0f);
            float minY = centerY - (sizeY / 2.0f);
            float maxY = centerY + (sizeY / 2.0f);
            float minZ = centerZ - (sizeZ / 2.0f);
            float maxZ = centerZ + (sizeZ / 2.0f);

            // Генерация случайных координат внутри зоны
            mob.position.positionX = minX + (dist(gen) * (maxX - minX));
            mob.position.positionY = minY + (dist(gen) * (maxY - minY));
            mob.position.positionZ = 200;

            // Поворот случайный от 0 до 360 градусов
            mob.position.rotationZ = rotDist(gen);

            // Генерация уникального ID
            mob.uid = Generators::generateUniqueMobUID();

            // Добавляем моба в список
            mobSpawnZones_[zoneId].spawnedMobsUIDList.push_back(mob.uid);
            mobSpawnZones_[zoneId].spawnedMobsList.push_back(mob);

            // Register mob instance in MobInstanceManager
            if (mobInstanceManager_)
            {
                mobInstanceManager_->registerMobInstance(mob);
            }

            // Debug logging to verify mob is alive
            logger_.log("[SPAWN_FIX] Spawned mob UID " + std::to_string(mob.uid) +
                        " - isDead: " + (mob.isDead ? "true" : "false") +
                        ", currentHealth: " + std::to_string(mob.currentHealth) +
                        ", maxHealth: " + std::to_string(mob.maxHealth));

            mobs.push_back(mob);
            zone->second.spawnedMobsCount++;
        }
    }
    else
    {
        logger_.log("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) + " has enough alive mobs (" +
                    std::to_string(currentAliveMobs) + "/" + std::to_string(zone->second.spawnCount) + ") - no spawning needed");
    }

    return mobs;
}

// detect that the mob is dead and decrease spawnedMobs
void
SpawnZoneManager::mobDied(int zoneId, int mobUID)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto zone = mobSpawnZones_.find(zoneId);
        if (zone != mobSpawnZones_.end())
        {
            // remove mob from the list of spawned mobs (internal version without mutex)
            removeMobByUIDInternal(mobUID);

            // decrease spawnedMobsCount
            zone->second.spawnedMobsCount--;

            logger_.log("[MOB_DEATH] Mob UID " + std::to_string(mobUID) + " died in zone " +
                        std::to_string(zoneId) + ". Alive count: " + std::to_string(zone->second.spawnedMobsCount) +
                        "/" + std::to_string(zone->second.spawnCount));

            // Note: New mobs will be spawned by the periodic respawn task
            // based on the actual alive mob count, not this legacy counter
        }
    } // Release SpawnZoneManager mutex before calling MobInstanceManager

    // Unregister from MobInstanceManager outside of lock to avoid deadlock
    if (mobInstanceManager_)
    {
        mobInstanceManager_->unregisterMobInstance(mobUID);
    }
}

// Delegate to MobInstanceManager for backward compatibility
MobDataStruct
SpawnZoneManager::getMobByUID(int mobUID)
{
    if (mobInstanceManager_)
    {
        return mobInstanceManager_->getMobInstance(mobUID);
    }

    // Fallback to local search if MobInstanceManager not available
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &zone : mobSpawnZones_)
    {
        for (const auto &mob : zone.second.spawnedMobsList)
        {
            if (mob.uid == mobUID)
            {
                return mob;
            }
        }
    }
    return MobDataStruct();
}

// Internal method for zone cleanup only (assumes mutex is already locked)
void
SpawnZoneManager::removeMobByUIDInternal(int mobUID)
{
    for (auto &zone : mobSpawnZones_)
    {
        auto it = std::find_if(zone.second.spawnedMobsList.begin(), zone.second.spawnedMobsList.end(), [&mobUID](const MobDataStruct &mob)
            { return mob.uid == mobUID; });
        if (it != zone.second.spawnedMobsList.end())
        {
            zone.second.spawnedMobsList.erase(it);

            // Also remove from UID list
            auto uidIt = std::find(zone.second.spawnedMobsUIDList.begin(), zone.second.spawnedMobsUIDList.end(), mobUID);
            if (uidIt != zone.second.spawnedMobsUIDList.end())
            {
                zone.second.spawnedMobsUIDList.erase(uidIt);
            }

            logger_.log("[INFO] Removed mob UID " + std::to_string(mobUID) + " from zone");
            return;
        }
    }
}

// Public method that takes mutex lock
void
SpawnZoneManager::removeMobByUID(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    removeMobByUIDInternal(mobUID);

    // Unregister from MobInstanceManager outside of internal mutex to avoid deadlock
    if (mobInstanceManager_)
    {
        mobInstanceManager_->unregisterMobInstance(mobUID);
    }
}
