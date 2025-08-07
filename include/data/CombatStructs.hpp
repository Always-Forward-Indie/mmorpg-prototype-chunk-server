#pragma once

#include "data/DataStructs.hpp"
#include <chrono>
#include <string>

/**
 * @brief Types of combat actions
 */
enum class CombatActionType : int
{
    BASIC_ATTACK = 1, // Regular weapon attack
    SPELL = 2,        // Magic spell with cast time
    SKILL = 3,        // Class ability
    CHANNELED = 4,    // Channeled ability (can be interrupted)
    INSTANT = 5,      // Instant ability
    AOE_ATTACK = 6,   // Area of effect attack
    BUFF = 7,         // Self or ally buff
    DEBUFF = 8        // Enemy debuff
};

/**
 * @brief Target types for combat actions
 */
enum class CombatTargetType : int
{
    SELF = 1,   // Self-target (buffs, heals)
    PLAYER = 2, // Target another player
    MOB = 3,    // Target a mob/NPC
    AREA = 4,   // Area target (ground-targeted)
    NONE = 5    // No target required
};

/**
 * @brief Combat action states
 */
enum class CombatActionState : int
{
    INITIATED = 1,   // Action started, checking requirements
    CASTING = 2,     // Currently casting/channeling
    EXECUTING = 3,   // Applying effects/damage
    COMPLETED = 4,   // Action finished successfully
    INTERRUPTED = 5, // Action was interrupted
    FAILED = 6       // Action failed (no mana, invalid target, etc.)
};

/**
 * @brief Interruption reasons
 */
enum class InterruptionReason : int
{
    PLAYER_CANCELLED = 1,  // Player manually cancelled
    MOVEMENT = 2,          // Player moved during cast
    DAMAGE_TAKEN = 3,      // Took damage that interrupts
    TARGET_LOST = 4,       // Target moved out of range/died
    RESOURCE_DEPLETED = 5, // Ran out of mana/energy
    DEATH = 6,             // Caster died
    STUN_EFFECT = 7        // Stunned or similar effect
};

/**
 * @brief Resource types for abilities
 */
enum class ResourceType : int
{
    MANA = 1,
    ENERGY = 2,
    STAMINA = 3,
    RAGE = 4,
    NONE = 5 // No resource cost
};

/**
 * @brief Combat action data structure
 */
struct CombatActionStruct
{
    int actionId;                // Unique action ID
    std::string actionName;      // Action name (e.g., "Fireball", "Sword Strike")
    CombatActionType actionType; // Type of action
    CombatTargetType targetType; // Type of target required

    int casterId;                  // ID of the character casting
    int targetId;                  // ID/UID of the target (0 for self/area)
    PositionStruct targetPosition; // Position for area targets

    float castTime;    // Cast time in seconds (0 for instant)
    float channelTime; // Channel time for channeled abilities
    float range;       // Maximum range
    float areaRadius;  // Radius for AoE (0 for single target)

    ResourceType resourceType; // Resource type consumed
    int resourceCost;          // Resource cost
    int damage;                // Base damage (0 for non-damage abilities)
    int healing;               // Healing amount (0 for non-healing)

    std::chrono::steady_clock::time_point startTime; // When action started
    std::chrono::steady_clock::time_point endTime;   // When action should complete

    CombatActionState state;            // Current state
    InterruptionReason interruptReason; // Reason for interruption (if any)

    bool requiresLineOfSight; // Whether line of sight is required
    bool canBeInterrupted;    // Whether action can be interrupted
    int cooldownMs;           // Cooldown in milliseconds

    // Animation data
    std::string animationName; // Animation to play
    float animationDuration;   // Animation duration
};

/**
 * @brief Damage result structure
 */
struct CombatResultStruct
{
    int casterId;
    int targetId;
    int actionId;
    CombatTargetType targetType;

    int damageDealt;
    int healingDone;

    bool isCritical;
    bool isBlocked;
    bool isDodged;
    bool isResisted;

    int remainingHealth;
    int remainingMana;

    std::string effectsApplied; // JSON string of applied effects

    bool isDamaged; // Indicates if the target was damaged
    bool targetDied;
};

/**
 * @brief Animation sync structure
 */
struct CombatAnimationStruct
{
    int characterId;
    std::string animationName;
    float duration;
    PositionStruct position;
    PositionStruct targetPosition; // For projectiles/directional anims
    bool isLooping;                // For channeled abilities
};
