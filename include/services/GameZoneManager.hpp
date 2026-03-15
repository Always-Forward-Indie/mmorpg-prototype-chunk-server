#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <optional>
#include <shared_mutex>
#include <vector>

/**
 * @brief Manages named game zones (loaded from the `zones` DB table).
 *
 * Each zone is an AABB rectangle in world-space.  Use getZoneForPosition()
 * to find which zone a character currently occupies, and call
 * loadGameZones() when the game-server sends the zone list on startup.
 */
class GameZoneManager
{
  public:
    explicit GameZoneManager(Logger &logger);

    /// Replace the full zone list (called on SET_GAME_ZONES event).
    void loadGameZones(const std::vector<GameZoneStruct> &zones);

    /**
     * @brief Return the zone that contains the given world position.
     * @return The first matching zone, or std::nullopt if outside all zones.
     */
    std::optional<GameZoneStruct> getZoneForPosition(const PositionStruct &pos) const;

    /// Return the full list (for debugging / admin tools).
    std::vector<GameZoneStruct> getAllZones() const;

  private:
    mutable std::shared_mutex mutex_;
    std::vector<GameZoneStruct> zones_;
    Logger &logger_;
};
