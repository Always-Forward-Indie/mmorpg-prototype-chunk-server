#pragma once

#include "utils/Logger.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace spdlog
{
class logger;
}

/**
 * @brief Thread-safe per-caster skill cooldown store (extracted from SkillSystem).
 *
 * Single shared instance owned by GameServices: every combat path (player
 * skills, mob AI, login-time restore) reads and writes the same cooldown
 * table, so a skill put on cooldown by one path is visible to all others.
 * Previously two SkillSystem instances each held a private table (split-brain).
 *
 * Keys: entityId -> (skillSlug -> ready-at time_point). The per-caster Global
 * Cooldown lives under the internal "__gcd__" key.
 */
class CooldownService
{
  public:
    explicit CooldownService(Logger &logger);

    /**
     * @brief Проверить доступность скила (только кулдаун, без маны).
     */
    bool isSkillAvailable(int casterId, const std::string &skillSlug);

    /**
     * @brief Установить кулдаун.
     */
    void setCooldown(int casterId, const std::string &skillSlug, int cooldownMs);

    /**
     * @brief Проверить кулдаун.
     */
    bool isOnCooldown(int casterId, const std::string &skillSlug);

    /**
     * @brief Проверить, активен ли Global Cooldown для кастера (реад-онли, без потребления).
     */
    bool isGCDActive(int casterId);

    /**
     * @brief HIGH-1 fix: Atomically check that the skill is NOT on cooldown and
     *        immediately set it if so.  Returns true (cooldown set, proceed with
     *        skill execution) or false (already on cooldown, reject).  Both
     *        check and set happen under the same unique_lock, eliminating the
     *        TOCTOU window between isSkillAvailable() and setCooldown().
     *
     * @param gcdMs   If > 0, also checks the per-caster Global Cooldown (stored
     *                under the internal "__gcd__" key) and sets it atomically
     *                alongside the per-skill cooldown.  Pass 0 to skip GCD.
     * @param outOnGCD  Set to true when the rejection reason is GCD (vs per-skill
     *                  cooldown), so callers can send the right error message.
     */
    bool trySetCooldown(int casterId, const std::string &skillSlug, int cooldownMs, int gcdMs = 0, bool *outOnGCD = nullptr);

    /**
     * @brief Restore a cooldown from a persisted remaining duration (e.g. on login).
     *        Always sets the entry, regardless of whether one exists already.
     *        No-op when remainingMs <= 0.
     */
    void restoreCooldown(int casterId, const std::string &skillSlug, int64_t remainingMs);

    /**
     * @brief Обновить кулдауны (удалить истекшие).
     */
    void updateCooldowns();

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Кулдауны: entityId -> (skillSlug -> timepoint)
    std::unordered_map<int, std::unordered_map<std::string, std::chrono::steady_clock::time_point>> cooldowns_;
    mutable std::shared_mutex cooldownsMutex_; // protects cooldowns_
};
