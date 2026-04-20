#include "events/handlers/CombatEventHandler.hpp"
#include "network/NetworkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/CombatSystem.hpp"
#include "services/GameServices.hpp"
#include "services/SkillSystem.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

CombatEventHandler::CombatEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "combat")
{
    log_ = gameServices_.getLogger().getSystem("combat");
    combatSystem_ = std::make_unique<CombatSystem>(&gameServices);
    skillSystem_ = std::make_unique<SkillSystem>(&gameServices);
    responseBuilder_ = std::make_unique<CombatResponseBuilder>(&gameServices);

    // Устанавливаем callback для отправки broadcast пакетов
    combatSystem_->setBroadcastCallback([this](const nlohmann::json &packet)
        { this->sendBroadcast(packet); });

    log_->info("CombatEventHandler initialized with new refactored architecture");
}

CombatEventHandler::~CombatEventHandler() = default;

CombatSystem *
CombatEventHandler::getCombatSystem() const
{
    return combatSystem_.get();
}

void
CombatEventHandler::sendBroadcast(const nlohmann::json &packet)
{
    try
    {
        std::string messageType = "success"; // По умолчанию success для AI атак
        std::string responseData = networkManager_.generateResponseMessage(messageType, packet);
        broadcastToAllClients(responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error sending broadcast: " + std::string(ex.what()));
    }
}

bool
CombatEventHandler::checkRateLimit(int clientId)
{
    // ARCH-2/3: reject requests arriving faster than COMBAT_RATE_LIMIT_MS from the same client
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(rateLimitMutex_);
    auto it = lastCombatRequest_.find(clientId);
    if (it != lastCombatRequest_.end())
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        if (elapsed < COMBAT_RATE_LIMIT_MS)
        {
            gameServices_.getLogger().log("Rate limit hit for client " + std::to_string(clientId) +
                                              " (elapsed=" + std::to_string(elapsed) + "ms)",
                YELLOW);
            return false;
        }
    }
    lastCombatRequest_[clientId] = now;
    return true;
}

void
CombatEventHandler::handlePlayerAttack(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    log_->info("handlePlayerAttack called for client ID: " + std::to_string(clientID));

    // Route skillUsage events BEFORE the rate limit check so they don't consume
    // the playerAttack token and then fail the second check inside handleSkillUsage.
    if (std::holds_alternative<nlohmann::json>(data))
    {
        const auto &json = std::get<nlohmann::json>(data);
        if (json.contains("header") && json["header"].contains("eventType") &&
            json["header"]["eventType"].get<std::string>() == "skillUsage")
        {
            handleSkillUsage(event);
            return;
        }
    }

    // ARCH-2: server-side combat rate limiting
    if (!checkRateLimit(clientID))
    {
        auto errorResponse = responseBuilder_->buildErrorResponse("Request too fast, slow down!", "playerAttack", clientID);
        std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
        if (clientSocket)
            networkManager_.sendResponse(clientSocket, errorData);
        return;
    }

    try
    {
        auto clientData = gameServices_.getClientManager().getClientData(clientID);
        if (clientData.characterId == 0)
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Character not found!", "playerAttack", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        if (!std::holds_alternative<nlohmann::json>(data))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Invalid request format!", "playerAttack", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        nlohmann::json requestData = std::get<nlohmann::json>(data);
        std::string skillSlug;
        int targetId;
        CombatTargetType targetType;

        if (!parsePlayerAttackRequest(requestData, skillSlug, targetId, targetType))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Invalid attack parameters!", "playerAttack", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        gameServices_.getLogger().log("Player attack: " + skillSlug + " on target " + std::to_string(targetId) +
                                          " (type: " + std::to_string(static_cast<int>(targetType)) + ")",
            GREEN);

        dispatchSkillAction(clientData.characterId, skillSlug, targetId, targetType, "playerAttack", clientSocket);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handlePlayerAttack: " + std::string(ex.what()));
        auto errorResponse = responseBuilder_->buildErrorResponse("Internal server error!", "playerAttack", clientID);
        std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
        if (clientSocket)
            networkManager_.sendResponse(clientSocket, errorData);
    }
}

void
CombatEventHandler::handleSkillUsage(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    // ARCH-2: server-side combat rate limiting
    if (!checkRateLimit(clientID))
    {
        auto errorResponse = responseBuilder_->buildErrorResponse("Request too fast, slow down!", "skillUsage", clientID);
        std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
        if (clientSocket)
            networkManager_.sendResponse(clientSocket, errorData);
        return;
    }

    try
    {
        auto clientData = gameServices_.getClientManager().getClientData(clientID);
        if (clientData.characterId == 0)
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Character not found!", "skillUsage", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        if (!isPlayerAlive(clientData.characterId))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Cannot use skills while dead", "skillUsage", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        if (!std::holds_alternative<nlohmann::json>(data))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Invalid request format!", "skillUsage", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        nlohmann::json requestData = std::get<nlohmann::json>(data);
        std::string skillSlug;
        int targetId;
        CombatTargetType targetType;

        if (!parseSkillUsageRequest(requestData, skillSlug, targetId, targetType))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Invalid skill parameters!", "skillUsage", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, errorData);
            return;
        }

        gameServices_.getLogger().log("Skill usage: " + skillSlug + " on target " + std::to_string(targetId) +
                                          " (type: " + std::to_string(static_cast<int>(targetType)) + ")",
            GREEN);

        dispatchSkillAction(clientData.characterId, skillSlug, targetId, targetType, "skillUsage", clientSocket);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSkillUsage: " + std::string(ex.what()));
        auto errorResponse = responseBuilder_->buildErrorResponse("Internal server error!", "skillUsage", clientID);
        std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
        if (clientSocket)
            networkManager_.sendResponse(clientSocket, errorData);
    }
}

void
CombatEventHandler::updateOngoingActions()
{
    try
    {
        auto results = combatSystem_->updateOngoingActions();
        combatSystem_->tickEffects();

        // Handle deferred skill results that require server-side follow-up
        // (e.g. teleport_respawn cast-time skills whose hasTeleport flag is set).
        for (const auto &executionResult : results)
        {
            if (executionResult.hasTeleport)
            {
                const PositionStruct &dest = executionResult.teleportPosition;
                int characterId = executionResult.casterId;

                ClientDataStruct clientData =
                    gameServices_.getClientManager().getClientDataByCharacterId(characterId);
                int casterClientId = clientData.clientId;
                auto clientSocket = gameServices_.getClientManager().getClientSocket(casterClientId);

                // 1. Send teleport response to the caster
                {
                    nlohmann::json teleportResp;
                    teleportResp["header"]["eventType"] = "respawnResult";
                    teleportResp["header"]["status"] = "success";
                    teleportResp["header"]["clientId"] = casterClientId;
                    teleportResp["body"]["characterId"] = characterId;
                    teleportResp["body"]["position"] = {
                        {"x", dest.positionX}, {"y", dest.positionY}, {"z", dest.positionZ}, {"rotationZ", dest.rotationZ}};
                    std::string msg = networkManager_.generateResponseMessage("success", teleportResp);
                    if (clientSocket && clientSocket->is_open())
                        networkManager_.sendResponse(clientSocket, msg);
                }

                // 2. Persist position
                {
                    nlohmann::json savePos;
                    savePos["header"]["eventType"] = "savePositions";
                    savePos["header"]["clientId"] = 0;
                    savePos["header"]["hash"] = "";
                    savePos["body"]["characters"] = nlohmann::json::array();
                    nlohmann::json posEntry;
                    posEntry["characterId"] = characterId;
                    posEntry["posX"] = dest.positionX;
                    posEntry["posY"] = dest.positionY;
                    posEntry["posZ"] = dest.positionZ;
                    posEntry["rotZ"] = dest.rotationZ;
                    savePos["body"]["characters"].push_back(posEntry);
                    gameServerWorker_.sendDataToGameServer(savePos.dump() + "\n");
                }

                // 3. Broadcast position update to other players
                {
                    nlohmann::json posBroadcast;
                    posBroadcast["header"]["eventType"] = "characterMoved";
                    posBroadcast["body"]["characterId"] = characterId;
                    posBroadcast["body"]["posX"] = dest.positionX;
                    posBroadcast["body"]["posY"] = dest.positionY;
                    posBroadcast["body"]["posZ"] = dest.positionZ;
                    posBroadcast["body"]["rotZ"] = dest.rotationZ;
                    broadcastToAllClients(posBroadcast.dump() + "\n", casterClientId);
                }

                // Reset server-side movement validation state so the next
                // moveCharacter packet is not rejected as a speed violation.
                gameServices_.getCharacterManager().setLastValidatedMovement(characterId, dest, 0);

                log_->info("[CombatEventHandler] Deferred teleport for char={}: ({},{},{})",
                    characterId,
                    dest.positionX,
                    dest.positionY,
                    dest.positionZ);
            }

            // Persist applied timed effects
            for (const auto &eff : executionResult.appliedEffects)
            {
                nlohmann::json effectPacket;
                effectPacket["header"]["eventType"] = "saveActiveEffect";
                effectPacket["header"]["clientId"] = 0;
                effectPacket["header"]["hash"] = "";
                effectPacket["body"]["characterId"] = executionResult.casterId;
                effectPacket["body"]["effectSlug"] = eff.effectSlug;
                effectPacket["body"]["attributeSlug"] = eff.attributeSlug;
                effectPacket["body"]["sourceType"] = "skill";
                effectPacket["body"]["value"] = static_cast<double>(eff.value);
                effectPacket["body"]["expiresAt"] = eff.expiresAt;
                effectPacket["body"]["tickMs"] = eff.tickMs;
                gameServerWorker_.sendDataToGameServer(effectPacket.dump() + "\n");
            }
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in updateOngoingActions: " + std::string(ex.what()));
    }
}

bool
CombatEventHandler::parsePlayerAttackRequest(const nlohmann::json &requestData,
    std::string &skillSlug,
    int &targetId,
    CombatTargetType &targetType)
{
    try
    {
        if (!requestData.contains("body"))
        {
            return false;
        }

        nlohmann::json body = requestData["body"];

        // Skill slug (по умолчанию basic_attack)
        skillSlug = "basic_attack";
        if (body.contains("skillSlug"))
        {
            skillSlug = body["skillSlug"];
        }

        // Target ID
        if (!body.contains("targetId"))
        {
            return false;
        }
        targetId = body["targetId"];

        // Target type
        if (!body.contains("targetType"))
        {
            return false;
        }
        int targetTypeInt = body["targetType"];
        targetType = static_cast<CombatTargetType>(targetTypeInt);

        // Проверяем валидность типа цели
        if (targetType != CombatTargetType::PLAYER &&
            targetType != CombatTargetType::MOB &&
            targetType != CombatTargetType::SELF)
        {
            return false;
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool
CombatEventHandler::parseSkillUsageRequest(const nlohmann::json &requestData,
    std::string &skillSlug,
    int &targetId,
    CombatTargetType &targetType)
{
    try
    {
        if (!requestData.contains("body"))
        {
            return false;
        }

        nlohmann::json body = requestData["body"];

        // Skill slug (обязательный)
        if (!body.contains("skillSlug"))
        {
            return false;
        }
        skillSlug = body["skillSlug"];

        // Target ID
        if (!body.contains("targetId"))
        {
            return false;
        }
        targetId = body["targetId"];

        // Target type
        if (!body.contains("targetType"))
        {
            return false;
        }
        int targetTypeInt = body["targetType"];
        targetType = static_cast<CombatTargetType>(targetTypeInt);

        // Проверяем валидность типа цели
        if (targetType != CombatTargetType::PLAYER &&
            targetType != CombatTargetType::MOB &&
            targetType != CombatTargetType::SELF &&
            targetType != CombatTargetType::AREA)
        {
            return false;
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

void
CombatEventHandler::broadcastSkillInitiation(const SkillInitiationResult &result)
{
    try
    {
        auto response = responseBuilder_->buildSkillInitiationBroadcast(result);
        std::string messageType = result.success ? "success" : "error";
        std::string responseData = networkManager_.generateResponseMessage(messageType, response);

        // Отправляем всем клиентам
        broadcastToAllClients(responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error broadcasting skill initiation: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::broadcastSkillExecution(const SkillExecutionResult &result)
{
    try
    {
        auto response = responseBuilder_->buildSkillExecutionBroadcast(result);
        std::string messageType = result.success ? "success" : "error";
        std::string responseData = networkManager_.generateResponseMessage(messageType, response);

        // Отправляем всем клиентам
        broadcastToAllClients(responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error broadcasting skill execution: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::dispatchSkillAction(int characterId,
    const std::string &skillSlug,
    int targetId,
    CombatTargetType targetType,
    const std::string &actionTag,
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    // LOW-4: shared initiate + execute logic factored out from handlePlayerAttack / handleSkillUsage
    auto initiationResult = combatSystem_->initiateSkillUsage(characterId, skillSlug, targetId, targetType);
    broadcastSkillInitiation(initiationResult);

    if (!initiationResult.success)
    {
        log_->warn("[dispatchSkillAction] Skill initiation failed for char " + std::to_string(characterId) +
                   " skill='" + skillSlug + "': " + initiationResult.errorMessage);
        return;
    }

    // Skills with castMs > 0 are deferred: ongoingAction stays CASTING until castMs
    // elapses, then updateOngoingActions() fires the result.
    // Skills with castMs == 0 are instant and executed here.
    if (initiationResult.castTime > 0.0f)
    {
        log_->info("[dispatchSkillAction] Skill '" + skillSlug +
                   "' for char " + std::to_string(characterId) +
                   " deferred — cast time " + std::to_string(initiationResult.castTime) + "s");
        return;
    }

    // Instant skill (castMs == 0): execute immediately.
    log_->info("[dispatchSkillAction] Executing instant skill '" + skillSlug +
               "' for char " + std::to_string(characterId) +
               " -> target " + std::to_string(targetId) + "'");

    // cooldownAlreadySet=true: initiateSkillUsage already called trySetCooldown above.
    // Passing false (the default) would cause useSkill to re-check the cooldown, see
    // it as already set, and return "Skill is on cooldown or not available".
    auto executionResult = combatSystem_->executeSkillUsage(characterId, skillSlug, targetId, targetType, /*cooldownAlreadySet=*/true);

    // Clear the ongoingActions_ entry so updateOngoingActions() doesn't fire it again.
    combatSystem_->clearOngoingAction(characterId);

    if (!executionResult.success)
    {
        log_->warn("[dispatchSkillAction] Skill execution failed for char " + std::to_string(characterId) +
                   " skill='" + skillSlug + "': " + executionResult.errorMessage);
    }
    else
    {
        log_->info("[dispatchSkillAction] Instant skill '" + skillSlug + "' executed OK for char " +
                   std::to_string(characterId) + " -> target " + std::to_string(targetId) +
                   " damage=" + std::to_string(executionResult.skillResult.damageResult.totalDamage));
    }
    broadcastSkillExecution(executionResult);

    // Handle teleport_respawn result: send teleport packet to the caster and
    // broadcast the new position to all nearby players.
    if (executionResult.hasTeleport)
    {
        const PositionStruct &dest = executionResult.teleportPosition;

        // Retrieve client data for the caster so we can address the response correctly
        ClientDataStruct clientData =
            gameServices_.getClientManager().getClientDataByCharacterId(characterId);
        int casterClientId = clientData.clientId;

        // 1. Send teleport response to the casting client
        {
            nlohmann::json teleportResp;
            teleportResp["header"]["eventType"] = "respawnResult";
            teleportResp["header"]["status"] = "success";
            teleportResp["header"]["clientId"] = casterClientId;
            teleportResp["body"]["characterId"] = characterId;
            teleportResp["body"]["position"] = {
                {"x", dest.positionX},
                {"y", dest.positionY},
                {"z", dest.positionZ},
                {"rotationZ", dest.rotationZ}};
            std::string msg = networkManager_.generateResponseMessage(
                "success", teleportResp);
            if (clientSocket)
                networkManager_.sendResponse(clientSocket, msg);
        }

        // 2. Persist the new position to the game server
        {
            nlohmann::json savePos;
            savePos["header"]["eventType"] = "savePositions";
            savePos["header"]["clientId"] = 0;
            savePos["header"]["hash"] = "";
            savePos["body"]["characters"] = nlohmann::json::array();
            nlohmann::json posEntry;
            posEntry["characterId"] = characterId;
            posEntry["posX"] = dest.positionX;
            posEntry["posY"] = dest.positionY;
            posEntry["posZ"] = dest.positionZ;
            posEntry["rotZ"] = dest.rotationZ;
            savePos["body"]["characters"].push_back(posEntry);
            gameServerWorker_.sendDataToGameServer(savePos.dump() + "\n");
        }

        // 3. Broadcast position update to all other players in the chunk
        {
            nlohmann::json posBroadcast;
            posBroadcast["header"]["eventType"] = "characterMoved";
            posBroadcast["body"]["characterId"] = characterId;
            posBroadcast["body"]["posX"] = dest.positionX;
            posBroadcast["body"]["posY"] = dest.positionY;
            posBroadcast["body"]["posZ"] = dest.positionZ;
            posBroadcast["body"]["rotZ"] = dest.rotationZ;
            broadcastToAllClients(posBroadcast.dump() + "\n", casterClientId);
        }

        // Reset server-side movement validation state so the next
        // moveCharacter packet is not rejected as a speed violation.
        gameServices_.getCharacterManager().setLastValidatedMovement(characterId, dest, 0);

        log_->info("[CombatEventHandler] Teleport skill for char={}: ({},{},{}) via skill={}",
            characterId,
            dest.positionX,
            dest.positionY,
            dest.positionZ,
            skillSlug);
    }

    // Persist any skill-applied timed effects to the game server so they survive logout.
    for (const auto &eff : executionResult.appliedEffects)
    {
        nlohmann::json effectPacket;
        effectPacket["header"]["eventType"] = "saveActiveEffect";
        effectPacket["header"]["clientId"] = 0;
        effectPacket["header"]["hash"] = "";
        effectPacket["body"]["characterId"] = characterId;
        effectPacket["body"]["effectSlug"] = eff.effectSlug;
        effectPacket["body"]["attributeSlug"] = eff.attributeSlug;
        effectPacket["body"]["sourceType"] = "skill";
        effectPacket["body"]["value"] = static_cast<double>(eff.value);
        effectPacket["body"]["expiresAt"] = eff.expiresAt;
        effectPacket["body"]["tickMs"] = eff.tickMs;
        gameServerWorker_.sendDataToGameServer(effectPacket.dump() + "\n");
    }
}

void
CombatEventHandler::handleInitiateCombatAction(const Event &event)
{
    // Для совместимости со старым API, делегируем к новому методу
    handlePlayerAttack(event);
}

void
CombatEventHandler::handleCompleteCombatAction(const Event &event)
{
    // Этот метод может быть реализован для завершения боевых действий
    // В новой архитектуре это обрабатывается через CombatSystem
    log_->info("handleCompleteCombatAction - using new architecture");
}

void
CombatEventHandler::handleCombatResult(const Event &event)
{
    // Обработка результатов боя
    // В новой архитектуре это обрабатывается через CombatSystem
    log_->info("handleCombatResult - using new architecture");
}
