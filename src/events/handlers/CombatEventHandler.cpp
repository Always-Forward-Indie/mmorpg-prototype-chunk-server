#include "events/handlers/CombatEventHandler.hpp"
#include "network/NetworkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/CombatSystem.hpp"
#include "services/GameServices.hpp"
#include "services/SkillSystem.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>

CombatEventHandler::CombatEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
    combatSystem_ = std::make_unique<CombatSystem>(&gameServices);
    skillSystem_ = std::make_unique<SkillSystem>(&gameServices);
    responseBuilder_ = std::make_unique<CombatResponseBuilder>(&gameServices);

    // Устанавливаем callback для отправки broadcast пакетов
    combatSystem_->setBroadcastCallback([this](const nlohmann::json &packet)
        { this->sendBroadcast(packet); });

    gameServices_.getLogger().log("CombatEventHandler initialized with new refactored architecture", GREEN);
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

void
CombatEventHandler::handlePlayerAttack(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    gameServices_.getLogger().log("handlePlayerAttack called for client ID: " + std::to_string(clientID), GREEN);

    try
    {
        // Получаем данные клиента
        auto clientData = gameServices_.getClientManager().getClientData(clientID);
        if (clientData.characterId == 0)
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Character not found!", "playerAttack", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
            {
                networkManager_.sendResponse(clientSocket, errorData);
            }
            return;
        }

        // Парсим данные запроса
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Invalid request format!", "playerAttack", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
            {
                networkManager_.sendResponse(clientSocket, errorData);
            }
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
            {
                networkManager_.sendResponse(clientSocket, errorData);
            }
            return;
        }

        gameServices_.getLogger().log("Player attack: " + skillSlug + " on target " + std::to_string(targetId) +
                                          " (type: " + std::to_string(static_cast<int>(targetType)) + ")",
            GREEN);

        // 1. Инициируем использование скила
        auto initiationResult = combatSystem_->initiateSkillUsage(clientData.characterId, skillSlug, targetId, targetType);
        broadcastSkillInitiation(initiationResult);

        if (!initiationResult.success)
        {
            return;
        }

        // 2. Если нет времени каста, сразу выполняем действие
        if (initiationResult.castTime <= 0.0f)
        {
            auto executionResult = combatSystem_->executeSkillUsage(clientData.characterId, skillSlug, targetId, targetType);
            broadcastSkillExecution(executionResult);
        }
        // Если есть время каста, действие завершится автоматически в updateOngoingActions()
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handlePlayerAttack: " + std::string(ex.what()));
        auto errorResponse = responseBuilder_->buildErrorResponse("Internal server error!", "playerAttack", clientID);
        std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
        if (clientSocket)
        {
            networkManager_.sendResponse(clientSocket, errorData);
        }
    }
}

void
CombatEventHandler::handleSkillUsage(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        // Получаем данные клиента
        auto clientData = gameServices_.getClientManager().getClientData(clientID);
        if (clientData.characterId == 0)
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Character not found!", "skillUsage", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
            {
                networkManager_.sendResponse(clientSocket, errorData);
            }
            return;
        }

        // Парсим данные запроса
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            auto errorResponse = responseBuilder_->buildErrorResponse("Invalid request format!", "skillUsage", clientID);
            std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
            if (clientSocket)
            {
                networkManager_.sendResponse(clientSocket, errorData);
            }
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
            {
                networkManager_.sendResponse(clientSocket, errorData);
            }
            return;
        }

        gameServices_.getLogger().log("Skill usage: " + skillSlug + " on target " + std::to_string(targetId) +
                                          " (type: " + std::to_string(static_cast<int>(targetType)) + ")",
            GREEN);

        // Инициируем использование скила
        auto initiationResult = combatSystem_->initiateSkillUsage(clientData.characterId, skillSlug, targetId, targetType);
        broadcastSkillInitiation(initiationResult);

        if (!initiationResult.success)
        {
            return;
        }

        // Если нет времени каста, сразу выполняем действие
        if (initiationResult.castTime <= 0.0f)
        {
            auto executionResult = combatSystem_->executeSkillUsage(clientData.characterId, skillSlug, targetId, targetType);
            broadcastSkillExecution(executionResult);
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleSkillUsage: " + std::string(ex.what()));
        auto errorResponse = responseBuilder_->buildErrorResponse("Internal server error!", "skillUsage", clientID);
        std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
        if (clientSocket)
        {
            networkManager_.sendResponse(clientSocket, errorData);
        }
    }
}

void
CombatEventHandler::handleAIAttack(int characterId)
{
    try
    {
        combatSystem_->processAIAttack(characterId);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleAIAttack: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::handleInterruptCombatAction(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();

    try
    {
        // Получаем данные клиента
        auto clientData = gameServices_.getClientManager().getClientData(clientID);
        if (clientData.characterId == 0)
        {
            return;
        }

        // Прерываем действие
        combatSystem_->interruptSkillUsage(clientData.characterId, InterruptionReason::PLAYER_CANCELLED);

        gameServices_.getLogger().log("Skill usage interrupted for character " + std::to_string(clientData.characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleInterruptCombatAction: " + std::string(ex.what()));
    }
}

void
CombatEventHandler::updateOngoingActions()
{
    try
    {
        combatSystem_->updateOngoingActions();
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
    gameServices_.getLogger().log("handleCompleteCombatAction - using new architecture", GREEN);
}

void
CombatEventHandler::handleCombatAnimation(const Event &event)
{
    // Обработка анимации боя
    // В новой архитектуре это может обрабатываться через CombatSystem
    gameServices_.getLogger().log("handleCombatAnimation - using new architecture", GREEN);
}

void
CombatEventHandler::handleCombatResult(const Event &event)
{
    // Обработка результатов боя
    // В новой архитектуре это обрабатывается через CombatSystem
    gameServices_.getLogger().log("handleCombatResult - using new architecture", GREEN);
}
