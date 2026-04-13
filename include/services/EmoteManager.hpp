#pragma once

#include "data/DataStructs.hpp"
#include <algorithm>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class GameServices;

/**
 * @brief Manages the emote/animation system.
 *
 * Emote definitions are loaded once from the game-server (SET_EMOTE_DEFINITIONS)
 * and stored as a global catalog.  Per-character unlocks arrive on every login
 * (SET_PLAYER_EMOTES) and are held in memory for the session lifetime.
 *
 * Thread-safety: std::shared_mutex — multiple concurrent readers, single writer.
 */
class EmoteManager
{
  public:
    explicit EmoteManager(GameServices *gs);

    // ── Static definitions ─────────────────────────────────────────────────
    void loadEmoteDefinitions(const std::vector<EmoteDefinitionStruct> &defs);
    EmoteDefinitionStruct getEmoteDefinition(const std::string &slug) const;
    /// Returns all definitions sorted by sortOrder ascending.
    std::vector<EmoteDefinitionStruct> getAllDefinitions() const;

    // ── Per-character lifecycle ────────────────────────────────────────────
    /// Called on joinGameCharacter; replaces any previous state for this character.
    void loadPlayerEmotes(int characterId, const std::vector<std::string> &unlockedSlugs);
    /// Called on client disconnect.
    void unloadPlayerEmotes(int characterId);

    // ── Queries ────────────────────────────────────────────────────────────
    /// Returns TRUE if the character has this emote unlocked (or it is a default emote).
    bool isUnlocked(int characterId, const std::string &emoteSlug) const;
    /// Returns full EmoteDefinitionStruct list for all emotes unlocked by the character,
    /// sorted by sortOrder ascending.
    std::vector<EmoteDefinitionStruct> getPlayerEmotes(int characterId) const;

  private:
    GameServices *gs_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, EmoteDefinitionStruct> definitions_; ///< slug → def
    std::unordered_map<int, std::vector<std::string>> playerEmotes_;     ///< characterId → unlocked slugs
};
