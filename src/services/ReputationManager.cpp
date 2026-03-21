#include "services/ReputationManager.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

ReputationManager::ReputationManager(Logger &logger)
    : logger_(logger),
      log_(spdlog::get("chunk-server"))
{
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void
ReputationManager::loadCharacterReputations(int characterId,
    const std::unordered_map<std::string, int> &reps)
{
    std::unique_lock lk(mutex_);
    data_[characterId] = reps;
    log_->info("[Reputation] Loaded {} faction entries for char={}", reps.size(), characterId);
}

void
ReputationManager::unloadCharacterReputations(int characterId)
{
    std::unique_lock lk(mutex_);
    data_.erase(characterId);
}

// ── Query ──────────────────────────────────────────────────────────────────

int
ReputationManager::getReputation(int characterId,
    const std::string &factionSlug) const
{
    std::shared_lock lk(mutex_);
    auto cit = data_.find(characterId);
    if (cit == data_.end())
        return 0;
    auto fit = cit->second.find(factionSlug);
    return fit != cit->second.end() ? fit->second : 0;
}

void
ReputationManager::fillReputationContext(int characterId,
    PlayerContextStruct &ctx) const
{
    std::shared_lock lk(mutex_);
    auto cit = data_.find(characterId);
    if (cit == data_.end())
        return;
    ctx.reputations = cit->second;
}

// static
std::string
ReputationManager::getTier(int value)
{
    if (value < -500)
        return "enemy";
    if (value < 0)
        return "stranger";
    if (value < 200)
        return "neutral";
    if (value < 500)
        return "friendly";
    return "ally";
}

// ── Mutation ───────────────────────────────────────────────────────────────

void
ReputationManager::changeReputation(int characterId,
    const std::string &factionSlug,
    int delta)
{
    if (factionSlug.empty() || delta == 0)
        return;

    int oldValue = 0;
    int newValue = 0;
    {
        std::unique_lock lk(mutex_);
        auto &charMap = data_[characterId];
        oldValue = charMap[factionSlug]; // default 0 if missing
        newValue = oldValue + delta;
        charMap[factionSlug] = newValue;
    }

    persist(characterId, factionSlug, newValue);

    // Fire tier-change callback if boundary crossed
    if (tierChangeCallback_)
    {
        if (getTier(oldValue) != getTier(newValue))
            tierChangeCallback_(characterId, factionSlug, getTier(newValue), newValue);
    }

    log_->info("[Reputation] char={} faction={} {} -> {} ({}{})",
        characterId,
        factionSlug,
        oldValue,
        newValue,
        delta >= 0 ? "+" : "",
        delta);
}

// ── Persistence ────────────────────────────────────────────────────────────

void
ReputationManager::persist(int characterId,
    const std::string &factionSlug,
    int value)
{
    if (!saveCallback_)
        return;
    try
    {
        nlohmann::json pkt;
        pkt["header"]["eventType"] = "saveReputation";
        pkt["body"]["characterId"] = characterId;
        pkt["body"]["factionSlug"] = factionSlug;
        pkt["body"]["value"] = value;
        saveCallback_(pkt.dump() + "\n");
    }
    catch (const std::exception &e)
    {
        log_->error("[Reputation] persist error: {}", e.what());
    }
}
