#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/CharacterStatsNotificationService.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include "utils/RandomUtils.hpp"
#include "utils/SpawnGeometry.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <nlohmann/json.hpp>
#include <tuple>
#include <spdlog/logger.h>

ChampionManager::ChampionManager(GameZoneManager &gameZones,
    GameConfigService &gameConfig,
    MobInstanceManager &mobInstances,
    CharacterManager &characters,
    MobManager &mobs,
    SpawnZoneManager &spawnZones,
    CharacterStatsNotificationService *statsNotify,
    Logger &logger)
    : gameZones_(gameZones),
      gameConfig_(gameConfig),
      mobInstances_(mobInstances),
      characters_(characters),
      mobs_(mobs),
      spawnZones_(spawnZones),
      statsNotify_(statsNotify),
      logger_(logger)
{
    log_ = logger_.getSystem("champion");
}

void
ChampionManager::setSendToGameServerCallback(std::function<void(const std::string &)> cb)
{
    sendToGameServerCb_ = std::move(cb);
}

// ── Threshold Champion ────────────────────────────────────────────────────────

void
ChampionManager::recordMobKill(int gameZoneId, int mobTemplateId, int spawnZoneId)
{
    // Origin attribution: the spawn zone's game zone wins over the death
    // position (fleeing/kiting must not void threshold progress).
    int zoneId = gameZoneId;
    if (spawnZoneId != 0)
    {
        const auto spawnZone = spawnZones_.getMobSpawnZoneByID(spawnZoneId);
        if (spawnZone.zoneId != 0 && spawnZone.gameZoneId != 0)
            zoneId = spawnZone.gameZoneId;
    }
    if (zoneId <= 0)
        return; // no attribution possible (unzoned death, unknown origin)

    std::lock_guard<std::mutex> lk(counterMutex_);

    // Do not accumulate if a champion of this template is already active in the zone
    {
        std::lock_guard<std::mutex> alck(activeMutex_);
        for (const auto &c : active_)
        {
            if (c.gameZoneId == zoneId && c.baseTemplateId == mobTemplateId)
                return;
        }
    }

    auto &count = zoneKillCounters_[zoneId][mobTemplateId];
    ++count;

    auto zones = gameZones_.getAllZones();
    auto it = std::find_if(zones.begin(), zones.end(), [zoneId](const GameZoneStruct &z)
        { return z.id == zoneId; });
    if (it == zones.end())
        return;

    if (count >= it->championThresholdKills)
    {
        count = 0;
        // Release counterMutex_ before spawnChampion to avoid potential deadlock
        // (spawnChampion acquires activeMutex_). We already have the info we need.
        // Unlock by exiting the lock_guard scope manually via a local bool flag.
    }
    else
    {
        return; // threshold not reached
    }

    // Post-threshold spawn chance (game_config champion.spawn_chance_pct,
    // default 100 = legacy always-spawn). Lets content tune rarity without
    // touching thresholds; tests pin both extremes.
    {
        const float chancePct = gameConfig_.getFloat("champion.spawn_chance_pct", kDefaultSpawnChancePct);
        const float roll = RandomUtils::uniform01() * 100.0f;
        if (roll >= chancePct)
        {
            log_->info("[Champion] Threshold reached in zone {} but chance roll failed ({:.1f} >= {:.1f}%)",
                zoneId, roll, chancePct);
            return;
        }
    }

    // Hard cap on simultaneous champions per zone (game_config
    // champion.max_active_per_zone, default 3): no flood even under farmed
    // thresholds. Counter already reset — natural retry next threshold.
    {
        const int maxActive = gameConfig_.getInt("champion.max_active_per_zone", kDefaultMaxActivePerZone);
        std::lock_guard<std::mutex> alck(activeMutex_);
        int zoneActive = 0;
        for (const auto &c : active_)
            if (c.gameZoneId == zoneId)
                ++zoneActive;
        if (zoneActive >= maxActive)
        {
            log_->warn("[Champion] Zone {} at cap ({}/{} active) — threshold spawn skipped",
                zoneId, zoneActive, maxActive);
            return;
        }
    }

    // Spawn outside of counterMutex_ lock
    spawnChampion(mobTemplateId, zoneId, "[Чемпион] ", 1.5f);
}

// ── Timed Champion ────────────────────────────────────────────────────────────

void
ChampionManager::loadTimedChampions(const std::vector<TimedChampionTemplate> &templates)
{
    std::lock_guard<std::mutex> lk(timedMutex_);
    timedStates_.clear();
    timedStates_.reserve(templates.size());
    for (const auto &t : templates)
    {
        timedStates_.push_back({t, false, false});
    }
    log_->info("[Timed] Loaded {} timed champion templates", templates.size());
}

void
ChampionManager::tickTimedChampions()
{
    const auto &cfg = gameConfig_;
    const int preAnnounceSec = cfg.getInt("champion.pre_announce_sec", 300); // 5 min before

    const int64_t nowEpoch = static_cast<int64_t>(std::time(nullptr));

    // First check for any champions that have exceeded their window
    checkDespawnedChampions();

    std::lock_guard<std::mutex> lk(timedMutex_);
    for (auto &state : timedStates_)
    {
        if (state.tmpl.nextSpawnAt <= 0)
            continue; // not initialised yet

        const int64_t timeUntilSpawn = state.tmpl.nextSpawnAt - nowEpoch;

        if (!state.spawned)
        {
            // Pre-announce
            if (timeUntilSpawn <= preAnnounceSec && !state.preAnnounceSent)
            {
                broadcastToGameZone(state.tmpl.gameZoneId,
                    "champion_spawned_soon",
                    nlohmann::json{{"slug", state.tmpl.slug}});
                state.preAnnounceSent = true;
            }

            // Spawn
            if (timeUntilSpawn <= 0)
            {
                int uid = spawnChampion(state.tmpl.mobTemplateId,
                    state.tmpl.gameZoneId,
                    "[!] ",
                    2.0f,
                    state.tmpl.slug);
                if (uid > 0)
                {
                    state.spawned = true;
                    state.preAnnounceSent = false;
                    log_->info("[Timed] Champion '{}' spawned (uid={})", state.tmpl.slug, uid);
                }
            }
        }
    }
}

// ── Survival Champion ─────────────────────────────────────────────────────────

void
ChampionManager::tickSurvivalEvolution()
{
    const int evolveHours = this->evolveHours();
    const int64_t evolveThresholdSec = static_cast<int64_t>(evolveHours) * 3600;

    const int64_t nowEpoch = static_cast<int64_t>(std::time(nullptr));

    auto living = mobInstances_.getAllLivingInstances();
    for (const auto &mob : living)
    {
        if (!mob.canEvolve || mob.hasEvolved || mob.spawnEpochSec == 0)
            continue;
        if (nowEpoch - mob.spawnEpochSec < evolveThresholdSec)
            continue;

        evolveSurvivalMob(mob.uid);
    }
}

// ── Champion killed ───────────────────────────────────────────────────────────

void
ChampionManager::onChampionKilled(int champUid, int killerCharId, const std::string &champSlug)
{
    int gameZoneId = 0;
    int baseTemplate = 0;
    std::string slug;

    {
        std::lock_guard<std::mutex> lk(activeMutex_);
        auto it = std::find_if(active_.begin(), active_.end(), [champUid](const ChampionInstance &c)
            { return c.uid == champUid; });
        if (it == active_.end())
            return;

        gameZoneId = it->gameZoneId;
        baseTemplate = it->baseTemplateId;
        slug = it->slug;
        active_.erase(it);
    }

    // Reset kill counter for Threshold Champions
    if (slug.empty())
    {
        std::lock_guard<std::mutex> ck(counterMutex_);
        zoneKillCounters_[gameZoneId][baseTemplate] = 0;
    }

    // Announce to zone
    auto killerData = characters_.getCharacterData(killerCharId);
    std::string killerName = (killerData.characterId != 0) ? killerData.characterName : "кто-то";

    broadcastToGameZone(gameZoneId, "champion_killed", nlohmann::json{{"killerCharId", killerCharId}, {"killerName", killerName}});

    // Notify game-server for Timed Champions so next_spawn_at can be updated
    if (!slug.empty())
    {
        sendTimedChampionKilledToGameServer(slug, killerCharId);

        // Mark state as not spawned so next cycle can re-spawn
        std::lock_guard<std::mutex> tlk(timedMutex_);
        for (auto &state : timedStates_)
        {
            if (state.tmpl.slug == slug)
            {
                state.spawned = false;
                break;
            }
        }
    }

    log_->info("[Champion] Champion uid={} killed by char={}", champUid, killerCharId);
}

// ── Public spawn helper ───────────────────────────────────────────────────────

int
ChampionManager::spawnChampion(int mobTemplateId,
    int gameZoneId,
    const std::string &namePrefix,
    float lootMult,
    const std::string &slug)
{
    const auto &cfg = gameConfig_;
    const float hpMult = cfg.getFloat("champion.hp_multiplier", 3.0f);
    const float dmgMult = cfg.getFloat("champion.damage_multiplier", 1.5f);

    auto base = mobs_.getMobById(mobTemplateId);
    if (base.id == 0)
    {
        log_->warn("[Champion] Mob template {} not found", mobTemplateId);
        return 0;
    }

    base.uid = Generators::generateUniqueMobUID();
    base.name = namePrefix + base.name;
    base.maxHealth = static_cast<int>(base.maxHealth * hpMult);
    base.currentHealth = base.maxHealth;
    base.maxMana = static_cast<int>(base.maxMana * hpMult);
    base.currentMana = base.maxMana;
    base.baseExperience = static_cast<int>(base.baseExperience * 2.0f);
    base.rankCode = "champion";
    base.rankMult = hpMult;
    base.isChampion = true;
    base.lootMultiplier = lootMult;
    base.spawnEpochSec = static_cast<int64_t>(std::time(nullptr));
    base.position = resolveChampionSpawnPoint(gameZoneId);

    // Scale physical_attack attribute by dmgMult
    for (auto &attr : base.attributes)
    {
        if (attr.slug == "physical_attack" || attr.slug == "magic_attack")
            attr.value = static_cast<int>(attr.value * dmgMult);
    }

    // Set zoneId to the first spawn zone inside the game zone for engine compatibility
    // (SpawnZoneManager looks up mobs by zoneId, not gameZoneId)
    const auto spawnZones = spawnZones_.getMobSpawnZones();
    for (const auto &[szId, sz] : spawnZones)
    {
        float cx = sz.centerX;
        float cy = sz.centerY;
        PositionStruct c;
        c.positionX = cx;
        c.positionY = cy;
        auto gz = gameZones_.getZoneForPosition(c);
        if (gz.has_value() && gz->id == gameZoneId)
        {
            base.zoneId = sz.zoneId;
            break;
        }
    }
    if (base.zoneId == 0)
        base.zoneId = -1; // Fallback: not tracked by SpawnZoneManager

    mobInstances_.registerMobInstance(base);

    const int despawnMin = cfg.getInt("champion.despawn_minutes", 30);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(activeMutex_);
        active_.push_back({base.uid, gameZoneId, mobTemplateId, slug, now, now + std::chrono::minutes(despawnMin)});
    }

    broadcastToGameZone(gameZoneId, "champion_spawned", nlohmann::json{{"mobSlug", base.slug}, {"uid", base.uid}});

    log_->info("[Champion] Spawned uid={} '{}' in gameZone={}", base.uid, base.name, gameZoneId);
    return base.uid;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void
ChampionManager::checkDespawnedChampions()
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(activeMutex_);

    for (auto it = active_.begin(); it != active_.end();)
    {
        if (now >= it->despawnAt)
        {
            mobInstances_.unregisterMobInstance(it->uid);
            broadcastToGameZone(it->gameZoneId, "champion_despawned", nlohmann::json::object());

            // Halve the kill counter (Threshold champions)
            if (it->slug.empty())
            {
                std::lock_guard<std::mutex> ck(counterMutex_);
                auto &cnt = zoneKillCounters_[it->gameZoneId][it->baseTemplateId];
                auto zones = gameZones_.getAllZones();
                auto zit = std::find_if(zones.begin(), zones.end(), [&](const GameZoneStruct &z)
                    { return z.id == it->gameZoneId; });
                if (zit != zones.end())
                    cnt = zit->championThresholdKills / 2;
            }
            else
            {
                // Timed champion timed out — mark as not spawned
                std::lock_guard<std::mutex> tlk(timedMutex_);
                for (auto &state : timedStates_)
                {
                    if (state.tmpl.slug == it->slug)
                    {
                        state.spawned = false;
                        break;
                    }
                }
            }

            log_->info("[Champion] Champion uid={} despawned (window expired)", it->uid);
            it = active_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
ChampionManager::evolveSurvivalMob(int mobUid)
{
    const auto &cfg = gameConfig_;
    const float hpBonusPct = cfg.getFloat("survival_champion.hp_bonus_pct", 0.5f);

    auto mob = mobInstances_.getMobInstance(mobUid);
    if (mob.uid == 0 || mob.hasEvolved)
        return;

    float hpRatio = (mob.maxHealth > 0)
                        ? static_cast<float>(mob.currentHealth) / static_cast<float>(mob.maxHealth)
                        : 1.0f;
    int newMax = static_cast<int>(mob.maxHealth * (1.0f + hpBonusPct));
    int newCur = static_cast<int>(newMax * hpRatio);

    mob.maxHealth = newMax;
    mob.currentHealth = newCur;
    mob.name = "[Выживший] " + mob.name;
    mob.hasEvolved = true;
    mob.isChampion = true;
    mob.lootMultiplier = 1.3f;
    mobInstances_.updateMobInstance(mob);

    // Register as active champion (for kill tracking and despawn)
    auto gameZone = gameZones_.getZoneForPosition(mob.position);
    int gzId = gameZone.has_value() ? gameZone->id : 0;
    {
        std::lock_guard<std::mutex> lk(activeMutex_);
        // No despawn time for Survival Champions — they live until killed
        active_.push_back({mobUid, gzId, mob.id, "", std::chrono::steady_clock::now(), std::chrono::steady_clock::time_point::max()});
    }

    broadcastToGameZone(gzId, "survival_evolved", nlohmann::json{{"uid", mobUid}, {"mobSlug", mob.slug}});

    log_->info("[Survival] Mob uid={} '{}' evolved after {}h alive",
        mobUid,
        mob.name,
        evolveHours());
}

PositionStruct
ChampionManager::resolveChampionSpawnPoint(int gameZoneId) const
{
    // Try to find a spawn zone whose centre lies within the game zone AABB
    const auto spawnZones = spawnZones_.getMobSpawnZones();
    const auto gameZones = gameZones_.getAllZones();

    auto gzIt = std::find_if(gameZones.begin(), gameZones.end(), [gameZoneId](const GameZoneStruct &z)
        { return z.id == gameZoneId; });
    if (gzIt == gameZones.end())
    {
        PositionStruct p;
        return p;
    }
    const GameZoneStruct &gz = *gzIt;

    // Collect spawn zones inside this game zone (shape-aware: a corner of
    // the enclosing AABB is not inside a CIRCLE/ANNULUS game zone).
    std::vector<const SpawnZoneStruct *> candidates;
    for (const auto &[szId, sz] : spawnZones)
    {
        if (gz.contains(sz.centerX, sz.centerY))
            candidates.push_back(&sz);
    }

    if (!candidates.empty())
    {
        // Pick a random spawn zone and a random point within it.
        // RNG: RandomUtils (one thread_local engine per thread — see RandomUtils.hpp).
        const SpawnZoneStruct &chosen =
            *candidates[RandomUtils::rangeInt(0, static_cast<int>(candidates.size()) - 1)];

        PositionStruct p;
        p.positionZ = (chosen.minZ + chosen.maxZ) * 0.5f;

        // Sampling math lives in SpawnGeometry (single source with mob
        // spawn and respawn points).
        if (chosen.shape == ZoneShape::ANNULUS)
        {
            std::tie(p.positionX, p.positionY) = SpawnGeometry::sampleAnnulus(
                chosen.centerX, chosen.centerY, chosen.innerRadius, chosen.outerRadius);
        }
        else if (chosen.shape == ZoneShape::CIRCLE)
        {
            std::tie(p.positionX, p.positionY) =
                SpawnGeometry::sampleCircle(chosen.centerX, chosen.centerY, chosen.outerRadius);
        }
        else
        {
            std::tie(p.positionX, p.positionY) =
                SpawnGeometry::sampleRect(chosen.minX, chosen.maxX, chosen.minY, chosen.maxY);
        }
        return p;
    }

    // Fallback: centre of the game zone AABB, with Z from any spawn zone inside this game zone.
    float fallbackZ = 500.0f;
    for (const auto &[szId, sz] : spawnZones)
    {
        if (gz.contains(sz.centerX, sz.centerY))
        {
            fallbackZ = (sz.minZ + sz.maxZ) * 0.5f;
            break;
        }
    }

    PositionStruct p;
    p.positionX = (gz.minX + gz.maxX) * 0.5f;
    p.positionY = (gz.minY + gz.maxY) * 0.5f;
    p.positionZ = fallbackZ;
    return p;
}

void
ChampionManager::broadcastToGameZone(int gameZoneId,
    const std::string &type,
    const nlohmann::json &data,
    const std::string &priority,
    const std::string &channel)
{
    if (!statsNotify_)
        return;
    statsNotify_->sendWorldNotificationToGameZone(gameZoneId, type, data, priority, channel);
}

void
ChampionManager::sendTimedChampionKilledToGameServer(const std::string &slug, int killerCharId)
{
    if (!sendToGameServerCb_)
        return;

    nlohmann::json pkt;
    pkt["header"]["eventType"] = "timedChampionKilled";
    pkt["header"]["clientId"] = 0;
    pkt["header"]["hash"] = "";
    pkt["body"]["slug"] = slug;
    pkt["body"]["killerCharId"] = killerCharId;
    pkt["body"]["killedAt"] = static_cast<int64_t>(std::time(nullptr));

    sendToGameServerCb_(pkt.dump() + "\n");
    log_->info("[Timed] Sent timedChampionKilled slug='{}' to game-server", slug);
}
