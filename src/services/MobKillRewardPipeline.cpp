#include "services/MobKillRewardPipeline.hpp"
#include "services/BestiaryManager.hpp"
#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/ClientManager.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/ItemSoulTiers.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobMovementManager.hpp"
#include "services/ReputationManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <spdlog/logger.h>
#include <vector>

MobKillRewardPipeline::MobKillRewardPipeline(CharacterManager &characters,
    MobInstanceManager &mobInstances,
    MobMovementManager &mobMovement,
    GameZoneManager &gameZones,
    ExperienceManager &experience,
    InventoryManager &inventory,
    ItemManager &items,
    GameConfigService &gameConfig,
    ClientManager &clients,
    BestiaryManager &bestiary,
    ChampionManager &champions,
    ReputationManager &reputation,
    Logger &logger)
    : characters_(characters),
      mobInstances_(mobInstances),
      mobMovement_(mobMovement),
      gameZones_(gameZones),
      experience_(experience),
      inventory_(inventory),
      items_(items),
      gameConfig_(gameConfig),
      clients_(clients),
      bestiary_(bestiary),
      champions_(champions),
      reputation_(reputation),
      logger_(logger)
{
    log_ = logger_.getSystem("combat");
}

void
MobKillRewardPipeline::setQuestHook(QuestHook hook)
{
    questHook_ = std::move(hook);
}

void
MobKillRewardPipeline::setNotifyCallback(NotifyFn callback)
{
    notifyCallback_ = std::move(callback);
}

void
MobKillRewardPipeline::setStatsUpdateCallback(StatsUpdateFn callback)
{
    statsUpdateCallback_ = std::move(callback);
}

void
MobKillRewardPipeline::setAnalyticsCallback(AnalyticsFn callback)
{
    analyticsCallback_ = std::move(callback);
}

void
MobKillRewardPipeline::setSaveKillCountCallback(SaveKillCountFn callback)
{
    saveKillCountCallback_ = std::move(callback);
}

void
MobKillRewardPipeline::execute(int mobId, int killerId)
{
    // Mob lookup stays here so stage logs match the original order.
    auto mobData = mobInstances_.getMobInstance(mobId);
    logger_.log("Mob " + std::to_string(mobId) + " (level " + std::to_string(mobData.level) +
                    ") was killed by " + std::to_string(killerId));

    grantProportionalXp(mobId, killerId);
    grantFellowshipBonus(mobId, killerId);
    trackItemSoul(killerId);
    fireKillHooks(mobId, killerId);
}

void
MobKillRewardPipeline::grantProportionalXp(int mobId, int killerId)
{
    // ── Proportional XP based on damage contribution ────────────────────
    // Splits mob XP among all players who damaged the mob, weighted by
    // damage dealt (threatTable).  The killer (last hit) receives an
    // additional +10 % finishing blow bonus on top of their share.
    // Falls back to killer-only XP if threatTable is empty.
    try
    {
        auto mobData = mobInstances_.getMobInstance(mobId);
        const int scaledBaseXp = static_cast<int>(std::lround(mobData.baseExperience * mobData.rankMult));

        auto mobMovData = mobMovement_.getMobMovementData(mobId);
        const auto &threatTable = mobMovData.threatTable;

        if (!threatTable.empty())
        {
            int totalThreat = 0;
            for (const auto &[pid, threat] : threatTable)
                totalThreat += threat;
            if (totalThreat <= 0) totalThreat = 1;

            for (const auto &[playerId, threat] : threatTable)
            {
                try
                {
                    auto playerData = characters_.getCharacterData(playerId);
                    if (playerData.characterId == 0)
                        continue;

                    const float share = static_cast<float>(threat) / static_cast<float>(totalThreat);
                    if (share < 0.05f) // minimum 5 % contribution threshold
                        continue;

                    const int personalBaseXp = ExperienceManager::calculateMobExperience(
                        mobData.level, playerData.characterLevel, scaledBaseXp);
                    int xpAward = static_cast<int>(std::lround(static_cast<float>(personalBaseXp) * share));

                    // Killer finishing blow bonus: +10 % on their share
                    if (playerId == killerId)
                        xpAward = static_cast<int>(std::lround(static_cast<float>(xpAward) * 1.10f));

                    if (xpAward > 0)
                    {
                        auto result = experience_.grantExperience(playerId, xpAward, "mob_kill", mobId);

                        if (result.success)
                        {
                            log_->info("[XP] char {} gained {} XP ({}% dmg) for mob {}",
                                playerId, xpAward, static_cast<int>(share * 100.0f), mobId);

                            if (result.levelUp)
                            {
                                logger_.log("Character " + std::to_string(playerId) +
                                                " leveled up to level " +
                                                std::to_string(result.experienceEvent.newLevel),
                                    CYAN);
                                sendLevelUpAnalytics(playerId,
                                    result.experienceEvent.newLevel,
                                    result.experienceEvent.newLevel - 1);
                            }
                        }
                        else
                        {
                            log_->error("Failed to grant experience to char " + std::to_string(playerId) +
                                        ": " + result.errorMessage);
                        }
                    }
                }
                catch (const std::exception &)
                {
                    // Non-player entry in threatTable — skip
                }
            }
        }
        else
        {
            // Fallback: no threat data → award full XP to killer only
            auto killerData = characters_.getCharacterData(killerId);
            if (killerData.characterId != 0)
            {
                int expAmount = ExperienceManager::calculateMobExperience(
                    mobData.level, killerData.characterLevel, scaledBaseXp);
                if (expAmount > 0)
                {
                    auto result = experience_.grantExperience(killerId, expAmount, "mob_kill", mobId);

                    if (result.success)
                    {
                        log_->info("Character " + std::to_string(killerId) + " gained " +
                                   std::to_string(expAmount) + " experience for killing mob " +
                                   std::to_string(mobId));

                        if (result.levelUp)
                        {
                            logger_.log("Character " + std::to_string(killerId) +
                                            " leveled up to level " +
                                            std::to_string(result.experienceEvent.newLevel),
                                CYAN);
                            sendLevelUpAnalytics(killerId,
                                result.experienceEvent.newLevel,
                                result.experienceEvent.newLevel - 1);
                        }
                    }
                    else
                    {
                        log_->error("Failed to grant experience: " + result.errorMessage);
                    }
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        log_->warn("[XP] Experience award error for mob " + std::to_string(mobId) + ": " + std::string(e.what()));
    }
}

void
MobKillRewardPipeline::grantFellowshipBonus(int mobId, int killerId)
{
    // --- Fellowship Bonus ---
    // If another player attacked this mob within the config window, both that
    // player and the killer receive a small bonus XP for fighting together.
    try
    {
        auto mobData = mobInstances_.getMobInstance(mobId);
        const float bonusPct = gameConfig_.getFloat("fellowship.bonus_pct", 0.07f);
        const int windowSec = gameConfig_.getInt("fellowship.attack_window_sec", 15);

        auto mobMovData = mobMovement_.getMobMovementData(mobId);
        const auto now = std::chrono::steady_clock::now();

        std::vector<int> fellows;
        for (const auto &[charId, lastAttack] : mobMovData.attackerTimestamps)
        {
            if (charId == killerId)
                continue;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastAttack).count();
            if (elapsed <= windowSec)
                fellows.push_back(charId);
        }

        // Anti-alt: resolve killer's accountId and exclude co-attackers from the same account
        const int killerAccountId = clients_.getClientDataByCharacterId(killerId).accountId;
        if (killerAccountId > 0)
        {
            fellows.erase(
                std::remove_if(fellows.begin(), fellows.end(), [&](int fellowId)
                    {
                        int fellowAccountId = clients_.getClientDataByCharacterId(fellowId).accountId;
                        return (fellowAccountId > 0 && fellowAccountId == killerAccountId);
                    }),
                fellows.end());
        }

        if (!fellows.empty())
        {
            const int scaledBaseXp = static_cast<int>(std::lround(mobData.baseExperience * mobData.rankMult));

            // Compute total threat for proportional fellowship bonus
            int totalThreat = 0;
            for (const auto &[pid, threat] : mobMovData.threatTable)
                totalThreat += threat;
            if (totalThreat <= 0) totalThreat = 1;

            // Helper: proportional fellowship bonus for a player
            auto grantBonus = [&](int playerId)
            {
                auto playerData = characters_.getCharacterData(playerId);
                if (playerData.characterId == 0) return;

                float share = 0.0f;
                auto threatIt = mobMovData.threatTable.find(playerId);
                if (threatIt != mobMovData.threatTable.end())
                    share = static_cast<float>(threatIt->second) / static_cast<float>(totalThreat);
                if (share < 0.05f) return; // minimum 5 % contribution

                const int personalBaseXp = ExperienceManager::calculateMobExperience(
                    mobData.level, playerData.characterLevel, scaledBaseXp);
                int bonus = static_cast<int>(std::lround(static_cast<float>(personalBaseXp) * share * bonusPct));
                if (bonus > 0)
                {
                    experience_.grantExperience(playerId, bonus, "fellowship_bonus", mobId);
                    if (notifyCallback_)
                    {
                        notifyCallback_(playerId,
                            "fellowship_bonus",
                            nlohmann::json{{"xpBonus", bonus}},
                            "low",
                            "float_text");
                    }
                    log_->info("[Fellowship] +" + std::to_string(bonus) + " XP -> char " + std::to_string(playerId));
                }
            };

            // Bonus to killer
            grantBonus(killerId);

            // Bonus to each fellow
            for (int fellowId : fellows)
                grantBonus(fellowId);

            log_->info("[Fellowship] Mob " + std::to_string(mobId) + " had " + std::to_string(fellows.size()) +
                       " fellow attacker(s)");
        }
    }
    catch (const std::exception &e)
    {
        log_->warn("[Fellowship] Error: " + std::string(e.what()));
    }
}

void
MobKillRewardPipeline::trackItemSoul(int killerId)
{
    // --- Item Soul: increment kill_count on killer's equipped weapon ---
    try
    {
        auto weapon = inventory_.getEquippedWeapon(killerId);
        if (weapon.has_value())
        {
            const auto &wItem = items_.getItemById(weapon->itemId);
            if (wItem.isEquippable)
            {
                int newKillCount = weapon->killCount + 1;
                inventory_.updateItemKillCount(killerId, weapon->id, newKillCount);

                // Debounce: only flush to DB at tier boundaries or every N kills to
                // avoid hammering the game-server on every mob death.
                // Tiers from ItemSoulTiers (single source of truth, Wave 2.4).
                const ItemSoulTiers::Table soulTiers = ItemSoulTiers::Table::load(gameConfig_);
                const bool tierCrossed = ItemSoulTiers::isTierBoundary(soulTiers, newKillCount);
                if ((newKillCount % soulTiers.dbFlushEveryKills == 0 || tierCrossed) && saveKillCountCallback_)
                    saveKillCountCallback_(killerId, weapon->id, newKillCount);

                // Notify the killer's client immediately so the weapon tooltip stays in sync.
                if (notifyCallback_)
                {
                    nlohmann::json killData;
                    killData["inventoryItemId"] = weapon->id;
                    killData["killCount"] = newKillCount;
                    notifyCallback_(killerId, "weapon_kill_count_update", killData, "low", "silent");
                }

                // When a tier boundary is crossed the soul bonus to effective attributes
                // changes. Push a full stats_update so the client reflects the new
                // effective values immediately (without waiting for the next combat event).
                if (tierCrossed && statsUpdateCallback_)
                    statsUpdateCallback_(killerId);

                log_->info("[ItemSoul] Weapon invId=" + std::to_string(weapon->id) +
                           " killCount=" + std::to_string(newKillCount));
            }
        }
    }
    catch (const std::exception &e)
    {
        log_->warn("[ItemSoul] Error updating kill_count: " + std::string(e.what()));
    }
}

void
MobKillRewardPipeline::fireKillHooks(int mobId, int killerId)
{
    auto mobData = mobInstances_.getMobInstance(mobId);

    // Quest trigger: notify QuestManager that the mob was killed.
    // Best-effort (a failing quest hook must not fail the kill rewards);
    // warn-level like the neighboring hooks (bestiary/champion/reputation).
    try
    {
        if (questHook_)
            questHook_(killerId, mobData.id);
    }
    catch (const std::exception &e)
    {
        log_->warn("[Quest] Error on mob kill hook for char={} ({}), rewards kept",
            killerId, e.what());
    }
    catch (...)
    {
    }

    // --- Bestiary: record kill for progression ---
    try
    {
        if (mobData.id > 0)
            bestiary_.recordKill(killerId, mobData.id);
    }
    catch (const std::exception &e)
    {
        log_->warn("[Bestiary] Error recording kill: " + std::string(e.what()));
    }

    // --- Threshold Champion kill counter ---
    // Attribution by ORIGIN (mob.zoneId = spawn zone id): fleeing mobs must
    // not void threshold progress. Position lookup is the fallback for mobs
    // with unknown origin (mob.zoneId 0).
    try
    {
        if (!mobData.isChampion && mobData.id > 0)
        {
            int fallbackZoneId = 0;
            auto gameZone = gameZones_.getZoneForPosition(mobData.position);
            if (gameZone.has_value())
                fallbackZoneId = gameZone->id;
            champions_.recordMobKill(fallbackZoneId, mobData.id, mobData.zoneId);
        }
    }
    catch (const std::exception &e)
    {
        log_->warn("[Champion] Error recording mob kill: " + std::string(e.what()));
    }

    // --- Champion death notification ---
    if (mobData.isChampion)
    {
        try
        {
            champions_.onChampionKilled(mobId, killerId, mobData.slug);
        }
        catch (const std::exception &e)
        {
            log_->warn("[Champion] Error handling champion death: " + std::string(e.what()));
        }
    }

    // --- Reputation: mob kill → faction rep change ---
    try
    {
        if (!mobData.factionSlug.empty() && mobData.repDeltaPerKill != 0)
        {
            reputation_.changeReputation(killerId, mobData.factionSlug, mobData.repDeltaPerKill);
        }
    }
    catch (const std::exception &e)
    {
        log_->warn("[Reputation] Error on mob kill: " + std::string(e.what()));
    }

    // Analytics: mob_killed (best-effort, never fails kill rewards)
    try
    {
        auto killerData = characters_.getCharacterData(killerId);
        if (killerData.characterId != 0 && !killerData.sessionId.empty())
        {
            int zoneId = 0;
            auto zoneOpt = gameZones_.getZoneForPosition(killerData.characterPosition);
            if (zoneOpt.has_value())
                zoneId = zoneOpt->id;
            nlohmann::json ap;
            ap["header"]["eventType"] = "analyticsEvent";
            ap["body"]["analyticsType"] = "mob_killed";
            ap["body"]["characterId"] = killerId;
            ap["body"]["sessionId"] = killerData.sessionId;
            ap["body"]["level"] = killerData.characterLevel;
            ap["body"]["zoneId"] = zoneId;
            ap["body"]["payload"] = {{"mobId", mobData.id}, {"mobSlug", mobData.slug}, {"mobLevel", mobData.level}};
            if (analyticsCallback_)
                analyticsCallback_(ap.dump() + "\n");
        }
    }
    catch (const std::exception &e)
    {
        log_->debug("[Analytics] mob_killed for char={} skipped ({})", killerId, e.what());
    }
    catch (...)
    {
    }
}

void
MobKillRewardPipeline::sendLevelUpAnalytics(int characterId, int newLevel, int oldLevel)
{
    try
    {
        auto levelData = characters_.getCharacterData(characterId);
        if (!levelData.sessionId.empty())
        {
            int zoneId = 0;
            auto zoneOpt = gameZones_.getZoneForPosition(levelData.characterPosition);
            if (zoneOpt.has_value())
                zoneId = zoneOpt->id;
            nlohmann::json ap;
            ap["header"]["eventType"] = "analyticsEvent";
            ap["body"]["analyticsType"] = "level_up";
            ap["body"]["characterId"] = characterId;
            ap["body"]["sessionId"] = levelData.sessionId;
            ap["body"]["level"] = newLevel;
            ap["body"]["zoneId"] = zoneId;
            ap["body"]["payload"] = {{"oldLevel", oldLevel}};
            if (analyticsCallback_)
                analyticsCallback_(ap.dump() + "\n");
        }
    }
    catch (const std::exception &e)
    {
        log_->debug("[Analytics] level_up for char={} skipped ({})", characterId, e.what());
    }
    catch (...)
    {
    }
}
