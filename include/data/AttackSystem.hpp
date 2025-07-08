#pragma once

#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include <functional>
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

/**
 * @b    // Conditional strategy changes
    std::function<void(AttackStrategy &, const CharacterDataStruct &)> adaptStrategy;ef Attack priority levels for target selection
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
 * @brief Attack pattern types for different combat styles
 */
enum class AttackPattern : int
{
    AGGRESSIVE = 1, // High damage, high risk
    DEFENSIVE = 2,  // Balanced, focus on survival
    SUPPORT = 3,    // Focus on healing/buffing allies
    CONTROL = 4,    // Focus on debuffs/crowd control
    BURST = 5,      // High damage in short time
    SUSTAINED = 6,  // Consistent damage over time
    ADAPTIVE = 7    // Changes based on situation
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
 * @brief Target evaluation criteria
 */
struct TargetCriteria
{
    float distanceWeight = 1.0f;      // Weight for distance consideration
    float healthWeight = 1.0f;        // Weight for health consideration
    float threatWeight = 1.0f;        // Weight for threat level
    float roleWeight = 1.0f;          // Weight for target's role
    float vulnerabilityWeight = 1.0f; // Weight for target's vulnerabilities

    float maxRange = 100.0f;         // Maximum attack range
    bool requiresLineOfSight = true; // Whether line of sight is needed
    bool canTargetAllies = false;    // Can target friendly units
    bool canTargetSelf = false;      // Can target self

    std::unordered_set<CombatRole> preferredRoles; // Preferred target roles
    std::unordered_set<CombatRole> avoidedRoles;   // Roles to avoid
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

/**
 * @brief Attack action configuration
 */
struct AttackAction
{
    int actionId;
    std::string name;
    CombatActionType type;

    // Resource requirements
    ResourceType resourceType = ResourceType::NONE;
    int resourceCost = 0;

    // Timing
    float castTime = 0.0f;
    float cooldown = 0.0f;
    float globalCooldown = 1.0f;

    // Range and targeting
    float minRange = 0.0f;
    float maxRange = 5.0f;
    float areaRadius = 0.0f;
    bool requiresLineOfSight = true;

    // Damage and effects
    int baseDamage = 0;
    int baseHealing = 0;
    float damageVariance = 0.1f; // ±10% damage variance

    // Target selection
    TargetCriteria targetCriteria;
    TargetSelectionStrategy preferredStrategy = TargetSelectionStrategy::NEAREST;

    // Animation and effects
    std::string animationName;
    float animationDuration = 1.0f;
    std::vector<std::string> soundEffects;
    std::vector<std::string> visualEffects;

    // Conditions for use
    std::function<bool(const CharacterDataStruct &)> canUse;
    std::function<float(const TargetCandidate &)> calculateDamage;
    std::function<void(int, int)> onHit; // Custom on-hit effects
    std::function<void(int)> onMiss;     // Custom on-miss effects
};

/**
 * @brief Combat sequence for chaining multiple attacks
 */
struct CombatSequence
{
    std::string name;
    std::vector<int> actionIds;      // Sequence of action IDs
    float sequenceDelay = 0.5f;      // Delay between actions
    bool interruptible = true;       // Can sequence be interrupted
    bool requiresAllActions = false; // Must complete all actions

    // Conditions for starting sequence
    std::function<bool(const CharacterDataStruct &)> canStart;

    // Sequence state
    int currentActionIndex = 0;
    std::chrono::steady_clock::time_point lastActionTime;
    bool isActive = false;
};

/**
 * @brief Attack strategy configuration
 */
struct AttackStrategy
{
    std::string name;
    AttackPattern pattern;
    TargetSelectionStrategy targetStrategy;

    // Priority actions for different situations
    std::vector<int> lowHealthActions;    // Actions when health is low
    std::vector<int> highResourceActions; // Actions when resources are high
    std::vector<int> emergencyActions;    // Actions for emergency situations
    std::vector<int> openerActions;       // Actions to start combat
    std::vector<int> finisherActions;     // Actions to finish enemies

    // Behavioral settings
    float aggressionLevel = 0.5f;      // 0.0 = passive, 1.0 = very aggressive
    float riskTolerance = 0.5f;        // Willingness to take risks
    float resourceConservation = 0.5f; // How much to conserve resources

    // Conditional strategy changes
    std::function<void(AttackStrategy &, const CharacterDataStruct &)> adaptStrategy;
};

/**
 * @brief Main attack system class
 */
class AttackSystem
{
  public:
    AttackSystem();
    ~AttackSystem() = default;

    // Action management
    void registerAction(const AttackAction &action);
    void removeAction(int actionId);
    AttackAction *getAction(int actionId);

    // Strategy management
    void registerStrategy(const AttackStrategy &strategy);
    void setActiveStrategy(int characterId, const std::string &strategyName);
    AttackStrategy *getActiveStrategy(int characterId);

    // Sequence management
    void registerSequence(const CombatSequence &sequence);
    void startSequence(int characterId, const std::string &sequenceName);
    void updateSequences(int characterId);

    // Target selection
    std::vector<TargetCandidate> findPotentialTargets(
        int attackerId,
        const PositionStruct &attackerPos,
        const TargetCriteria &criteria,
        const std::vector<CharacterDataStruct> &availableTargets);

    TargetCandidate *selectBestTarget(
        const std::vector<TargetCandidate> &candidates,
        TargetSelectionStrategy strategy,
        const AttackStrategy &attackStrategy);

    // Attack execution
    CombatActionStruct createAttackAction(
        int attackerId,
        const AttackAction &action,
        const TargetCandidate &target);

    bool canExecuteAction(
        const CharacterDataStruct &attacker,
        const AttackAction &action);

    // AI decision making
    AttackAction *selectBestAction(
        int characterId,
        const CharacterDataStruct &character,
        const std::vector<TargetCandidate> &availableTargets);

    void updateAI(int characterId, const CharacterDataStruct &character);

    // Damage calculation
    int calculateDamage(
        const AttackAction &action,
        const CharacterDataStruct &attacker,
        const CharacterDataStruct &target);

    float calculateHitChance(
        const AttackAction &action,
        const CharacterDataStruct &attacker,
        const CharacterDataStruct &target);

    // Utility functions
    float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2);
    bool hasLineOfSight(const PositionStruct &pos1, const PositionStruct &pos2);
    float calculateThreatLevel(const CharacterDataStruct &character);
    CombatRole determineCombatRole(const CharacterDataStruct &character);

  private:
    // Action and strategy storage
    std::unordered_map<int, AttackAction> actions_;
    std::unordered_map<std::string, AttackStrategy> strategies_;
    std::unordered_map<std::string, CombatSequence> sequences_;

    // Character state tracking
    std::unordered_map<int, std::string> activeStrategies_;
    std::unordered_map<int, std::vector<CombatSequence *>> activeSequences_;
    std::unordered_map<int, std::chrono::steady_clock::time_point> lastActionTimes_;
    std::unordered_map<int, std::unordered_map<int, std::chrono::steady_clock::time_point>> cooldowns_;

    // Targeting helpers
    float evaluateTarget(
        const TargetCandidate &candidate,
        const TargetCriteria &criteria,
        const AttackStrategy &strategy);

    void calculateTargetScore(
        TargetCandidate &candidate,
        const TargetCriteria &criteria);

    bool isValidTarget(
        int attackerId,
        TargetCandidate &candidate,
        const TargetCriteria &criteria);

    // AI helpers
    bool shouldUseEmergencyActions(const CharacterDataStruct &character);
    bool isInCombat(const CharacterDataStruct &character);
    void adaptStrategyToSituation(int characterId, const CharacterDataStruct &character);

    // Random number generation
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<float> dist_;
};
