#pragma once

#include "utils/Logger.hpp"
#include <functional>
#include <memory>
#include <string>

namespace spdlog
{
class logger;
}
class CharacterManager;
class ExperienceManager;
class DurabilityService;
class GameZoneManager;

/**
 * @brief Player-death flow (Increment 10.4).
 *
 * Extracted from CombatSystem::handleTargetDeath(PLAYER) 1-1: XP-debt penalty
 * (10 % floored at current-level start, never removed immediately), death
 * durability wear (via DurabilityService), final stats snapshot, player_death
 * analytics. Each stage keeps its original try/catch boundary. The isDead
 * guard (HoT-race / double-penalty protection) returns false when the death
 * must be skipped.
 */
class PlayerDeathPipeline
{
  public:
    /// Full stats_update push (characterId)
    using StatsUpdateFn = std::function<void(int)>;
    /// Analytics packet sender (analyticsEvent JSON string)
    using AnalyticsFn = std::function<void(const std::string &)>;

    PlayerDeathPipeline(CharacterManager &characters,
        ExperienceManager &experience,
        DurabilityService &durability,
        GameZoneManager &gameZones,
        Logger &logger);

    void setStatsUpdateCallback(StatsUpdateFn callback);
    void setAnalyticsCallback(AnalyticsFn callback);

    /// Process a player death. Returns false when skipped by the isDead guard.
    bool execute(int targetId);

  private:
    CharacterManager &characters_;
    ExperienceManager &experience_;
    DurabilityService &durability_;
    GameZoneManager &gameZones_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    StatsUpdateFn statsUpdateCallback_;
    AnalyticsFn analyticsCallback_;
};
