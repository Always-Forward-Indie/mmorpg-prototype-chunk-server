#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace spdlog
{
class logger;
}
class GameServices;

/**
 * @brief MasteryManager — use-based weapon/skill mastery progression.
 *
 * Each character accumulates mastery value [0..100] per mastery_slug by
 * landing hits.  Growth is faster against same-level targets and slower
 * against weaker ones (diminishing returns).
 *
 * Tier milestones apply permanent ActiveEffects to the character:
 *   tier1 (20):  +1% damage bonus
 *   tier2 (50):  +5% damage bonus
 *   tier3 (80):  +3% crit_chance
 *   tier4 (100): +2% parry_chance
 *
 * Data is loaded from DB on login (SET_PLAYER_MASTERIES) and flushed
 * via saveCallback_ every N hits or on milestone.
 *
 * Thread-safety: shared_mutex on all public paths.
 */
class MasteryManager
{
  public:
    explicit MasteryManager(GameServices *gs);
    ~MasteryManager() = default;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void loadCharacterMasteries(int characterId,
        const std::unordered_map<std::string, float> &masteries);

    void unloadCharacterMasteries(int characterId);

    // ── Query ──────────────────────────────────────────────────────────────
    /// Returns 0.0f if not found.
    float getMasteryValue(int characterId, const std::string &masterySlug) const;

    /// Returns all mastery values for a character (empty map if not loaded).
    std::unordered_map<std::string, float> getAllMasteries(int characterId) const;

    /// Fills PlayerContextStruct::masteries for condition evaluation.
    void fillMasteryContext(int characterId, PlayerContextStruct &ctx) const;

    // ── Progression ────────────────────────────────────────────────────────
    /**
     * @brief Called after each successful player hit on a mob.
     * @param characterId   Attacker character.
     * @param masterySlug   Which mastery grows (from equipped weapon).
     * @param charLevel     Character level (for level-factor formula).
     * @param targetLevel   Mob level.
     */
    void onPlayerAttack(int characterId, const std::string &masterySlug, int charLevel, int targetLevel);

    // ── Persistence ────────────────────────────────────────────────────────
    using SaveCallback = std::function<void(const std::string &)>;
    void setSaveCallback(SaveCallback cb)
    {
        saveCallback_ = std::move(cb);
    }

    /**
     * @brief Called every time mastery progress is flushed (or on milestone) to
     *        push an incremental update to the owning client.
     * @param characterId  Affected character.
     * @param masterySlug  Changed mastery.
     * @param newValue     New value [0..100].
     * @param tierSlug     Non-empty only when a threshold was just crossed.
     */
    using ClientNotifyCallback = std::function<void(int, const std::string &, float, const std::string &)>;
    void setClientNotifyCallback(ClientNotifyCallback cb)
    {
        clientNotifyCallback_ = std::move(cb);
    }

  private:
    float calculateDelta(float currentValue, int charLevel, int targetLevel) const;
    void checkAndApplyMilestone(int characterId, const std::string &masterySlug, float oldValue, float newValue);
    void persist(int characterId, const std::string &masterySlug, float value);

    GameServices *gs_;
    std::shared_ptr<spdlog::logger> log_;

    // characterId → (mastery_slug → value)
    std::unordered_map<int, std::unordered_map<std::string, float>> data_;
    mutable std::shared_mutex mutex_;

    // characterId → (mastery_slug → hitCount for debounce)
    std::unordered_map<int, std::unordered_map<std::string, int>> hitCounters_;

    SaveCallback saveCallback_;
    ClientNotifyCallback clientNotifyCallback_;
};
