#pragma once

#include "data/AttackSystem.hpp"
#include "events/handlers/CombatEventHandler.hpp"
#include <memory>

/**
 * @brief Example usage and setup of the Attack System
 *
 * This class demonstrates how to configure and use the new attack system
 */
class AttackSystemExample
{
  public:
    static void setupBasicAttacks(AttackSystem &attackSystem);
    static void setupBasicStrategies(AttackSystem &attackSystem);
    static void setupExampleSequences(AttackSystem &attackSystem);
    static void demonstratePlayerAttack(CombatEventHandler &combatHandler, int playerId, int targetId);
    static void demonstrateAIBehavior(AttackSystem &attackSystem, int npcId);
    static void setupDifferentRoles(AttackSystem &attackSystem);

  private:
    static AttackAction createBasicMeleeAttack();
    static AttackAction createFireballSpell();
    static AttackAction createHealingSpell();
    static AttackAction createStunAttack();
    static AttackAction createAOEAttack();

    static AttackStrategy createAggressiveStrategy();
    static AttackStrategy createDefensiveStrategy();
    static AttackStrategy createSupportStrategy();
    static AttackStrategy createBalancedStrategy();

    static CombatSequence createComboSequence();
    static CombatSequence createBurstSequence();
};

// Implementation

void
AttackSystemExample::setupBasicAttacks(AttackSystem &attackSystem)
{
    // Basic melee attack
    AttackAction meleeAttack = createBasicMeleeAttack();
    attackSystem.registerAction(meleeAttack);

    // Fireball spell
    AttackAction fireball = createFireballSpell();
    attackSystem.registerAction(fireball);

    // Healing spell
    AttackAction heal = createHealingSpell();
    attackSystem.registerAction(heal);

    // Stun attack
    AttackAction stun = createStunAttack();
    attackSystem.registerAction(stun);

    // AOE attack
    AttackAction aoe = createAOEAttack();
    attackSystem.registerAction(aoe);
}

void
AttackSystemExample::setupBasicStrategies(AttackSystem &attackSystem)
{
    // Aggressive strategy
    AttackStrategy aggressive = createAggressiveStrategy();
    attackSystem.registerStrategy(aggressive);

    // Defensive strategy
    AttackStrategy defensive = createDefensiveStrategy();
    attackSystem.registerStrategy(defensive);

    // Support strategy
    AttackStrategy support = createSupportStrategy();
    attackSystem.registerStrategy(support);

    // Balanced strategy
    AttackStrategy balanced = createBalancedStrategy();
    attackSystem.registerStrategy(balanced);
}

void
AttackSystemExample::setupExampleSequences(AttackSystem &attackSystem)
{
    // Combo sequence
    CombatSequence combo = createComboSequence();
    attackSystem.registerSequence(combo);

    // Burst sequence
    CombatSequence burst = createBurstSequence();
    attackSystem.registerSequence(burst);
}

void
AttackSystemExample::demonstratePlayerAttack(CombatEventHandler &combatHandler, int playerId, int targetId)
{
    // Create attack request
    nlohmann::json attackRequest;
    attackRequest["actionId"] = 1; // Basic attack
    attackRequest["targetId"] = targetId;

    // Create event
    Event attackEvent(Event::Type::PLAYER_ATTACK, attackRequest, playerId, nullptr);

    // Handle the attack
    combatHandler.handlePlayerAttack(attackEvent);
}

void
AttackSystemExample::demonstrateAIBehavior(AttackSystem &attackSystem, int npcId)
{
    // Set AI strategy
    attackSystem.setActiveStrategy(npcId, "balanced");

    // The AI will automatically select targets and actions based on the strategy
    // This would typically be called periodically by the game loop

    // Example: Set up adaptive strategy
    AttackStrategy adaptiveStrategy;
    adaptiveStrategy.name = "adaptive_npc";
    adaptiveStrategy.pattern = AttackPattern::ADAPTIVE;
    adaptiveStrategy.aggressionLevel = 0.7f;
    adaptiveStrategy.riskTolerance = 0.5f;

    // Custom adaptation logic
    adaptiveStrategy.adaptStrategy = [](AttackStrategy &strategy, const CharacterStatusStruct &character)
    {
        float healthPercent = static_cast<float>(character.currentHealth) / character.maxHealth;

        if (healthPercent < 0.3f)
        {
            // Low health: become defensive
            strategy.aggressionLevel = 0.2f;
            strategy.riskTolerance = 0.1f;
        }
        else if (healthPercent > 0.8f)
        {
            // High health: become aggressive
            strategy.aggressionLevel = 0.9f;
            strategy.riskTolerance = 0.8f;
        }
    };

    attackSystem.registerStrategy(adaptiveStrategy);
    attackSystem.setActiveStrategy(npcId, "adaptive_npc");
}

void
AttackSystemExample::setupDifferentRoles(AttackSystem &attackSystem)
{
    // Tank strategy
    AttackStrategy tankStrategy;
    tankStrategy.name = "tank";
    tankStrategy.pattern = AttackPattern::DEFENSIVE;
    tankStrategy.targetStrategy = TargetSelectionStrategy::MOST_DANGEROUS;
    tankStrategy.aggressionLevel = 0.3f;
    tankStrategy.riskTolerance = 0.8f; // Tanks can take risks
    attackSystem.registerStrategy(tankStrategy);

    // DPS strategy
    AttackStrategy dpsStrategy;
    dpsStrategy.name = "dps";
    dpsStrategy.pattern = AttackPattern::AGGRESSIVE;
    dpsStrategy.targetStrategy = TargetSelectionStrategy::WEAKEST;
    dpsStrategy.aggressionLevel = 0.8f;
    dpsStrategy.riskTolerance = 0.4f;
    attackSystem.registerStrategy(dpsStrategy);

    // Healer strategy
    AttackStrategy healerStrategy;
    healerStrategy.name = "healer";
    healerStrategy.pattern = AttackPattern::SUPPORT;
    healerStrategy.targetStrategy = TargetSelectionStrategy::NEAREST;
    healerStrategy.aggressionLevel = 0.2f;
    healerStrategy.riskTolerance = 0.1f;
    attackSystem.registerStrategy(healerStrategy);
}

// Helper methods for creating actions

AttackAction
AttackSystemExample::createBasicMeleeAttack()
{
    AttackAction action;
    action.actionId = 1;
    action.name = "Basic Attack";
    action.type = CombatActionType::BASIC_ATTACK;
    action.resourceType = ResourceType::NONE;
    action.resourceCost = 0;
    action.castTime = 0.0f;
    action.cooldown = 1.0f;
    action.minRange = 0.0f;
    action.maxRange = 3.0f;
    action.baseDamage = 20;
    action.damageVariance = 0.1f;
    action.animationName = "melee_attack";
    action.animationDuration = 1.0f;

    // Target criteria
    action.targetCriteria.maxRange = 3.0f;
    action.targetCriteria.requiresLineOfSight = true;
    action.targetCriteria.canTargetAllies = false;
    action.preferredStrategy = TargetSelectionStrategy::NEAREST;

    return action;
}

AttackAction
AttackSystemExample::createFireballSpell()
{
    AttackAction action;
    action.actionId = 2;
    action.name = "Fireball";
    action.type = CombatActionType::SPELL;
    action.resourceType = ResourceType::MANA;
    action.resourceCost = 30;
    action.castTime = 2.5f;
    action.cooldown = 3.0f;
    action.minRange = 5.0f;
    action.maxRange = 20.0f;
    action.baseDamage = 50;
    action.damageVariance = 0.15f;
    action.animationName = "cast_fireball";
    action.animationDuration = 2.5f;

    action.targetCriteria.maxRange = 20.0f;
    action.targetCriteria.requiresLineOfSight = true;
    action.targetCriteria.canTargetAllies = false;
    action.preferredStrategy = TargetSelectionStrategy::WEAKEST;

    return action;
}

AttackAction
AttackSystemExample::createHealingSpell()
{
    AttackAction action;
    action.actionId = 3;
    action.name = "Heal";
    action.type = CombatActionType::BUFF;
    action.resourceType = ResourceType::MANA;
    action.resourceCost = 20;
    action.castTime = 1.5f;
    action.cooldown = 2.0f;
    action.minRange = 0.0f;
    action.maxRange = 15.0f;
    action.baseDamage = 0;
    action.baseHealing = 40;
    action.animationName = "cast_heal";
    action.animationDuration = 1.5f;

    action.targetCriteria.maxRange = 15.0f;
    action.targetCriteria.requiresLineOfSight = true;
    action.targetCriteria.canTargetAllies = true;
    action.targetCriteria.canTargetSelf = true;
    action.preferredStrategy = TargetSelectionStrategy::WEAKEST; // Heal most damaged ally

    return action;
}

AttackAction
AttackSystemExample::createStunAttack()
{
    AttackAction action;
    action.actionId = 4;
    action.name = "Stun Strike";
    action.type = CombatActionType::SKILL;
    action.resourceType = ResourceType::STAMINA;
    action.resourceCost = 25;
    action.castTime = 0.5f;
    action.cooldown = 8.0f;
    action.minRange = 0.0f;
    action.maxRange = 4.0f;
    action.baseDamage = 15;
    action.animationName = "stun_attack";
    action.animationDuration = 1.0f;

    action.targetCriteria.maxRange = 4.0f;
    action.targetCriteria.requiresLineOfSight = true;
    action.targetCriteria.canTargetAllies = false;
    action.preferredStrategy = TargetSelectionStrategy::MOST_DANGEROUS;

    return action;
}

AttackAction
AttackSystemExample::createAOEAttack()
{
    AttackAction action;
    action.actionId = 5;
    action.name = "Explosive Blast";
    action.type = CombatActionType::AOE_ATTACK;
    action.resourceType = ResourceType::MANA;
    action.resourceCost = 50;
    action.castTime = 3.0f;
    action.cooldown = 10.0f;
    action.minRange = 5.0f;
    action.maxRange = 25.0f;
    action.areaRadius = 8.0f;
    action.baseDamage = 35;
    action.animationName = "cast_explosion";
    action.animationDuration = 3.0f;

    action.targetCriteria.maxRange = 25.0f;
    action.targetCriteria.requiresLineOfSight = true;
    action.targetCriteria.canTargetAllies = false;
    action.preferredStrategy = TargetSelectionStrategy::RANDOM; // Target dense areas

    return action;
}

// Helper methods for creating strategies

AttackStrategy
AttackSystemExample::createAggressiveStrategy()
{
    AttackStrategy strategy;
    strategy.name = "aggressive";
    strategy.pattern = AttackPattern::AGGRESSIVE;
    strategy.targetStrategy = TargetSelectionStrategy::WEAKEST;
    strategy.aggressionLevel = 0.9f;
    strategy.riskTolerance = 0.7f;
    strategy.resourceConservation = 0.2f;

    strategy.openerActions = {2};    // Start with fireball
    strategy.finisherActions = {1};  // Finish with basic attack
    strategy.emergencyActions = {3}; // Heal when low

    return strategy;
}

AttackStrategy
AttackSystemExample::createDefensiveStrategy()
{
    AttackStrategy strategy;
    strategy.name = "defensive";
    strategy.pattern = AttackPattern::DEFENSIVE;
    strategy.targetStrategy = TargetSelectionStrategy::MOST_DANGEROUS;
    strategy.aggressionLevel = 0.3f;
    strategy.riskTolerance = 0.2f;
    strategy.resourceConservation = 0.8f;

    strategy.emergencyActions = {3}; // Prioritize healing
    strategy.openerActions = {4};    // Start with stun

    return strategy;
}

AttackStrategy
AttackSystemExample::createSupportStrategy()
{
    AttackStrategy strategy;
    strategy.name = "support";
    strategy.pattern = AttackPattern::SUPPORT;
    strategy.targetStrategy = TargetSelectionStrategy::NEAREST;
    strategy.aggressionLevel = 0.2f;
    strategy.riskTolerance = 0.1f;
    strategy.resourceConservation = 0.6f;

    strategy.highResourceActions = {3}; // Use healing when mana is high
    strategy.emergencyActions = {3};    // Always prioritize healing

    return strategy;
}

AttackStrategy
AttackSystemExample::createBalancedStrategy()
{
    AttackStrategy strategy;
    strategy.name = "balanced";
    strategy.pattern = AttackPattern::ADAPTIVE;
    strategy.targetStrategy = TargetSelectionStrategy::AI_TACTICAL;
    strategy.aggressionLevel = 0.5f;
    strategy.riskTolerance = 0.5f;
    strategy.resourceConservation = 0.5f;

    strategy.openerActions = {1, 2}; // Basic attack or fireball
    strategy.finisherActions = {1};  // Basic attack to finish
    strategy.emergencyActions = {3}; // Heal when needed

    return strategy;
}

// Helper methods for creating sequences

CombatSequence
AttackSystemExample::createComboSequence()
{
    CombatSequence sequence;
    sequence.name = "stun_combo";
    sequence.actionIds = {4, 1, 1}; // Stun -> Basic Attack -> Basic Attack
    sequence.sequenceDelay = 0.3f;
    sequence.interruptible = true;
    sequence.requiresAllActions = false;

    return sequence;
}

CombatSequence
AttackSystemExample::createBurstSequence()
{
    CombatSequence sequence;
    sequence.name = "magic_burst";
    sequence.actionIds = {2, 5}; // Fireball -> AOE
    sequence.sequenceDelay = 1.0f;
    sequence.interruptible = false;
    sequence.requiresAllActions = true;

    return sequence;
}
