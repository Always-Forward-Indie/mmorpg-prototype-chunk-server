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

            // Get client data to get character ID
            auto clientData = gameServices_.getClientManager().getClientData(clientID);
            action.casterId = clientData.characterId; // Ensure caster ID matches character ID

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
        else if (std::holds_alternative<CombatActionPacket>(data))
        {
            // Handle simplified combat action packet (usually from mobs)
            CombatActionPacket actionPacket = std::get<CombatActionPacket>(data);

            // For simplified packets, just broadcast the initiation without full processing
            nlohmann::json actionResponse = ResponseBuilder()
                                                .setHeader("message", "Combat action initiated!")
                                                .setHeader("clientId", clientID)
                                                .setHeader("eventType", "initiateCombatAction")
                                                .setBody("action", combatActionPacketToJson(actionPacket))
                                                .build();

            std::string responseData = networkManager_.generateResponseMessage("success", actionResponse);
            broadcastToAllClients(responseData);

            gameServices_.getLogger().log("[COMBAT] Simplified combat action broadcasted - " +
                                          actionPacket.actionName + " from " + std::to_string(actionPacket.casterId) +
                                          " to " + std::to_string(actionPacket.targetId));
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
        else if (std::holds_alternative<CombatAnimationPacket>(data))
        {
            // Handle simplified combat animation packet (usually from mobs)
            CombatAnimationPacket animationPacket = std::get<CombatAnimationPacket>(data);

            // Broadcast animation to all clients
            nlohmann::json animationResponse = ResponseBuilder()
                                                   .setHeader("message", "Combat animation sync!")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "combatAnimation")
                                                   .setBody("animation", combatAnimationPacketToJson(animationPacket))
                                                   .build();

            std::string responseData = networkManager_.generateResponseMessage("success", animationResponse);
            broadcastToAllClients(responseData);

            gameServices_.getLogger().log("[COMBAT] Simplified animation broadcasted - " +
                                          animationPacket.animationName + " from character " +
                                          std::to_string(animationPacket.characterId));
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
    gameServices_.getLogger().log("validateCombatAction: Starting validation for action ID " + std::to_string(action.actionId), GREEN);

    // Validate target
    if (action.targetType != CombatTargetType::SELF && action.targetType != CombatTargetType::NONE)
    {
        gameServices_.getLogger().log("validateCombatAction: Validating target " + std::to_string(action.targetId) + " of type " + std::to_string(static_cast<int>(action.targetType)), GREEN);

        if (!validateTarget(action.casterId, action.targetId, action.targetType, action.range, action.requiresLineOfSight))
        {
            gameServices_.getLogger().logError("validateCombatAction: Target validation failed", RED);
            return false;
        }
        gameServices_.getLogger().log("validateCombatAction: Target validation passed", GREEN);
    }

    // Validate resources
    gameServices_.getLogger().log("validateCombatAction: Validating resources - type: " + std::to_string(static_cast<int>(action.resourceType)) + ", cost: " + std::to_string(action.resourceCost), GREEN);
    if (!validateResources(action.casterId, action.resourceType, action.resourceCost))
    {
        gameServices_.getLogger().logError("validateCombatAction: Resource validation failed", RED);
        return false;
    }
    gameServices_.getLogger().log("validateCombatAction: Resource validation passed", GREEN);

    // Check if caster is already performing an action
    if (ongoingActions_.find(action.casterId) != ongoingActions_.end())
    {
        gameServices_.getLogger().logError("validateCombatAction: Caster is already performing an action", RED);
        return false;
    }
    gameServices_.getLogger().log("validateCombatAction: No ongoing action found for caster", GREEN);

    gameServices_.getLogger().log("validateCombatAction: All validation checks passed", GREEN);
    return true;
}

bool
CombatEventHandler::validateTarget(int casterId, int targetId, CombatTargetType targetType, float range, bool requiresLOS)
{
    gameServices_.getLogger().log("validateTarget: Checking target " + std::to_string(targetId) + " for caster " + std::to_string(casterId), GREEN);

    if (targetId == 0)
    {
        gameServices_.getLogger().logError("validateTarget: Target ID is 0", RED);
        return false;
    }

    // Check distance based on target type
    float distance = getDistanceToTarget(casterId, targetId, targetType);
    gameServices_.getLogger().log("validateTarget: Distance to target: " + std::to_string(distance) + ", max range: " + std::to_string(range), GREEN);

    if (distance > range)
    {
        gameServices_.getLogger().logError("validateTarget: Target is out of range", RED);
        return false;
    }

    // TODO: Implement line of sight check if required
    if (requiresLOS)
    {
        gameServices_.getLogger().log("validateTarget: Line of sight check required (not implemented)", YELLOW);
        // Line of sight validation logic
    }

    gameServices_.getLogger().log("validateTarget: Target validation passed", GREEN);
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

        // Log positions for debugging
        gameServices_.getLogger().log("Caster position: (" + std::to_string(casterData.characterPosition.positionX) + ", " +
                                          std::to_string(casterData.characterPosition.positionY) + ", " +
                                          std::to_string(casterData.characterPosition.positionZ) + ")",
            GREEN);
        gameServices_.getLogger().log("Target position: (" + std::to_string(targetPosition.positionX) + ", " +
                                          std::to_string(targetPosition.positionY) + ", " +
                                          std::to_string(targetPosition.positionZ) + ")",
            GREEN);

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

        // Handle mob aggro if player attacked a mob and damage was actually applied
        if (actionPtr->targetType == CombatTargetType::MOB && actualDamage > 0)
        {
            // First check if the mob is still alive before setting aggro
            bool mobIsAlive = gameServices_.getMobInstanceManager().isMobAlive(actionPtr->targetId);
            if (mobIsAlive)
            {
                // Get the attacker's character ID to trigger mob aggro
                try
                {
                    auto attackerData = gameServices_.getCharacterManager().getCharacterData(actionPtr->casterId);
                    if (attackerData.characterId > 0)
                    {
                        // Trigger mob aggro on the attacker
                        gameServices_.getMobMovementManager().handleMobAttacked(actionPtr->targetId, attackerData.characterId);
                        gameServices_.getLogger().log("[AGGRO] Mob " + std::to_string(actionPtr->targetId) +
                                                          " now targets player " + std::to_string(attackerData.characterId),
                            YELLOW);
                    }
                }
                catch (const std::exception &e)
                {
                    gameServices_.getLogger().logError("Error triggering mob aggro: " + std::string(e.what()), RED);
                }
            }
            else
            {
                gameServices_.getLogger().log("[COMBAT] Mob " + std::to_string(actionPtr->targetId) + " died, skipping aggro", GREEN);
            }
        }

        // Update remaining health after damage application
        try
        {
            if (actionPtr->targetType == CombatTargetType::MOB)
            {
                auto mobData = gameServices_.getMobInstanceManager().getMobInstance(actionPtr->targetId);
                result.remainingHealth = mobData.currentHealth;
                result.remainingMana = mobData.currentMana;
            }
            else if (actionPtr->targetType == CombatTargetType::PLAYER)
            {
                // Get updated health from CharacterManager after damage was applied
                auto targetData = gameServices_.getCharacterManager().getCharacterData(actionPtr->targetId);
                result.remainingHealth = targetData.characterCurrentHealth;
                result.remainingMana = targetData.characterCurrentMana;

                gameServices_.getLogger().log("Player remaining health from updated manager: " + std::to_string(result.remainingHealth), GREEN);
            }

            if (result.remainingHealth <= 0)
            {
                result.targetDied = true;
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error updating combat result after damage: " + std::string(e.what()), RED);
        }
    }

    if (result.healingDone > 0)
    {
        int actualHealing = applyHealingToTarget(actionPtr->targetId, actionPtr->targetType, result.healingDone);
        result.healingDone = actualHealing;
    }

    // Set cooldown
    setCooldown(actionPtr->casterId, actionPtr->actionId, actionPtr->cooldownMs);

    actionPtr->state = CombatActionState::COMPLETED;

    gameServices_.getLogger().log("Combat action completed - Final damage: " + std::to_string(result.damageDealt) +
                                      ", Final remaining health: " + std::to_string(result.remainingHealth),
        GREEN);

    // Broadcast result
    nlohmann::json resultResponse = ResponseBuilder()
                                        .setHeader("message", "Combat action completed!")
                                        .setHeader("characterId", actionPtr->casterId)
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
    result.targetType = action.targetType;
    result.damageDealt = action.damage;
    result.healingDone = action.healing;
    result.isCritical = false; // TODO: Implement crit calculation
    result.isBlocked = false;  // TODO: Implement block calculation
    result.isDodged = false;   // TODO: Implement dodge calculation
    result.isResisted = false; // TODO: Implement resistance calculation
    result.targetDied = false;

    // Get current target health and mana for result
    try
    {
        if (result.damageDealt > 0)
        {
            result.isDamaged = true;
        }
        else
        {
            result.isDamaged = false;
        }

        if (action.targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
            result.remainingHealth = mobData.currentHealth - result.damageDealt;
            result.remainingMana = mobData.currentMana;

            if (result.remainingHealth <= 0)
            {
                result.remainingHealth = 0;
                result.targetDied = true;
            }
        }
        else if (action.targetType == CombatTargetType::PLAYER)
        {
            auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
            result.remainingHealth = targetData.characterCurrentHealth - result.damageDealt;
            result.remainingMana = targetData.characterCurrentMana;

            if (result.remainingHealth <= 0)
            {
                result.remainingHealth = 0;
                result.targetDied = true;
            }
        }
        else
        {
            // Default values for other target types
            result.remainingHealth = 100;
            result.remainingMana = 50;
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting target stats for combat result: " + std::string(e.what()), RED);
        result.remainingHealth = 0;
        result.remainingMana = 0;
    }

    gameServices_.getLogger().log("Combat result calculated - Damage: " + std::to_string(result.damageDealt) +
                                      ", Remaining Health: " + std::to_string(result.remainingHealth) +
                                      ", Remaining Mana: " + std::to_string(result.remainingMana),
        GREEN);

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
            gameServices_.getLogger().log("applyDamageToTarget: Player " + std::to_string(targetId) + " health before damage: " + std::to_string(targetData.characterCurrentHealth), GREEN);

            int actualDamage = std::min(damage, targetData.characterCurrentHealth);
            int newHealth = targetData.characterCurrentHealth - actualDamage;

            gameServices_.getLogger().log("applyDamageToTarget: Applied " + std::to_string(actualDamage) + " damage, health after: " + std::to_string(newHealth), GREEN);

            // Update character health in manager
            gameServices_.getCharacterManager().updateCharacterHealth(targetId, newHealth);

            return actualDamage;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
            int actualDamage = std::min(damage, mobData.currentHealth);

            // Update mob health using MobInstanceManager and get result
            auto updateResult = gameServices_.getMobInstanceManager().updateMobHealth(targetId, mobData.currentHealth - actualDamage);

            // Return early if update failed or mob was already dead
            if (!updateResult.success)
            {
                gameServices_.getLogger().logError("Failed to update mob health for UID: " + std::to_string(targetId));
                return 0;
            }

            if (updateResult.wasAlreadyDead)
            {
                gameServices_.getLogger().log("Attempted to damage already dead mob " + std::to_string(targetId) + ", ignoring", YELLOW);
                return 0;
            }

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
        int newHealth = targetData.characterCurrentHealth + actualHealing;

        // Update character data
        gameServices_.getCharacterManager().updateCharacterHealth(targetId, newHealth);

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
            int newHealth = targetData.characterCurrentHealth + actualHealing;

            // Update character data
            gameServices_.getCharacterManager().updateCharacterHealth(targetId, newHealth);

            return actualHealing;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
            int maxHealing = mobData.maxHealth - mobData.currentHealth;
            int actualHealing = std::min(healing, maxHealing);

            // Update mob health using MobInstanceManager and get result
            auto updateResult = gameServices_.getMobInstanceManager().updateMobHealth(targetId, mobData.currentHealth + actualHealing);

            if (!updateResult.success)
            {
                gameServices_.getLogger().logError("Failed to update mob health for healing: " + std::to_string(targetId));
                return 0;
            }

            if (updateResult.wasAlreadyDead)
            {
                gameServices_.getLogger().log("Attempted to heal dead mob " + std::to_string(targetId) + ", ignoring", YELLOW);
                return 0;
            }

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
    std::string targetTypeStr = "UNKNOWN";
    switch (action.targetType)
    {
    case CombatTargetType::PLAYER:
        targetTypeStr = "PLAYER";
        break;
    case CombatTargetType::MOB:
        targetTypeStr = "MOB";
        break;
    case CombatTargetType::AREA:
        targetTypeStr = "AREA";
        break;
    case CombatTargetType::SELF:
        targetTypeStr = "SELF";
        break;
    case CombatTargetType::NONE:
        targetTypeStr = "NONE";
        break;
    }

    return nlohmann::json{
        {"actionId", action.actionId},
        {"actionName", action.actionName},
        {"actionType", static_cast<int>(action.actionType)},
        {"targetType", static_cast<int>(action.targetType)},
        {"targetTypeString", targetTypeStr}, // Human-readable target type
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
    std::string targetTypeStr = "UNKNOWN";
    switch (result.targetType)
    {
    case CombatTargetType::PLAYER:
        targetTypeStr = "PLAYER";
        break;
    case CombatTargetType::MOB:
        targetTypeStr = "MOB";
        break;
    case CombatTargetType::AREA:
        targetTypeStr = "AREA";
        break;
    case CombatTargetType::SELF:
        targetTypeStr = "SELF";
        break;
    case CombatTargetType::NONE:
        targetTypeStr = "NONE";
        break;
    }

    return nlohmann::json{
        {"casterId", result.casterId},
        {"targetId", result.targetId},
        {"targetType", static_cast<int>(result.targetType)},
        {"targetTypeString", targetTypeStr},
        {"actionId", result.actionId},
        {"damageDealt", result.damageDealt},
        {"healingDone", result.healingDone},
        {"isResisted", result.isResisted},
        {"isCritical", result.isCritical},
        {"isBlocked", result.isBlocked},
        {"isDodged", result.isDodged},
        {"remainingHealth", result.remainingHealth},
        {"remainingMana", result.remainingMana},
        {"isDamaged", result.isDamaged},
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

nlohmann::json
CombatEventHandler::combatActionPacketToJson(const CombatActionPacket &actionPacket)
{
    std::string targetTypeStr = "UNKNOWN";
    switch (actionPacket.targetType)
    {
    case CombatTargetType::PLAYER:
        targetTypeStr = "PLAYER";
        break;
    case CombatTargetType::MOB:
        targetTypeStr = "MOB";
        break;
    case CombatTargetType::AREA:
        targetTypeStr = "AREA";
        break;
    case CombatTargetType::SELF:
        targetTypeStr = "SELF";
        break;
    case CombatTargetType::NONE:
        targetTypeStr = "NONE";
        break;
    }

    return nlohmann::json{
        {"actionId", actionPacket.actionId},
        {"actionName", actionPacket.actionName},
        {"actionType", static_cast<int>(actionPacket.actionType)},
        {"targetType", static_cast<int>(actionPacket.targetType)},
        {"targetTypeString", targetTypeStr},
        {"casterId", actionPacket.casterId},
        {"targetId", actionPacket.targetId}};
}

nlohmann::json
CombatEventHandler::combatAnimationPacketToJson(const CombatAnimationPacket &animationPacket)
{
    return nlohmann::json{
        {"characterId", animationPacket.characterId},
        {"animationName", animationPacket.animationName},
        {"duration", animationPacket.duration},
        {"isLooping", animationPacket.isLooping}};
}

void
CombatEventHandler::initializeActionDefinitions()
{
    // Example action definitions - these would typically be loaded from database/config
    gameServices_.getLogger().log("Initializing action definitions...", GREEN);

    CombatActionStruct basicAttack;
    basicAttack.actionId = 1;
    basicAttack.actionName = "Basic Attack";
    basicAttack.actionType = CombatActionType::BASIC_ATTACK;
    basicAttack.targetType = CombatTargetType::MOB; // This will be overridden dynamically
    basicAttack.castTime = 0.0f;
    basicAttack.range = 150.0f; // Increased range for testing
    basicAttack.damage = 10;
    basicAttack.resourceType = ResourceType::NONE;
    basicAttack.resourceCost = 0;
    basicAttack.cooldownMs = 1000;
    basicAttack.animationName = "attack_basic";
    basicAttack.animationDuration = 1.0f;
    basicAttack.canBeInterrupted = false;
    actionDefinitions_[1] = basicAttack;
    gameServices_.getLogger().log("Added action definition: " + basicAttack.actionName + " (ID: " + std::to_string(basicAttack.actionId) + ") - can target players and mobs", GREEN);

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
    gameServices_.getLogger().log("Added action definition: " + fireball.actionName + " (ID: " + std::to_string(fireball.actionId) + ")", GREEN);

    gameServices_.getLogger().log("Action definitions initialized. Total actions: " + std::to_string(actionDefinitions_.size()), GREEN);
}

void
CombatEventHandler::handlePlayerAttack(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    // Add logging to debug the issue
    gameServices_.getLogger().log("handlePlayerAttack called for client ID: " + std::to_string(clientID), GREEN);

    // Log the type of data we received
    if (std::holds_alternative<nlohmann::json>(data))
    {
        gameServices_.getLogger().log("Received JSON data in handlePlayerAttack", GREEN);
    }
    else if (std::holds_alternative<std::string>(data))
    {
        gameServices_.getLogger().log("Received string data in handlePlayerAttack", YELLOW);
    }
    else if (std::holds_alternative<int>(data))
    {
        gameServices_.getLogger().log("Received int data in handlePlayerAttack", YELLOW);
    }
    else
    {
        gameServices_.getLogger().log("Received unknown data type in handlePlayerAttack", RED);
    }

    try
    {
        // Extract attack request data from event
        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json attackRequest = std::get<nlohmann::json>(data);
            gameServices_.getLogger().log("Attack request JSON: " + attackRequest.dump(), GREEN);

            // get client data
            auto clientData = gameServices_.getClientManager().getClientData(clientID);
            // Get attacker character data
            auto attackerData = gameServices_.getCharacterManager().getCharacterData(clientData.characterId);
            gameServices_.getLogger().log("Got character data for client ID: " + std::to_string(clientID) + ", character ID: " + std::to_string(attackerData.characterId), GREEN);
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

            gameServices_.getLogger().log("Looking for action ID: " + std::to_string(actionId), GREEN);

            // First check our local action definitions
            auto actionIt = actionDefinitions_.find(actionId);
            if (actionIt == actionDefinitions_.end())
            {
                gameServices_.getLogger().logError("Attack action not found for ID: " + std::to_string(actionId), RED);
                sendErrorResponse(clientSocket, "Invalid action ID!", "playerAttack", clientID);
                return;
            }

            CombatActionStruct actionDef = actionIt->second;
            gameServices_.getLogger().log("Found attack action: " + actionDef.actionName, GREEN);

            // Check if can execute action (simplified for now)
            // if (!attackSystem_->canExecuteAction(attackerData, *attackAction))
            // {
            //     sendErrorResponse(clientSocket, "Cannot execute action - insufficient resources or on cooldown!", "playerAttack", clientID);
            //     return;
            // }

            // For now, just check basic cooldown
            if (isOnCooldown(clientData.characterId, actionId))
            {
                sendErrorResponse(clientSocket, "Action is on cooldown!", "playerAttack", clientID);
                return;
            }

            // Get target ID and type from request
            int targetId = 0;
            CombatTargetType targetType = CombatTargetType::MOB; // Default

            if (attackRequest.contains("targetId"))
            {
                targetId = attackRequest["targetId"];
                gameServices_.getLogger().log("Target ID from request: " + std::to_string(targetId), GREEN);
            }
            else
            {
                sendErrorResponse(clientSocket, "No target specified!", "playerAttack", clientID);
                return;
            }

            // Get target type from request
            if (attackRequest.contains("targetType"))
            {
                std::string targetTypeStr = attackRequest["targetType"];
                if (targetTypeStr == "Player")
                {
                    targetType = CombatTargetType::PLAYER;
                    gameServices_.getLogger().log("Target type from request: PLAYER", GREEN);
                }
                else if (targetTypeStr == "MOB")
                {
                    targetType = CombatTargetType::MOB;
                    gameServices_.getLogger().log("Target type from request: MOB", GREEN);
                }
                else
                {
                    gameServices_.getLogger().logError("Unknown target type: " + targetTypeStr + ", defaulting to MOB", YELLOW);
                    targetType = CombatTargetType::MOB;
                }
            }
            else
            {
                // Fallback to old logic for backward compatibility
                if (targetId < 1000000)
                {
                    targetType = CombatTargetType::PLAYER;
                    gameServices_.getLogger().log("Target type inferred as PLAYER (ID < 1000000)", YELLOW);
                }
                else
                {
                    targetType = CombatTargetType::MOB;
                    gameServices_.getLogger().log("Target type inferred as MOB (ID >= 1000000)", YELLOW);
                }
            }

            // Create combat action using our action definition
            CombatActionStruct combatAction;
            combatAction.actionId = actionDef.actionId;
            combatAction.actionName = actionDef.actionName;
            combatAction.actionType = actionDef.actionType;
            combatAction.targetType = targetType;           // Use the type from request
            combatAction.casterId = clientData.characterId; // Use character ID, not client ID
            combatAction.targetId = targetId;
            combatAction.castTime = actionDef.castTime;
            combatAction.range = actionDef.range;
            combatAction.damage = actionDef.damage;
            combatAction.healing = 0; // No healing for basic attack
            combatAction.resourceType = actionDef.resourceType;
            combatAction.resourceCost = actionDef.resourceCost;
            combatAction.cooldownMs = actionDef.cooldownMs;
            combatAction.animationName = actionDef.animationName;
            combatAction.animationDuration = actionDef.animationDuration;
            combatAction.canBeInterrupted = actionDef.canBeInterrupted;
            combatAction.requiresLineOfSight = true;

            // Set target position based on target type
            if (combatAction.targetType == CombatTargetType::MOB)
            {
                try
                {
                    auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
                    combatAction.targetPosition = mobData.position;
                    gameServices_.getLogger().log("Set target position for mob " + std::to_string(targetId) + ": (" +
                                                      std::to_string(mobData.position.positionX) + ", " +
                                                      std::to_string(mobData.position.positionY) + ", " +
                                                      std::to_string(mobData.position.positionZ) + ")",
                        GREEN);
                }
                catch (const std::exception &e)
                {
                    gameServices_.getLogger().logError("Failed to get mob position for target " + std::to_string(targetId) + ": " + e.what(), RED);
                    sendErrorResponse(clientSocket, "Target mob not found!", "playerAttack", clientID);
                    return;
                }
            }
            else if (combatAction.targetType == CombatTargetType::PLAYER)
            {
                try
                {
                    auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
                    if (targetData.characterId == 0)
                    {
                        gameServices_.getLogger().logError("Target player " + std::to_string(targetId) + " not found", RED);
                        sendErrorResponse(clientSocket, "Target player not found!", "playerAttack", clientID);
                        return;
                    }
                    combatAction.targetPosition = targetData.characterPosition;
                    gameServices_.getLogger().log("Set target position for player " + std::to_string(targetId) + ": (" +
                                                      std::to_string(targetData.characterPosition.positionX) + ", " +
                                                      std::to_string(targetData.characterPosition.positionY) + ", " +
                                                      std::to_string(targetData.characterPosition.positionZ) + ")",
                        GREEN);
                }
                catch (const std::exception &e)
                {
                    gameServices_.getLogger().logError("Failed to get player position for target " + std::to_string(targetId) + ": " + e.what(), RED);
                    sendErrorResponse(clientSocket, "Target player not found!", "playerAttack", clientID);
                    return;
                }
            }

            gameServices_.getLogger().log("Created combat action for target: " + std::to_string(targetId), GREEN);

            // Instead of using the full combat action system, send simplified packets for players too

            // 1. Send simplified combat action packet to notify combat start
            CombatActionPacket actionPacket;
            actionPacket.actionId = actionDef.actionId;
            actionPacket.actionName = actionDef.actionName;
            actionPacket.actionType = actionDef.actionType;
            actionPacket.targetType = targetType;
            actionPacket.casterId = clientData.characterId;
            actionPacket.targetId = targetId;

            nlohmann::json actionResponse = ResponseBuilder()
                                                .setHeader("message", "Combat action initiated!")
                                                .setHeader("clientId", clientID)
                                                .setHeader("eventType", "initiateCombatAction")
                                                .setBody("action", combatActionPacketToJson(actionPacket))
                                                .build();

            std::string actionResponseData = networkManager_.generateResponseMessage("success", actionResponse);
            broadcastToAllClients(actionResponseData);

            // 2. Send simplified combat animation packet
            CombatAnimationPacket animationPacket;
            animationPacket.characterId = clientData.characterId;
            animationPacket.animationName = actionDef.animationName;
            animationPacket.duration = actionDef.animationDuration;
            animationPacket.isLooping = false;

            nlohmann::json animationResponse = ResponseBuilder()
                                                   .setHeader("message", "Combat animation started!")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "combatAnimation")
                                                   .setBody("animation", combatAnimationPacketToJson(animationPacket))
                                                   .build();

            std::string animationResponseData = networkManager_.generateResponseMessage("success", animationResponse);
            broadcastToAllClients(animationResponseData);

            // 3. For instant actions (like basic attack), immediately calculate and apply damage
            if (actionDef.castTime <= 0)
            {
                // Validate the action first
                if (!validateTarget(clientData.characterId, targetId, targetType, actionDef.range, true))
                {
                    sendErrorResponse(clientSocket, "Target validation failed!", "playerAttack", clientID);
                    return;
                }

                // Check cooldown
                if (isOnCooldown(clientData.characterId, actionDef.actionId))
                {
                    sendErrorResponse(clientSocket, "Action is on cooldown!", "playerAttack", clientID);
                    return;
                }

                // Set cooldown
                setCooldown(clientData.characterId, actionDef.actionId, actionDef.cooldownMs);

                // Calculate and apply damage
                CombatResultStruct result;
                result.casterId = clientData.characterId;
                result.targetId = targetId;
                result.actionId = actionDef.actionId;
                result.targetType = targetType;
                result.damageDealt = actionDef.damage;
                result.healingDone = 0;
                result.isCritical = false;
                result.isBlocked = false;
                result.isDodged = false;
                result.isResisted = false;
                result.isDamaged = (result.damageDealt > 0);
                result.targetDied = false;

                // Apply damage and get actual damage dealt
                int actualDamage = applyDamageToTarget(targetId, targetType, result.damageDealt);
                result.damageDealt = actualDamage;

                // Get remaining health and mana after damage
                try
                {
                    if (targetType == CombatTargetType::MOB)
                    {
                        auto mobData = gameServices_.getMobInstanceManager().getMobInstance(targetId);
                        result.remainingHealth = mobData.currentHealth;
                        result.remainingMana = mobData.currentMana;

                        // Handle mob aggro if damage was dealt
                        if (actualDamage > 0)
                        {
                            gameServices_.getMobMovementManager().handleMobAttacked(targetId, clientData.characterId);
                        }
                    }
                    else if (targetType == CombatTargetType::PLAYER)
                    {
                        auto targetData = gameServices_.getCharacterManager().getCharacterData(targetId);
                        result.remainingHealth = targetData.characterCurrentHealth;
                        result.remainingMana = targetData.characterCurrentMana;
                    }

                    if (result.remainingHealth <= 0)
                    {
                        result.targetDied = true;
                    }
                }
                catch (const std::exception &e)
                {
                    gameServices_.getLogger().logError("Error getting target stats after damage: " + std::string(e.what()));
                }

                // Send combat result
                nlohmann::json resultResponse = ResponseBuilder()
                                                    .setHeader("message", "Combat action completed!")
                                                    .setHeader("clientId", clientID)
                                                    .setHeader("eventType", "combatResult")
                                                    .setBody("result", combatResultToJson(result))
                                                    .build();

                std::string resultResponseData = networkManager_.generateResponseMessage("success", resultResponse);
                broadcastToAllClients(resultResponseData);

                gameServices_.getLogger().log("[COMBAT] Player attack completed - Damage: " + std::to_string(actualDamage) +
                                                  ", Target health: " + std::to_string(result.remainingHealth),
                    GREEN);
            }

            Logger logger;
            logger.log("Player (client " + std::to_string(clientID) + ", character " + std::to_string(clientData.characterId) + ") initiated attack '" + actionDef.actionName + "' on target " + std::to_string(targetId));
        }
        else
        {
            gameServices_.getLogger().logError("handlePlayerAttack: Event data is not JSON type!", RED);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing player attack: " + std::string(e.what()), RED);
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
