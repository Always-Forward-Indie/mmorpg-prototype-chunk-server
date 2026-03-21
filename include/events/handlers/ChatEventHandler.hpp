#pragma once

#include "data/DataStructs.hpp"
#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include <chrono>
#include <unordered_map>

/**
 * @brief Handler for player chat messages.
 *
 * Supported channels:
 *  - LOCAL   : broadcasted to all players within localRadius units of the sender
 *  - ZONE    : broadcasted to all players on this chunk server
 *  - WHISPER : delivered privately to a single player by character name
 *
 * All validation (text length, rate limiting) is performed server-side.
 */
class ChatEventHandler : public BaseEventHandler
{
  public:
    ChatEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    void handleChatMessageEvent(const Event &event);

  private:
    // ── Rate limiting ─────────────────────────────────────────────────────────

    static constexpr int RATE_LIMIT_MAX = 3;          ///< max messages per window
    static constexpr int RATE_LIMIT_WINDOW_MS = 2000; ///< window duration (ms)
    static constexpr int MAX_TEXT_LENGTH = 255;

    struct RateBucket
    {
        int count = 0;
        std::chrono::steady_clock::time_point windowStart{};
    };

    std::unordered_map<int, RateBucket> rateBuckets_; ///< keyed by clientId
    std::mutex rateMutex_;

    /// Returns true if the message is allowed, false if rate-limited.
    bool checkRateLimit(int clientId);

    // ── Delivery helpers ──────────────────────────────────────────────────────

    void deliverZone(const ChatMessageStruct &msg, const std::string &payload);
    void deliverLocal(const ChatMessageStruct &msg, const std::string &payload);
    void deliverWhisper(const ChatMessageStruct &msg, const std::string &payload, int senderClientId);

    /// Build the JSON string that goes to all recipients.
    std::string buildPayload(const ChatMessageStruct &msg) const;
};
