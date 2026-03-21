#include "events/handlers/ChatEventHandler.hpp"
#include "events/EventData.hpp"
#include "services/CharacterManager.hpp"
#include "services/ClientManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

ChatEventHandler::ChatEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "chat")
{
}

// ── Rate limiting ─────────────────────────────────────────────────────────────

bool
ChatEventHandler::checkRateLimit(int clientId)
{
    std::lock_guard<std::mutex> lock(rateMutex_);
    auto now = std::chrono::steady_clock::now();

    auto &bucket = rateBuckets_[clientId];
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - bucket.windowStart)
                       .count();

    if (elapsed >= RATE_LIMIT_WINDOW_MS)
    {
        bucket.count = 1;
        bucket.windowStart = now;
        return true;
    }

    if (bucket.count >= RATE_LIMIT_MAX)
        return false;

    ++bucket.count;
    return true;
}

// ── Payload builder ───────────────────────────────────────────────────────────

std::string
ChatEventHandler::buildPayload(const ChatMessageStruct &msg) const
{
    std::string channelStr;
    switch (msg.channel)
    {
    case ChatChannel::LOCAL:
        channelStr = "local";
        break;
    case ChatChannel::WHISPER:
        channelStr = "whisper";
        break;
    default:
        channelStr = "zone";
        break;
    }

    nlohmann::json response;
    response["header"]["eventType"] = "chatMessage";
    response["header"]["clientId"] = 0; // broadcast — no single recipient id
    response["header"]["message"] = "success";
    response["body"]["channel"] = channelStr;
    response["body"]["senderName"] = msg.senderName;
    response["body"]["senderId"] = msg.senderCharId;
    response["body"]["text"] = msg.text;
    response["body"]["timestamp"] = msg.timestamps.serverRecvMs;

    return networkManager_.generateResponseMessage("success", response);
}

// ── Delivery helpers ──────────────────────────────────────────────────────────

void
ChatEventHandler::deliverZone(const ChatMessageStruct &msg, const std::string &payload)
{
    broadcastToAllClients(payload, -1 /* include sender */);
}

void
ChatEventHandler::deliverLocal(const ChatMessageStruct &msg, const std::string &payload)
{
    PositionStruct senderPos =
        gameServices_.getCharacterManager().getCharacterPosition(msg.senderCharId);

    // Iterate all connected characters, check distance
    auto characters = gameServices_.getCharacterManager().getCharactersList();
    for (const auto &ch : characters)
    {
        float dx = ch.characterPosition.positionX - senderPos.positionX;
        float dy = ch.characterPosition.positionY - senderPos.positionY;
        float dz = ch.characterPosition.positionZ - senderPos.positionZ;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq <= msg.localRadius * msg.localRadius)
        {
            ClientDataStruct client =
                gameServices_.getClientManager().getClientDataByCharacterId(ch.characterId);
            if (client.clientId <= 0)
                continue;

            auto sock = gameServices_.getClientManager().getClientSocket(client.clientId);
            if (sock && sock->is_open())
                networkManager_.sendResponse(sock, payload);
        }
    }
}

void
ChatEventHandler::deliverWhisper(const ChatMessageStruct &msg,
    const std::string &payload,
    int senderClientId)
{
    // Find target among connected characters by name
    auto characters = gameServices_.getCharacterManager().getCharactersList();
    for (const auto &ch : characters)
    {
        if (ch.characterName == msg.targetName)
        {
            ClientDataStruct target =
                gameServices_.getClientManager().getClientDataByCharacterId(ch.characterId);
            if (target.clientId <= 0)
                break; // name found but no live session

            auto sock = gameServices_.getClientManager().getClientSocket(target.clientId);
            if (sock && sock->is_open())
                networkManager_.sendResponse(sock, payload);
            return;
        }
    }

    // Target not found — notify sender
    auto senderSock = gameServices_.getClientManager().getClientSocket(senderClientId);
    if (senderSock && senderSock->is_open())
    {
        nlohmann::json errResp;
        errResp["header"]["eventType"] = "chatMessage";
        errResp["header"]["clientId"] = senderClientId;
        errResp["header"]["message"] = "Player '" + msg.targetName + "' not found";
        errResp["body"] = nullptr;
        networkManager_.sendResponse(senderSock,
            networkManager_.generateResponseMessage("error", errResp));
    }
}

// ── Main handler ──────────────────────────────────────────────────────────────

void
ChatEventHandler::handleChatMessageEvent(const Event &event)
{
    const auto &data = event.getData();
    const int clientId = event.getClientID();

    if (!std::holds_alternative<ChatMessageStruct>(data))
    {
        log_->error("[ChatEventHandler] unexpected data type for CHAT_MESSAGE");
        return;
    }

    ChatMessageStruct msg = std::get<ChatMessageStruct>(data);

    // Rate limit check
    if (!checkRateLimit(clientId))
    {
        log_->warn("[ChatEventHandler] rate limit exceeded for client " + std::to_string(clientId));
        return;
    }

    // Populate sender name server-side — never trust the client
    CharacterDataStruct charData =
        gameServices_.getCharacterManager().getCharacterData(msg.senderCharId);
    msg.senderName = charData.characterName;

    if (msg.senderName.empty())
    {
        log_->warn("[ChatEventHandler] sender name not found for char " + std::to_string(msg.senderCharId));
        return;
    }

    const std::string payload = buildPayload(msg);

    switch (msg.channel)
    {
    case ChatChannel::ZONE:
        deliverZone(msg, payload);
        break;
    case ChatChannel::LOCAL:
        deliverLocal(msg, payload);
        break;
    case ChatChannel::WHISPER:
        deliverWhisper(msg, payload, clientId);
        break;
    }

    log_->debug("[ChatEventHandler] chat delivered: channel=" + std::to_string(static_cast<int>(msg.channel)) + " sender=" + msg.senderName);
}
