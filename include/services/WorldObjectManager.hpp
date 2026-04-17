#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <chrono>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Manages world interactive objects on the chunk server.
 *
 * Static definitions (WorldObjectDataStruct) are loaded once at startup via
 * setWorldObjects() when the game-server sends the setWorldObjects packet.
 *
 * Runtime state for scope="global" objects (WorldObjectInstanceStruct) is kept
 * in memory and updated as players interact.  State changes are broadcast to
 * all players via WorldObjectEventHandler.
 *
 * Per-player state for scope="per_player" objects is stored as boolean flags
 * in PlayerContextStruct::flagsBool with key "wio_interacted_<objectId>".
 *
 * Channeled interaction sessions are tracked here during the channel window;
 * WorldObjectEventHandler drives the ticker and completes or cancels them.
 *
 * Thread-safety: std::shared_mutex (read-heavy scenario).
 */
class WorldObjectManager
{
  public:
    explicit WorldObjectManager(Logger &logger);

    // ── Static data ──────────────────────────────────────────────────────────

    /// Replace all object definitions (called once on setWorldObjects from game-server).
    void setWorldObjects(const std::vector<WorldObjectDataStruct> &objects);

    /// Returns true once definitions have been loaded.
    bool isLoaded() const;

    /// Lookup a definition by id.  Returns default struct (id=0) if not found.
    WorldObjectDataStruct getObjectById(int objectId) const;

    /// All definitions for a zone.
    std::vector<WorldObjectDataStruct> getObjectsInZone(int zoneId) const;

    /// All definitions (e.g. to broadcast on player join).
    std::vector<WorldObjectDataStruct> getAllObjects() const;

    // ── Global runtime state ─────────────────────────────────────────────────

    /// Current state of a global-scope object ("active" | "depleted" | "disabled").
    /// Returns "active" for per_player objects or unknown ids.
    std::string getGlobalState(int objectId) const;

    /// Transition a global object to "depleted" and record depletedAt.
    void depleteGlobalObject(int objectId);

    /// Force-set a global object state (e.g. disabled by dialogue action).
    void setGlobalState(int objectId, const std::string &state);

    /// Called on a regular tick (e.g. 1-second interval) to check respawn timers.
    /// Returns ids of objects that transitioned from depleted → active.
    std::vector<int> tickRespawns();

    // ── Channel sessions ─────────────────────────────────────────────────────

    /// Record the start of a channeled interaction.
    void startChannelSession(int characterId, int objectId, int channelTimeSec);

    /// Returns the objectId currently being channeled by the character, or 0.
    int getActiveChannelObjectId(int characterId) const;

    /// Returns channel progress [0.0, 1.0] for a character, or -1.0 if not channeling.
    float getChannelProgress(int characterId) const;

    /// Remove the channel session for a character (cancel or complete).
    /// Returns false if no session existed.
    bool removeChannelSession(int characterId);

    /// Returns ids of characters whose channel has completed (elapsed >= channelTimeSec).
    /// Removes each completed session internally.
    std::vector<std::pair<int /*charId*/, int /*objectId*/>> pollCompletedChannels();

  private:
    Logger &logger_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<int, WorldObjectDataStruct> objects_;    ///< objectId → definition
    std::unordered_map<int, WorldObjectInstanceStruct> states_; ///< objectId → runtime state (global only)

    struct ChannelSession
    {
        int objectId = 0;
        std::chrono::steady_clock::time_point startedAt;
        int channelTimeSec = 0;
    };
    std::unordered_map<int, ChannelSession> channels_; ///< characterId → session
};
