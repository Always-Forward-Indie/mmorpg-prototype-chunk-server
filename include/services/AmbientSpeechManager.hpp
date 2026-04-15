#pragma once

#include "data/DataStructs.hpp"
#include "services/DialogueConditionEvaluator.hpp"
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Manages NPC ambient speech configurations and per-player pool filtering.
 *
 * Ambient speech configs are loaded once at startup from the game-server
 * (SET_NPC_AMBIENT_SPEECH).  When a player joins or their quest/flag context
 * changes, buildFilteredPoolForPlayer() is called to produce a client-ready
 * JSON payload that contains only the lines whose conditions evaluate to true
 * for that specific player.
 *
 * Thread-safety: std::shared_mutex (multiple concurrent readers, single writer).
 */
class AmbientSpeechManager
{
  public:
    AmbientSpeechManager() = default;

    // ── Static data ──────────────────────────────────────────────────────────

    /**
     * @brief Replace all ambient speech configs with the list received from
     *        game-server (called once at startup on SET_NPC_AMBIENT_SPEECH).
     */
    void setAmbientSpeechData(const std::vector<NPCAmbientSpeechConfigStruct> &configs);

    /**
     * @brief Returns true once at least one config has been loaded.
     */
    bool isLoaded() const;

    // ── Per-player filtering ─────────────────────────────────────────────────

    /**
     * @brief Build a client-ready JSON object for a single NPC's ambient pool,
     *        filtered to lines whose conditions match the player's current context.
     *
     * Lines are grouped by priority descending.  Within each priority group the
     * full line objects are included so the client can perform weighted-random
     * selection locally.
     *
     * @param npcId    NPC to build the pool for.
     * @param ctx      Player context used to evaluate line conditions.
     * @return JSON object: { npcId, minIntervalSec, maxIntervalSec, pools:[...] }
     *         or null JSON if no config for this NPC / no lines pass conditions.
     */
    nlohmann::json buildFilteredPoolForPlayer(int npcId,
        const PlayerContextStruct &ctx) const;

    /**
     * @brief Build filtered pools for a set of NPC ids.
     *        Convenience wrapper used when sending NPC_AMBIENT_POOLS on join.
     *
     * @param npcIds   IDs of NPCs visible to the player (from getNPCsInArea).
     * @param ctx      Player context.
     * @return JSON array of pool objects (one per NPC that has any valid lines).
     */
    nlohmann::json buildFilteredPoolsForPlayer(const std::vector<int> &npcIds,
        const PlayerContextStruct &ctx) const;

  private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<int, NPCAmbientSpeechConfigStruct> configs_; ///< npcId → config
};
