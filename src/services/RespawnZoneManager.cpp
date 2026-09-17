#include "services/RespawnZoneManager.hpp"
#include "utils/RandomUtils.hpp"
#include "utils/SpawnGeometry.hpp"
#include <limits>
#include <spdlog/logger.h>
#include <tuple>

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

    // Unreachable with a non-empty list (the loop above always picks the
    // first zone at minimum), kept as a guard. NOTE: isDefault currently
    // has no effect — nearest always wins. Preferring the default zone
    // would change live respawn behavior and needs a product decision.
    return zones_.front();
}

PositionStruct
RespawnZoneManager::getRandomPointInZone(const RespawnZoneStruct &zone) const
{
    // Fallback: no area bounds defined → return fixed position
    if (!zone.isAreaDefined())
        return zone.position;

    // Sampling math lives in SpawnGeometry (single source with mob spawn
    // and champion placement); only the Z blend stays local.
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    switch (zone.shape)
    {
    case ZoneShape::CIRCLE:
    {
        std::tie(x, y) = SpawnGeometry::sampleCircle(zone.centerX, zone.centerY, zone.outerRadius);
        break;
    }
    case ZoneShape::ANNULUS:
    {
        std::tie(x, y) =
            SpawnGeometry::sampleAnnulus(zone.centerX, zone.centerY, zone.innerRadius, zone.outerRadius);
        break;
    }
    case ZoneShape::RECT:
    default:
    {
        std::tie(x, y) = SpawnGeometry::sampleRect(zone.minX, zone.maxX, zone.minY, zone.maxY);
        break;
    }
    }

    z = zone.minZ + RandomUtils::uniform01() * (zone.maxZ - zone.minZ);

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
