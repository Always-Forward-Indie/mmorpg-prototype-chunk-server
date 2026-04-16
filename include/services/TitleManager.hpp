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
 * @brief TitleManager — player title collection and equip/unequip logic.
 *
 * Titles are cosmetic + mechanical rewards unlocked via quests, reputation,
 * mastery milestones, or bestiary goals. A player may have any number of
 * earned titles but can display/equip **only one** at a time.
 *
 * Equipping a title applies its bonuses as permanent ActiveEffects with
 * sourceType = "title". Unequipping removes those effects and triggers a
 * stats recalculation.
 *
 * Title definitions are loaded from the game-server (SET_TITLE_DEFINITIONS).
 * Per-character earned titles + equipped slug are loaded on login
 * (SET_PLAYER_TITLES) and persisted via saveCallback_ on every change.
 *
 * Thread-safety: all public methods are protected by a shared_mutex.
 */
class TitleManager
{
  public:
    explicit TitleManager(GameServices *gs);
    ~TitleManager() = default;

    // ── Static data ───────────────────────────────────────────────────────────
    void loadTitleDefinitions(const std::vector<TitleDefinitionStruct> &definitions);

    /// Returns a definition by slug; returns empty struct (id==0) if not found.
    TitleDefinitionStruct getTitleDefinition(const std::string &slug) const;

    /// Returns all known title definitions (for client display).
    std::vector<TitleDefinitionStruct> getAllDefinitions() const;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void loadPlayerTitles(int characterId, const PlayerTitleStateStruct &state);
    void unloadPlayerTitles(int characterId);

    // ── Query ─────────────────────────────────────────────────────────────────
    PlayerTitleStateStruct getPlayerTitles(int characterId) const;
    bool hasTitle(int characterId, const std::string &titleSlug) const;

    // ── Mutation ──────────────────────────────────────────────────────────────
    /**
     * @brief Grant a new title to a character.
     *        No-op if already earned. Persists and notifies client.
     */
    void grantTitle(int characterId, const std::string &titleSlug);

    /**
     * @brief Check all title definitions with the given earnCondition and grant
     *        any whose conditionParams are satisfied.
     * @param characterId   Target character.
     * @param condition     e.g. "bestiary", "mastery", "reputation", "level", "quest"
     * @param eventData     Runtime values to match against conditionParams.
     *                      bestiary:   {"mobSlug":…, "tier":N}
     *                      mastery:    {"masterySlug":…, "tier":N}
     *                      reputation: {"factionSlug":…, "tierName":…}
     *                      level:      {"level":N}
     *                      quest:      {"questSlug":…}
     */
    void checkAndGrantTitles(int characterId,
        const std::string &condition,
        const nlohmann::json &eventData);

    /**
     * @brief Equip a title (or unequip by passing an empty slug).
     *        Removes old title bonuses, applies new ones as ActiveEffects.
     *        Triggers stats_update via StatsNotificationService.
     *        Persists equipped slug via saveCallback_.
     * @return false if the slug is non-empty but not in the earned set.
     */
    bool equipTitle(int characterId, const std::string &titleSlug);

    /**
     * @brief Re-apply bonuses for the currently equipped title.
     *        Call after setCharacterActiveEffects() to ensure title bonuses survive the replace.
     */
    void reapplyEquippedBonuses(int characterId);

    // ── Persistence ───────────────────────────────────────────────────────────
    /// JSON string ("eventType":"savePlayerTitle") sent to Game Server.
    using SaveCallback = std::function<void(const std::string &)>;
    void setSaveCallback(SaveCallback cb)
    {
        saveCallback_ = std::move(cb);
    }

    /// Called after equipTitle succeeds — sends current title state to client.
    using NotifyClientCallback = std::function<void(int /*characterId*/, const nlohmann::json & /*packet*/)>;
    void setNotifyClientCallback(NotifyClientCallback cb)
    {
        notifyClientCallback_ = std::move(cb);
    }

    /// Push the current title state (earned list + equipped) to the owning client.
    /// Public so that PlayerReady phase can re-send after the scene is loaded.
    void sendTitleUpdateToClient(int characterId);

  private:
    void applyTitleBonuses(int characterId, const std::string &titleSlug);
    void removeTitleBonuses(int characterId, const std::string &titleSlug);
    void persist(int characterId, const std::string &equippedSlug, const std::vector<std::string> &earned);

    GameServices *gs_;
    std::shared_ptr<spdlog::logger> log_;

    // Static: slug → definition
    std::unordered_map<std::string, TitleDefinitionStruct> definitions_;

    // Per-character state
    std::unordered_map<int, PlayerTitleStateStruct> data_;

    mutable std::shared_mutex mutex_;

    SaveCallback saveCallback_;
    NotifyClientCallback notifyClientCallback_;
};
