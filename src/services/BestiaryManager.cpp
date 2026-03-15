#include "services/BestiaryManager.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

// Protocol-defined category slugs for tiers 1-6 (index = tierNum - 1)
const std::vector<std::string> BestiaryManager::kCategorySlugs_ = {
    "basic_info", "weaknesses", "common_loot", "uncommon_loot", "rare_loot", "very_rare_loot"};

BestiaryManager::BestiaryManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger_.getSystem("bestiary");
}

void
BestiaryManager::loadBestiaryData(int characterId, const std::vector<std::pair<int, int>> &entries)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto &charMap = data_[characterId];
    for (const auto &[mobTemplateId, killCount] : entries)
    {
        charMap[mobTemplateId] = killCount;
    }
    log_->info("[Bestiary] Loaded " + std::to_string(entries.size()) +
               " entries for char " + std::to_string(characterId));
}

int
BestiaryManager::getKillCount(int characterId, int mobTemplateId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto charIt = data_.find(characterId);
    if (charIt == data_.end())
        return 0;
    auto mobIt = charIt->second.find(mobTemplateId);
    return (mobIt != charIt->second.end()) ? mobIt->second : 0;
}

std::vector<int>
BestiaryManager::getRevealedTiers(int characterId, int mobTemplateId, const std::vector<int> &thresholds) const
{
    int kills = getKillCount(characterId, mobTemplateId);
    std::vector<int> revealed;
    for (int i = 0; i < static_cast<int>(thresholds.size()); ++i)
    {
        if (kills >= thresholds[i])
            revealed.push_back(i + 1); // 1-based tier index
    }
    return revealed;
}

nlohmann::json
BestiaryManager::buildEntryJson(
    int characterId,
    int mobTemplateId,
    const std::string &mobSlug,
    const MobDataStruct &mobStatic,
    const std::vector<std::string> &weaknesses,
    const std::vector<std::string> &resistances,
    const std::vector<MobLootInfoStruct> &allLootForMob,
    const std::function<std::string(int)> &itemSlugFn) const
{
    std::vector<int> threshCopy;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        threshCopy = thresholds_;
    }
    if (threshCopy.empty())
        threshCopy = {1, 5, 15, 30, 75, 150}; // protocol defaults

    int kills = getKillCount(characterId, mobTemplateId);

    nlohmann::json entry;
    entry["mobTemplateId"] = mobTemplateId;
    entry["mobSlug"] = mobSlug;
    entry["killCount"] = kills;

    nlohmann::json tiersArr = nlohmann::json::array();
    bool nextLockedSeen = false;

    for (int i = 0; i < static_cast<int>(threshCopy.size()); ++i)
    {
        const std::string &catSlug = (i < static_cast<int>(kCategorySlugs_.size()))
                                         ? kCategorySlugs_[i]
                                         : ("tier_" + std::to_string(i + 1));

        bool unlocked = (kills >= threshCopy[i]);

        nlohmann::json tierObj;
        tierObj["tier"] = i + 1;
        tierObj["categorySlug"] = catSlug;
        tierObj["requiredKills"] = threshCopy[i];
        tierObj["unlocked"] = unlocked;

        if (!unlocked && !nextLockedSeen)
        {
            tierObj["requiredKillsLeft"] = threshCopy[i] - kills;
            nextLockedSeen = true;
        }

        if (unlocked)
        {
            nlohmann::json data;
            if (catSlug == "basic_info")
            {
                data["level"] = mobStatic.level;
                data["rank"] = mobStatic.rankCode;
                data["hpMin"] = mobStatic.hpMin;
                data["hpMax"] = mobStatic.hpMax;
                data["type"] = mobStatic.mobTypeSlug;
                data["biomeSlug"] = mobStatic.biomeSlug;
            }
            else if (catSlug == "weaknesses")
            {
                data["weaknesses"] = weaknesses;
                data["resistances"] = resistances;
            }
            else
            {
                // Loot tiers: catSlug → lootTier value in MobLootInfoStruct
                std::string lootTierVal;
                if (catSlug == "common_loot")
                    lootTierVal = "common";
                else if (catSlug == "uncommon_loot")
                    lootTierVal = "uncommon";
                else if (catSlug == "rare_loot")
                    lootTierVal = "rare";
                else if (catSlug == "very_rare_loot")
                    lootTierVal = "very_rare";

                nlohmann::json lootArr = nlohmann::json::array();
                for (const auto &li : allLootForMob)
                {
                    if (li.lootTier == lootTierVal)
                    {
                        nlohmann::json lootEntry;
                        lootEntry["itemSlug"] = itemSlugFn(li.itemId);
                        lootEntry["chance"] = li.dropChance;
                        lootArr.push_back(lootEntry);
                    }
                }
                data["loot"] = lootArr;
            }
            tierObj["data"] = data;
        }

        tiersArr.push_back(tierObj);
    }

    entry["tiers"] = tiersArr;
    return entry;
}

void
BestiaryManager::recordKill(int characterId, int mobTemplateId)
{
    int newCount = 0;
    std::vector<int> threshCopy;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        newCount = ++data_[characterId][mobTemplateId];
        threshCopy = thresholds_;
    }
    persist(characterId, mobTemplateId, newCount);

    // Detect newly crossed tier thresholds and fire notifications
    if (notifyCallback_ && !threshCopy.empty())
    {
        int oldCount = newCount - 1;
        for (int i = 0; i < static_cast<int>(threshCopy.size()); ++i)
        {
            if (oldCount < threshCopy[i] && newCount >= threshCopy[i])
            {
                const std::string &catSlug = (i < static_cast<int>(kCategorySlugs_.size()))
                                                 ? kCategorySlugs_[i]
                                                 : ("tier_" + std::to_string(i + 1));
                notifyCallback_(characterId, mobTemplateId, i + 1, catSlug);
            }
        }
    }
}

void
BestiaryManager::setThresholds(std::vector<int> thresholds)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    thresholds_ = std::move(thresholds);
}

void
BestiaryManager::setSaveCallback(std::function<void(const std::string &)> callback)
{
    saveCallback_ = std::move(callback);
}

void
BestiaryManager::setNotifyCallback(std::function<void(int, int, int, const std::string &)> callback)
{
    notifyCallback_ = std::move(callback);
}

void
BestiaryManager::persist(int characterId, int mobTemplateId, int killCount)
{
    if (!saveCallback_)
        return;
    try
    {
        nlohmann::json pkt;
        pkt["header"]["eventType"] = "saveBestiaryKill";
        pkt["body"]["characterId"] = characterId;
        pkt["body"]["mobTemplateId"] = mobTemplateId;
        pkt["body"]["killCount"] = killCount;
        saveCallback_(pkt.dump() + "\n");
    }
    catch (const std::exception &e)
    {
        log_->error("[Bestiary] persist error: " + std::string(e.what()));
    }
}
