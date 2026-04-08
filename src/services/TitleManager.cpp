#include "services/TitleManager.hpp"
#include "services/GameServices.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

TitleManager::TitleManager(GameServices *gs)
    : gs_(gs),
      log_(spdlog::get("chunk-server"))
{
}

// ── Static data ────────────────────────────────────────────────────────────────

void
TitleManager::loadTitleDefinitions(const std::vector<TitleDefinitionStruct> &definitions)
{
    std::unique_lock lk(mutex_);
    definitions_.clear();
    for (const auto &d : definitions)
        definitions_[d.slug] = d;
    log_->info("[Title] Loaded {} title definitions", definitions.size());
}

TitleDefinitionStruct
TitleManager::getTitleDefinition(const std::string &slug) const
{
    std::shared_lock lk(mutex_);
    auto it = definitions_.find(slug);
    return it != definitions_.end() ? it->second : TitleDefinitionStruct{};
}

std::vector<TitleDefinitionStruct>
TitleManager::getAllDefinitions() const
{
    std::shared_lock lk(mutex_);
    std::vector<TitleDefinitionStruct> out;
    out.reserve(definitions_.size());
    for (const auto &[slug, def] : definitions_)
        out.push_back(def);
    return out;
}

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void
TitleManager::loadPlayerTitles(int characterId, const PlayerTitleStateStruct &state)
{
    {
        std::unique_lock lk(mutex_);
        data_[characterId] = state;
    }
    // Re-apply equipped title bonuses on every login (effects may have been wiped by SET_PLAYER_ACTIVE_EFFECTS)
    if (!state.equippedSlug.empty())
        applyTitleBonuses(characterId, state.equippedSlug);

    // Push current title state to client so they see earned/equipped titles immediately on login
    sendTitleUpdateToClient(characterId);

    log_->info("[Title] Loaded {} titles for char={}, equipped='{}'",
        state.earnedSlugs.size(),
        characterId,
        state.equippedSlug);
}

void
TitleManager::unloadPlayerTitles(int characterId)
{
    std::unique_lock lk(mutex_);
    data_.erase(characterId);
}

void
TitleManager::reapplyEquippedBonuses(int characterId)
{
    std::string equipped;
    {
        std::shared_lock lk(mutex_);
        auto it = data_.find(characterId);
        if (it == data_.end() || it->second.equippedSlug.empty())
            return;
        equipped = it->second.equippedSlug;
    }
    applyTitleBonuses(characterId, equipped);
}

// ── Query ──────────────────────────────────────────────────────────────────────

PlayerTitleStateStruct
TitleManager::getPlayerTitles(int characterId) const
{
    std::shared_lock lk(mutex_);
    auto it = data_.find(characterId);
    return it != data_.end() ? it->second : PlayerTitleStateStruct{};
}

bool
TitleManager::hasTitle(int characterId, const std::string &titleSlug) const
{
    std::shared_lock lk(mutex_);
    auto it = data_.find(characterId);
    if (it == data_.end())
        return false;
    const auto &earned = it->second.earnedSlugs;
    return std::find(earned.begin(), earned.end(), titleSlug) != earned.end();
}

// ── Mutation ───────────────────────────────────────────────────────────────────

void
TitleManager::grantTitle(int characterId, const std::string &titleSlug)
{
    if (titleSlug.empty())
        return;

    bool isNew = false;
    std::vector<std::string> earned;
    std::string equipped;
    {
        std::unique_lock lk(mutex_);
        auto &state = data_[characterId];
        auto &ev = state.earnedSlugs;
        if (std::find(ev.begin(), ev.end(), titleSlug) == ev.end())
        {
            ev.push_back(titleSlug);
            isNew = true;
        }
        earned = state.earnedSlugs;
        equipped = state.equippedSlug;
    }

    if (!isNew)
        return;

    persist(characterId, equipped, earned);
    sendTitleUpdateToClient(characterId);
    log_->info("[Title] Granted '{}' to char={}", titleSlug, characterId);
}

bool
TitleManager::equipTitle(int characterId, const std::string &titleSlug)
{
    // Unequip: empty slug is always valid
    if (!titleSlug.empty() && !hasTitle(characterId, titleSlug))
    {
        log_->warn("[Title] equipTitle: char={} does not have title '{}'", characterId, titleSlug);
        return false;
    }

    std::string oldSlug;
    std::vector<std::string> earned;
    {
        std::unique_lock lk(mutex_);
        auto it = data_.find(characterId);
        if (it == data_.end())
        {
            log_->warn("[Title] equipTitle: char={} not loaded", characterId);
            return false;
        }
        oldSlug = it->second.equippedSlug;
        it->second.equippedSlug = titleSlug;
        earned = it->second.earnedSlugs;
    }

    // Swap effects outside the lock
    if (!oldSlug.empty())
        removeTitleBonuses(characterId, oldSlug);
    if (!titleSlug.empty())
        applyTitleBonuses(characterId, titleSlug);

    persist(characterId, titleSlug, earned);
    sendTitleUpdateToClient(characterId);

    if (gs_)
        gs_->getStatsNotificationService().sendStatsUpdate(characterId);

    log_->info("[Title] char={} equipped '{}' (was '{}')", characterId, titleSlug, oldSlug);
    return true;
}

// ── Private helpers ────────────────────────────────────────────────────────────

void
TitleManager::applyTitleBonuses(int characterId, const std::string &titleSlug)
{
    if (!gs_)
        return;
    TitleDefinitionStruct def = getTitleDefinition(titleSlug);
    if (def.id == 0 || def.bonuses.empty())
        return;

    for (const auto &bonus : def.bonuses)
    {
        ActiveEffectStruct eff;
        eff.effectSlug = "title_" + titleSlug + "_" + bonus.attributeSlug;
        eff.effectTypeSlug = "title_bonus";
        eff.attributeSlug = bonus.attributeSlug;
        eff.value = bonus.value;
        eff.sourceType = "title";
        eff.expiresAt = 0; // permanent
        eff.tickMs = 0;    // stat modifier, not DoT
        try
        {
            gs_->getCharacterManager().addActiveEffect(characterId, eff);
        }
        catch (const std::exception &e)
        {
            log_->error("[Title] applyTitleBonuses error for char={}: {}", characterId, e.what());
        }
    }
}

void
TitleManager::removeTitleBonuses(int characterId, const std::string &titleSlug)
{
    if (!gs_)
        return;
    TitleDefinitionStruct def = getTitleDefinition(titleSlug);
    if (def.id == 0 || def.bonuses.empty())
        return;

    for (const auto &bonus : def.bonuses)
    {
        const std::string effectSlug = "title_" + titleSlug + "_" + bonus.attributeSlug;
        try
        {
            gs_->getCharacterManager().removeActiveEffectBySlug(characterId, effectSlug);
        }
        catch (const std::exception &e)
        {
            log_->error("[Title] removeTitleBonuses error for char={}: {}", characterId, e.what());
        }
    }
}

void
TitleManager::persist(int characterId,
    const std::string &equippedSlug,
    const std::vector<std::string> &earned)
{
    if (!saveCallback_)
        return;
    try
    {
        nlohmann::json pkt;
        pkt["header"]["eventType"] = "savePlayerTitle";
        pkt["body"]["characterId"] = characterId;
        pkt["body"]["equippedSlug"] = equippedSlug;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &s : earned)
            arr.push_back(s);
        pkt["body"]["earnedSlugs"] = arr;
        saveCallback_(pkt.dump() + "\n");
    }
    catch (const std::exception &e)
    {
        log_->error("[Title] persist error for char={}: {}", characterId, e.what());
    }
}

void
TitleManager::sendTitleUpdateToClient(int characterId)
{
    if (!notifyClientCallback_)
        return;
    try
    {
        PlayerTitleStateStruct state = getPlayerTitles(characterId);

        nlohmann::json earnedArr = nlohmann::json::array();
        for (const auto &slug : state.earnedSlugs)
        {
            TitleDefinitionStruct def = getTitleDefinition(slug);
            nlohmann::json entry;
            entry["slug"] = slug;
            entry["displayName"] = def.displayName;
            entry["description"] = def.description;
            entry["earnCondition"] = def.earnCondition;
            nlohmann::json bonusArr = nlohmann::json::array();
            for (const auto &b : def.bonuses)
                bonusArr.push_back({{"attributeSlug", b.attributeSlug}, {"value", b.value}});
            entry["bonuses"] = bonusArr;
            earnedArr.push_back(entry);
        }

        nlohmann::json equippedEntry = nullptr;
        if (!state.equippedSlug.empty())
        {
            TitleDefinitionStruct def = getTitleDefinition(state.equippedSlug);
            equippedEntry = {
                {"slug", state.equippedSlug},
                {"displayName", def.displayName},
                {"description", def.description},
                {"earnCondition", def.earnCondition}};
            nlohmann::json bonusArr = nlohmann::json::array();
            for (const auto &b : def.bonuses)
                bonusArr.push_back({{"attributeSlug", b.attributeSlug}, {"value", b.value}});
            equippedEntry["bonuses"] = bonusArr;
        }

        nlohmann::json pkt;
        pkt["header"]["eventType"] = "player_titles_update";
        pkt["header"]["status"] = "success";
        pkt["body"]["characterId"] = characterId;
        pkt["body"]["equippedTitle"] = equippedEntry;
        pkt["body"]["earnedTitles"] = earnedArr;

        notifyClientCallback_(characterId, pkt);
    }
    catch (const std::exception &e)
    {
        log_->error("[Title] sendTitleUpdateToClient error for char={}: {}", characterId, e.what());
    }
}
