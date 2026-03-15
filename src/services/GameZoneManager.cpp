#include "services/GameZoneManager.hpp"
#include <spdlog/logger.h>

GameZoneManager::GameZoneManager(Logger &logger)
    : logger_(logger)
{
}

void
GameZoneManager::loadGameZones(const std::vector<GameZoneStruct> &zones)
{
    std::unique_lock lock(mutex_);
    zones_ = zones;
    logger_.log("[GameZoneManager] Loaded " + std::to_string(zones_.size()) + " game zones");
}

std::optional<GameZoneStruct>
GameZoneManager::getZoneForPosition(const PositionStruct &pos) const
{
    std::shared_lock lock(mutex_);
    for (const auto &zone : zones_)
    {
        if (pos.positionX >= zone.minX && pos.positionX <= zone.maxX &&
            pos.positionY >= zone.minY && pos.positionY <= zone.maxY)
        {
            return zone;
        }
    }
    return std::nullopt;
}

std::vector<GameZoneStruct>
GameZoneManager::getAllZones() const
{
    std::shared_lock lock(mutex_);
    return zones_;
}
