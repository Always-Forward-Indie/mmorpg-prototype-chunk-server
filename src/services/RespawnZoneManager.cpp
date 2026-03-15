#include "services/RespawnZoneManager.hpp"
#include <limits>
#include <spdlog/logger.h>

RespawnZoneManager::RespawnZoneManager(Logger &logger)
    : logger_(logger)
{
}

void
RespawnZoneManager::loadRespawnZones(const std::vector<RespawnZoneStruct> &zones)
{
    std::unique_lock lock(mutex_);
    zones_ = zones;
    logger_.log("[RespawnZoneManager] Loaded " + std::to_string(zones_.size()) + " respawn zones");
}

RespawnZoneStruct
RespawnZoneManager::findNearest(const PositionStruct &deathPosition) const
{
    std::shared_lock lock(mutex_);

    if (zones_.empty())
    {
        logger_.logError("[RespawnZoneManager] No respawn zones loaded — returning empty zone");
        return RespawnZoneStruct{};
    }

    const RespawnZoneStruct *best = nullptr;
    float bestDist = std::numeric_limits<float>::max();

    for (const auto &zone : zones_)
    {
        float dx = zone.position.positionX - deathPosition.positionX;
        float dy = zone.position.positionY - deathPosition.positionY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = &zone;
        }
    }

    if (best)
        return *best;

    // Fallback: find zone marked as default
    for (const auto &zone : zones_)
    {
        if (zone.isDefault)
            return zone;
    }

    return zones_.front();
}

std::vector<RespawnZoneStruct>
RespawnZoneManager::getAllZones() const
{
    std::shared_lock lock(mutex_);
    return zones_;
}
