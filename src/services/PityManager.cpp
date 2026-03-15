#include "services/PityManager.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

PityManager::PityManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger_.getSystem("pity");
}

void
PityManager::loadPityData(int characterId, const std::vector<std::pair<int, int>> &entries)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (const auto &[itemId, killCount] : entries)
    {
        counters_[makeKey(characterId, itemId)] = killCount;
    }
    log_->info("[Pity] Loaded " + std::to_string(entries.size()) +
               " counters for char " + std::to_string(characterId));
}

int
PityManager::getKillCount(int characterId, int itemId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = counters_.find(makeKey(characterId, itemId));
    return (it != counters_.end()) ? it->second : 0;
}

float
PityManager::getExtraDropChance(int characterId, int itemId, int softPityKills, float softBonusPerKill) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = counters_.find(makeKey(characterId, itemId));
    if (it == counters_.end())
        return 0.0f;
    int cnt = it->second;
    if (cnt <= softPityKills)
        return 0.0f;
    return static_cast<float>(cnt - softPityKills) * softBonusPerKill;
}

bool
PityManager::isHardPity(int characterId, int itemId, int hardPityKills) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = counters_.find(makeKey(characterId, itemId));
    if (it == counters_.end())
        return false;
    return it->second >= hardPityKills;
}

void
PityManager::incrementCounter(int characterId, int itemId, int hintThreshold, std::function<void()> hintCallback)
{
    int newCount = 0;
    bool crossedHint = false;

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto key = makeKey(characterId, itemId);
        newCount = ++counters_[key];
        // Only fire hint callback on the exact crossing tick to avoid spam
        crossedHint = (hintThreshold > 0 && newCount == hintThreshold);
    }

    if (crossedHint && hintCallback)
        hintCallback();

    // Persist every 10 increments to reduce write pressure
    if (newCount % 10 == 0)
        persist(characterId, itemId, newCount);
}

void
PityManager::resetCounter(int characterId, int itemId)
{
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        counters_[makeKey(characterId, itemId)] = 0;
    }
    persist(characterId, itemId, 0);
}

void
PityManager::setSaveCallback(std::function<void(const std::string &)> callback)
{
    saveCallback_ = std::move(callback);
}

void
PityManager::persist(int characterId, int itemId, int killCount)
{
    if (!saveCallback_)
        return;
    try
    {
        nlohmann::json pkt;
        pkt["header"]["eventType"] = "savePityCounter";
        pkt["body"]["characterId"] = characterId;
        pkt["body"]["itemId"] = itemId;
        pkt["body"]["killCount"] = killCount;
        saveCallback_(pkt.dump() + "\n");
    }
    catch (const std::exception &e)
    {
        log_->error("[Pity] persist error: " + std::string(e.what()));
    }
}
