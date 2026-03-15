#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utils/Logger.hpp>

namespace spdlog
{
class logger;
}

/**
 * @brief PityManager — per-player, per-item rare-drop pity counter.
 *
 * Tracks how many times each player has killed a mob that can drop a specific
 * item WITHOUT that item dropping.  When the counter passes the soft-pity
 * threshold, the drop chance is gradually increased.  When it reaches the
 * hard-pity cap the item is guaranteed on the next eligible kill and the
 * counter is reset.
 *
 * Pity counters are lazily loaded from the DB on character login
 * (via SET_PLAYER_PITY event) and persisted back to the game-server via
 * the saveCallback_ function whenever the counter changes significantly.
 *
 * Thread-safety: all public methods are protected by a shared_mutex.
 */
class PityManager
{
  public:
    explicit PityManager(Logger &logger);
    ~PityManager() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * @brief Load pity counters for a character (called on SET_PLAYER_PITY).
     * @param characterId   Owner of the counters.
     * @param entries       Pairs of {itemId, killCount}.
     */
    void loadPityData(int characterId, const std::vector<std::pair<int, int>> &entries);

    // ── Query ─────────────────────────────────────────────────────────────────

    /**
     * @brief Return the current kill count without a drop for this (char, item).
     */
    int getKillCount(int characterId, int itemId) const;

    /**
     * @brief Calculate the extra drop chance that soft pity adds.
     *
     * Returns 0.0f if the counter has not yet reached the soft-pity threshold.
     *
     * @param characterId      Target player.
     * @param itemId           Item being rolled.
     * @param softPityKills    Threshold count from config.
     * @param softBonusPerKill Flat bonus per kill after threshold from config.
     */
    float getExtraDropChance(int characterId, int itemId, int softPityKills, float softBonusPerKill) const;

    /**
     * @brief Return true when the counter has reached or exceeded hardPityKills.
     *        Guarantees a drop on the current roll.
     */
    bool isHardPity(int characterId, int itemId, int hardPityKills) const;

    // ── Mutation (called from LootManager) ────────────────────────────────────

    /**
     * @brief Increment the pity counter after a miss.
     *        Sends worldNotification hint when hintThreshold is crossed.
     *
     * @param characterId       Player who killed the mob.
     * @param itemId            Dropped-item candidate that did NOT drop.
     * @param hintThreshold     kill_count at which to fire the pity_hint notification.
     * @param hintCallback      Called once when hintThreshold is first crossed.
     */
    void incrementCounter(int characterId, int itemId, int hintThreshold, std::function<void()> hintCallback = nullptr);

    /**
     * @brief Reset the pity counter for (characterId, itemId) after a successful drop.
     */
    void resetCounter(int characterId, int itemId);

    // ── Persistence ───────────────────────────────────────────────────────────

    /**
     * @brief Set the callback used to persist a pity counter to the game-server.
     *        The callback receives a newline-terminated JSON string.
     */
    void setSaveCallback(std::function<void(const std::string &)> callback);

  private:
    /// Composite key: avoids heap allocation vs pair
    static int64_t makeKey(int characterId, int itemId) noexcept
    {
        return (static_cast<int64_t>(characterId) << 32) | static_cast<uint32_t>(itemId);
    }

    void persist(int characterId, int itemId, int killCount);

    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<int64_t, int> counters_; // key → kill_count_without_drop

    std::function<void(const std::string &)> saveCallback_;
};
