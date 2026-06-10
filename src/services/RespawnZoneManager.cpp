#include "services/RespawnZoneManager.hpp"
#include <limits>
#include <random>
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

PositionStruct
RespawnZoneManager::getRandomPointInZone(const RespawnZoneStruct &zone) const
{
    // Fallback: no area bounds defined → return fixed position
    if (!zone.isAreaDefined())
        return zone.position;

    static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    static std::uniform_real_distribution<float> angleDist(0.0f,
        static_cast<float>(2.0 * M_PI));

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    switch (zone.shape)
    {
    case ZoneShape::CIRCLE:
    {
        float angle = angleDist(rng);
        float r = zone.outerRadius * std::sqrt(unit(rng));
        x = zone.centerX + r * std::cos(angle);
        y = zone.centerY + r * std::sin(angle);
        break;
    }
    case ZoneShape::ANNULUS:
    {
        float angle = angleDist(rng);
        float r2in = zone.innerRadius * zone.innerRadius;
        float r2out = zone.outerRadius * zone.outerRadius;
        float r = std::sqrt(r2in + unit(rng) * (r2out - r2in));
        x = zone.centerX + r * std::cos(angle);
        y = zone.centerY + r * std::sin(angle);
        break;
    }
    case ZoneShape::RECT:
    default:
    {
        x = zone.minX + unit(rng) * (zone.maxX - zone.minX);
        y = zone.minY + unit(rng) * (zone.maxY - zone.minY);
        break;
    }
    }

    z = zone.minZ + unit(rng) * (zone.maxZ - zone.minZ);

    PositionStruct pos;
    pos.positionX = x;
    pos.positionY = y;
    pos.positionZ = z;
    pos.rotationZ = 0.0f;
    return pos;
}

std::vector<RespawnZoneStruct>
RespawnZoneManager::getAllZones() const
{
    std::shared_lock lock(mutex_);
    return zones_;
}
