#pragma once

#include "utils/Logger.hpp"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace spdlog
{
class logger;
}
class CharacterManager;
class MobInstanceManager;
class MobMovementManager;
class GameZoneManager;
class ExperienceManager;
class InventoryManager;
class ItemManager;
class GameConfigService;
class ClientManager;
class BestiaryManager;
class ChampionManager;
class ReputationManager;

/**
 * @brief Mob-kill reward flow (Increment 10.3).
 *
 * Extracted from CombatSystem::handleMobDeath 1-1: proportional XP by damage
 * contribution (+10 % killer bonus, 5 % threshold, killer-only fallback),
 * fellowship bonus with anti-alt filter, Item Soul kill-count with tier
 * flush, quest/bestiary/champion/reputation hooks, level_up + mob_killed
 * analytics. Explicit Logger-only/DI dependencies; GameServices-coupled side
 * effects (quest hook, client notifications, stats updates, analytics and
 * kill-count persistence packets) are std::function seams wired by
 * CombatSystem. Each stage keeps its original try/catch boundary.
 */
class MobKillRewardPipeline
{
  public:
    /// Quest hook: QuestManager::onMobKilled(killerId, mobTemplateId)
    using QuestHook = std::function<void(int, int)>;
    /// Client notification (characterId, type, data, priority, channel)
    using NotifyFn = std::function<void(int,
        const std::string &,
        const nlohmann::json &,
        const std::string &,
        const std::string &)>;
    /// Full stats_update push (characterId)
    using StatsUpdateFn = std::function<void(int)>;
    /// Analytics packet sender (analyticsEvent JSON string)
    using AnalyticsFn = std::function<void(const std::string &)>;
    /// Kill-count persistence (characterId, inventoryItemId, killCount)
    using SaveKillCountFn = std::function<void(int, int, int)>;

    MobKillRewardPipeline(CharacterManager &characters,
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
        Logger &logger);

    void setQuestHook(QuestHook hook);
    void setNotifyCallback(NotifyFn callback);
    void setStatsUpdateCallback(StatsUpdateFn callback);
    void setAnalyticsCallback(AnalyticsFn callback);
    void setSaveKillCountCallback(SaveKillCountFn callback);

    /// Run the full reward flow for a mob death (mob lookup, XP, fellowship,
    /// item soul, hooks, analytics). Never throws — stages guard themselves.
    void execute(int mobId, int killerId);

  private:
    CharacterManager &characters_;
    MobInstanceManager &mobInstances_;
    MobMovementManager &mobMovement_;
    GameZoneManager &gameZones_;
    ExperienceManager &experience_;
    InventoryManager &inventory_;
    ItemManager &items_;
    GameConfigService &gameConfig_;
    ClientManager &clients_;
    BestiaryManager &bestiary_;
    ChampionManager &champions_;
    ReputationManager &reputation_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    QuestHook questHook_;
    NotifyFn notifyCallback_;
    StatsUpdateFn statsUpdateCallback_;
    AnalyticsFn analyticsCallback_;
    SaveKillCountFn saveKillCountCallback_;

    void grantProportionalXp(int mobId, int killerId);
    void grantFellowshipBonus(int mobId, int killerId);
    void trackItemSoul(int killerId);
    void fireKillHooks(int mobId, int killerId);

    /// level_up analytics for one participant (session-gated, never throws).
    void sendLevelUpAnalytics(int characterId, int newLevel, int oldLevel);
};
