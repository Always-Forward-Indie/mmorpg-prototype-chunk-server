#include "services/EmoteManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>

EmoteManager::EmoteManager(Logger &logger)
    : logger_(logger)
{
}

// ── Static definitions ─────────────────────────────────────────────────────

void
EmoteManager::loadEmoteDefinitions(const std::vector<EmoteDefinitionStruct> &defs)
{
    std::unique_lock lk(mutex_);
    definitions_.clear();
    for (const auto &d : defs)
        definitions_[d.slug] = d;
}

EmoteDefinitionStruct
EmoteManager::getEmoteDefinition(const std::string &slug) const
{
    std::shared_lock lk(mutex_);
    auto it = definitions_.find(slug);
    return it != definitions_.end() ? it->second : EmoteDefinitionStruct{};
}

std::vector<EmoteDefinitionStruct>
EmoteManager::getAllDefinitions() const
{
    std::shared_lock lk(mutex_);
    std::vector<EmoteDefinitionStruct> out;
    out.reserve(definitions_.size());
    for (const auto &[slug, def] : definitions_)
        out.push_back(def);
    std::sort(out.begin(), out.end(), [](const EmoteDefinitionStruct &a, const EmoteDefinitionStruct &b)
        { return a.sortOrder < b.sortOrder; });
    return out;
}

// ── Per-character lifecycle ────────────────────────────────────────────────

void
EmoteManager::loadPlayerEmotes(int characterId, const std::vector<std::string> &unlockedSlugs)
{
    std::unique_lock lk(mutex_);
    playerEmotes_[characterId] = unlockedSlugs;
}

void
EmoteManager::unloadPlayerEmotes(int characterId)
{
    std::unique_lock lk(mutex_);
    playerEmotes_.erase(characterId);
}

// ── Queries ────────────────────────────────────────────────────────────────

bool
EmoteManager::isUnlocked(int characterId, const std::string &emoteSlug) const
{
    std::shared_lock lk(mutex_);
    auto it = playerEmotes_.find(characterId);
    if (it != playerEmotes_.end())
    {
        const auto &slugs = it->second;
        if (std::find(slugs.begin(), slugs.end(), emoteSlug) != slugs.end())
            return true;
    }
    // Default emotes are granted automatically to every character.
    auto defIt = definitions_.find(emoteSlug);
    return defIt != definitions_.end() && defIt->second.isDefault;
}

std::vector<EmoteDefinitionStruct>
EmoteManager::getPlayerEmotes(int characterId) const
{
    std::shared_lock lk(mutex_);
    std::vector<EmoteDefinitionStruct> out;
    auto it = playerEmotes_.find(characterId);
    if (it != playerEmotes_.end())
    {
        out.reserve(it->second.size());
        for (const auto &slug : it->second)
        {
            auto defIt = definitions_.find(slug);
            if (defIt != definitions_.end())
                out.push_back(defIt->second);
        }
    }
    // Union with default emotes (dedupe by slug).
    for (const auto &[slug, def] : definitions_)
    {
        if (!def.isDefault)
            continue;
        bool present = false;
        for (const auto &e : out)
        {
            if (e.slug == slug)
            {
                present = true;
                break;
            }
        }
        if (!present)
            out.push_back(def);
    }
    std::sort(out.begin(), out.end(), [](const EmoteDefinitionStruct &a, const EmoteDefinitionStruct &b)
        { return a.sortOrder < b.sortOrder; });
    return out;
}
