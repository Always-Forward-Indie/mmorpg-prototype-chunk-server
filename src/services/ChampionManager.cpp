#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/CharacterStatsNotificationService.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameServices.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <nlohmann/json.hpp>
#include <random>
#include <spdlog/logger.h>

ChampionManager::ChampionManager(GameServices *gs)
    : gs_(gs)
{
    log_ = gs_->getLogger().getSystem("champion");
}

void
ChampionManager::setSendToGameServerCallback(std::function<void(const std::string &)> cb)
{
    sendToGameServerCb_ = std::move(cb);
}

// ── Threshold Champion ────────────────────────────────────────────────────────

void
ChampionManager::recordMobKill(int gameZoneId, int mobTemplateId)
{
    std::lock_guard<std::mutex> lk(counterMutex_);

    // Do not accumulate if a champion of this template is already active in the zone
    {
        std::lock_guard<std::mutex> alck(activeMutex_);
        for (const auto &c : active_)
        {
            if (c.gameZoneId == gameZoneId && c.baseTemplateId == mobTemplateId)
                return;
        }
    }

    auto &count = zoneKillCounters_[gameZoneId][mobTemplateId];
    ++count;

    auto zones = gs_->getGameZoneManager().getAllZones();
    auto it = std::find_if(zones.begin(), zones.end(), [gameZoneId](const GameZoneStruct &z)
        { return z.id == gameZoneId; });
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

    // Spawn outside of counterMutex_ lock
    spawnChampion(mobTemplateId, gameZoneId, "[Чемпион] ", 1.5f);
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
    const auto &cfg = gs_->getGameConfigService();
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
    const auto &cfg = gs_->getGameConfigService();
    const int evolveHours = cfg.getInt("survival_champion.evolve_hours", 12);
    const int64_t evolveThresholdSec = static_cast<int64_t>(evolveHours) * 3600;

    const int64_t nowEpoch = static_cast<int64_t>(std::time(nullptr));

    auto living = gs_->getMobInstanceManager().getAllLivingInstances();
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
    auto killerData = gs_->getCharacterManager().getCharacterData(killerCharId);
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
    const auto &cfg = gs_->getGameConfigService();
    const float hpMult = cfg.getFloat("champion.hp_multiplier", 3.0f);
    const float dmgMult = cfg.getFloat("champion.damage_multiplier", 1.5f);

    auto base = gs_->getMobManager().getMobById(mobTemplateId);
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
    const auto spawnZones = gs_->getSpawnZoneManager().getMobSpawnZones();
    for (const auto &[szId, sz] : spawnZones)
    {
        float cx = (sz.posX + sz.sizeX) * 0.5f;
        float cy = (sz.posY + sz.sizeY) * 0.5f;
        PositionStruct c;
        c.positionX = cx;
        c.positionY = cy;
        auto gz = gs_->getGameZoneManager().getZoneForPosition(c);
        if (gz.has_value() && gz->id == gameZoneId)
        {
            base.zoneId = sz.zoneId;
            break;
        }
    }
    if (base.zoneId == 0)
        base.zoneId = -1; // Fallback: not tracked by SpawnZoneManager

    gs_->getMobInstanceManager().registerMobInstance(base);

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
            gs_->getMobInstanceManager().unregisterMobInstance(it->uid);
            broadcastToGameZone(it->gameZoneId, "champion_despawned", nlohmann::json::object());

            // Halve the kill counter (Threshold champions)
            if (it->slug.empty())
            {
                std::lock_guard<std::mutex> ck(counterMutex_);
                auto &cnt = zoneKillCounters_[it->gameZoneId][it->baseTemplateId];
                auto zones = gs_->getGameZoneManager().getAllZones();
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
    const auto &cfg = gs_->getGameConfigService();
    const float hpBonusPct = cfg.getFloat("survival_champion.hp_bonus_pct", 0.5f);

    auto mob = gs_->getMobInstanceManager().getMobInstance(mobUid);
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
    gs_->getMobInstanceManager().updateMobInstance(mob);

    // Register as active champion (for kill tracking and despawn)
    auto gameZone = gs_->getGameZoneManager().getZoneForPosition(mob.position);
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
        gs_->getGameConfigService().getInt("survival_champion.evolve_hours", 12));
}

PositionStruct
ChampionManager::resolveChampionSpawnPoint(int gameZoneId) const
{
    // Try to find a spawn zone whose centre lies within the game zone AABB
    const auto spawnZones = gs_->getSpawnZoneManager().getMobSpawnZones();
    const auto gameZones = gs_->getGameZoneManager().getAllZones();

    auto gzIt = std::find_if(gameZones.begin(), gameZones.end(), [gameZoneId](const GameZoneStruct &z)
        { return z.id == gameZoneId; });
    if (gzIt == gameZones.end())
    {
        PositionStruct p;
        return p;
    }
    const GameZoneStruct &gz = *gzIt;

    // Collect spawn zones inside this game zone
    std::vector<const SpawnZoneStruct *> candidates;
    for (const auto &[szId, sz] : spawnZones)
    {
        float cx = (sz.posX + sz.sizeX) * 0.5f;
        float cy = (sz.posY + sz.sizeY) * 0.5f;
        if (cx >= gz.minX && cx <= gz.maxX && cy >= gz.minY && cy <= gz.maxY)
            candidates.push_back(&sz);
    }

    if (!candidates.empty())
    {
        // Pick a random spawn zone and a random point within it
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size()) - 1);
        const SpawnZoneStruct &chosen = *candidates[pick(rng)];

        std::uniform_real_distribution<float> distX(chosen.posX, chosen.sizeX);
        std::uniform_real_distribution<float> distY(chosen.posY, chosen.sizeY);

        PositionStruct p;
        p.positionX = distX(rng);
        p.positionY = distY(rng);
        p.positionZ = 200.0f;
        return p;
    }

    // Fallback: centre of the game zone AABB
    PositionStruct p;
    p.positionX = (gz.minX + gz.maxX) * 0.5f;
    p.positionY = (gz.minY + gz.maxY) * 0.5f;
    p.positionZ = 200.0f;
    return p;
}

void
ChampionManager::broadcastToGameZone(int gameZoneId,
    const std::string &type,
    const nlohmann::json &data,
    const std::string &priority,
    const std::string &channel)
{
    gs_->getStatsNotificationService().sendWorldNotificationToGameZone(gameZoneId, type, data, priority, channel);
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
