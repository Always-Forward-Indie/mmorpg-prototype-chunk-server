#pragma once
// Targeting DTOs shared with the event layer (A5 extract).
//
// These four declarations are the only survivors of data/AttackSystem.hpp:
// the AttackSystem class itself was never instantiated (no production or
// test call sites; only EventData.hpp included the header for these types)
// and was deleted with its .cpp. The enums/structs below back
// AttackRequestStruct / AttackSequenceStruct / TargetSelectionStruct in
// events/EventData.hpp. Copied verbatim — no semantic change.
#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"

#include <string>
#include <vector>

/**
 * @brief Attack priority levels for target selection
 */
enum class AttackPriority : int
{
    LOW = 1,      // Least threatening targets
    NORMAL = 2,   // Standard priority
    HIGH = 3,     // High value targets (healers, mages)
    CRITICAL = 4, // Must be eliminated first (low health, dangerous)
    DEFEND = 5    // Protect specific targets
};

/**
 * @brief Target selection strategies
 */
enum class TargetSelectionStrategy : int
{
    NEAREST = 1,           // Attack nearest enemy
    WEAKEST = 2,           // Attack lowest health enemy
    STRONGEST = 3,         // Attack highest health enemy
    MOST_DANGEROUS = 4,    // Attack highest threat enemy
    SUPPORT_FIRST = 5,     // Prioritize healers/support
    RANDOM = 6,            // Random target selection
    PLAYER_PREFERENCE = 7, // Use player's targeting preferences
    AI_TACTICAL = 8        // Advanced AI decision making
};

/**
 * @brief Combat role definitions
 */
enum class CombatRole : int
{
    TANK = 1,         // Absorb damage, protect allies
    DPS = 2,          // Deal damage
    HEALER = 3,       // Heal and support allies
    SUPPORT = 4,      // Buffs, debuffs, utility
    HYBRID = 5,       // Multiple roles
    CROWD_CONTROL = 6 // Disable enemies
};

/**
 * @brief Represents a potential target with evaluation metrics
 */
struct TargetCandidate
{
    int targetId;
    PositionStruct position;
    CharacterDataStruct *data;

    float distance;
    float healthPercent;
    float threatLevel;
    CombatRole role;
    AttackPriority priority;

    float totalScore;          // Calculated targeting score
    bool isValidTarget;        // Whether this is a valid target
    std::string invalidReason; // Why target is invalid (if applicable)
};
