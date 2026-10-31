#pragma once

#include "data/DataStructs.hpp"
#include <deque>
#include <functional>
#include <iostream>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <utils/Logger.hpp>
#include <vector>

class CharacterManager
{
  public:
    // Constructor
    CharacterManager(Logger &logger);

    // add character to the list
    void addCharacter(const CharacterDataStruct &characterData);

    // Remove character by ID
    void removeCharacter(int characterID);

    // Load characters list
    void loadCharactersList(std::vector<CharacterDataStruct> charactersList);

    // Load character data
    void loadCharacterData(CharacterDataStruct characterData);

    // Load character Attributes
    void loadCharacterAttributes(std::vector<CharacterAttributeStruct> characterAttributes);

    // Load character Skills
    void loadCharacterSkills(std::vector<CharacterSkillStruct> characterSkills);

    // set character position
    void setCharacterPosition(int characterID, PositionStruct position);
    void setLastValidatedMovement(int characterID, PositionStruct position, int64_t srvMs);
    // Single teleport commit: authoritative memory + validation reset +
    // window reset + stale-move grace mark (one call, no split state).
    void commitTeleportPosition(int characterID, PositionStruct position);
    // Clear the grace mark on normal accept (post-teleport truth resumes).
    void clearTeleportCommit(int characterID);

    // Get characters list
    std::vector<CharacterDataStruct> getCharactersList();

    // Get basic character data by character ID
    CharacterDataStruct getCharacterData(int characterID);

    // Get character attributes by character ID
    std::vector<CharacterAttributeStruct> getCharacterAttributes(int characterID);

    // Get character skills by character ID
    std::vector<SkillStruct> getCharacterSkills(int characterID);

    // Get character position by character ID
    PositionStruct getCharacterPosition(int characterID);

    // Update character health (raw setter — prefer applyDamage/applyHeal for combat)
    void updateCharacterHealth(int characterID, int newHealth);

    // Atomic read-modify-write primitive: runs fn(struct&) under the single
    // character-map lock. Use for multi-field mutations (XP grants, restores)
    // that copy-modify-write would race (AoE multi-kill, damage interleaved
    // with level-up). RULES for fn: pure struct surgery only — never call
    // other managers or CharacterManager methods (lock-order: character map
    // is innermost; ExperienceCache reads are allowed, it never calls back).
    // Returns false when the character is absent (fn not run).
    bool applyUnderLock(int characterID, const std::function<void(CharacterDataStruct &)> &fn);

    // Update character mana
    void updateCharacterMana(int characterID, int newMana);

    // Set post-respawn invulnerability end timestamp (game time, seconds)
    void setRespawnInvulUntil(int characterID, float timestamp);

    void clearDeathState(int characterID);

    // Atomically apply damage: reads HP, subtracts delta, clamps to [0, maxHp], writes back.
    // Returns {newHealth, currentMana, died} where died=true means health crossed 0 this call.
    struct HealthUpdateResult
    {
        int newHealth;
        int currentMana;
        bool died;
    };
    HealthUpdateResult applyDamageToCharacter(int characterID, int damageAmount);

    // Atomically apply healing: reads HP, adds delta, clamps to maxHp, writes back.
    HealthUpdateResult applyHealToCharacter(int characterID, int healAmount);

    // Atomically deduct mana cost: reads mana, subtracts cost, clamps to 0, writes back. Returns new mana.
    int applyManaCostToCharacter(int characterID, int costAmount);

    // Atomically check-and-deduct mana. Returns true and deducts if current >= amount; false otherwise (no side effect).
    bool trySpendMana(int characterID, int amount);

    // Store active effects received from game-server on login
    void setCharacterActiveEffects(int characterID, std::vector<ActiveEffectStruct> effects);

    // Add a single active effect. Buffs/debuffs refresh by slug (no stacking);
    // dot/hot stack up to kMaxDotHotStacks same-slug instances, each ticking
    // separately; at cap the oldest is refreshed. Handles both stat modifiers
    // and HoT/DoT.
    static constexpr int kMaxDotHotStacks = 3;
    void addActiveEffect(int characterID, const ActiveEffectStruct &effect);
    void removeActiveEffectBySlug(int characterID, const std::string &effectSlug);

    // Atomically restore mana: reads mana, adds amount, clamps to maxMana, writes back. Returns new mana.
    int restoreManaToCharacter(int characterID, int amount);

    // Mark a character as recently in combat (called when they take or deal damage).
    // Suppresses HP/MP regeneration for configurable duration after the last hit.
    void markCharacterInCombat(int characterID);

    // Store player flags received from game-server on login (dialogue/quest conditions)
    void setCharacterFlags(int characterID, std::vector<PlayerFlagStruct> flags);

    // Replace attributes in-place (used for refresh after level-up / equip change)
    void replaceCharacterAttributes(int characterID, std::vector<CharacterAttributeStruct> attributes);

    // Tick all active DoT/HoT effects for every loaded character.
    // Applies health changes in-place and removes effects that have expired.
    // Returns tick events to broadcast, and the set of character IDs whose
    // expired effects were removed (so callers can send stats_update).
    std::pair<std::vector<EffectTickResult>, std::unordered_set<int>> processEffectTicks();

    // Crowd control: true when a live stun/silence/root is present (unix sec).
    // Used by skill initiation (stun/silence block casts) and movement
    // (stun/root block moves). Mob side mirrors via MobInstanceManager.
    bool hasLiveCC(int characterID, const std::string &ccType, int64_t nowSec) const;
    // DR-gated CC application: returns effective duration sec, or -1 when
    // immune (4th same-type CC inside immuneWindowSec; history pruned).
    // On success records the application AND appends the effect with the
    // scaled expiry (caller passes base duration + attribute; value unused).
    float applyCrowdControl(int characterID, const ActiveEffectStruct &effect,
        float baseDurationSec, int64_t nowSec, float immuneWindowSec = 15.0f);

    // Remove all expired active effects from a character (expiresAt != 0 && <= now).
    void removeExpiredActiveEffects(int characterID);

    // Get characters in a specific zone
    std::vector<CharacterDataStruct> getCharactersInZone(float centerX, float centerY, float radius);

    // Get character by ID (returns empty struct if not found)
    CharacterDataStruct getCharacterById(int characterID);

    // Rewound position lookup for lag-compensated range validation.
    // Returns the newest sample at-or-before tsMs; clamps to the oldest
    // sample when tsMs predates the trail; nullopt when no trail exists.
    std::optional<PositionStruct> getCharacterPositionAt(int characterID, int64_t tsMs) const;

    // NOTE: planar distance now lives in utils/DistanceUtils (dist2D) — the
    // per-class copy was removed (Wave 2.1).

    // Skill system helpers
    void addCharacterSkill(int characterID, const SkillStruct &skill);
    void modifyFreeSkillPoints(int characterID, int delta);
    int getCharacterFreeSkillPoints(int characterID) const;
    /// Assign or clear a hotbar slot for a character (in-memory only; persist via game server)
    void updateSkillBarSlot(int characterID, int slotIndex, const std::string &skillSlug);

    void addExperienceDebt(int characterID, int debt);

    void updateLastPlayTimeSaveAt(int characterID, std::chrono::steady_clock::time_point t);

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    /// HIGH-9 fix: O(1) lookups on every hot-path operation (attack, DoT tick, move).
    /// Previously std::vector with O(N) linear search — 200K iterations/s at 2K players.
    std::unordered_map<int, CharacterDataStruct> charactersMap_;

    // Mutex for charactersMap_
    mutable std::shared_mutex mutex_;

    // Lag-compensation trail: charId -> ring of (wall-ms, position),
    // newest last, capped at kPosHistoryDepth. Recorded on every accepted
    // move (setCharacterPosition); cleared on teleport commit (pre-teleport
    // samples would rewind the validator into the old location).
    // Same mutex_.
    static constexpr size_t kPosHistoryDepth = 20;
    std::unordered_map<int, std::deque<std::pair<int64_t, PositionStruct>>> posHistory_;

    // CC diminishing-returns history: charId → ccType → application times
    // (unix sec). Same mutex_; pruned on every application.
    std::unordered_map<int, std::unordered_map<std::string, std::vector<int64_t>>> ccHistory_;
};