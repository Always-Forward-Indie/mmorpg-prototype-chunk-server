#pragma once
// Pure join-evict decision table (B2 extract).
//
// CharacterEventHandler::evictStaleSession opens with three ordered guards
// before the six flush steps: nothing loaded → nothing to evict; character
// present but no client mapped → pre-loaded join data, not a stale session;
// same client reconnecting → duplicate join packet. Only otherwise does the
// full evict run. Order is load-bearing (first match wins) and is pinned by
// unit tests; the handler keeps lookups, logging and the flush steps.
enum class JoinEvictDecision
{
    NothingToEvict, ///< character not loaded — return immediately
    PreloadedSkip,  ///< loaded but no client mapped (async join preload)
    DuplicateSkip,  ///< same client re-sent joinGameCharacter
    FullEvict,      ///< genuine stale session — run all flush steps
};

class JoinFlushPolicy
{
  public:
    /// @param charLoaded  getCharacterData(id).characterId != 0
    /// @param staleClientId client currently mapped to the character (0 = none)
    /// @param newClientId   client that sent the new join
    static JoinEvictDecision decide(bool charLoaded, int staleClientId, int newClientId)
    {
        if (!charLoaded)
            return JoinEvictDecision::NothingToEvict;
        if (staleClientId == 0)
            return JoinEvictDecision::PreloadedSkip;
        if (staleClientId == newClientId)
            return JoinEvictDecision::DuplicateSkip;
        return JoinEvictDecision::FullEvict;
    }

    JoinFlushPolicy() = delete;
};
