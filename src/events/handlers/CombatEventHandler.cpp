#include "events/handlers/CombatEventHandler.hpp"
#include "data/AttackSystem.hpp"
#include "data/CombatStructs.hpp"
#include "events/EventData.hpp"
#include <chrono>
#include <cmath>

CombatEventHandler::CombatEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices),
      lastUpdateTime_(std::chrono::steady_clock::now()),
      attackSystem_(std::make_unique<AttackSystem>())
{
    // Initialize action definitions - this could be loaded from database/config
    initializeActionDefinitions();
}

void
CombatEventHandler::handleInitiateCombatAction(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<CombatActionStruct>(data))
        {
            CombatActionStruct action = std::get<CombatActionStruct>(data);
            action.casterId = clientID; // Ensure caster ID matches client

            // Validate the combat action
            if (!validateCombatAction(action))
            {
                sendErrorResponse(clientSocket, "Combat action validation failed!", "initiateCombatAction", clientID);
                return;
            }

            // Check cooldown
            if (isOnCooldown(action.casterId, action.actionId))
            {
                sendErrorResponse(clientSocket, "Action is on cooldown!", "initiateCombatAction", clientID);
                return;
            }

            // Consume resources upfront for non-channeled abilities
            if (action.actionType != CombatActionType::CHANNELED)
            {
                consumeResources(action.casterId, action.resourceType, action.resourceCost);
            }

            // Set action timing
            auto now = std::chrono::steady_clock::now();
            action.startTime = now;
            action.endTime = now + std::chrono::milliseconds(static_cast<int>(action.castTime * 1000));
            action.state = (action.castTime > 0) ? CombatActionState::CASTING : CombatActionState::EXECUTING;

            // Store ongoing action
            auto actionPtr = std::make_shared<CombatActionStruct>(action);
            ongoingActions_[action.casterId] = actionPtr;

            // Send animation to all clients
            CombatAnimationStruct animation;
            animation.characterId = action.casterId;
            animation.animationName = action.animationName;
            animation.duration = action.animationDuration;
            // Get caster position from character manager
            auto casterData = gameServices_.getCharacterManager().getCharacterData(action.casterId);
            animation.position = casterData.characterPosition;

            if (action.targetType == CombatTargetType::PLAYER || action.targetType == CombatTargetType::MOB)
            {
                // Get target position for directional animations
                if (action.targetType == CombatTargetType::PLAYER)
                {
                    auto targetData = gameServices_.getCharacterManager().getCharacterData(action.targetId);
                    animation.targetPosition = targetData.characterPosition;
                }
                else if (action.targetType == CombatTargetType::MOB)
                {
                    // For mobs, get position from mob instance manager using UID
                    auto mobData = gameServices_.getMobInstanceManager().getMobInstance(action.targetId);
                    animation.targetPosition = mobData.position;
                }
            }
            else if (action.targetType == CombatTargetType::AREA)
            {
                animation.targetPosition = action.targetPosition;
            }

            animation.isLooping = (action.actionType == CombatActionType::CHANNELED);

            // Broadcast animation start
            nlohmann::json animationResponse = ResponseBuilder()
                                                   .setHeader("message", "Combat animation started!")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "combatAnimation")
                                                   .setBody("animation", combatAnimationToJson(animation))
                                                   .build();

            std::string animationData = networkManager_.generateResponseMessage("success", animationResponse);
            broadcastToAllClients(animationData);

            // Send action initiation response
            nlohmann::json actionResponse = ResponseBuilder()
                                                .setHeader("message", "Combat action initiated!")
                                                .setHeader("clientId", clientID)
                                                .setHeader("eventType", "initiateCombatAction")
                                                .setBody("action", combatActionToJson(action))
                                                .build();

            std::string responseData = networkManager_.generateResponseMessage("success", actionResponse);

            // For instant actions, complete immediately
            if (action.castTime <= 0)
            {
                completeCombatActionInternal(actionPtr);
            }

            broadcastToAllClients(responseData);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting combat action data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleInitiateCombatAction: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::handleCompleteCombatAction(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        if (std::holds_alternative<CombatActionStruct>(data))
        {
            CombatActionStruct action = std::get<CombatActionStruct>(data);

            // Find ongoing action
            auto it = ongoingActions_.find(action.casterId);
            if (it != ongoingActions_.end())
            {
                auto actionPtr = it->second;
                completeCombatActionInternal(actionPtr);
            }
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleCompleteCombatAction: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::handleInterruptCombatAction(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        if (std::holds_alternative<CombatActionStruct>(data))
        {
            CombatActionStruct action = std::get<CombatActionStruct>(data);

            // Find and interrupt ongoing action
            auto it = ongoingActions_.find(action.casterId);
            if (it != ongoingActions_.end())
            {
                auto actionPtr = it->second;
                actionPtr->state = CombatActionState::INTERRUPTED;
                actionPtr->interruptReason = action.interruptReason;

                // Refund resources for channeled abilities
                if (actionPtr->actionType == CombatActionType::CHANNELED)
                {
                    // Partial refund based on time elapsed
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - actionPtr->startTime);
                    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(actionPtr->endTime - actionPtr->startTime);

                    if (totalDuration.count() > 0)
                    {
                        float refundPercent = 1.0f - (static_cast<float>(elapsed.count()) / static_cast<float>(totalDuration.count()));
                        int refundAmount = static_cast<int>(actionPtr->resourceCost * refundPercent);

                        // Refund resources (implementation depends on resource system)
                        // refundResources(actionPtr->casterId, actionPtr->resourceType, refundAmount);
                    }
                }

                // Remove from ongoing actions
                ongoingActions_.erase(it);

                // Broadcast interruption
                nlohmann::json interruptResponse = ResponseBuilder()
                                                       .setHeader("message", "Combat action interrupted!")
                                                       .setHeader("clientId", clientID)
                                                       .setHeader("eventType", "interruptCombatAction")
                                                       .setBody("action", combatActionToJson(*actionPtr))
                                                       .build();

                std::string responseData = networkManager_.generateResponseMessage("success", interruptResponse);
                broadcastToAllClients(responseData);
            }
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleInterruptCombatAction: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::handleCombatAnimation(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        if (std::holds_alternative<CombatAnimationStruct>(data))
        {
            CombatAnimationStruct animation = std::get<CombatAnimationStruct>(data);

            // Broadcast animation to all clients
            nlohmann::json animationResponse = ResponseBuilder()
                                                   .setHeader("message", "Combat animation sync!")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "combatAnimation")
                                                   .setBody("animation", combatAnimationToJson(animation))
                                                   .build();

            std::string responseData = networkManager_.generateResponseMessage("success", animationResponse);
            broadcastToAllClients(responseData);
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleCombatAnimation: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::handleCombatResult(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        if (std::holds_alternative<CombatResultStruct>(data))
        {
            CombatResultStruct result = std::get<CombatResultStruct>(data);

            // Broadcast combat result to all clients
            nlohmann::json resultResponse = ResponseBuilder()
                                                .setHeader("message", "Combat result!")
                                                .setHeader("clientId", clientID)
                                                .setHeader("eventType", "combatResult")
                                                .setBody("result", combatResultToJson(result))
                                                .build();

            std::string responseData = networkManager_.generateResponseMessage("success", resultResponse);
            broadcastToAllClients(responseData);
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleCombatResult: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::updateOngoingActions()
{
    auto now = std::chrono::steady_clock::now();

    // Check for completed actions
    for (auto it = ongoingActions_.begin(); it != ongoingActions_.end();)
    {
        auto actionPtr = it->second;

        if (actionPtr->state == CombatActionState::CASTING && now >= actionPtr->endTime)
        {
            // Cast completed, execute action
            completeCombatActionInternal(actionPtr);
            it = ongoingActions_.erase(it);
        }
        else if (actionPtr->state == CombatActionState::INTERRUPTED ||
                 actionPtr->state == CombatActionState::COMPLETED ||
                 actionPtr->state == CombatActionState::FAILED)
        {
            // Remove completed/failed actions
            it = ongoingActions_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    lastUpdateTime_ = now;
}

// Private helper methods implementation...

bool
CombatEventHandler::validateCombatAction(const CombatActionStruct &action)
{
    // Validate target
    if (action.targetType != CombatTargetType::SELF && action.targetType != CombatTargetType::NONE)
    {
        if (!validateTarget(action.casterId, action.targetId, action.targetType, action.range, action.requiresLineOfSight))
        {
            return false;
        }
    }

    // Validate resources
    if (!validateResources(action.casterId, action.resourceType, action.resourceCost))
    {
        return false;
    }

    // Check if caster is already performing an action
    if (ongoingActions_.find(action.casterId) != ongoingActions_.end())
    {
        return false;
    }

    return true;
}

bool
CombatEventHandler::validateTarget(int casterId, int targetId, CombatTargetType targetType, float range, bool requiresLOS)
{
    if (targetId == 0)
        return false;

    // Check distance based on target type
    float distance = getDistanceToTarget(casterId, targetId, targetType);
    if (distance > range)
    {
        return false;
    }

    // TODO: Implement line of sight check if required
    if (requiresLOS)
    {
        // Line of sight validation logic
    }

    return true;
}

bool
CombatEventHandler::validateResources(int casterId, ResourceType resourceType, int resourceCost)
{
    if (resourceType == ResourceType::NONE || resourceCost <= 0)
    {
        return true;
    }

    // Get character data and check resources
    try
    {
        auto characterData = gameServices_.getCharacterManager().getCharacterData(casterId);

        switch (resourceType)
        {
        case ResourceType::MANA:
            return characterData.characterCurrentMana >= resourceCost;
        case ResourceType::ENERGY:
        case ResourceType::STAMINA:
        case ResourceType::RAGE:
            // These would require additional fields in CharacterDataStruct
            return true; // Placeholder
        default:
            return true;
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error validating resources: " + std::string(ex.what()));
        return false;
    }
}

void
CombatEventHandler::consumeResources(int casterId, ResourceType resourceType, int resourceCost)
{
    if (resourceType == ResourceType::NONE || resourceCost <= 0)
    {
        return;
    }

    try
    {
        auto characterData = gameServices_.getCharacterManager().getCharacterData(casterId);

        switch (resourceType)
        {
        case ResourceType::MANA:
            characterData.characterCurrentMana = std::max(0, characterData.characterCurrentMana - resourceCost);
            // Update character data in manager
            // gameServices_.getCharacterManager().updateCharacterResources(casterId, characterData);
            break;
        default:
            break;
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error consuming resources: " + std::string(ex.what()));
    }
}

float
CombatEventHandler::getDistance(int char1Id, int char2Id)
{
    try
    {
        auto char1Data = gameServices_.getCharacterManager().getCharacterData(char1Id);
        auto char2Data = gameServices_.getCharacterManager().getCharacterData(char2Id);

        float dx = char1Data.characterPosition.positionX - char2Data.characterPosition.positionX;
        float dy = char1Data.characterPosition.positionY - char2Data.characterPosition.positionY;
        float dz = char1Data.characterPosition.positionZ - char2Data.characterPosition.positionZ;

        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error calculating distance: " + std::string(ex.what()));
        return 9999.0f; // Return large distance on error
    }
}

float
CombatEventHandler::getDistanceToTarget(int casterId, int targetId, CombatTargetType targetType)
{
    try
    {
        auto casterData = gameServices_.getCharacterManager().getCharacterData(casterId);

        PositionStruct targetPosition;

        if (targetType == CombatTargetType::PLAYER)
        {
            auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
            targetPosition = targetData.characterPosition;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
            targetPosition = mobData.position;
        }
        else
        {
            gameServices_.getLogger().logError("Unsupported target type for distance calculation");
            return 9999.0f;
        }

        float dx = casterData.characterPosition.positionX - targetPosition.positionX;
        float dy = casterData.characterPosition.positionY - targetPosition.positionY;
        float dz = casterData.characterPosition.positionZ - targetPosition.positionZ;

        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error calculating distance to target: " + std::string(ex.what()));
        return 9999.0f; // Return large distance on error
    }
}

bool
CombatEventHandler::isOnCooldown(int casterId, int actionId)
{
    auto casterIt = cooldowns_.find(casterId);
    if (casterIt != cooldowns_.end())
    {
        auto actionIt = casterIt->second.find(actionId);
        if (actionIt != casterIt->second.end())
        {
            return std::chrono::steady_clock::now() < actionIt->second;
        }
    }
    return false;
}

void
CombatEventHandler::setCooldown(int casterId, int actionId, int cooldownMs)
{
    if (cooldownMs > 0)
    {
        auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(cooldownMs);
        cooldowns_[casterId][actionId] = endTime;
    }
}

void
CombatEventHandler::completeCombatActionInternal(std::shared_ptr<CombatActionStruct> actionPtr)
{
    actionPtr->state = CombatActionState::EXECUTING;

    // Calculate and apply effects
    CombatResultStruct result = calculateDamage(*actionPtr, actionPtr->casterId, actionPtr->targetId);

    // Apply damage/healing based on target type
    if (result.damageDealt > 0)
    {
        int actualDamage = applyDamageToTarget(actionPtr->targetId, actionPtr->targetType, result.damageDealt);
        result.damageDealt = actualDamage;
    }

    if (result.healingDone > 0)
    {
        int actualHealing = applyHealingToTarget(actionPtr->targetId, actionPtr->targetType, result.healingDone);
        result.healingDone = actualHealing;
    }

    // Set cooldown
    setCooldown(actionPtr->casterId, actionPtr->actionId, actionPtr->cooldownMs);

    actionPtr->state = CombatActionState::COMPLETED;

    // Broadcast result
    nlohmann::json resultResponse = ResponseBuilder()
                                        .setHeader("message", "Combat action completed!")
                                        .setHeader("clientId", actionPtr->casterId)
                                        .setHeader("eventType", "combatResult")
                                        .setBody("result", combatResultToJson(result))
                                        .build();

    std::string responseData = networkManager_.generateResponseMessage("success", resultResponse);
    broadcastToAllClients(responseData);
}

CombatResultStruct
CombatEventHandler::calculateDamage(const CombatActionStruct &action, int casterId, int targetId)
{
    CombatResultStruct result;
    result.casterId = casterId;
    result.targetId = targetId;
    result.actionId = action.actionId;
    result.damageDealt = action.damage;
    result.healingDone = action.healing;
    result.isCritical = false; // TODO: Implement crit calculation
    result.isBlocked = false;
    result.isDodged = false;
    result.isResisted = false;
    result.targetDied = false;

    // TODO: Apply damage modifiers, resistances, etc.

    return result;
}

int
CombatEventHandler::applyDamage(int targetId, int damage)
{
    try
    {
        auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
        int actualDamage = std::min(damage, targetData.characterCurrentHealth);
        targetData.characterCurrentHealth -= actualDamage;

        // Update character data
        // gameServices_.getCharacterManager().updateCharacterHealth(targetId, targetData.characterCurrentHealth);

        return actualDamage;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error applying damage: " + std::string(ex.what()));
        return 0;
    }
}

int
CombatEventHandler::applyDamageToTarget(int targetId, CombatTargetType targetType, int damage)
{
    try
    {
        if (targetType == CombatTargetType::PLAYER)
        {
            auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
            int actualDamage = std::min(damage, targetData.characterCurrentHealth);
            targetData.characterCurrentHealth -= actualDamage;

            // Update character data
            // gameServices_.getCharacterManager().updateCharacterHealth(targetId, targetData.characterCurrentHealth);

            return actualDamage;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
            int actualDamage = std::min(damage, mobData.currentHealth);

            // Update mob health using MobInstanceManager
            gameServices_.getMobInstanceManager().updateMobHealth(targetId, mobData.currentHealth - actualDamage);

            return actualDamage;
        }
        else
        {
            gameServices_.getLogger().logError("Unsupported target type for damage application");
            return 0;
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error applying damage to target: " + std::string(ex.what()));
        return 0;
    }
}

int
CombatEventHandler::applyHealing(int targetId, int healing)
{
    try
    {
        auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
        int maxHealing = targetData.characterMaxHealth - targetData.characterCurrentHealth;
        int actualHealing = std::min(healing, maxHealing);
        targetData.characterCurrentHealth += actualHealing;

        // Update character data
        // gameServices_.getCharacterManager().updateCharacterHealth(targetId, targetData.characterCurrentHealth);

        return actualHealing;
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error applying healing: " + std::string(ex.what()));
        return 0;
    }
}

int
CombatEventHandler::applyHealingToTarget(int targetId, CombatTargetType targetType, int healing)
{
    try
    {
        if (targetType == CombatTargetType::PLAYER)
        {
            auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
            int maxHealing = targetData.characterMaxHealth - targetData.characterCurrentHealth;
            int actualHealing = std::min(healing, maxHealing);
            targetData.characterCurrentHealth += actualHealing;

            // Update character data
            // gameServices_.getCharacterManager().updateCharacterHealth(targetId, targetData.characterCurrentHealth);

            return actualHealing;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
            int maxHealing = mobData.maxHealth - mobData.currentHealth;
            int actualHealing = std::min(healing, maxHealing);

            // Update mob health using MobInstanceManager
            gameServices_.getMobInstanceManager().updateMobHealth(targetId, mobData.currentHealth + actualHealing);

            return actualHealing;
        }
        else
        {
            gameServices_.getLogger().logError("Unsupported target type for healing application");
            return 0;
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error applying healing to target: " + std::string(ex.what()));
        return 0;
    }
}

nlohmann::json
CombatEventHandler::combatActionToJson(const CombatActionStruct &action)
{
    return nlohmann::json{
        {"actionId", action.actionId},
        {"actionName", action.actionName},
        {"actionType", static_cast<int>(action.actionType)},
        {"targetType", static_cast<int>(action.targetType)},
        {"casterId", action.casterId},
        {"targetId", action.targetId},
        {"targetPosition", {{"x", action.targetPosition.positionX}, {"y", action.targetPosition.positionY}, {"z", action.targetPosition.positionZ}}},
        {"castTime", action.castTime},
        {"range", action.range},
        {"damage", action.damage},
        {"state", static_cast<int>(action.state)},
        {"animationName", action.animationName}};
}

nlohmann::json
CombatEventHandler::combatResultToJson(const CombatResultStruct &result)
{
    return nlohmann::json{
        {"casterId", result.casterId},
        {"targetId", result.targetId},
        {"actionId", result.actionId},
        {"damageDealt", result.damageDealt},
        {"healingDone", result.healingDone},
        {"isCritical", result.isCritical},
        {"isBlocked", result.isBlocked},
        {"isDodged", result.isDodged},
        {"remainingHealth", result.remainingHealth},
        {"remainingMana", result.remainingMana},
        {"targetDied", result.targetDied}};
}

nlohmann::json
CombatEventHandler::combatAnimationToJson(const CombatAnimationStruct &animation)
{
    return nlohmann::json{
        {"characterId", animation.characterId},
        {"animationName", animation.animationName},
        {"duration", animation.duration},
        {"position", {{"x", animation.position.positionX}, {"y", animation.position.positionY}, {"z", animation.position.positionZ}}},
        {"targetPosition", {{"x", animation.targetPosition.positionX}, {"y", animation.targetPosition.positionY}, {"z", animation.targetPosition.positionZ}}},
        {"isLooping", animation.isLooping}};
}

void
CombatEventHandler::initializeActionDefinitions()
{
    // Example action definitions - these would typically be loaded from database/config

    CombatActionStruct basicAttack;
    basicAttack.actionId = 1;
    basicAttack.actionName = "Basic Attack";
    basicAttack.actionType = CombatActionType::BASIC_ATTACK;
    basicAttack.targetType = CombatTargetType::MOB;
    basicAttack.castTime = 0.0f;
    basicAttack.range = 3.0f;
    basicAttack.damage = 10;
    basicAttack.resourceType = ResourceType::NONE;
    basicAttack.resourceCost = 0;
    basicAttack.cooldownMs = 1000;
    basicAttack.animationName = "attack_basic";
    basicAttack.animationDuration = 1.0f;
    basicAttack.canBeInterrupted = false;
    actionDefinitions_[1] = basicAttack;

    CombatActionStruct fireball;
    fireball.actionId = 2;
    fireball.actionName = "Fireball";
    fireball.actionType = CombatActionType::SPELL;
    fireball.targetType = CombatTargetType::MOB;
    fireball.castTime = 2.5f;
    fireball.range = 15.0f;
    fireball.damage = 35;
    fireball.resourceType = ResourceType::MANA;
    fireball.resourceCost = 25;
    fireball.cooldownMs = 3000;
    fireball.animationName = "cast_fireball";
    fireball.animationDuration = 2.5f;
    fireball.canBeInterrupted = true;
    actionDefinitions_[2] = fireball;
}

void
CombatEventHandler::handlePlayerAttack(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        // Extract attack request data from event
        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json attackRequest = std::get<nlohmann::json>(data);

            // Get attacker character data
            auto attackerData = gameServices_.getCharacterManager().getCharacterData(clientID);
            if (attackerData.characterId == 0)
            {
                sendErrorResponse(clientSocket, "Character not found!", "playerAttack", clientID);
                return;
            }

            // Determine action to use (from request or AI selection)
            int actionId = 1; // Default to basic attack
            if (attackRequest.contains("actionId"))
            {
                actionId = attackRequest["actionId"];
            }

            AttackAction *attackAction = attackSystem_->getAction(actionId);
            if (!attackAction)
            {
                sendErrorResponse(clientSocket, "Invalid action ID!", "playerAttack", clientID);
                return;
            }

            // Check if can execute action
            if (!attackSystem_->canExecuteAction(attackerData, *attackAction))
            {
                sendErrorResponse(clientSocket, "Cannot execute action - insufficient resources or on cooldown!", "playerAttack", clientID);
                return;
            }

            // Get available targets
            std::vector<TargetCandidate> targets = getAvailableTargets(clientID, attackAction->targetCriteria);

            if (targets.empty())
            {
                sendErrorResponse(clientSocket, "No valid targets available!", "playerAttack", clientID);
                return;
            }

            // Select target (either from request or use AI)
            TargetCandidate *selectedTarget = nullptr;
            if (attackRequest.contains("targetId"))
            {
                int requestedTargetId = attackRequest["targetId"];
                for (auto &target : targets)
                {
                    if (target.targetId == requestedTargetId)
                    {
                        selectedTarget = &target;
                        break;
                    }
                }
                if (!selectedTarget)
                {
                    sendErrorResponse(clientSocket, "Requested target is not valid!", "playerAttack", clientID);
                    return;
                }
            }
            else
            {
                // Use AI to select best target
                AttackStrategy *strategy = attackSystem_->getActiveStrategy(clientID);
                if (strategy)
                {
                    selectedTarget = attackSystem_->selectBestTarget(targets, strategy->targetStrategy, *strategy);
                }
                else
                {
                    selectedTarget = attackSystem_->selectBestTarget(targets, TargetSelectionStrategy::NEAREST, AttackStrategy{});
                }
            }

            if (!selectedTarget)
            {
                sendErrorResponse(clientSocket, "Could not select valid target!", "playerAttack", clientID);
                return;
            }

            // Create combat action
            CombatActionStruct combatAction = attackSystem_->createAttackAction(clientID, *attackAction, *selectedTarget);

            // Calculate damage
            if (selectedTarget->data)
            {
                combatAction.damage = attackSystem_->calculateDamage(*attackAction, attackerData, *selectedTarget->data);
            }
            else
            {
                combatAction.damage = attackAction->baseDamage;
            }

            // Initiate the combat action using existing logic
            EventData eventData = combatAction;
            Event combatEvent(Event::INITIATE_COMBAT_ACTION, clientID, eventData);
            handleInitiateCombatAction(combatEvent);

            Logger logger;
            logger.log("Player " + std::to_string(clientID) + " initiated attack '" + attackAction->name + "' on target " + std::to_string(selectedTarget->targetId));
        }
    }
    catch (const std::exception &e)
    {
        sendErrorResponse(clientSocket, "Error processing player attack: " + std::string(e.what()), "playerAttack", clientID);
    }
}

void
CombatEventHandler::handleAIAttack(int characterId)
{
    try
    {
        // Get character data (assuming this is an NPC/mob)
        auto characterData = gameServices_.getCharacterManager().getCharacterData(characterId);
        if (characterData.characterId == 0)
        {
            Logger logger;
            logger.log("Character " + std::to_string(characterId) + " not found for AI attack");
            return;
        }

        // Get available targets
        TargetCriteria criteria;
        criteria.maxRange = 10.0f;
        criteria.canTargetAllies = false;
        criteria.requiresLineOfSight = true;

        std::vector<TargetCandidate> targets = getAvailableTargets(characterId, criteria);

        if (targets.empty())
        {
            return; // No targets available
        }

        // Use AI to select best action
        AttackAction *bestAction = attackSystem_->selectBestAction(characterId, characterData, targets);
        if (!bestAction)
        {
            return; // No suitable action found
        }

        // Select target using AI
        AttackStrategy *strategy = attackSystem_->getActiveStrategy(characterId);
        TargetSelectionStrategy targetStrategy = strategy ? strategy->targetStrategy : TargetSelectionStrategy::AI_TACTICAL;
        AttackStrategy defaultStrategy{};
        TargetCandidate *selectedTarget = attackSystem_->selectBestTarget(targets, targetStrategy, strategy ? *strategy : defaultStrategy);

        if (!selectedTarget)
        {
            return; // No suitable target found
        }

        // Create and execute combat action
        CombatActionStruct combatAction = attackSystem_->createAttackAction(characterId, *bestAction, *selectedTarget);
        combatAction.damage = attackSystem_->calculateDamage(*bestAction, characterData, *selectedTarget->data);

        // Execute the action directly (no client socket for AI)
        auto actionPtr = std::make_shared<CombatActionStruct>(combatAction);
        ongoingActions_[characterId] = actionPtr;

        // Broadcast animation to all clients
        CombatAnimationStruct animation;
        animation.characterId = characterId;
        animation.animationName = combatAction.animationName;
        animation.duration = combatAction.animationDuration;
        animation.position = characterData.characterPosition;
        animation.targetPosition = selectedTarget->position;

        nlohmann::json animationResponse = ResponseBuilder()
                                               .setHeader("message", "AI combat animation started!")
                                               .setHeader("characterId", characterId)
                                               .setHeader("eventType", "combatAnimation")
                                               .setBody("animation", combatAnimationToJson(animation))
                                               .build();

        std::string animationData = networkManager_.generateResponseMessage("success", animationResponse);
        broadcastToAllClients(animationData);

        Logger logger;
        logger.log("AI character " + std::to_string(characterId) + " initiated attack '" + bestAction->name + "' on target " + std::to_string(selectedTarget->targetId));

        // Update AI state
        attackSystem_->updateAI(characterId, characterData);
    }
    catch (const std::exception &e)
    {
        Logger logger;
        logger.log("Error in AI attack for character " + std::to_string(characterId) + ": " + e.what());
    }
}

std::vector<TargetCandidate>
CombatEventHandler::getAvailableTargets(int attackerId, const TargetCriteria &criteria)
{
    std::vector<CharacterDataStruct> allCharacters;

    // Get attacker position
    auto attackerData = gameServices_.getCharacterManager().getCharacterData(attackerId);
    if (attackerData.characterId == 0)
    {
        return std::vector<TargetCandidate>();
    }

    // For now, return empty target list as we need proper character discovery
    // TODO: Implement proper character discovery system that returns all characters in range

    return attackSystem_->findPotentialTargets(attackerId, attackerData.characterPosition, criteria, allCharacters);
}
