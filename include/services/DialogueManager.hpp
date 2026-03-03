#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Thread-safe manager for in-memory dialogue graph cache.
 *
 * Populated once at chunk-server startup from game-server via
 * SET_ALL_DIALOGUES / SET_NPC_DIALOGUE_MAPPINGS events.
 * All read operations are lock-guarded for thread safety.
 */
class DialogueManager
{
  public:
    explicit DialogueManager(Logger &logger);

    // --- Populate from game-server ---

    /**
     * @brief Cache all dialogue graphs received from game-server.
     * Replaces any previously stored data.
     */
    void setDialogues(const std::vector<DialogueGraphStruct> &dialogues);

    /**
     * @brief Cache all NPC→Dialogue mappings received from game-server.
     * Replaces any previously stored data.
     */
    void setNPCDialogueMappings(const std::vector<NPCDialogueMappingStruct> &mappings);

    // --- Lookup ---

    /**
     * @brief Select the best dialogue for an NPC given the player context.
     *
     * Iterates npc_dialogue entries ordered by priority DESC.
     * For each entry evaluates condition_group using the provided context.
     * Returns the first matching dialogueId, or -1 if none match.
     *
     * @param npcId  NPC identifier.
     * @param ctx    Current player context (level, flags, quest states).
     * @return dialogueId or -1.
     */
    int selectDialogueForNPC(int npcId, const PlayerContextStruct &ctx) const;

    /**
     * @brief Get a dialogue graph by its numeric ID.
     * @return Pointer to DialogueGraphStruct or nullptr if not found.
     */
    const DialogueGraphStruct *getDialogueById(int id) const;

    /**
     * @brief Get a dialogue graph by its slug.
     * @return Pointer to DialogueGraphStruct or nullptr if not found.
     */
    const DialogueGraphStruct *getDialogueBySlug(const std::string &slug) const;

    /**
     * @brief Whether static data has been loaded at least once.
     */
    bool isLoaded() const;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<int, DialogueGraphStruct> dialoguesById_;
    std::unordered_map<std::string, int> dialoguesBySlug_; ///< slug → id
    /// npcId → mappings sorted by priority DESC
    std::unordered_map<int, std::vector<NPCDialogueMappingStruct>> npcMappings_;
    bool loaded_ = false;
    Logger &logger_;
};
