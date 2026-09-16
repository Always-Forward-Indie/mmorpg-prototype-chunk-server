#pragma once

#include <string>

/**
 * @brief Minimal stats-update sink (Champion-style nullable dependency).
 *
 * RegenManager notifies through this interface instead of GameServices so unit
 * tests can inject a fake without dragging the whole world into the test
 * binary. Production passes &statsNotificationService_ (implicit upcast).
 * May be null — callers then apply regen silently (same pattern as
 * ChampionManager::statsNotify_).
 */
class IStatsNotifier
{
  public:
    virtual ~IStatsNotifier() = default;
    virtual void sendStatsUpdate(int characterId) = 0;
    virtual void sendStatsUpdate(int characterId, const std::string &source) = 0;
};
