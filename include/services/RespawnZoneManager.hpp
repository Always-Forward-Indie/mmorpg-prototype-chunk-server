#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <cmath>
#include <shared_mutex>
#include <vector>

/**
 * @brief Manages respawn zones (safe revival points in the world).
 *
 * Holds the list of respawn zones received from the game server on startup.
 * Provides findNearest() to select where a dead player should be teleported.
 */
class RespawnZoneManager
{
  public:
    explicit RespawnZoneManager(Logger &logger);

    /**
     * @brief Replace the full list of respawn zones (called on SET_RESPAWN_ZONES event).
     */
    void loadRespawnZones(const std::vector<RespawnZoneStruct> &zones);

    /**
     * @brief Find the nearest respawn zone to a given world position.
     *
     * Falls back to the default zone if no zones are loaded or all are far away.
     * Returns a zone with id == 0 only when the list is completely empty.
     */
    RespawnZoneStruct findNearest(const PositionStruct &deathPosition) const;

    /**
     * @brief Return all loaded respawn zones (for debugging / admin tools).
     */
    std::vector<RespawnZoneStruct> getAllZones() const;

  private:
    mutable std::shared_mutex mutex_;
    std::vector<RespawnZoneStruct> zones_;
    Logger &logger_;
};
