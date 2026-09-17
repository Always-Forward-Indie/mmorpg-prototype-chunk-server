#include "events/handlers/BaseEventHandler.hpp"
#include "utils/TimestampUtils.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <unordered_set>
#include <vector>

BaseEventHandler::BaseEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices,
    const std::string &loggerSubsystem)
    : networkManager_(networkManager),
      gameServerWorker_(gameServerWorker),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem(loggerSubsystem);
}

bool
BaseEventHandler::isPlayerAlive(int characterId)
{
    // Probe with safe default: unknown character counts as not-alive.
    // Deliberately silent — called on hot paths, and "missing" is routine
    // (offline chars), not an error.
    try
    {
        const auto &charData = gameServices_.getCharacterManager().getCharacterData(characterId);
        return !charData.isDead;
    }
    catch (...)
    {
        return false;
    }
}

std::shared_ptr<boost::asio::ip::tcp::socket>
BaseEventHandler::getClientSocket(const Event &event)
{
    int clientID = event.getClientID();

    // Always get socket from ClientManager, never from Event
    // This prevents use-after-free issues with socket references in Events
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = nullptr;
    try
    {
        clientSocket = gameServices_.getClientManager().getClientSocket(clientID);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error getting socket for client ID " + std::to_string(clientID) + ": " + e.what(), RED);
        clientSocket = nullptr;
    }

    return clientSocket;
}

void
BaseEventHandler::sendErrorResponse(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        log_->error("Cannot send error response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", message)
                                  .setHeader("hash", hash)
                                  .setHeader("clientId", clientId)
                                  .setHeader("eventType", eventType)
                                  .setBody("", "")
                                  .build();

    std::string responseData = networkManager_.generateResponseMessage("error", response);

    try
    {
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error sending error response for " + eventType +
                                           " to client " + std::to_string(clientId) + ": " + ex.what());
    }
}

void
BaseEventHandler::sendSuccessResponse(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const std::string &bodyKey,
    const nlohmann::json &bodyValue,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        log_->error("Cannot send success response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    ResponseBuilder builder;
    builder.setHeader("message", message)
        .setHeader("hash", hash)
        .setHeader("clientId", clientId)
        .setHeader("eventType", eventType);

    if (!bodyKey.empty())
    {
        builder.setBody(bodyKey, bodyValue);
    }
    else
    {
        builder.setBody("", "");
    }

    nlohmann::json response = builder.build();
    std::string responseData = networkManager_.generateResponseMessage("success", response);

    try
    {
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error sending success response for " + eventType +
                                           " to client " + std::to_string(clientId) + ": " + ex.what());
    }
}

void
BaseEventHandler::sendGameServerResponse(const std::string &status, const nlohmann::json &response)
{
    std::string responseData = networkManager_.generateResponseMessage(status, response);
    gameServerWorker_.sendDataToGameServer(responseData);
}

void
BaseEventHandler::broadcastToAllClients(const std::string &responseData, int excludeClientId)
{
    // CRITICAL-8 fix:
    // 1. ONE shared_ptr allocation for the whole broadcast (not N string copies)
    // 2. ONE shared_lock acquisition instead of N individual getClientSocket() calls
    // At 2000 clients x 100 broadcasts/s: 100 lock acquisitions vs previous 200,000
    // P0: generation-stamped snapshot — the registration (sock, gen) is captured
    // atomically; per-socket write queues additionally validate owner identity,
    // so a reconnected socket (possibly at a reused address) never inherits a
    // stale queue or misses fan-out traffic.
    auto sharedData = std::make_shared<const std::string>(responseData);
    auto snapshots = gameServices_.getClientManager().getActiveSnapshots(excludeClientId);

    for (auto &snap : snapshots)
    {
        if (snap.sock && snap.sock->is_open())
        {
            try
            {
                networkManager_.sendResponse(snap.sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error broadcasting to client: " + std::string(ex.what()));
            }
        }
    }
}

void
BaseEventHandler::sendErrorResponseWithTimestamps(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const TimestampStruct &timestamps,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        log_->error("Cannot send error response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    nlohmann::json response = ResponseBuilder()
                                  .setHeader("message", message)
                                  .setHeader("hash", hash)
                                  .setHeader("clientId", clientId)
                                  .setHeader("eventType", eventType)
                                  .setTimestamps(timestamps)
                                  .setBody("", "")
                                  .build();

    std::string errorData = networkManager_.generateResponseMessage("error", response, timestamps);
    networkManager_.sendResponse(clientSocket, errorData);
}

void
BaseEventHandler::sendSuccessResponseWithTimestamps(
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    const std::string &message,
    const std::string &eventType,
    int clientId,
    const TimestampStruct &timestamps,
    const std::string &bodyKey,
    const nlohmann::json &bodyValue,
    const std::string &hash)
{
    if (!clientSocket || !clientSocket->is_open())
    {
        log_->error("Cannot send success response: invalid or closed socket for client " + std::to_string(clientId));
        return;
    }

    auto builder = ResponseBuilder()
                       .setHeader("message", message)
                       .setHeader("hash", hash)
                       .setHeader("clientId", clientId)
                       .setHeader("eventType", eventType)
                       .setTimestamps(timestamps);

    if (!bodyKey.empty())
    {
        builder.setBody(bodyKey, bodyValue);
    }

    nlohmann::json response = builder.build();
    std::string successData = networkManager_.generateResponseMessage("success", response, timestamps);
    networkManager_.sendResponse(clientSocket, successData);
}

void
BaseEventHandler::broadcastToClientIds(
    const std::string &status,
    const nlohmann::json &response,
    const TimestampStruct &timestamps,
    const std::vector<int> &recipientIds)
{
    if (recipientIds.empty())
        return;
    // P0: generation-stamped snapshot (see broadcastToAllClients).
    std::string rawData = networkManager_.generateResponseMessage(status, response, timestamps);
    auto sharedData = std::make_shared<const std::string>(rawData);
    auto snapshots = gameServices_.getClientManager().getActiveSnapshots(-1);
    std::unordered_set<int> want(recipientIds.begin(), recipientIds.end());

    for (auto &snap : snapshots)
    {
        if (snap.sock && snap.sock->is_open() && want.find(snap.clientId) != want.end())
        {
            try
            {
                networkManager_.sendResponse(snap.sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error in broadcastToClientIds: " + std::string(ex.what()));
            }
        }
    }
}

bool
BaseEventHandler::charPos(int characterId, float &x, float &y)
{
    // Probe with safe default (see isPlayerAlive): false routes the caller to
    // fail-open broadcast, so silence here is correct, not a lost error.
    if (characterId <= 0)
        return false;
    try
    {
        auto data = gameServices_.getCharacterManager().getCharacterData(characterId);
        if (data.characterId == 0)
            return false;
        x = data.characterPosition.positionX;
        y = data.characterPosition.positionY;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool
BaseEventHandler::mobPos(int mobUid, float &x, float &y)
{
    // Probe with safe default (see isPlayerAlive).
    if (mobUid <= 0)
        return false;
    try
    {
        auto mob = gameServices_.getMobInstanceManager().getMobInstance(mobUid);
        if (mob.uid == 0)
            return false;
        x = mob.position.positionX;
        y = mob.position.positionY;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int
BaseEventHandler::clientForCharacter(int characterId)
{
    // Probe with safe default (see isPlayerAlive): 0 = no participant.
    if (characterId <= 0)
        return 0;
    try
    {
        return gameServices_.getClientManager().getClientDataByCharacterId(characterId).clientId;
    }
    catch (...)
    {
        return 0;
    }
}

void
BaseEventHandler::broadcastPositional(const std::string &status, const nlohmann::json &response,
    const TimestampStruct &timestamps, float x, float y,
    const std::vector<int> &participantClientIds, int excludeClientId)
{
    auto &interest = gameServices_.getInterestManager();
    if (!interest.isEnabled())
    {
        broadcastToAllClientsWithTimestamps(status, response, timestamps, excludeClientId);
        return;
    }
    auto clients = gameServices_.getClientManager().getClientsListReadOnly();
    std::vector<std::pair<int, bool>> viewers;
    viewers.reserve(clients.size());
    for (const auto &c : clients)
        viewers.emplace_back(c.clientId, c.isWorldReady);
    auto ids = interest.recipientsFor(x, y, viewers, excludeClientId);
    for (int p : participantClientIds)
    {
        if (p > 0 && p != excludeClientId &&
            std::find(ids.begin(), ids.end(), p) == ids.end())
            ids.push_back(p);
    }
    if (ids.empty())
        return; // nobody subscribed and nobody fail-open: nothing to send to
    broadcastToClientIds(status, response, timestamps, ids);
}

void
BaseEventHandler::broadcastPositional(const std::string &status, const nlohmann::json &response,
    float x, float y, const std::vector<int> &participantClientIds, int excludeClientId)
{
    auto &interest = gameServices_.getInterestManager();
    if (!interest.isEnabled())
    {
        std::string rawData = networkManager_.generateResponseMessage(status, response);
        broadcastToAllClients(rawData, excludeClientId);
        return;
    }
    auto clients = gameServices_.getClientManager().getClientsListReadOnly();
    std::vector<std::pair<int, bool>> viewers;
    viewers.reserve(clients.size());
    for (const auto &c : clients)
        viewers.emplace_back(c.clientId, c.isWorldReady);
    auto ids = interest.recipientsFor(x, y, viewers, excludeClientId);
    for (int p : participantClientIds)
    {
        if (p > 0 && p != excludeClientId &&
            std::find(ids.begin(), ids.end(), p) == ids.end())
            ids.push_back(p);
    }
    if (ids.empty())
        return;
    std::string rawData = networkManager_.generateResponseMessage(status, response);
    auto sharedData = std::make_shared<const std::string>(rawData);
    auto snapshots = gameServices_.getClientManager().getActiveSnapshots(-1);
    std::unordered_set<int> want(ids.begin(), ids.end());
    for (auto &snap : snapshots)
    {
        if (snap.sock && snap.sock->is_open() && want.find(snap.clientId) != want.end())
        {
            try
            {
                networkManager_.sendResponse(snap.sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error in broadcastPositional: " + std::string(ex.what()));
            }
        }
    }
}

// Resolve (x, y, participants) from a broadcast packet body. Returns false
// when the shape is unknown or the position unresolvable => caller fails
// open to legacy broadcast-all. ECasterType::MOB == 3 (shared convention).
// The catch below is the fail-open itself: an unparsable packet routes to
// broadcast-all, which is always safe — hence silent by design.
bool
BaseEventHandler::resolveRoute(BaseEventHandler &h, const nlohmann::json &packet,
    float &x, float &y, std::vector<int> &participants)
{
    try
    {
        if (!packet.contains("body") || !packet["body"].is_object())
            return false;
        // Some broadcasts nest the result (e.g. healingResult.skillResult).
        const nlohmann::json *payload = &packet["body"];
        nlohmann::json nested;
        if (!payload->contains("casterId") && !payload->contains("characterId") &&
            payload->contains("skillResult") && (*payload)["skillResult"].is_object())
        {
            nested = (*payload)["skillResult"];
            payload = &nested;
        }
        const auto &body = *payload;
        const int casterId = body.value("casterId", 0);
        const int targetId = body.value("targetId", 0);
        const int targetType = body.value("targetType", -1);
        const int charId = body.value("characterId", 0);
        if (casterId > 0)
        {
            const int casterType = body.value("casterType", targetType);
            bool asMob = (casterType == 3);
            if (!asMob && !h.charPos(casterId, x, y))
                asMob = true; // not a known character: try mob
            if (asMob && !h.mobPos(casterId, x, y))
                return false; // unknown caster: fail open
            participants.push_back(h.clientForCharacter(casterId));
            if (targetId > 0 && targetType != 3)
                participants.push_back(h.clientForCharacter(targetId));
            return true;
        }
        if (charId > 0)
        {
            if (!h.charPos(charId, x, y))
                return false;
            participants.push_back(h.clientForCharacter(charId));
            return true;
        }
    }
    catch (...)
    {
    }
    return false;
}

void
BaseEventHandler::broadcastRouted(const std::string &status, const nlohmann::json &packet)
{
    float x = 0.0f, y = 0.0f;
    std::vector<int> participants;
    if (!resolveRoute(*this, packet, x, y, participants))
    {
        std::string rawData = networkManager_.generateResponseMessage(status, packet);
        broadcastToAllClients(rawData);
        return;
    }
    broadcastPositional(status, packet, x, y, participants);
}

void
BaseEventHandler::broadcastRouted(const std::string &status, const nlohmann::json &packet,
    const TimestampStruct &timestamps)
{
    float x = 0.0f, y = 0.0f;
    std::vector<int> participants;
    if (!resolveRoute(*this, packet, x, y, participants))
    {
        std::string rawData = networkManager_.generateResponseMessage(status, packet, timestamps);
        broadcastToAllClients(rawData);
        return;
    }
    broadcastPositional(status, packet, timestamps, x, y, participants);
}

void
BaseEventHandler::sendPositionalRaw(const std::string &rawData, float x, float y,
    const std::vector<int> &participantClientIds, int excludeClientId)
{
    auto &interest = gameServices_.getInterestManager();
    if (!interest.isEnabled())
    {
        broadcastToAllClients(rawData, excludeClientId);
        return;
    }
    auto clients = gameServices_.getClientManager().getClientsListReadOnly();
    std::vector<std::pair<int, bool>> viewers;
    viewers.reserve(clients.size());
    for (const auto &c : clients)
        viewers.emplace_back(c.clientId, c.isWorldReady);
    auto ids = interest.recipientsFor(x, y, viewers, excludeClientId);
    for (int p : participantClientIds)
    {
        if (p > 0 && p != excludeClientId &&
            std::find(ids.begin(), ids.end(), p) == ids.end())
            ids.push_back(p);
    }
    if (ids.empty())
        return;
    auto sharedData = std::make_shared<const std::string>(rawData);
    auto snapshots = gameServices_.getClientManager().getActiveSnapshots(-1);
    std::unordered_set<int> want(ids.begin(), ids.end());
    for (auto &snap : snapshots)
    {
        if (snap.sock && snap.sock->is_open() && want.find(snap.clientId) != want.end())
        {
            try
            {
                networkManager_.sendResponse(snap.sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error in sendPositionalRaw: " + std::string(ex.what()));
            }
        }
    }
}

void
BaseEventHandler::broadcastToAllClientsWithTimestamps(
    const std::string &status,
    const nlohmann::json &response,
    const TimestampStruct &timestamps,
    int excludeClientId)
{
    // CRITICAL-8 fix: one allocation + one shared_lock acquisition for the whole broadcast
    // P0: generation-stamped snapshot (see broadcastToAllClients).
    std::string rawData = networkManager_.generateResponseMessage(status, response, timestamps);
    auto sharedData = std::make_shared<const std::string>(rawData);
    auto snapshots = gameServices_.getClientManager().getActiveSnapshots(excludeClientId);

    for (auto &snap : snapshots)
    {
        if (snap.sock && snap.sock->is_open())
        {
            try
            {
                networkManager_.sendResponse(snap.sock, sharedData);
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error in broadcastWithTimestamps: " + std::string(ex.what()));
            }
        }
    }
}
