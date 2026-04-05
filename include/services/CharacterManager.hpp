#pragma once

#include "data/DataStructs.hpp"
#include <iostream>
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

    // Update character mana
    void updateCharacterMana(int characterID, int newMana);

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

    // Add a single active effect. If an effect with the same effectSlug already exists,
    // its expiresAt is refreshed (no stacking). Handles both stat modifiers and HoT/DoT.
    void addActiveEffect(int characterID, const ActiveEffectStruct &effect);

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

    // Remove all expired active effects from a character (expiresAt != 0 && <= now).
    void removeExpiredActiveEffects(int characterID);

    // Get characters in a specific zone
    std::vector<CharacterDataStruct> getCharactersInZone(float centerX, float centerY, float radius);

    // Get character by ID (returns empty struct if not found)
    CharacterDataStruct getCharacterById(int characterID);

    // Calculate distance between two positions
    float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2);

    // Skill system helpers
    void addCharacterSkill(int characterID, const SkillStruct &skill);
    void modifyFreeSkillPoints(int characterID, int delta);
    int getCharacterFreeSkillPoints(int characterID) const;

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    /// HIGH-9 fix: O(1) lookups on every hot-path operation (attack, DoT tick, move).
    /// Previously std::vector with O(N) linear search — 200K iterations/s at 2K players.
    std::unordered_map<int, CharacterDataStruct> charactersMap_;

    // Mutex for charactersMap_
    mutable std::shared_mutex mutex_;
};