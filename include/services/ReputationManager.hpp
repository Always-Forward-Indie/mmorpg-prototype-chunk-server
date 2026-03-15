#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace spdlog
{
class logger;
}

/**
 * @brief ReputationManager — per-player reputation with world factions.
 *
 * Tracks numeric reputation per (characterId, factionSlug).  Values are loaded
 * from the DB on character login (via SET_PLAYER_REPUTATIONS) and persisted
 * back via saveCallback_ on every change.
 *
 * Tier thresholds from §5.1:
 *   < -500 → enemy  |  -500..0 → stranger  |  0..199 → neutral
 *   200..499 → friendly  |  ≥500 → ally
 *
 * Thread-safety: all public methods are protected by a shared_mutex.
 */
class ReputationManager
{
  public:
    explicit ReputationManager(Logger &logger);
    ~ReputationManager() = default;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void loadCharacterReputations(int characterId,
        const std::unordered_map<std::string, int> &reps);

    void unloadCharacterReputations(int characterId);

    // ── Query ──────────────────────────────────────────────────────────────
    /// Returns 0 if faction/character not found.
    int getReputation(int characterId, const std::string &factionSlug) const;

    /// Fills PlayerContextStruct::reputations for condition evaluation.
    void fillReputationContext(int characterId, PlayerContextStruct &ctx) const;

    /// Human-readable tier for a raw value: "enemy"|"stranger"|"neutral"|"friendly"|"ally"
    static std::string getTier(int value);

    // ── Mutation ───────────────────────────────────────────────────────────
    /**
     * @brief Change reputation for a character with a faction.
     *        Persists via saveCallback_ and fires a worldNotification on tier
     *        change (handled through optional tierChangeCallback_).
     */
    void changeReputation(int characterId, const std::string &factionSlug, int delta);

    // ── Persistence ────────────────────────────────────────────────────────
    /** JSON string ("eventType":"saveReputation") sent to Game Server. */
    using SaveCallback = std::function<void(const std::string &)>;
    void setSaveCallback(SaveCallback cb)
    {
        saveCallback_ = std::move(cb);
    }

    /** Optional: called when a tier boundary is crossed. */
    using TierChangeCallback = std::function<void(int /*characterId*/,
        const std::string & /*factionSlug*/,
        const std::string & /*newTier*/,
        int /*value*/)>;
    void setTierChangeCallback(TierChangeCallback cb)
    {
        tierChangeCallback_ = std::move(cb);
    }

  private:
    void persist(int characterId, const std::string &factionSlug, int value);

    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // characterId → (faction_slug → value)
    std::unordered_map<int, std::unordered_map<std::string, int>> data_;
    mutable std::shared_mutex mutex_;

    SaveCallback saveCallback_;
    TierChangeCallback tierChangeCallback_;
};
