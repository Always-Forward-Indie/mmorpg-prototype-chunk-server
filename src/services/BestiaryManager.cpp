#include "services/BestiaryManager.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

// Protocol-defined category slugs for tiers 1-6 (index = tierNum - 1)
// Actual kill thresholds come from game_config (bestiary.tierN_kills); these labels are stable.
const std::vector<std::string> BestiaryManager::kCategorySlugs_ = {
    "basic_info",    // T1 — level, rank, HP range, type, biome
    "lore",          // T2 — lore key (client resolves from locale)
    "combat_info",   // T3 — weaknesses, resistances, ability slugs
    "loot_table",    // T4 — full item list (slug only, no chances)
    "drop_rates",    // T5 — full item list with drop chances
    "hunter_mastery" // T6 — title/achievement milestone
};

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

std::vector<std::pair<int, int>>
BestiaryManager::getKnownMobs(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<int, int>> result;
    auto charIt = data_.find(characterId);
    if (charIt == data_.end())
        return result;
    for (const auto &[mobTemplateId, killCount] : charIt->second)
    {
        if (killCount > 0)
            result.emplace_back(mobTemplateId, killCount);
    }
    return result;
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
    const std::vector<std::string> &abilities,
    const std::vector<MobLootInfoStruct> &allLootForMob,
    const std::function<std::string(int)> &itemSlugFn) const
{
    std::vector<int> threshCopy;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        threshCopy = thresholds_;
    }
    if (threshCopy.empty())
        threshCopy = {1, 10, 25, 50, 100, 300}; // protocol defaults

    int kills = getKillCount(characterId, mobTemplateId);

    nlohmann::json entry;
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
            else if (catSlug == "lore")
            {
                // Lore text lives in the client locale under mobs.{mobSlug}.lore
                data["loreKey"] = mobSlug;
            }
            else if (catSlug == "combat_info")
            {
                data["weaknesses"] = weaknesses;
                data["resistances"] = resistances;
                data["abilities"] = abilities;
            }
            else if (catSlug == "loot_table")
            {
                // All item slugs without drop chances — tells player what to look for
                nlohmann::json items = nlohmann::json::array();
                std::vector<std::string> seen;
                for (const auto &li : allLootForMob)
                {
                    if (!li.isHarvestOnly)
                    {
                        std::string slug = itemSlugFn(li.itemId);
                        if (!slug.empty() && std::find(seen.begin(), seen.end(), slug) == seen.end())
                        {
                            items.push_back(slug);
                            seen.push_back(slug);
                        }
                    }
                }
                data["items"] = items;
            }
            else if (catSlug == "drop_rates")
            {
                // Full loot table with exact chances
                nlohmann::json lootArr = nlohmann::json::array();
                for (const auto &li : allLootForMob)
                {
                    if (!li.isHarvestOnly)
                    {
                        nlohmann::json lootEntry;
                        lootEntry["itemSlug"] = itemSlugFn(li.itemId);
                        lootEntry["chance"] = li.dropChance;
                        lootArr.push_back(lootEntry);
                    }
                }
                data["loot"] = lootArr;
            }
            else if (catSlug == "hunter_mastery")
            {
                // Client localises these via locale keys
                data["titleSlug"] = mobSlug + "_hunter";
                data["achievementSlug"] = mobSlug + "_master";
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

    // Always notify client of the updated kill count so the overview list stays in sync
    if (killUpdateCallback_)
        killUpdateCallback_(characterId, mobTemplateId, newCount);

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
                notifyCallback_(characterId, mobTemplateId, i + 1, newCount, catSlug);
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
BestiaryManager::setNotifyCallback(std::function<void(int, int, int, int, const std::string &)> callback)
{
    notifyCallback_ = std::move(callback);
}

void
BestiaryManager::setKillUpdateCallback(std::function<void(int, int, int)> callback)
{
    killUpdateCallback_ = std::move(callback);
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
