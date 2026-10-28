#pragma once
#include "data/DataStructs.hpp"
#include "services/QuestResolvers.hpp"
#include "services/QuestStore.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Forward declare to break circular dependency
class GameServices;

/**
 * @brief Thin quest orchestrator (Increment 11).
 *
 * Owns QuestStore (static data + progress + transitions under its mutex) and
 * QuestResolvers (ID → slug JSON enrichment). Network leaves through seams
 * (game-server persistence sender, QUEST_UPDATE sender) bound by ChunkServer.
 */
class QuestManager
{
  public:
    QuestManager(GameServices *services, Logger &logger);

    // === Static data (set at startup, delegated to store) ===

    /**
     * @brief Load all quest static definitions from game-server.
     */
    void setQuests(const std::vector<QuestStruct> &quests);

    /**
     * @brief Get a quest definition by slug. Returns nullptr if not found.
     */
    const QuestStruct *getQuestBySlug(const std::string &slug) const;

    /**
     * @brief Get a quest definition by id. Returns nullptr if not found.
     */
    const QuestStruct *getQuestById(int id) const;

    // === Player quest progress (per character, delegated to store) ===

    /**
     * @brief Load active quest progress for a character (called on join).
     */
    void loadPlayerQuests(int characterId,
        const std::vector<PlayerQuestProgressStruct> &quests);

    /**
     * @brief Unload all quest data for a disconnecting character (after flushAllProgress).
     */
    void unloadPlayerQuests(int characterId);

    /**
     * @brief Fill PlayerContextStruct with quest states + progress for condition evaluation.
     */
    void fillQuestContext(int characterId, PlayerContextStruct &ctx) const;

    /**
     * @brief Snapshot of all progress rows for a character (admin getState).
     */
    std::vector<PlayerQuestProgressStruct> getPlayerQuestStates(int characterId) const;

    // === Quest logic ===

    /**
     * @brief Put a quest into active state for a character.
     * @return true on success, false if already active / level too low / etc.
     */
    bool offerQuest(int characterId, const std::string &questSlug);

    /**
     * @brief Atomic turn-in: in-memory state + rewards without individual saves.
     * Durable write is a single claimQuestTurnIn txn on the game server; the
     * game drops duplicates by state (already_claimed / not_completable).
     * @return Notifications for client + claim packet for the game server.
     */
    struct TurnInClaim
    {
        std::vector<nlohmann::json> notifications;
        // Single atomic claimQuestTurnIn packet (empty when nothing to persist,
        // e.g. beginTurnIn rejected). Sent via pendingGameServerPackets (fact).
        std::string claimPacket;
    };
    TurnInClaim turnInQuestAtomic(int characterId, const std::string &questSlug, int clientId);

    /**
     * @brief Fail/abandon a quest. Sets state to "failed" and persists.
     * Works for quests in active, offered, or completed state.
     * @return true on success, false if quest not found / already turned_in or failed.
     */
    bool failQuest(int characterId, const std::string &questSlug);

    /**
     * @brief Admin reset of one quest (issue #120): erase in-memory progress
     * so the quest can be accepted again (subject to content repeatable flag
     * at offer time). The caller must also delete the DB row (the dispatcher
     * sends deletePlayerQuest to the game server).
     * @return questId (>0) when the quest slug is known, 0 otherwise.
     */
    int resetQuest(int characterId, const std::string &questSlug);

    /**
     * @brief Manually advance quest step by slug (from action group).
     */
    void advanceQuestStepBySlug(int characterId, const std::string &questSlug);

    // === Trigger hooks (delegated to store) ===

    void onMobKilled(int characterId, int mobId);
    void onItemObtained(int characterId, int itemId, int quantity);
    void onNPCTalked(int characterId, int npcId);
    void onPositionReached(int characterId, float x, float y);
    void onWorldObjectInteracted(int characterId, int objectId);

    // === Flag helpers ===

    /**
     * @brief Read a boolean player flag from the character's in-memory flag list.
     * @return The stored value, or false if the flag is not set.
     */
    bool getFlagBool(int characterId, const std::string &key) const;

    /**
     * @brief Set a boolean player flag: updates in-memory cache and queues
     *        persistence to the game-server.
     */
    void setFlagBool(int characterId, const std::string &key, bool value);

    /**
     * @brief In-memory only flag write (atomic claim path).
     * The claimDialogueGift packet carries the durable write; queueing a
     * second updatePlayerFlag would split the txn again.
     */
    void setFlagBoolInMemory(int characterId, const std::string &key, bool value);

    /**
     * @brief Set an integer player flag: updates in-memory cache and queues
     *        persistence to the game-server.
     */
    void setFlagInt(int characterId, const std::string &key, int value);

    // === Persistence (delegated to store) ===

    /**
     * @brief Queue a flag update for async persistence to game-server.
     */
    void queueFlagUpdate(const UpdatePlayerFlagStruct &flagUpdate);

    /**
     * @brief Flush dirty quest progress records to game-server.
     * Called by Scheduler every 5 seconds.
     */
    void flushDirtyProgress();

    /**
     * @brief Flush ALL progress for a character (called on disconnect).
     */
    void flushAllProgress(int characterId);

    /**
     * @brief Flush all queued flag updates to game-server immediately.
     */
    void flushPendingFlags();

    /** Mark that saved flags have been fully loaded from game-server for a character. */
    void markFlagsLoaded(int characterId);

    /** Returns true once saved flags have arrived from game-server. Used to guard
     *  exploration-XP checks that must not fire before flags are known. */
    bool areFlagsLoaded(int characterId) const;

    /** Clear flags-loaded state when the character disconnects. */
    void clearFlagsLoaded(int characterId);

    /**
     * @brief Whether static quest data has been loaded.
     */
    bool isLoaded() const;

    /**
     * @brief Return the quest state string for a character by quest slug.
     * @return State string ("active", "completed", "turned_in", "failed", "offered")
     *         or empty string if the character has no progress on this quest.
     */
    std::string getQuestStateBySlug(int characterId, const std::string &questSlug) const;

    /**
     * @brief Set the game-server persistence sender (network-agnostic seam).
     * Forwards to the store. Called from ChunkServer after construction;
     * tests bind a capture lambda instead (Wave 4.1 — same pattern as the
     * phase-2 QuestStore seam).
     */
    void setSendToGameServerCallback(QuestStore::SendToGameServerFn fn);

    /**
     * @brief Set the QUEST_UPDATE packet sender (network-agnostic seam).
     * Replaces the old NetworkManager* wiring: ChunkServer binds the real
     * sendResponse path, tests bind a capture (or leave unset — updates are
     * then skipped with an error log, same as a null manager before).
     */
    using QuestUpdateSender = std::function<void(
        std::shared_ptr<boost::asio::ip::tcp::socket>, const nlohmann::json &)>;
    void setQuestUpdateSender(QuestUpdateSender fn);

    // ── Public enrichment helpers (used by DialogueActionExecutor, DialogueEventHandler) ──

    /**
     * @brief Resolve a quest step to a client-ready JSON object (IDs → slugs).
     * Must NOT be called while the store mutex is held by the same thread.
     */
    nlohmann::json resolveStepForClient(const QuestStepStruct &step) const;

    /**
     * @brief Resolve quest rewards to a client-ready JSON array respecting isHidden.
     * @param revealHidden  If true (used in quest_turned_in), all rewards are fully disclosed.
     * Must NOT be called while the store mutex is held by the same thread.
     */
    nlohmann::json resolveRewardsForClient(const std::vector<QuestRewardStruct> &rewards, bool revealHidden = false, int classId = 0) const;

  private:
    /// Send QUEST_UPDATE packet to the character's client
    void sendQuestUpdate(int characterId, const PlayerQuestProgressStruct &pq, const QuestStruct &quest);

    GameServices *services_;
    QuestUpdateSender questUpdateSender_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    QuestStore store_;
    QuestResolvers resolvers_;
};
