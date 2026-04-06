#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ItemManager;
class InventoryManager;

/**
 * @brief In-memory skill trainer NPC store.
 *
 * Holds the learnable skill list for every trainer NPC.  Data is populated
 * once at chunk startup via setTrainerData() when the game-server sends the
 * `setTrainerData` packet.  All public methods are thread-safe.
 *
 * Analogous to VendorManager for vendor NPCs.
 */
class TrainerManager
{
  public:
    TrainerManager(ItemManager &itemManager, Logger &logger);

    // ── Data loading ─────────────────────────────────────────────────────────

    /** Replace the full trainer dataset (called once on chunk startup). */
    void setTrainerData(const std::vector<TrainerNPCDataStruct> &trainers);

    // ── Queries ──────────────────────────────────────────────────────────────

    /** Return a pointer to the trainer entry for npcId, or nullptr if not a trainer. */
    const TrainerNPCDataStruct *getTrainerByNpcId(int npcId) const;

    /**
     * @brief Return a pointer to a specific skill entry for the given npcId, or nullptr.
     *
     * Used by RequestLearnSkillRequestStruct handler to look up costs without a dialogue.
     */
    const ClassSkillTreeEntryStruct *getSkillEntry(int npcId, const std::string &skillSlug) const;

    /**
     * @brief Build the skill shop JSON payload to send to the client.
     *
     * Each skill entry includes per-skill canLearn / isLearned / prereqMet flags
     * so the client can render affordability indicators in the UI.
     *
     * @param npcId        Trainer NPC id
     * @param ctx          Player context (level, freeSkillPoints, learnedSkillSlugs)
     * @param inventoryMgr Inventory manager to check gold and book ownership
     * @return nullptr (is_null()) if npcId is not a known trainer
     */
    nlohmann::json buildSkillShopJson(
        int npcId,
        const PlayerContextStruct &ctx,
        const InventoryManager &inventoryMgr) const;

  private:
    ItemManager &itemManager_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<int, TrainerNPCDataStruct> trainers_; ///< npcId → data
};
