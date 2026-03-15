#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Manages in-memory P2P trade sessions (one per participant pair).
 *
 * A session is born when both players accept the trade invite and dies when
 * the trade completes, is cancelled, or the TTL (60 s) expires.
 *
 * All public methods are thread-safe.
 */
class TradeSessionManager
{
  public:
    explicit TradeSessionManager(Logger &logger);

    // ── Session lifecycle ────────────────────────────────────────────────────

    /**
     * @brief Create a new trade session between two characters.
     *        Any existing session for either character is closed first.
     * @return Reference to the newly created session.
     */
    TradeSessionStruct &createSession(
        int clientAId, int charAId, int clientBId, int charBId);

    /** Retrieve session by id.  Returns nullptr if not found. */
    TradeSessionStruct *getSession(const std::string &sessionId);

    /** Retrieve the active session a character is participating in. */
    TradeSessionStruct *getSessionByCharacter(int characterId);

    /** Close and remove the session. */
    void closeSession(const std::string &sessionId);

    /** Close any session belonging to the character. */
    void closeSessionByCharacter(int characterId);

    // ── Maintenance ──────────────────────────────────────────────────────────

    /** Remove sessions whose last activity exceeds TTL_SECONDS.
     *  Should be called from the Scheduler every 30 seconds. */
    void cleanupExpiredSessions();

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TradeSessionStruct> sessions_;
    std::unordered_map<int, std::string> characterToSession_; ///< charId → sessionId
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};
