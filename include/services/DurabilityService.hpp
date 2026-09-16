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
class InventoryManager;
class ItemManager;
class GameConfigService;

/**
 * @brief Weapon/armor/death durability wear + threshold warnings (Increment 10.1).
 *
 * Extracted from CombatSystem 1-1 (weapon hit wear, armor hit wear, death
 * penalty, tiered durability_warning notifications, saveDurabilityChange
 * persistence packets). Explicit Logger-only dependencies; game-server
 * persistence, client notifications and attribute refresh are std::function
 * seams wired by CombatSystem (ChunkServer wiring untouched).
 */
class DurabilityService
{
  public:
    /// Fire-and-forget persistence packet sender (saveDurabilityChange JSON string)
    using SaveFn = std::function<void(const std::string &)>;
    /// Attribute refresh trigger on warning-boundary crossing (characterId)
    using RefreshFn = std::function<void(int characterId)>;
    /// Client notification sender (characterId, type, data, priority, channel)
    using NotifyFn = std::function<void(int,
        const std::string &,
        const nlohmann::json &,
        const std::string &,
        const std::string &)>;

    DurabilityService(InventoryManager &inventory,
        ItemManager &items,
        GameConfigService &gameConfig,
        Logger &logger);

    void setSaveCallback(SaveFn callback);
    void setRefreshAttributesCallback(RefreshFn callback);
    void setNotifyCallback(NotifyFn callback);

    /// Weapon wears on every landed hit (durability.weapon_loss_per_hit).
    void applyWeaponHitWear(int characterId);

    /// Equipped armor (except main_hand/two_hand) wears when its owner is hit.
    void applyArmorHitWear(int characterId);

    /// All equipped durable items wear on death (durability.death_penalty_pct).
    void applyDeathPenalty(int characterId);

  private:
    InventoryManager &inventory_;
    ItemManager &items_;
    GameConfigService &gameConfig_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    SaveFn saveCallback_;
    RefreshFn refreshAttributesCallback_;
    NotifyFn notifyCallback_;

    /// Send a durability change to the game server for persistence.
    void saveDurabilityChange(int characterId, int inventoryItemId, int durabilityCurrent);

    /** Fire refreshAttributesCallback_ if durability just crossed a warning threshold. */
    void checkAndTriggerDurabilityWarning(int characterId, int oldDur, int newDur, int maxDur);
};
