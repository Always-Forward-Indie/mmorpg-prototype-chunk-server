#include "services/PlayerDeathPipeline.hpp"
#include "services/CharacterManager.hpp"
#include "services/DurabilityService.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameZoneManager.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

PlayerDeathPipeline::PlayerDeathPipeline(CharacterManager &characters,
    ExperienceManager &experience,
    DurabilityService &durability,
    GameZoneManager &gameZones,
    Logger &logger)
    : characters_(characters),
      experience_(experience),
      durability_(durability),
      gameZones_(gameZones),
      logger_(logger)
{
    log_ = logger_.getSystem("combat");
}

void
PlayerDeathPipeline::setStatsUpdateCallback(StatsUpdateFn callback)
{
    statsUpdateCallback_ = std::move(callback);
}

void
PlayerDeathPipeline::setAnalyticsCallback(AnalyticsFn callback)
{
    analyticsCallback_ = std::move(callback);
}

bool
PlayerDeathPipeline::execute(int targetId)
{
    // Логика смерти игрока - отнимаем опыт
    try
    {
        auto characterData = characters_.getCharacterData(targetId);

        // Guard: character may have been healed between the death event and this handler
        // (e.g. HoT tick fired concurrently). Also prevents double-death penalty.
        if (!characterData.isDead)
        {
            logger_.log("Player " + std::to_string(targetId) +
                            " handleTargetDeath called but isDead=false — skipping penalty");
            return false;
        }

        const int expFloor = (characterData.characterLevel > 1)
            ? experience_.getExperienceForLevelFromGameServer(characterData.characterLevel - 1)
            : 0;
        int penaltyAmount = ExperienceManager::calculateDeathPenalty(
            characterData.characterLevel, characterData.characterExperiencePoints, expFloor);

        if (penaltyAmount > 0)
        {
            // Set experience debt instead of removing XP immediately.
            // The debt will be paid off gradually as the player earns XP (50% per gain).
            characterData.experienceDebt += penaltyAmount;
            characters_.addExperienceDebt(targetId, penaltyAmount);
            logger_.log("Player " + std::to_string(targetId) + " died — experience debt set to " +
                        std::to_string(characterData.experienceDebt));
        }
        else
        {
            log_->info("Player " + std::to_string(targetId) + " died but no experience debt was added");
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error handling player death experience penalty: " + std::string(e.what()));
    }

    // Durability: death penalty — reduce all equipped durable items
    try
    {
        durability_.applyDeathPenalty(targetId);
    }
    catch (const std::exception &e)
    {
        log_->warn("[COMBAT] Death durability penalty error: " + std::string(e.what()));
    }

    // Send final stats snapshot after all death penalties (XP, durability) are applied.
    // This covers DoT/AoE/mob-attack death paths that have no sendStatsUpdate after
    // handleTargetDeath. The direct-skill path sends an additional one shortly after,
    // which is harmless (client just replaces state with the latest values).
    try
    {
        if (statsUpdateCallback_)
            statsUpdateCallback_(targetId);
    }
    catch (const std::exception &e)
    {
        log_->warn("[COMBAT] Death stats update error: " + std::string(e.what()));
    }

    // Analytics: player_death
    try
    {
        auto deadData = characters_.getCharacterData(targetId);
        if (deadData.characterId != 0 && !deadData.sessionId.empty())
        {
            int zoneId = 0;
            auto zoneOpt = gameZones_.getZoneForPosition(deadData.characterPosition);
            if (zoneOpt.has_value())
                zoneId = zoneOpt->id;
            nlohmann::json ap;
            ap["header"]["eventType"] = "analyticsEvent";
            ap["body"]["analyticsType"] = "player_death";
            ap["body"]["characterId"] = targetId;
            ap["body"]["sessionId"] = deadData.sessionId;
            ap["body"]["level"] = deadData.characterLevel;
            ap["body"]["zoneId"] = zoneId;
            ap["body"]["payload"] = nlohmann::json::object();
            if (analyticsCallback_)
                analyticsCallback_(ap.dump() + "\n");
        }
    }
    catch (...)
    {
    }

    return true;
}
