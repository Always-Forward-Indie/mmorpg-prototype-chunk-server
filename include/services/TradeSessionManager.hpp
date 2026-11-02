#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <chrono>
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

    /**
     * @brief Copy a session under the manager lock (UAF-safe read).
     * Handlers must prefer this (or the transition helpers below) over
     * getSession: the live entry may be reaped by cleanupExpiredSessions
     * or closed on another worker thread at any moment.
     * @return false when the session is already gone.
     */
    bool copySession(const std::string &sessionId, TradeSessionStruct &out);

    /** True when the character participates in a live session. */
    bool hasSessionFor(int characterId);

    /** Retrieve the active session a character is participating in. */
    TradeSessionStruct *getSessionByCharacter(int characterId);

    /** Close and remove the session. */
    void closeSession(const std::string &sessionId);

    /** Close any session belonging to the character. */
    void closeSessionByCharacter(int characterId);

    /**
     * @brief Atomic offer update: mutate + snapshot under one lock.
     * The handler must use `out` (a copy) from here on.
     * @return false when the session is already gone.
     */
    bool applyOfferUpdate(const std::string &sessionId, bool isA,
        const std::vector<TradeOfferItemStruct> &items, int gold,
        TradeSessionStruct &out);

    /** Confirm-outcome snapshot (same UAF rationale as applyOfferUpdate). */
    struct ConfirmOutcome
    {
        bool found = false;
        bool bothConfirmed = false;
        TradeSessionStruct snapshot;
    };
    ConfirmOutcome confirmSide(const std::string &sessionId, bool isA);

    /** Clear one side's confirm flag (e.g. range re-check refusal). */
    void unconfirmSide(const std::string &sessionId, bool isA);

    /**
     * @brief Copy + erase for cancel (same UAF rationale).
     * @return false when already gone (idempotent cancel).
     */
    bool takeSessionForCancel(const std::string &sessionId, TradeSessionStruct &out);

    /** Pending trade invites: accept must match one, else `no_pending_invite`.
     *  Invite TTL is 60 s (same as sessions). Thread-safe. */
    void addInvite(int fromCharacterId, int toCharacterId);
    bool consumeInvite(int fromCharacterId, int toCharacterId);
    void removeInvitesFor(int characterId);

    // ── Maintenance ──────────────────────────────────────────────────────────

    /** Remove sessions whose last activity exceeds TTL_SECONDS.
     *  Should be called from the Scheduler every 30 seconds. */
    void cleanupExpiredSessions();

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TradeSessionStruct> sessions_;
    std::unordered_map<int, std::string> characterToSession_; ///< charId → sessionId
    struct PendingInvite
    {
        int fromCharacterId = 0;
        std::chrono::steady_clock::time_point created{};
    };
    std::unordered_map<int, PendingInvite> pendingInvites_; ///< toCharId → invite
    static constexpr long long INVITE_TTL_MS = 60000;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};
