#include "services/SpawnZoneManager.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Generators.hpp"
#include "utils/RandomUtils.hpp"
#include "utils/SpawnGeometry.hpp"
#include <algorithm>
#include <ctime>
#include <spdlog/logger.h>

SpawnZoneManager::SpawnZoneManager(
    MobManager &mobManager,
    Logger &logger) : mobManager_(mobManager),
                      logger_(logger),
                      mobInstanceManager_(nullptr)
{
    log_ = logger.getSystem("spawn");
}

void
SpawnZoneManager::setMobInstanceManager(MobInstanceManager *mobInstanceManager)
{
    mobInstanceManager_ = mobInstanceManager;
}

void
SpawnZoneManager::setGameZoneManager(GameZoneManager *gameZoneManager)
{
    gameZoneManager_ = gameZoneManager;
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
            log_->error("No spawn zones found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : selectSpawnZones)
        {
            SpawnZoneStruct spawnZone;
            spawnZone.zoneId = row.zoneId;
            spawnZone.zoneName = row.zoneName;
            spawnZone.gameZoneId = row.gameZoneId;
            spawnZone.shape = row.shape;
            spawnZone.minX = row.minX;
            spawnZone.maxX = row.maxX;
            spawnZone.minY = row.minY;
            spawnZone.maxY = row.maxY;
            spawnZone.minZ = row.minZ;
            spawnZone.maxZ = row.maxZ;
            spawnZone.centerX = row.centerX;
            spawnZone.centerY = row.centerY;
            spawnZone.innerRadius = row.innerRadius;
            spawnZone.outerRadius = row.outerRadius;
            spawnZone.exclusionGameZoneId = row.exclusionGameZoneId;
            spawnZone.mobEntries = row.mobEntries;
            spawnZone.spawnEnabled = true;

            // Hot-reload preserve: static pushes re-arrive on chunk↔game
            // reconnect while mobs are live. Config (geometry/entries) is
            // refreshed, but runtime tracking (lists/counter) survives —
            // otherwise the zone forgets its mobs and double-spawns.
            const auto existing = mobSpawnZones_.find(spawnZone.zoneId);
            if (existing != mobSpawnZones_.end())
            {
                spawnZone.spawnedMobsList = existing->second.spawnedMobsList;
                spawnZone.spawnedMobsUIDList = existing->second.spawnedMobsUIDList;
                spawnZone.spawnedMobsCount = existing->second.spawnedMobsCount;
            }
            else
            {
                spawnZone.spawnedMobsCount = 0;
            }

            logger_.log("[LOAD_ZONE] Loaded zone " + std::to_string(spawnZone.zoneId) +
                        " '" + spawnZone.zoneName + "' - mobEntries: " + std::to_string(spawnZone.mobEntries.size()) +
                        ", totalSpawnCount: " + std::to_string(spawnZone.totalSpawnCount()) +
                        ", shape: " + std::to_string(static_cast<int>(spawnZone.shape)) +
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
            log_->error("No mobs found in the GS");
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

            // Copy per-mob AI config (migration 011)
            mobData.aggroRange = row.aggroRange;
            mobData.attackRange = row.attackRange;
            mobData.attackCooldown = row.attackCooldown;
            mobData.chaseMultiplier = row.chaseMultiplier;
            mobData.patrolSpeed = row.patrolSpeed;

            // Social behaviour (migration 012)
            mobData.isSocial = row.isSocial;
            mobData.chaseDuration = row.chaseDuration;

            // Survival / Rare mob groundwork (Stage 3, migration 038)
            mobData.canEvolve = row.canEvolve;
            mobData.isRare = row.isRare;
            mobData.rareSpawnChance = row.rareSpawnChance;
            mobData.rareSpawnCondition = row.rareSpawnCondition;

            // Social systems (Stage 4, migration 039)
            mobData.factionSlug = row.factionSlug;
            mobData.repDeltaPerKill = row.repDeltaPerKill;

            // Route into the mob's own zone (row.zoneId), not zone 0.
            mobData.zoneId = row.zoneId;
            mobData.uid = row.uid;
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

    log_->info("[SPAWN_DEBUG] Attempting to spawn mobs in zone " + std::to_string(zoneId));

    {
        std::shared_lock<std::shared_mutex> readLock(mutex_);
        auto zone = mobSpawnZones_.find(zoneId);
        if (zone == mobSpawnZones_.end())
        {
            log_->error("Spawn zone " + std::to_string(zoneId) + " not found in GS");
            return mobs;
        }

        logger_.log("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) + " found - spawnedMobsCount: " +
                    std::to_string(zone->second.spawnedMobsCount) + ", totalSpawnCount: " +
                    std::to_string(zone->second.totalSpawnCount()));
    }

    // RNG: RandomUtils single source (one thread_local engine per thread,
    // function-local distributions). Replaces the former per-call
    // std::random_device/mt19937 trio — same sampling math, race-free and
    // seedable for tests via seedForTests().

    std::unique_lock<std::shared_mutex> writeLock(mutex_);
    auto zone = mobSpawnZones_.find(zoneId);
    if (zone == mobSpawnZones_.end())
        return mobs;

    log_->info("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) + " - checking spawn conditions");

    // Count alive mobs per template type so each entry can independently top up its quota.
    std::map<int, int> aliveByType;
    if (mobInstanceManager_)
    {
        auto liveMobs = mobInstanceManager_->getMobInstancesInZone(zoneId);
        for (const auto &m : liveMobs)
            if (!m.isDead && m.currentHealth > 0)
                aliveByType[m.id]++;
    }
    else
    {
        for (const auto &m : zone->second.spawnedMobsList)
            if (!m.isDead && m.currentHealth > 0)
                aliveByType[m.id]++;
    }

    // Collect existing positions for separation checks.
    std::vector<PositionStruct> occupiedPositions;
    if (mobInstanceManager_)
    {
        auto existing = mobInstanceManager_->getMobPositionsInZone(zoneId);
        occupiedPositions.reserve(existing.size());
        for (const auto &ep : existing)
            occupiedPositions.push_back(ep.second);
    }

    // Lambdas for shape-aware random point generation (thin wrappers over
    // SpawnGeometry reading this zone's config; sampling math lives there).
    auto sampleRect = [&]() -> std::pair<float, float>
    {
        return SpawnGeometry::sampleRect(zone->second.minX, zone->second.maxX, zone->second.minY, zone->second.maxY);
    };

    auto sampleCircle = [&]() -> std::pair<float, float>
    {
        return SpawnGeometry::sampleCircle(zone->second.centerX, zone->second.centerY, zone->second.outerRadius);
    };

    auto sampleAnnulus = [&]() -> std::pair<float, float>
    {
        return SpawnGeometry::sampleAnnulus(
            zone->second.centerX, zone->second.centerY, zone->second.innerRadius, zone->second.outerRadius);
    };

    // Stratified angular sampling: divides the ring into totalSlots equal sectors
    // and places mob within slotIdx-th sector. Guarantees even angular spread
    // and prevents visual clumping that pure random produces in narrow rings.
    auto sampleAnnulusStratified = [&](int slotIdx, int totalSlots) -> std::pair<float, float>
    {
        return SpawnGeometry::sampleAnnulusSector(zone->second.centerX,
            zone->second.centerY,
            zone->second.innerRadius,
            zone->second.outerRadius,
            slotIdx,
            totalSlots);
    };

    // Process each mob-type entry independently.
    for (const auto &entry : zone->second.mobEntries)
    {
        int alive = aliveByType[entry.mobId];
        int needed = entry.maxCount - alive;
        if (needed <= 0)
            continue;

        logger_.log("[SPAWN_DEBUG] Zone " + std::to_string(zoneId) +
                    " mob_id=" + std::to_string(entry.mobId) +
                    " alive=" + std::to_string(alive) +
                    " needed=" + std::to_string(needed));

        for (int i = 0; i < needed; i++)
        {
            MobDataStruct mob = mobManager_.getMobById(entry.mobId);

            logger_.log("[DEBUG] Template mob from MobManager - ID: " + std::to_string(mob.id) +
                        ", isDead: " + (mob.isDead ? "true" : "false") +
                        ", currentHealth: " + std::to_string(mob.currentHealth) +
                        ", maxHealth: " + std::to_string(mob.maxHealth) +
                        ", name: " + mob.name);

            if (mob.id == 0 || mob.name.empty())
            {
                log_->info("[SPAWN_DELAY] Mob template ID " + std::to_string(entry.mobId) +
                           " not loaded yet, delaying spawn");
                // Skip only this entry type this tick — later entries must
                // still spawn (a bare return would starve them until the
                // missing template arrives).
                break;
            }

            mob.zoneId = zoneId;

            constexpr float SPAWN_EDGE_GAP = 50.0f;
            constexpr float FALLBACK_SEP = 140.0f;
            const float minSpawnSeparation = (mob.radius > 0)
                                                 ? (mob.radius * 2.0f + SPAWN_EDGE_GAP)
                                                 : FALLBACK_SEP;
            constexpr int MAX_SPAWN_ATTEMPTS = 20;
            bool positionFound = false;

            for (int attempt = 0; attempt < MAX_SPAWN_ATTEMPTS; ++attempt)
            {
                float candidateX, candidateY;
                switch (zone->second.shape)
                {
                case ZoneShape::CIRCLE:
                    std::tie(candidateX, candidateY) = sampleCircle();
                    break;
                case ZoneShape::ANNULUS:
                    // First attempt uses stratified sector for even angular distribution.
                    // Retries fall back to fully random to escape separation failures.
                    if (needed > 1 && attempt == 0)
                        std::tie(candidateX, candidateY) = sampleAnnulusStratified(i, needed);
                    else
                        std::tie(candidateX, candidateY) = sampleAnnulus();
                    break;
                default:
                    std::tie(candidateX, candidateY) = sampleRect();
                    break;
                }

                // Reject candidates inside the zone's exclusion game zone
                // (e.g. a safe village overlapping the spawn area). Shape-aware
                // via GameZoneManager; unwired manager or zero id = no filtering.
                // Exhausted attempts skip this mob until the next tick — a mob
                // is never spawned inside the exclusion.
                bool excluded = false;
                if (zone->second.exclusionGameZoneId != 0 && gameZoneManager_ != nullptr)
                {
                    PositionStruct probe;
                    probe.positionX = candidateX;
                    probe.positionY = candidateY;
                    const auto gz = gameZoneManager_->getZoneForPosition(probe);
                    if (gz.has_value() && gz->id == zone->second.exclusionGameZoneId)
                        excluded = true;
                }

                bool tooClose = excluded;
                for (const auto &occ : occupiedPositions)
                {
                    float ddx = candidateX - occ.positionX;
                    float ddy = candidateY - occ.positionY;
                    if (ddx * ddx + ddy * ddy < minSpawnSeparation * minSpawnSeparation)
                    {
                        tooClose = true;
                        break;
                    }
                }

                if (!tooClose)
                {
                    mob.position.positionX = candidateX;
                    mob.position.positionY = candidateY;
                    positionFound = true;
                    break;
                }
            }

            if (!positionFound)
            {
                logger_.log("[SPAWN_SKIP] No valid spawn position found for mob (radius=" +
                            std::to_string(mob.radius) + ", sep=" +
                            std::to_string(static_cast<int>(minSpawnSeparation)) +
                            ") in zone " + std::to_string(zoneId) +
                            " after " + std::to_string(MAX_SPAWN_ATTEMPTS) +
                            " attempts - will retry on next tick");
                continue;
            }

            mob.position.positionZ = (zone->second.minZ + zone->second.maxZ) * 0.5f;
            mob.position.rotationZ = RandomUtils::range(0.0f, 360.0f);
            mob.uid = Generators::generateUniqueMobUID();
            mob.spawnEpochSec = static_cast<int64_t>(std::time(nullptr));

            mobSpawnZones_[zoneId].spawnedMobsUIDList.push_back(mob.uid);
            mobSpawnZones_[zoneId].spawnedMobsList.push_back(mob);

            if (mobInstanceManager_)
                mobInstanceManager_->registerMobInstance(mob);

            logger_.log("[SPAWN_FIX] Spawned mob UID " + std::to_string(mob.uid) +
                        " - isDead: " + (mob.isDead ? "true" : "false") +
                        ", currentHealth: " + std::to_string(mob.currentHealth) +
                        ", maxHealth: " + std::to_string(mob.maxHealth));

            occupiedPositions.push_back(mob.position);
            mobs.push_back(mob);
            zone->second.spawnedMobsCount++;
        }
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

            // Floor at zero: cleanup and harvest paths can both report the
            // same death (double mobDied), and the counter is display-only —
            // the respawn task uses the live MobInstanceManager count.
            if (zone->second.spawnedMobsCount > 0)
                zone->second.spawnedMobsCount--;

            logger_.log("[MOB_DEATH] Mob UID " + std::to_string(mobUID) + " died in zone " +
                        std::to_string(zoneId) + ". Alive count: " + std::to_string(zone->second.spawnedMobsCount) +
                        "/" + std::to_string(zone->second.totalSpawnCount()));

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

            log_->info("[INFO] Removed mob UID " + std::to_string(mobUID) + " from zone");
            return;
        }
    }
}

// Public method that takes mutex lock
void
SpawnZoneManager::removeMobByUID(int mobUID)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        removeMobByUIDInternal(mobUID);
    } // Release SpawnZoneManager mutex before calling MobInstanceManager

    // Unregister from MobInstanceManager outside of lock to avoid deadlock
    // (same pattern as mobDied: MobInstanceManager takes its own locks).
    if (mobInstanceManager_)
    {
        mobInstanceManager_->unregisterMobInstance(mobUID);
    }
}
