#include "services/DialogueSessionManager.hpp"
#include <chrono>

DialogueSessionManager::DialogueSessionManager(Logger &logger)
    : logger_(logger)
{
}

DialogueSessionStruct &
DialogueSessionManager::createSession(int clientId, int characterId, int npcId, int dialogueId, int startNodeId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Close any existing session for this character
    auto existingIt = characterToSession_.find(characterId);
    if (existingIt != characterToSession_.end())
    {
        sessions_.erase(existingIt->second);
        characterToSession_.erase(existingIt);
    }

    // Build a unique session ID
    auto now = std::chrono::steady_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch())
                       .count();
    std::string sessionId = "dlg_" + std::to_string(clientId) + "_" + std::to_string(ms);

    DialogueSessionStruct session;
    session.sessionId = sessionId;
    session.characterId = characterId;
    session.clientId = clientId;
    session.npcId = npcId;
    session.dialogueId = dialogueId;
    session.currentNodeId = startNodeId;
    session.lastActivity = now;

    sessions_[sessionId] = std::move(session);
    characterToSession_[characterId] = sessionId;

    logger_.log("[DialogueSession] Created session " + sessionId +
                " for character " + std::to_string(characterId) +
                " NPC " + std::to_string(npcId));

    return sessions_[sessionId];
}

DialogueSessionStruct *
DialogueSessionManager::getSession(const std::string &sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionId);
    return (it != sessions_.end()) ? &it->second : nullptr;
}

DialogueSessionStruct *
DialogueSessionManager::getSessionByCharacter(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto cit = characterToSession_.find(characterId);
    if (cit == characterToSession_.end())
        return nullptr;
    auto sit = sessions_.find(cit->second);
    return (sit != sessions_.end()) ? &sit->second : nullptr;
}

void
DialogueSessionManager::closeSession(const std::string &sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return;

    int characterId = it->second.characterId;
    sessions_.erase(it);
    characterToSession_.erase(characterId);

    logger_.log("[DialogueSession] Closed session " + sessionId);
}

void
DialogueSessionManager::closeSessionByCharacter(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto cit = characterToSession_.find(characterId);
    if (cit == characterToSession_.end())
        return;

    sessions_.erase(cit->second);
    characterToSession_.erase(cit);
}

void
DialogueSessionManager::cleanupExpiredSessions()
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> toRemove;

    for (const auto &[sid, session] : sessions_)
    {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - session.lastActivity)
                       .count();
        if (age >= DialogueSessionStruct::TTL_SECONDS)
            toRemove.push_back(sid);
    }

    for (const auto &sid : toRemove)
    {
        auto it = sessions_.find(sid);
        if (it != sessions_.end())
        {
            characterToSession_.erase(it->second.characterId);
            sessions_.erase(it);
            logger_.log("[DialogueSession] Expired session removed: " + sid);
        }
    }

    if (!toRemove.empty())
        logger_.log("[DialogueSession] Cleaned up " + std::to_string(toRemove.size()) +
                    " expired dialogue sessions.");
}
