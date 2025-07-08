#include "data/AttackSystem.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

AttackSystem::AttackSystem() : gen_(rd_()), dist_(0.0f, 1.0f)
{
}

void
AttackSystem::registerAction(const AttackAction &action)
{
    actions_[action.actionId] = action;
    Logger logger;
    logger.log("AttackSystem: Registered attack action: " + action.name + " (ID: " + std::to_string(action.actionId) + ")");
}

void
AttackSystem::removeAction(int actionId)
{
    auto it = actions_.find(actionId);
    if (it != actions_.end())
    {
        Logger logger;
        logger.log("AttackSystem: Removed attack action ID: " + std::to_string(actionId));
        actions_.erase(it);
    }
}

AttackAction *
AttackSystem::getAction(int actionId)
{
    auto it = actions_.find(actionId);
    return (it != actions_.end()) ? &it->second : nullptr;
}

void
AttackSystem::registerStrategy(const AttackStrategy &strategy)
{
    strategies_[strategy.name] = strategy;
    Logger logger;
    logger.log("AttackSystem: Registered attack strategy: " + strategy.name);
}

void
AttackSystem::setActiveStrategy(int characterId, const std::string &strategyName)
{
    if (strategies_.find(strategyName) != strategies_.end())
    {
        activeStrategies_[characterId] = strategyName;
        Logger logger;
        logger.log("AttackSystem: Set strategy '" + strategyName + "' for character " + std::to_string(characterId));
    }
    else
    {
        Logger logger;
        logger.logError("AttackSystem: Strategy '" + strategyName + "' not found");
    }
}

AttackStrategy *
AttackSystem::getActiveStrategy(int characterId)
{
    auto it = activeStrategies_.find(characterId);
    if (it != activeStrategies_.end())
    {
        auto strategyIt = strategies_.find(it->second);
        if (strategyIt != strategies_.end())
        {
            return &strategyIt->second;
        }
    }
    return nullptr;
}

void
AttackSystem::registerSequence(const CombatSequence &sequence)
{
    sequences_[sequence.name] = sequence;
    Logger logger;
    logger.log("AttackSystem: Registered combat sequence: " + sequence.name);
}

void
AttackSystem::startSequence(int characterId, const std::string &sequenceName)
{
    auto it = sequences_.find(sequenceName);
    if (it != sequences_.end())
    {
        CombatSequence sequence = it->second;
        sequence.isActive = true;
        sequence.currentActionIndex = 0;
        sequence.lastActionTime = std::chrono::steady_clock::now();

        activeSequences_[characterId].push_back(&sequences_[sequenceName]);
        Logger logger;
        logger.log("AttackSystem: Started sequence '" + sequenceName + "' for character " + std::to_string(characterId));
    }
}

void
AttackSystem::updateSequences(int characterId)
{
    auto it = activeSequences_.find(characterId);
    if (it == activeSequences_.end())
        return;

    auto &sequences = it->second;
    auto now = std::chrono::steady_clock::now();

    for (auto seqIt = sequences.begin(); seqIt != sequences.end();)
    {
        CombatSequence *seq = *seqIt;

        if (!seq->isActive)
        {
            seqIt = sequences.erase(seqIt);
            continue;
        }

        // Check if enough time has passed for next action
        auto timeSinceLastAction = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - seq->lastActionTime)
                                       .count();

        if (timeSinceLastAction >= seq->sequenceDelay * 1000)
        {
            // Move to next action in sequence
            seq->currentActionIndex++;

            if (seq->currentActionIndex >= seq->actionIds.size())
            {
                // Sequence completed
                seq->isActive = false;
                Logger logger;
                logger.log("AttackSystem: Completed sequence '" + seq->name + "' for character " + std::to_string(characterId));
            }
            else
            {
                seq->lastActionTime = now;
            }
        }

        ++seqIt;
    }
}

std::vector<TargetCandidate>
AttackSystem::findPotentialTargets(
    int attackerId,
    const PositionStruct &attackerPos,
    const TargetCriteria &criteria,
    const std::vector<CharacterDataStruct> &availableTargets)
{
    std::vector<TargetCandidate> candidates;

    for (const auto &target : availableTargets)
    {
        if (target.characterId == attackerId)
            continue; // Skip self unless allowed

        TargetCandidate candidate;
        candidate.targetId = target.characterId;
        candidate.position = target.characterPosition;
        candidate.data = const_cast<CharacterDataStruct *>(&target);
        candidate.distance = calculateDistance(attackerPos, target.characterPosition);
        candidate.healthPercent = static_cast<float>(target.characterCurrentHealth) / target.characterMaxHealth;
        candidate.threatLevel = calculateThreatLevel(target);
        candidate.role = determineCombatRole(target);
        candidate.isValidTarget = isValidTarget(attackerId, candidate, criteria);

        if (candidate.isValidTarget)
        {
            calculateTargetScore(candidate, criteria);
            candidates.push_back(candidate);
        }
    }

    // Sort by score (highest first)
    std::sort(candidates.begin(), candidates.end(), [](const TargetCandidate &a, const TargetCandidate &b)
        { return a.totalScore > b.totalScore; });

    return candidates;
}

TargetCandidate *
AttackSystem::selectBestTarget(
    const std::vector<TargetCandidate> &candidates,
    TargetSelectionStrategy strategy,
    const AttackStrategy &attackStrategy)
{
    if (candidates.empty())
        return nullptr;

    std::vector<TargetCandidate> validCandidates;
    for (const auto &candidate : candidates)
    {
        if (candidate.isValidTarget)
        {
            validCandidates.push_back(candidate);
        }
    }

    if (validCandidates.empty())
        return nullptr;

    switch (strategy)
    {
    case TargetSelectionStrategy::NEAREST:
        return const_cast<TargetCandidate *>(&*std::min_element(validCandidates.begin(), validCandidates.end(), [](const TargetCandidate &a, const TargetCandidate &b)
            { return a.distance < b.distance; }));

    case TargetSelectionStrategy::WEAKEST:
        return const_cast<TargetCandidate *>(&*std::min_element(validCandidates.begin(), validCandidates.end(), [](const TargetCandidate &a, const TargetCandidate &b)
            { return a.healthPercent < b.healthPercent; }));

    case TargetSelectionStrategy::STRONGEST:
        return const_cast<TargetCandidate *>(&*std::max_element(validCandidates.begin(), validCandidates.end(), [](const TargetCandidate &a, const TargetCandidate &b)
            { return a.healthPercent < b.healthPercent; }));

    case TargetSelectionStrategy::MOST_DANGEROUS:
        return const_cast<TargetCandidate *>(&*std::max_element(validCandidates.begin(), validCandidates.end(), [](const TargetCandidate &a, const TargetCandidate &b)
            { return a.threatLevel < b.threatLevel; }));

    case TargetSelectionStrategy::SUPPORT_FIRST:
        for (const auto &candidate : validCandidates)
        {
            if (candidate.role == CombatRole::HEALER || candidate.role == CombatRole::SUPPORT)
            {
                return const_cast<TargetCandidate *>(&candidate);
            }
        }
        // Fall back to highest score if no support found
        return const_cast<TargetCandidate *>(&validCandidates[0]);

    case TargetSelectionStrategy::RANDOM:
    {
        std::uniform_int_distribution<size_t> randomDist(0, validCandidates.size() - 1);
        return const_cast<TargetCandidate *>(&validCandidates[randomDist(gen_)]);
    }

    case TargetSelectionStrategy::AI_TACTICAL:
        // Use the pre-calculated scores (already sorted by score)
        return const_cast<TargetCandidate *>(&validCandidates[0]);

    default:
        return const_cast<TargetCandidate *>(&validCandidates[0]);
    }
}

CombatActionStruct
AttackSystem::createAttackAction(
    int attackerId,
    const AttackAction &action,
    const TargetCandidate &target)
{
    CombatActionStruct combatAction;

    combatAction.actionId = action.actionId;
    combatAction.actionName = action.name;
    combatAction.actionType = action.type;
    combatAction.targetType = CombatTargetType::PLAYER; // Assuming player target for now

    combatAction.casterId = attackerId;
    combatAction.targetId = target.targetId;
    combatAction.targetPosition = target.position;

    combatAction.castTime = action.castTime;
    combatAction.channelTime = 0.0f; // Will be set based on action type
    combatAction.range = action.maxRange;
    combatAction.areaRadius = action.areaRadius;

    combatAction.resourceType = action.resourceType;
    combatAction.resourceCost = action.resourceCost;
    combatAction.damage = action.baseDamage;
    combatAction.healing = action.baseHealing;

    combatAction.startTime = std::chrono::steady_clock::now();
    combatAction.endTime = combatAction.startTime + std::chrono::milliseconds(
                                                        static_cast<int>(action.castTime * 1000));

    combatAction.state = CombatActionState::INITIATED;
    combatAction.requiresLineOfSight = action.requiresLineOfSight;
    combatAction.canBeInterrupted = (action.type == CombatActionType::CHANNELED ||
                                     action.type == CombatActionType::SPELL);
    combatAction.cooldownMs = static_cast<int>(action.cooldown * 1000);

    combatAction.animationName = action.animationName;
    combatAction.animationDuration = action.animationDuration;

    return combatAction;
}

bool
AttackSystem::canExecuteAction(
    const CharacterDataStruct &attacker,
    const AttackAction &action)
{
    // Check resources
    if (action.resourceType != ResourceType::NONE)
    {
        switch (action.resourceType)
        {
        case ResourceType::MANA:
            if (attacker.characterCurrentMana < action.resourceCost)
                return false;
            break;
        case ResourceType::STAMINA:
            // Add stamina check when available
            break;
        // Add other resource types as needed
        default:
            break;
        }
    }

    // Check cooldown
    auto cooldownIt = cooldowns_.find(attacker.characterId);
    if (cooldownIt != cooldowns_.end())
    {
        auto actionCooldownIt = cooldownIt->second.find(action.actionId);
        if (actionCooldownIt != cooldownIt->second.end())
        {
            auto now = std::chrono::steady_clock::now();
            auto timeSinceCooldown = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - actionCooldownIt->second)
                                         .count();

            if (timeSinceCooldown < action.cooldown * 1000)
            {
                return false;
            }
        }
    }

    // Check custom conditions
    if (action.canUse && !action.canUse(attacker))
    {
        return false;
    }

    return true;
}

AttackAction *
AttackSystem::selectBestAction(
    int characterId,
    const CharacterDataStruct &character,
    const std::vector<TargetCandidate> &availableTargets)
{
    AttackStrategy *strategy = getActiveStrategy(characterId);
    if (!strategy)
    {
        Logger logger;
        logger.log("No active strategy for character " + std::to_string(characterId));
        return nullptr;
    }

    // Check if in emergency situation
    if (shouldUseEmergencyActions(character) && !strategy->emergencyActions.empty())
    {
        for (int actionId : strategy->emergencyActions)
        {
            AttackAction *action = getAction(actionId);
            if (action && canExecuteAction(character, *action))
            {
                return action;
            }
        }
    }

    // Check if should use opener actions (start of combat)
    if (!isInCombat(character) && !strategy->openerActions.empty())
    {
        for (int actionId : strategy->openerActions)
        {
            AttackAction *action = getAction(actionId);
            if (action && canExecuteAction(character, *action))
            {
                return action;
            }
        }
    }

    // Check for finisher actions (low health targets)
    for (const auto &target : availableTargets)
    {
        if (target.healthPercent < 0.2f && !strategy->finisherActions.empty())
        {
            for (int actionId : strategy->finisherActions)
            {
                AttackAction *action = getAction(actionId);
                if (action && canExecuteAction(character, *action))
                {
                    return action;
                }
            }
        }
    }

    // Default action selection based on available actions
    AttackAction *bestAction = nullptr;
    float bestScore = -1.0f;

    for (const auto &actionPair : actions_)
    {
        const AttackAction &action = actionPair.second;

        if (!canExecuteAction(character, action))
            continue;

        // Calculate action score based on strategy
        float score = 0.0f;

        // Add base score based on damage potential
        score += action.baseDamage * 0.1f;

        // Add score based on strategy preferences
        switch (strategy->pattern)
        {
        case AttackPattern::AGGRESSIVE:
            score += action.baseDamage * strategy->aggressionLevel;
            break;
        case AttackPattern::DEFENSIVE:
            score += action.baseHealing * (1.0f - strategy->aggressionLevel);
            break;
        case AttackPattern::SUPPORT:
            if (action.type == CombatActionType::BUFF || action.type == CombatActionType::DEBUFF)
            {
                score += 50.0f;
            }
            break;
        default:
            break;
        }

        // Prefer actions with shorter cooldowns
        if (action.cooldown > 0)
        {
            score += (10.0f / action.cooldown);
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestAction = const_cast<AttackAction *>(&action);
        }
    }

    return bestAction;
}

void
AttackSystem::updateAI(int characterId, const CharacterDataStruct &character)
{
    // Update sequences
    updateSequences(characterId);

    // Adapt strategy based on current situation
    adaptStrategyToSituation(characterId, character);

    // Update last action time if needed
    lastActionTimes_[characterId] = std::chrono::steady_clock::now();
}

int
AttackSystem::calculateDamage(
    const AttackAction &action,
    const CharacterDataStruct &attacker,
    const CharacterDataStruct &target)
{
    float baseDamage = static_cast<float>(action.baseDamage);

    // Apply damage variance
    float variance = (dist_(gen_) - 0.5f) * 2.0f * action.damageVariance;
    baseDamage *= (1.0f + variance);

    // Apply attacker modifiers (use character attributes if available)
    float attackerModifier = 1.0f;
    for (const auto &attr : attacker.attributes)
    {
        if (attr.slug == "strength")
        {
            attackerModifier += attr.value * 0.01f; // 1% per strength point
            break;
        }
    }
    baseDamage *= attackerModifier;

    // Apply target defense
    float defense = 0.0f;
    for (const auto &attr : target.attributes)
    {
        if (attr.slug == "defense")
        {
            defense = static_cast<float>(attr.value);
            break;
        }
    }

    float damageReduction = defense / (defense + 100.0f); // Diminishing returns formula
    baseDamage *= (1.0f - damageReduction);

    // Custom damage calculation if provided
    if (action.calculateDamage)
    {
        TargetCandidate targetCandidate;
        targetCandidate.targetId = target.characterId;
        targetCandidate.data = const_cast<CharacterDataStruct *>(&target);
        targetCandidate.healthPercent = static_cast<float>(target.characterCurrentHealth) / target.characterMaxHealth;

        baseDamage = action.calculateDamage(targetCandidate);
    }

    return std::max(1, static_cast<int>(baseDamage)); // Minimum 1 damage
}

float
AttackSystem::calculateHitChance(
    const AttackAction &action,
    const CharacterDataStruct &attacker,
    const CharacterDataStruct &target)
{
    float baseHitChance = 0.95f; // 95% base hit chance

    // Apply accuracy vs evasion from attributes
    float accuracy = 0.0f;
    float evasion = 0.0f;

    for (const auto &attr : attacker.attributes)
    {
        if (attr.slug == "accuracy")
        {
            accuracy = static_cast<float>(attr.value);
            break;
        }
    }

    for (const auto &attr : target.attributes)
    {
        if (attr.slug == "evasion")
        {
            evasion = static_cast<float>(attr.value);
            break;
        }
    }

    float hitChance = baseHitChance + (accuracy - evasion) * 0.01f;

    // Apply range penalty for long-range attacks
    float distance = calculateDistance(attacker.characterPosition, target.characterPosition);
    if (distance > action.maxRange * 0.8f)
    {
        float rangePenalty = (distance - action.maxRange * 0.8f) / (action.maxRange * 0.2f);
        hitChance *= (1.0f - rangePenalty * 0.1f); // Up to 10% penalty
    }

    return std::clamp(hitChance, 0.05f, 0.95f); // Clamp between 5% and 95%
}

float
AttackSystem::calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2)
{
    float dx = pos1.positionX - pos2.positionX;
    float dy = pos1.positionY - pos2.positionY;
    float dz = pos1.positionZ - pos2.positionZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool
AttackSystem::hasLineOfSight(const PositionStruct &pos1, const PositionStruct &pos2)
{
    // Simplified line of sight check
    // In a real implementation, this would check for obstacles, terrain, etc.

    float distance = calculateDistance(pos1, pos2);

    // For now, assume line of sight exists if distance is reasonable
    return distance <= 100.0f; // Maximum line of sight distance
}

float
AttackSystem::calculateThreatLevel(const CharacterDataStruct &character)
{
    float threat = 0.0f;

    // Base threat from level
    threat += character.characterLevel * 10.0f;

    // Threat from attributes
    for (const auto &attr : character.attributes)
    {
        if (attr.slug == "strength")
        {
            threat += attr.value * 2.0f;
        }
        else if (attr.slug == "magic")
        {
            threat += attr.value * 1.5f;
        }
    }

    // Threat from health (high health = sustained threat)
    float healthPercent = static_cast<float>(character.characterCurrentHealth) / character.characterMaxHealth;
    threat *= healthPercent;

    // Role-based threat modifiers
    CombatRole role = determineCombatRole(character);
    switch (role)
    {
    case CombatRole::HEALER:
        threat *= 1.5f; // Healers are high priority
        break;
    case CombatRole::DPS:
        threat *= 1.3f;
        break;
    case CombatRole::TANK:
        threat *= 0.8f; // Lower priority due to high defense
        break;
    default:
        break;
    }

    return threat;
}

CombatRole
AttackSystem::determineCombatRole(const CharacterDataStruct &character)
{
    // Simple role determination based on attributes
    int magic = 0, strength = 0, defense = 0;

    for (const auto &attr : character.attributes)
    {
        if (attr.slug == "magic")
        {
            magic = attr.value;
        }
        else if (attr.slug == "strength")
        {
            strength = attr.value;
        }
        else if (attr.slug == "defense")
        {
            defense = attr.value;
        }
    }

    if (magic > strength && magic > defense)
    {
        if (character.characterCurrentMana > character.characterMaxMana * 0.5f)
        {
            return CombatRole::HEALER; // High magic, good mana = likely healer
        }
        else
        {
            return CombatRole::DPS; // High magic = damage dealer
        }
    }
    else if (defense > strength && defense > magic)
    {
        return CombatRole::TANK;
    }
    else if (strength > magic && strength > defense)
    {
        return CombatRole::DPS;
    }

    return CombatRole::HYBRID;
}

// Private helper methods
float
AttackSystem::evaluateTarget(
    const TargetCandidate &candidate,
    const TargetCriteria &criteria,
    const AttackStrategy &strategy)
{
    float score = 0.0f;

    // Distance scoring (closer is better for most cases)
    float distanceScore = (criteria.maxRange - candidate.distance) / criteria.maxRange;
    score += distanceScore * criteria.distanceWeight;

    // Health scoring (depends on strategy)
    float healthScore = 0.0f;
    switch (strategy.pattern)
    {
    case AttackPattern::AGGRESSIVE:
        healthScore = 1.0f - candidate.healthPercent; // Prefer low health targets
        break;
    case AttackPattern::DEFENSIVE:
        healthScore = candidate.healthPercent; // Prefer high health targets
        break;
    default:
        healthScore = 1.0f - candidate.healthPercent;
        break;
    }
    score += healthScore * criteria.healthWeight;

    // Threat level scoring
    score += (candidate.threatLevel / 100.0f) * criteria.threatWeight;

    // Role preference scoring
    if (criteria.preferredRoles.find(candidate.role) != criteria.preferredRoles.end())
    {
        score += 50.0f * criteria.roleWeight;
    }
    if (criteria.avoidedRoles.find(candidate.role) != criteria.avoidedRoles.end())
    {
        score -= 25.0f * criteria.roleWeight;
    }

    return score;
}

void
AttackSystem::calculateTargetScore(
    TargetCandidate &candidate,
    const TargetCriteria &criteria)
{
    // This is a simplified scoring system
    float score = 0.0f;

    // Distance component (closer = better)
    if (candidate.distance <= criteria.maxRange)
    {
        score += (criteria.maxRange - candidate.distance) / criteria.maxRange * 100.0f;
    }

    // Health component (low health = easier target)
    score += (1.0f - candidate.healthPercent) * 50.0f;

    // Threat component (high threat = priority target)
    score += candidate.threatLevel * 0.1f;

    candidate.totalScore = score;
}

bool
AttackSystem::isValidTarget(
    int attackerId,
    TargetCandidate &candidate,
    const TargetCriteria &criteria)
{
    // Check distance
    if (candidate.distance > criteria.maxRange)
    {
        candidate.invalidReason = "Target out of range";
        return false;
    }

    // Check line of sight
    if (criteria.requiresLineOfSight)
    {
        if (!hasLineOfSight(candidate.data->characterPosition, candidate.position))
        {
            candidate.invalidReason = "No line of sight";
            return false;
        }
    }

    // Check if target is self (only allowed if criteria allows it)
    if (candidate.targetId == attackerId && !criteria.canTargetSelf)
    {
        candidate.invalidReason = "Cannot target self";
        return false;
    }

    // Check if target is alive
    if (candidate.data->characterCurrentHealth <= 0)
    {
        candidate.invalidReason = "Target is dead";
        return false;
    }

    return true;
}

bool
AttackSystem::shouldUseEmergencyActions(const CharacterDataStruct &character)
{
    float healthPercent = static_cast<float>(character.characterCurrentHealth) / character.characterMaxHealth;
    return healthPercent < 0.25f; // Emergency when below 25% health
}

bool
AttackSystem::isInCombat(const CharacterDataStruct &character)
{
    // Simple combat detection - in real implementation this would be more sophisticated
    auto it = lastActionTimes_.find(character.characterId);
    if (it != lastActionTimes_.end())
    {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastAction = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second)
                                       .count();

        return timeSinceLastAction < 10; // In combat if action within last 10 seconds
    }

    return false;
}

void
AttackSystem::adaptStrategyToSituation(int characterId, const CharacterDataStruct &character)
{
    AttackStrategy *strategy = getActiveStrategy(characterId);
    if (!strategy || !strategy->adaptStrategy)
        return;

    strategy->adaptStrategy(*strategy, character);
}
