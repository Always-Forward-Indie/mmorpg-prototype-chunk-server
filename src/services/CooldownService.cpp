#include "services/CooldownService.hpp"
#include "utils/Logger.hpp"
#include <spdlog/logger.h>

namespace
{
const std::string GCD_KEY = "__gcd__";
}

CooldownService::CooldownService(Logger &logger)
    : logger_(logger)
{
    log_ = logger_.getSystem("skill");
}

bool
CooldownService::isSkillAvailable(int casterId, const std::string &skillSlug)
{
    // Проверяем кулдаун
    if (isOnCooldown(casterId, skillSlug))
    {
        return false;
    }

    // Дополнительные проверки можно добавить здесь
    return true;
}

void
CooldownService::setCooldown(int casterId, const std::string &skillSlug, int cooldownMs)
{
    auto now = std::chrono::steady_clock::now();
    auto endTime = now + std::chrono::milliseconds(cooldownMs);
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);
    cooldowns_[casterId][skillSlug] = endTime;
}

bool
CooldownService::trySetCooldown(int casterId, const std::string &skillSlug, int cooldownMs, int gcdMs, bool *outOnGCD)
{
    // HIGH-1: single unique_lock covers both the check and the set.
    // No other thread can sneak in between, eliminating the TOCTOU race.
    auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);

    auto &perEntity = cooldowns_[casterId];

    // Check per-skill cooldown
    auto &skillEntry = perEntity[skillSlug];
    if (now < skillEntry)
    {
        if (outOnGCD)
            *outOnGCD = false;
        return false;
    }

    // Check Global Cooldown (players only — gcdMs > 0 means caller wants GCD)
    if (gcdMs > 0)
    {
        auto &gcdEntry = perEntity[GCD_KEY];
        if (now < gcdEntry)
        {
            if (outOnGCD)
                *outOnGCD = true;
            return false;
        }
        // Set GCD
        gcdEntry = now + std::chrono::milliseconds(gcdMs);
    }

    // Set per-skill cooldown
    skillEntry = now + std::chrono::milliseconds(cooldownMs);
    return true;
}

bool
CooldownService::isOnCooldown(int casterId, const std::string &skillSlug)
{
    std::shared_lock<std::shared_mutex> lock(cooldownsMutex_);
    auto it = cooldowns_.find(casterId);
    if (it == cooldowns_.end())
        return false;
    auto skillIt = it->second.find(skillSlug);
    if (skillIt == it->second.end())
        return false;
    return std::chrono::steady_clock::now() < skillIt->second;
}

void
CooldownService::restoreCooldown(int casterId, const std::string &skillSlug, int64_t remainingMs)
{
    if (remainingMs <= 0)
        return;
    auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(remainingMs);
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);
    cooldowns_[casterId][skillSlug] = endTime;
}

bool
CooldownService::isGCDActive(int casterId)
{
    std::shared_lock<std::shared_mutex> lock(cooldownsMutex_);
    auto it = cooldowns_.find(casterId);
    if (it == cooldowns_.end())
        return false;
    auto gcdIt = it->second.find(GCD_KEY);
    if (gcdIt == it->second.end())
        return false;
    return std::chrono::steady_clock::now() < gcdIt->second;
}

void
CooldownService::updateCooldowns()
{
    auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);
    for (auto &casterCooldowns : cooldowns_)
    {
        auto it = casterCooldowns.second.begin();
        while (it != casterCooldowns.second.end())
        {
            if (now >= it->second)
            {
                it = casterCooldowns.second.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
