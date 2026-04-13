#include "services/CharacterManager.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <spdlog/logger.h>
#include <unordered_map>
#include <vector>

CharacterManager::CharacterManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("character");
    // Initialize properties or perform any setup here
}

void
CharacterManager::loadCharactersList(std::vector<CharacterDataStruct> charactersList)
{
    try
    {
        if (charactersList.empty())
        {
            log_->error("No characters found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : charactersList)
        {
            // HIGH-9: O(1) insert/update
            charactersMap_[row.characterId].characterId = row.characterId;
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading characters: " + std::string(e.what()));
    }
}

void
CharacterManager::loadCharacterAttributes(std::vector<CharacterAttributeStruct> characterAttributes)
{
    try
    {
        if (characterAttributes.empty())
        {
            // Log that the data is empty
            log_->error("No character attributes found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : characterAttributes)
        {
            CharacterAttributeStruct attributeData;
            attributeData.character_id = row.character_id;
            attributeData.id = row.id;
            attributeData.name = row.name;
            attributeData.slug = row.slug;
            attributeData.value = row.value;

            // CRITICAL-4 + HIGH-9 fix: O(1) map lookup instead of OOB vector index
            auto it = charactersMap_.find(attributeData.character_id);
            if (it != charactersMap_.end())
            {
                it->second.attributes.push_back(attributeData);
            }
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading character attributes: " + std::string(e.what()));
    }
}

void
CharacterManager::loadCharacterSkills(std::vector<CharacterSkillStruct> characterSkills)
{
    try
    {
        if (characterSkills.empty())
        {
            log_->error("No character skills found in GS");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);

        // CRITICAL-5 + HIGH-9 fix: O(1) map lookup per skill
        for (auto &[id, character] : charactersMap_)
        {
            character.skills.clear();
        }

        int assignedCount = 0;
        for (const auto &skill : characterSkills)
        {
            auto it = charactersMap_.find(skill.characterId);
            if (it != charactersMap_.end())
            {
                it->second.skills.push_back(skill.skill);
                ++assignedCount;
            }
        }

        logger_.log("Loaded " + std::to_string(characterSkills.size()) +
                    " skills, assigned " + std::to_string(assignedCount) + " to characters");
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading character skills: " + std::string(e.what()));
    }
}

void
CharacterManager::loadCharacterData(CharacterDataStruct characterData)
{
    try
    {
        if (characterData.characterId == 0)
        {
            log_->error("No character data found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        // HIGH-9: O(1) map lookup + update
        auto it = charactersMap_.find(characterData.characterId);
        if (it != charactersMap_.end())
        {
            // Preserve chunk-server-only movement validation state.
            // loadCharacterData is called from ExperienceManager and CombatSystem with a
            // snapshot taken earlier; any movement accepted between that snapshot and now
            // would be lost in a full overwrite, causing the next speed-check to compare
            // the new position against a stale lastValidatedPosition.
            characterData.lastValidatedPosition = it->second.lastValidatedPosition;
            characterData.lastMoveSrvMs = it->second.lastMoveSrvMs;
            it->second = characterData;
        }
        else
        {
            // Character not yet in map (race during login) — insert it
            charactersMap_[characterData.characterId] = characterData;
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading character data: " + std::string(e.what()));
    }
}

void
CharacterManager::addCharacter(const CharacterDataStruct &characterData)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (charactersMap_.count(characterData.characterId))
    {
        // Character already cached (e.g. from a previous session that did not cleanly
        // disconnect).  Overwrite with the fresh data received from the Game Server so
        // that position and stats always reflect the current DB state on login.
        log_->warn("Character with ID " + std::to_string(characterData.characterId) +
                   " already exists — overwriting with fresh Game Server data.");
    }

    charactersMap_[characterData.characterId] = characterData;
    log_->info("Character with ID " + std::to_string(characterData.characterId) + " added/updated.");
}

// remove character by ID
void
CharacterManager::removeCharacter(int characterID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        charactersMap_.erase(it);
        log_->info("Character with ID " + std::to_string(characterID) + " removed.");
    }
    else
    {
        log_->error("Character with ID " + std::to_string(characterID) + " not found. Cannot remove.");
    }
}

std::vector<CharacterDataStruct>
CharacterManager::getCharactersList()
{
    // HIGH-9: collect map values into vector for API compatibility
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<CharacterDataStruct> result;
    result.reserve(charactersMap_.size());
    for (const auto &[id, ch] : charactersMap_)
    {
        result.push_back(ch);
    }
    return result;
}

CharacterDataStruct
CharacterManager::getCharacterData(int characterID)
{
    // HIGH-9: O(1) lookup
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        return it->second;
    return CharacterDataStruct();
}

std::vector<CharacterAttributeStruct>
CharacterManager::getCharacterAttributes(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        return it->second.attributes;
    return {};
}

std::vector<SkillStruct>
CharacterManager::getCharacterSkills(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        return it->second.skills;
    return {};
}

PositionStruct
CharacterManager::getCharacterPosition(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        return it->second.characterPosition;
    return PositionStruct();
}

void
CharacterManager::setCharacterPosition(int characterID, PositionStruct position)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        it->second.characterPosition = position;
}

void
CharacterManager::setLastValidatedMovement(int characterID, PositionStruct position, int64_t srvMs)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        it->second.lastValidatedPosition = position;
        it->second.lastMoveSrvMs = srvMs;
    }
}

void
CharacterManager::updateCharacterHealth(int characterID, int newHealth)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        it->second.characterCurrentHealth = newHealth;
        logger_.log("Updated character " + std::to_string(characterID) + " health to " + std::to_string(newHealth));
        return;
    }
    log_->error("Character " + std::to_string(characterID) + " not found when updating health");
}

CharacterManager::HealthUpdateResult
CharacterManager::applyDamageToCharacter(int characterID, int damageAmount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        auto &ch = it->second;
        bool wasAlive = (ch.characterCurrentHealth > 0);
        int newHp = std::max(0, ch.characterCurrentHealth - damageAmount);
        ch.characterCurrentHealth = newHp;
        return {newHp, ch.characterCurrentMana, wasAlive && (newHp <= 0)};
    }
    log_->error("Character " + std::to_string(characterID) + " not found in applyDamageToCharacter");
    return {0, 0, false};
}

CharacterManager::HealthUpdateResult
CharacterManager::applyHealToCharacter(int characterID, int healAmount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        auto &ch = it->second;
        int newHp = std::min(ch.characterMaxHealth, ch.characterCurrentHealth + healAmount);
        ch.characterCurrentHealth = newHp;
        return {newHp, ch.characterCurrentMana, false};
    }
    log_->error("Character " + std::to_string(characterID) + " not found in applyHealToCharacter");
    return {0, 0, false};
}

int
CharacterManager::applyManaCostToCharacter(int characterID, int costAmount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        int newMana = std::max(0, it->second.characterCurrentMana - costAmount);
        it->second.characterCurrentMana = newMana;
        return newMana;
    }
    log_->error("Character " + std::to_string(characterID) + " not found in applyManaCostToCharacter");
    return 0;
}

bool
CharacterManager::trySpendMana(int characterID, int amount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        if (it->second.characterCurrentMana < amount)
            return false;
        it->second.characterCurrentMana -= amount;
        return true;
    }
    return false;
}

void
CharacterManager::updateCharacterMana(int characterID, int newMana)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        it->second.characterCurrentMana = newMana;
        logger_.log("Updated character " + std::to_string(characterID) + " mana to " + std::to_string(newMana));
        return;
    }
    log_->error("Character " + std::to_string(characterID) + " not found when updating mana");
}

std::vector<CharacterDataStruct>
CharacterManager::getCharactersInZone(float centerX, float centerY, float radius)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<CharacterDataStruct> charactersInZone;

    for (const auto &[id, character] : charactersMap_)
    {
        float distance = calculateDistance(
            {centerX, centerY, 0.0f, 0.0f},
            character.characterPosition);

        if (distance <= radius && character.characterCurrentHealth > 0)
        {
            charactersInZone.push_back(character);
        }
    }

    return charactersInZone;
}

CharacterDataStruct
CharacterManager::getCharacterById(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        return it->second;
    return CharacterDataStruct{};
}

float
CharacterManager::calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2)
{
    float dx = pos1.positionX - pos2.positionX;
    float dy = pos1.positionY - pos2.positionY;
    return std::sqrt(dx * dx + dy * dy);
}

void
CharacterManager::setCharacterActiveEffects(int characterID, std::vector<ActiveEffectStruct> effects)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        it->second.activeEffects = std::move(effects);
        logger_.log("[CharacterManager] Set " +
                    std::to_string(it->second.activeEffects.size()) +
                    " active effects for character " + std::to_string(characterID));
        return;
    }
    log_->error("[CharacterManager] setCharacterActiveEffects: character " +
                std::to_string(characterID) + " not found");
}

void
CharacterManager::addActiveEffect(int characterID, const ActiveEffectStruct &effect)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
    {
        log_->error("[CharacterManager] addActiveEffect: character " + std::to_string(characterID) + " not found");
        return;
    }

    auto &effects = it->second.activeEffects;
    // Refresh if an effect with the same slug already exists (no stacking)
    auto existing = std::find_if(effects.begin(), effects.end(), [&effect](const ActiveEffectStruct &e)
        { return e.effectSlug == effect.effectSlug; });
    if (existing != effects.end())
    {
        existing->expiresAt = effect.expiresAt;
        existing->value = effect.value;
        existing->nextTickAt = effect.nextTickAt;
        log_->info("[CharacterManager] Refreshed effect '" + effect.effectSlug + "' for character " + std::to_string(characterID));
    }
    else
    {
        effects.push_back(effect);
        log_->info("[CharacterManager] Added effect '" + effect.effectSlug + "' for character " + std::to_string(characterID));
    }
}

void
CharacterManager::removeActiveEffectBySlug(int characterID, const std::string &effectSlug)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
    {
        log_->error("[CharacterManager] removeActiveEffectBySlug: character " + std::to_string(characterID) + " not found");
        return;
    }
    auto &effects = it->second.activeEffects;
    auto before = effects.size();
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [&effectSlug](const ActiveEffectStruct &e)
            { return e.effectSlug == effectSlug; }),
        effects.end());
    if (effects.size() < before)
        log_->info("[CharacterManager] Removed effect '{}' from character {}", effectSlug, characterID);
}

int
CharacterManager::restoreManaToCharacter(int characterID, int amount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        int newMana = std::min(it->second.characterMaxMana,
            it->second.characterCurrentMana + amount);
        it->second.characterCurrentMana = newMana;
        return newMana;
    }
    log_->error("[CharacterManager] restoreManaToCharacter: character " + std::to_string(characterID) + " not found");
    return 0;
}

void
CharacterManager::markCharacterInCombat(int characterID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
        it->second.lastInCombatAt = std::chrono::steady_clock::now();
}

void
CharacterManager::setCharacterFlags(int characterID, std::vector<PlayerFlagStruct> flags)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        it->second.flags = std::move(flags);
        logger_.log("[CharacterManager] Set " +
                    std::to_string(it->second.flags.size()) +
                    " flags for character " + std::to_string(characterID));
        return;
    }
    log_->error("[CharacterManager] setCharacterFlags: character " +
                std::to_string(characterID) + " not found");
}
void
CharacterManager::replaceCharacterAttributes(int characterID, std::vector<CharacterAttributeStruct> attributes)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it != charactersMap_.end())
    {
        it->second.attributes = std::move(attributes);
        logger_.log("[CharacterManager] Replaced " +
                    std::to_string(it->second.attributes.size()) +
                    " attributes for character " + std::to_string(characterID));
        return;
    }
    log_->error("[CharacterManager] replaceCharacterAttributes: character " +
                std::to_string(characterID) + " not found");
}

std::pair<std::vector<EffectTickResult>, std::unordered_set<int>>
CharacterManager::processEffectTicks()
{
    std::vector<EffectTickResult> results;
    std::unordered_set<int> expiredCharacters;
    const auto now = std::chrono::steady_clock::now();
    const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                               .count();

    // Intermediate representation for phase-2 HP application.
    struct PendingTick
    {
        int characterId;
        std::string effectSlug;
        std::string effectTypeSlug;
        float value; // abs amount per tick
    };
    std::vector<PendingTick> pending;

    // ── Phase 1: wide unique_lock ─────────────────────────────────────────────
    // Advance nextTickAt, remove expired effects, collect pending HP-change work.
    // No external calls are made here — the lock window is kept to pure in-memory
    // iteration so concurrent shared_lock readers (e.g. getCharactersList) are
    // blocked for the shortest possible time.
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto &[id, character] : charactersMap_)
        {
            for (auto &eff : character.activeEffects)
            {
                if (eff.tickMs <= 0)
                    continue; // not a tick effect
                if (eff.effectTypeSlug != "dot" && eff.effectTypeSlug != "hot")
                    continue;
                if (character.characterCurrentHealth <= 0)
                    continue; // already dead — skip ticks
                if (now < eff.nextTickAt)
                    continue; // not yet time

                // Advance nextTickAt by one interval (non-accumulating drift)
                eff.nextTickAt += std::chrono::milliseconds(eff.tickMs);
                if (eff.nextTickAt < now)
                    eff.nextTickAt = now + std::chrono::milliseconds(eff.tickMs);

                pending.push_back({character.characterId, eff.effectSlug, eff.effectTypeSlug, std::abs(eff.value)});
            }

            // Remove expired effects
            const std::size_t countBefore = character.activeEffects.size();
            character.activeEffects.erase(
                std::remove_if(character.activeEffects.begin(), character.activeEffects.end(), [&](const ActiveEffectStruct &e)
                    { return e.expiresAt != 0 && e.expiresAt <= nowSec; }),
                character.activeEffects.end());
            if (character.activeEffects.size() < countBefore)
                expiredCharacters.insert(character.characterId);
        }
    } // unique_lock released here

    // ── Phase 2: per-call narrow locks ───────────────────────────────────────
    // Apply HP deltas one character at a time. Each call acquires its own
    // unique_lock internally, allowing other threads to interleave between ticks.
    for (const auto &pt : pending)
    {
        EffectTickResult tick;
        tick.characterId = pt.characterId;
        tick.effectSlug = pt.effectSlug;
        tick.effectTypeSlug = pt.effectTypeSlug;
        tick.value = pt.value;

        if (pt.effectTypeSlug == "dot")
        {
            auto hr = applyDamageToCharacter(pt.characterId, static_cast<int>(pt.value));
            tick.newHealth = hr.newHealth;
            tick.newMana = hr.currentMana;
            tick.targetDied = hr.died;
        }
        else // hot
        {
            auto hr = applyHealToCharacter(pt.characterId, static_cast<int>(pt.value));
            tick.newHealth = hr.newHealth;
            tick.newMana = hr.currentMana;
            tick.targetDied = false;
        }
        results.push_back(tick);
    }

    return {results, expiredCharacters};
}

void
CharacterManager::removeExpiredActiveEffects(int characterID)
{
    const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                               .count();

    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
        return;
    auto &effects = it->second.activeEffects;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [&](const ActiveEffectStruct &eff)
            { return eff.expiresAt != 0 && eff.expiresAt <= nowSec; }),
        effects.end());
}

// ── Skill system helpers ──────────────────────────────────────────────────

void
CharacterManager::addCharacterSkill(int characterID, const SkillStruct &skill)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
        return;
    // Avoid duplicates (same slug) — replace if already present
    auto &skills = it->second.skills;
    for (auto &s : skills)
    {
        if (s.skillSlug == skill.skillSlug)
        {
            s = skill;
            return;
        }
    }
    skills.push_back(skill);
}

void
CharacterManager::modifyFreeSkillPoints(int characterID, int delta)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
        return;
    int updated = it->second.freeSkillPoints + delta;
    if (updated < 0)
        updated = 0;
    it->second.freeSkillPoints = updated;
}

int
CharacterManager::getCharacterFreeSkillPoints(int characterID) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
        return 0;
    return it->second.freeSkillPoints;
}

void
CharacterManager::updateSkillBarSlot(int characterID, int slotIndex, const std::string &skillSlug)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = charactersMap_.find(characterID);
    if (it == charactersMap_.end())
        return;
    auto &slots = it->second.skillBarSlots;
    if (skillSlug.empty())
    {
        // Clear the slot: remove the entry if present
        slots.erase(
            std::remove_if(slots.begin(), slots.end(), [slotIndex](const SkillBarSlotStruct &s)
                { return s.slotIndex == slotIndex; }),
            slots.end());
    }
    else
    {
        // Assign: replace existing entry or append new one
        for (auto &s : slots)
        {
            if (s.slotIndex == slotIndex)
            {
                s.skillSlug = skillSlug;
                return;
            }
        }
        slots.push_back({slotIndex, skillSlug});
    }
}
