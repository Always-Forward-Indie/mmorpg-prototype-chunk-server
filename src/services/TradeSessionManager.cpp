#include "services/TradeSessionManager.hpp"
#include <chrono>
#include <spdlog/logger.h>

TradeSessionManager::TradeSessionManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("trade");
}

TradeSessionStruct &
TradeSessionManager::createSession(
    int clientAId, int charAId, int clientBId, int charBId)
{
    std::lock_guard lock(mutex_);

    // Close any existing session for either character
    for (int charId : {charAId, charBId})
    {
        auto it = characterToSession_.find(charId);
        if (it != characterToSession_.end())
        {
            sessions_.erase(it->second);
            characterToSession_.erase(it);
        }
    }

    auto now = std::chrono::steady_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch())
                       .count();
    std::string sessionId =
        "trade_" + std::to_string(charAId) + "_" + std::to_string(charBId) + "_" + std::to_string(ms);

    TradeSessionStruct session;
    session.sessionId = sessionId;
    session.charAId = charAId;
    session.charBId = charBId;
    session.clientAId = clientAId;
    session.clientBId = clientBId;
    session.lastActivity = now;

    sessions_[sessionId] = std::move(session);
    characterToSession_[charAId] = sessionId;
    characterToSession_[charBId] = sessionId;

    logger_.log("[TradeSession] Created session " + sessionId +
                " between chars " + std::to_string(charAId) + " and " + std::to_string(charBId));
    return sessions_[sessionId];
}

TradeSessionStruct *
TradeSessionManager::getSession(const std::string &sessionId)
{
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(sessionId);
    return (it != sessions_.end()) ? &it->second : nullptr;
}

TradeSessionStruct *
TradeSessionManager::getSessionByCharacter(int characterId)
{
    std::lock_guard lock(mutex_);
    auto cit = characterToSession_.find(characterId);
    if (cit == characterToSession_.end())
        return nullptr;
    auto sit = sessions_.find(cit->second);
    return (sit != sessions_.end()) ? &sit->second : nullptr;
}

void
TradeSessionManager::closeSession(const std::string &sessionId)
{
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end())
        return;
    characterToSession_.erase(it->second.charAId);
    characterToSession_.erase(it->second.charBId);
    sessions_.erase(it);
}

void
TradeSessionManager::closeSessionByCharacter(int characterId)
{
    std::lock_guard lock(mutex_);
    auto cit = characterToSession_.find(characterId);
    if (cit == characterToSession_.end())
        return;
    const std::string &sid = cit->second;
    auto sit = sessions_.find(sid);
    if (sit != sessions_.end())
    {
        characterToSession_.erase(sit->second.charAId);
        characterToSession_.erase(sit->second.charBId);
        sessions_.erase(sit);
    }
    else
    {
        characterToSession_.erase(cit);
    }
}

void
TradeSessionManager::cleanupExpiredSessions()
{
    std::lock_guard lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end();)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.lastActivity)
                           .count();
        if (elapsed > TradeSessionStruct::TTL_SECONDS)
        {
            characterToSession_.erase(it->second.charAId);
            characterToSession_.erase(it->second.charBId);
            log_->info("[TradeSession] TTL-expired session {}", it->first);
            it = sessions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
