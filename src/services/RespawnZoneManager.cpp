#include "services/RespawnZoneManager.hpp"
#include "utils/RandomUtils.hpp"
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

    // RNG: RandomUtils (one thread_local engine per thread; distributions are
    // function-local, never shared — see RandomUtils.hpp).
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    switch (zone.shape)
    {
    case ZoneShape::CIRCLE:
    {
        float angle = RandomUtils::angle();
        float r = zone.outerRadius * std::sqrt(RandomUtils::uniform01());
        x = zone.centerX + r * std::cos(angle);
        y = zone.centerY + r * std::sin(angle);
        break;
    }
    case ZoneShape::ANNULUS:
    {
        float angle = RandomUtils::angle();
        float r2in = zone.innerRadius * zone.innerRadius;
        float r2out = zone.outerRadius * zone.outerRadius;
        float r = std::sqrt(r2in + RandomUtils::uniform01() * (r2out - r2in));
        x = zone.centerX + r * std::cos(angle);
        y = zone.centerY + r * std::sin(angle);
        break;
    }
    case ZoneShape::RECT:
    default:
    {
        x = zone.minX + RandomUtils::uniform01() * (zone.maxX - zone.minX);
        y = zone.minY + RandomUtils::uniform01() * (zone.maxY - zone.minY);
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
