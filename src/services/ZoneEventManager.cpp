#include "services/ZoneEventManager.hpp"
#include "services/GameServices.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <spdlog/spdlog.h>

using SteadyClock = std::chrono::steady_clock;

ZoneEventManager::ZoneEventManager(GameServices *gs)
    : gs_(gs),
      log_(spdlog::get("chunk_server"))
{
    // Initialise with an empty snapshot
    std::atomic_store(&snapshot_, std::make_shared<const EventSnapshot>());
}

// ── Template management ────────────────────────────────────────────────────

void
ZoneEventManager::loadTemplates(const std::vector<ZoneEventTemplate> &templates)
{
    std::unique_lock lk(mutex_);
    templates_ = templates;
    log_->info("[ZoneEvent] Loaded {} event templates", templates.size());
}

// ── Activation / deactivation ──────────────────────────────────────────────

void
ZoneEventManager::startEvent(const std::string &slug, int overrideGameZoneId)
{
    // Find template
    auto tit = std::find_if(templates_.begin(), templates_.end(), [&slug](const ZoneEventTemplate &t)
        { return t.slug == slug; });
    if (tit == templates_.end())
    {
        log_->warn("[ZoneEvent] startEvent: unknown slug '{}'", slug);
        return;
    }

    const ZoneEventTemplate &tmpl = *tit;
    int zoneId = (overrideGameZoneId != 0) ? overrideGameZoneId : tmpl.gameZoneId;

    ActiveZoneEvent ev;
    ev.slug = slug;
    ev.gameZoneId = zoneId;
    ev.lootMultiplier = tmpl.lootMultiplier;
    ev.spawnRateMultiplier = tmpl.spawnRateMultiplier;
    ev.mobSpeedMultiplier = tmpl.mobSpeedMultiplier;
    ev.endsAt = SteadyClock::now() + std::chrono::seconds(tmpl.durationSec);

    {
        std::unique_lock lk(mutex_);
        active_[slug] = ev;
        lastTriggerTime_[slug] = SteadyClock::now();
    }

    rebuildSnapshot();

    // Broadcast to zone
    if (gs_)
    {
        nlohmann::json data;
        data["eventSlug"] = slug;
        data["durationSec"] = tmpl.durationSec;
        data["gameZoneId"] = zoneId;
        gs_->getStatsNotificationService().sendWorldNotificationToGameZone(
            zoneId, "zone_event_start", tmpl.announceKey.empty() ? "Началось мировое событие: " + slug : tmpl.announceKey, data);
    }

    log_->info("[ZoneEvent] Event '{}' started in gameZone={}, duration={}s",
        slug,
        zoneId,
        tmpl.durationSec);
}

void
ZoneEventManager::endEvent(const std::string &slug)
{
    endEventInternal(slug);
}

void
ZoneEventManager::endEventInternal(const std::string &slug)
{
    int zoneId = 0;
    {
        std::unique_lock lk(mutex_);
        auto it = active_.find(slug);
        if (it == active_.end())
            return;
        zoneId = it->second.gameZoneId;
        active_.erase(it);
    }

    rebuildSnapshot();

    if (gs_ && zoneId >= 0)
    {
        gs_->getStatsNotificationService().sendWorldNotificationToGameZone(
            zoneId, "zone_event_end", "Событие завершилось.", {{"eventSlug", slug}});
    }

    log_->info("[ZoneEvent] Event '{}' ended", slug);
}

// ── Ticker ─────────────────────────────────────────────────────────────────

void
ZoneEventManager::tickEventScheduler()
{
    const auto now = SteadyClock::now();

    // 1. Expire finished events
    {
        std::unique_lock lk(mutex_);
        std::vector<std::string> toExpire;
        for (const auto &[slug, ev] : active_)
            if (now >= ev.endsAt)
                toExpire.push_back(slug);
        lk.unlock();
        for (const auto &slug : toExpire)
            endEventInternal(slug);
    }

    // 2. Try to trigger events from templates
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (const auto &tmpl : templates_)
    {
        // Skip if already active
        {
            std::shared_lock slk(mutex_);
            if (active_.count(tmpl.slug))
                continue;
        }

        if (tmpl.triggerType == "scheduled" && tmpl.intervalHours > 0)
        {
            std::shared_lock slk(mutex_);
            auto lastIt = lastTriggerTime_.find(tmpl.slug);
            if (lastIt == lastTriggerTime_.end() || now - lastIt->second >= std::chrono::hours(tmpl.intervalHours))
            {
                slk.unlock();
                startEvent(tmpl.slug);
            }
        }
        else if (tmpl.triggerType == "random" && tmpl.randomChancePerHour > 0.0f)
        {
            // Tick runs every 30 s → 120 ticks/hour
            float tickChance = tmpl.randomChancePerHour / 120.0f;
            if (dist(rng_) < tickChance)
                startEvent(tmpl.slug);
        }
    }
}

// ── Hot-path accessors (lock-free) ─────────────────────────────────────────

float
ZoneEventManager::getLootMultiplier(int gameZoneId) const
{
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return 1.0f;

    float mult = 1.0f;
    // Zone-specific
    auto it = snap->byZone.find(gameZoneId);
    if (it != snap->byZone.end())
        mult *= it->second.lootMultiplier;
    // Global events (gameZoneId == 0)
    for (const auto &ev : snap->global)
        mult *= ev.lootMultiplier;
    return mult;
}

float
ZoneEventManager::getMobSpeedMultiplier(int gameZoneId) const
{
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return 1.0f;

    float mult = 1.0f;
    auto it = snap->byZone.find(gameZoneId);
    if (it != snap->byZone.end())
        mult *= it->second.mobSpeedMultiplier;
    for (const auto &ev : snap->global)
        mult *= ev.mobSpeedMultiplier;
    return mult;
}

float
ZoneEventManager::getSpawnRateMultiplier(int gameZoneId) const
{
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return 1.0f;

    float mult = 1.0f;
    auto it = snap->byZone.find(gameZoneId);
    if (it != snap->byZone.end())
        mult *= it->second.spawnRateMultiplier;
    for (const auto &ev : snap->global)
        mult *= ev.spawnRateMultiplier;
    return mult;
}

bool
ZoneEventManager::hasActiveEvent(int gameZoneId) const
{
    auto snap = std::atomic_load(&snapshot_);
    if (!snap)
        return false;
    return snap->byZone.count(gameZoneId) > 0 || !snap->global.empty();
}

// ── Private ────────────────────────────────────────────────────────────────

void
ZoneEventManager::rebuildSnapshot()
{
    auto newSnap = std::make_shared<EventSnapshot>();

    std::shared_lock lk(mutex_);
    for (const auto &[slug, ev] : active_)
    {
        if (ev.gameZoneId == 0)
            newSnap->global.push_back(ev);
        else
            newSnap->byZone[ev.gameZoneId] = ev; // last writer wins per zone
    }

    std::atomic_store(&snapshot_, std::static_pointer_cast<const EventSnapshot>(newSnap));
}
