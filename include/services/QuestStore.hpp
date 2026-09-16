#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations
class GameServerWorker;

/**
 * @brief External seams for QuestStore transitions (Increment 11).
 *
 * The store owns quest static data, per-character progress and the mutex, and
 * keeps the exact lock scopes / reentrancy of the pre-split QuestManager.
 * Everything that touches the outside world (client packets, character level,
 * inventory reads, reputation) arrives through these seams, bound by the
 * QuestManager orchestrator in production and faked in unit tests.
 */
struct QuestStoreSeams
{
    /// Push a QUEST_UPDATE packet (never takes the store lock — same contract
    /// as the pre-split sendQuestUpdate; may be invoked while mutex_ is held).
    std::function<void(int characterId,
        const PlayerQuestProgressStruct &pq,
        const QuestStruct &quest)>
        sendQuestUpdate;
    /// Character level for offerQuest minLevel gate (0 when unknown).
    std::function<int(int characterId)> getCharacterLevel;
    /// Inventory snapshot for collect-step seeding (empty when unknown).
    std::function<std::vector<PlayerInventoryItemStruct>(int characterId)> getPlayerInventory;
    /// Reputation change for failQuest auto-rep (invoked while mutex_ is held, 1-1).
    std::function<void(int characterId, const std::string &faction, int delta)> changeReputation;
};

/**
 * @brief Quest static data + per-character progress + transitions.
 *
 * Pure-map logic with injected seams; no GameServices/NetworkManager.
 * Locking discipline (narrow turnIn snapshot, send-under-lock in triggers,
 * resolve* never under lock) is preserved 1-1 from QuestManager.
 */
class QuestStore
{
  public:
    QuestStore(Logger &logger, QuestStoreSeams seams);

    // === Static data (set at startup) ===
    void setQuests(const std::vector<QuestStruct> &quests);
    const QuestStruct *getQuestBySlug(const std::string &slug) const;
    const QuestStruct *getQuestById(int id) const;
    bool isLoaded() const;

    // === Player quest progress (per character) ===
    void loadPlayerQuests(int characterId,
        const std::vector<PlayerQuestProgressStruct> &quests);
    void unloadPlayerQuests(int characterId);
    void fillQuestContext(int characterId, PlayerContextStruct &ctx) const;
    std::string getQuestStateBySlug(int characterId, const std::string &questSlug) const;

    // === Quest logic ===
    bool offerQuest(int characterId, const std::string &questSlug);

    /**
     * @brief Snapshot + mark turned_in under one narrow lock scope.
     * @return false when the quest is unknown or not in completed state
     *         (snapshots untouched); true fills pqOut/questOut, 1-1 with the
     *         pre-split turnInQuest lock scope.
     */
    bool beginTurnIn(int characterId,
        const std::string &questSlug,
        PlayerQuestProgressStruct &pqOut,
        QuestStruct &questOut);

    bool failQuest(int characterId, const std::string &questSlug);
    void advanceQuestStepBySlug(int characterId, const std::string &questSlug);

    // === Trigger hooks ===
    void onMobKilled(int characterId, int mobId);
    void onItemObtained(int characterId, int itemId, int quantity);
    void onNPCTalked(int characterId, int npcId);
    void onPositionReached(int characterId, float x, float y);
    void onWorldObjectInteracted(int characterId, int objectId);

    // === Persistence ===
    void queueFlagUpdate(const UpdatePlayerFlagStruct &flagUpdate);
    void flushDirtyProgress();
    void flushAllProgress(int characterId);
    void flushPendingFlags();
    void markFlagsLoaded(int characterId);
    bool areFlagsLoaded(int characterId) const;
    void clearFlagsLoaded(int characterId);
    void setGameServerWorker(GameServerWorker *worker);

  private:
    void checkStepCompletion(int characterId, PlayerQuestProgressStruct &pq);
    void advanceStep(int characterId, PlayerQuestProgressStruct &pq);
    void completeQuest(int characterId, PlayerQuestProgressStruct &pq);

    mutable std::mutex mutex_;

    std::unordered_map<std::string, QuestStruct> questsBySlug_;
    std::unordered_map<int, QuestStruct *> questsById_; ///< Points into questsBySlug_ values

    /// characterId → (questId → progress)
    std::unordered_map<int, std::unordered_map<int, PlayerQuestProgressStruct>> playerProgress_;

    /// Pending flag updates waiting for transmission to game-server
    std::vector<UpdatePlayerFlagStruct> pendingFlagUpdates_;

    /// Characters whose persisted flags have been fully loaded from game-server
    std::unordered_set<int> flagsLoadedCharacters_;

    bool loaded_ = false;

    GameServerWorker *gameServerWorker_ = nullptr;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    QuestStoreSeams seams_;
};
