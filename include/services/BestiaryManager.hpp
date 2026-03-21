#pragma once

#include "data/DataStructs.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utils/Logger.hpp>
#include <vector>

namespace spdlog
{
class logger;
}

/**
 * @brief BestiaryManager — per-player, per-mob kill counter for gradual
 *        information reveal in the Bestiary system.
 *
 * Kill counts are loaded from the DB on character login (SET_PLAYER_BESTIARY)
 * and persisted back to the game-server via saveCallback_ on each kill.
 *
 * Reveal tiers are calculated against thresholds supplied via setThresholds()
 * (called when game config arrives from the game-server).
 *
 * Thread-safety: all public methods are protected by a shared_mutex.
 */
class BestiaryManager
{
  public:
    explicit BestiaryManager(Logger &logger);
    ~BestiaryManager() = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * @brief Load bestiary kill counts for a character (called on SET_PLAYER_BESTIARY).
     */
    void loadBestiaryData(int characterId, const std::vector<std::pair<int, int>> &entries);

    // ── Query ─────────────────────────────────────────────────────────────────

    /**
     * @brief Return kill count for (characterId, mobTemplateId).
     */
    int getKillCount(int characterId, int mobTemplateId) const;

    /**
     * @brief Return all mob template IDs with at least 1 kill for a character.
     *        Used to send the bestiary overview list to the client on request.
     */
    std::vector<std::pair<int, int>> getKnownMobs(int characterId) const;

    /**
     * @brief Compute which tiers are revealed given the current kill count.
     */
    std::vector<int> getRevealedTiers(int characterId, int mobTemplateId, const std::vector<int> &thresholds) const;

    /**
     * @brief Build the full JSON bestiary entry (new tiers format) to send to a client.
     *
     * Returns all tiers — unlocked ones with full 'data', locked ones with
     * metadata only (no data leakage).
     *
     * Tier layout (thresholds from config, defaults 1/10/25/50/100/300):
     *   T1 basic_info    — level, rank, HP range, type, biome
     *   T2 lore          — loreKey (client resolves from locale)
     *   T3 combat_info   — weaknesses, resistances, abilities (skill slugs)
     *   T4 loot_table    — all item slugs (no chances — what to hunt for)
     *   T5 drop_rates    — all items with exact drop chances
     *   T6 hunter_mastery— titleSlug, achievementSlug milestone
     *
     * @param characterId      Target player.
     * @param mobTemplateId    Mob template ID.
     * @param mobSlug          Mob slug (localisation key on the client).
     * @param mobStatic        Static mob data for basic_info tier.
     * @param weaknesses       Element slugs the mob is weak against.
     * @param resistances      Element slugs the mob resists.
     * @param abilities        Skill slugs the mob can use (for combat_info tier).
     * @param allLootForMob    All loot rows for this mob template.
     * @param itemSlugFn       Resolver: itemId → itemSlug.
     */
    nlohmann::json buildEntryJson(
        int characterId,
        int mobTemplateId,
        const std::string &mobSlug,
        const MobDataStruct &mobStatic,
        const std::vector<std::string> &weaknesses,
        const std::vector<std::string> &resistances,
        const std::vector<std::string> &abilities,
        const std::vector<MobLootInfoStruct> &allLootForMob,
        const std::function<std::string(int)> &itemSlugFn) const;

    // ── Mutation ──────────────────────────────────────────────────────────────

    /**
     * @brief Increment kill count for (characterId, mobTemplateId) and persist.
     *        Fires notifyCallback_ for each newly crossed threshold tier.
     */
    void recordKill(int characterId, int mobTemplateId);

    // ── Configuration ─────────────────────────────────────────────────────────

    /**
     * @brief Set tier kill thresholds from game config.
     *        Expected order: [tier1, tier2, tier3, tier4, tier5, tier6].
     *        Protocol defaults: [1, 10, 25, 50, 100, 300].
     */
    void setThresholds(std::vector<int> thresholds);

    // ── Persistence ───────────────────────────────────────────────────────────

    /**
     * @brief Set the callback used to persist a bestiary entry to the game-server.
     */
    void setSaveCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Set the callback invoked when a new bestiary tier is unlocked.
     *        Signature: (characterId, mobTemplateId, tierNum 1-based, newKillCount, categorySlug)
     */
    void setNotifyCallback(std::function<void(int, int, int, int, const std::string &)> callback);

    /**
     * @brief Set the callback invoked on every kill (including non-tier-crossing ones).
     *        Used to push the updated kill count to the client so the overview list
     *        stays in sync without requiring a full re-request.
     *        Signature: (characterId, mobTemplateId, newKillCount)
     */
    void setKillUpdateCallback(std::function<void(int, int, int)> callback);

  private:
    void persist(int characterId, int mobTemplateId, int killCount);

    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    /// characterId → { mobTemplateId → killCount }
    std::unordered_map<int, std::unordered_map<int, int>> data_;

    std::vector<int> thresholds_;

    std::function<void(const std::string &)> saveCallback_;
    std::function<void(int, int, int, int, const std::string &)> notifyCallback_;
    std::function<void(int, int, int)> killUpdateCallback_;

    /// Category slug for each tier index (0-based), matching thresholds_ positions.
    static const std::vector<std::string> kCategorySlugs_;
};
