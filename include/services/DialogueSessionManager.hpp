#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Manages in-memory dialogue sessions (one per connected player).
 *
 * A session is created when a player successfully starts a dialogue
 * and destroyed when the dialogue ends (end-node reached, player closed,
 * NPC out-of-range or TTL expired).
 *
 * All public methods are thread-safe.
 */
class DialogueSessionManager
{
  public:
    explicit DialogueSessionManager(Logger &logger);

    // --- Session lifecycle ---

    /**
     * @brief Create a new dialogue session for a player.
     *
     * If the character already has an active session it is closed first.
     * The generated sessionId has the format "dlg_{clientId}_{steadyMs}".
     *
     * @return Reference to the newly created session.
     */
    DialogueSessionStruct &createSession(int clientId, int characterId, int npcId, int dialogueId, int startNodeId);

    /**
     * @brief Retrieve a session by its unique sessionId.
     * @return Pointer to session or nullptr.
     */
    DialogueSessionStruct *getSession(const std::string &sessionId);

    /**
     * @brief Retrieve the active session for a character.
     * @return Pointer to session or nullptr.
     */
    DialogueSessionStruct *getSessionByCharacter(int characterId);

    /**
     * @brief Close the session identified by sessionId.
     */
    void closeSession(const std::string &sessionId);

    /**
     * @brief Close any session belonging to this character.
     */
    void closeSessionByCharacter(int characterId);

    // --- Maintenance ---

    /**
     * @brief Remove sessions whose last activity exceeds DialogueSessionStruct::TTL_SECONDS.
     * Should be called from Scheduler every 60 seconds.
     */
    void cleanupExpiredSessions();

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, DialogueSessionStruct> sessions_; ///< sessionId → session
    std::unordered_map<int, std::string> characterToSession_;         ///< characterId → sessionId
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};
