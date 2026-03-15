#pragma once

#include <memory>

namespace spdlog
{
class logger;
}
class GameServices;

/**
 * @brief Manages automatic HP and MP regeneration for all active characters.
 *
 * Design decisions:
 *  - Regen is suppressed for `disableInCombatMs` milliseconds after the last
 *    damage event (markCharacterInCombat must be called by CombatSystem).
 *  - Regen amounts are driven by character attributes (CON for HP, WIS for MP)
 *    and tunable coefficients from GameConfigService.
 *  - All config keys carry the prefix "regen." so they can be managed in the
 *    shared config table alongside other gameplay constants.
 *  - tickRegen() is called by the Scheduler (interval configurable, default 4 s).
 *  - Only characters with currentHp < maxHp (or currentMp < maxMp) are touched;
 *    dead characters (currentHp == 0) are always skipped.
 *
 * Config keys consumed (with defaults):
 *   regen.baseHpRegen         (default 2)   – flat HP restored per tick
 *   regen.baseMpRegen         (default 1)   – flat MP restored per tick
 *   regen.hpRegenConCoeff     (default 0.3) – extra HP per point of CON
 *   regen.mpRegenWisCoeff     (default 0.5) – extra MP per point of WIS
 *   regen.disableInCombatMs   (default 8000)– ms since last hit before regen resumes
 */
class RegenManager
{
  public:
    explicit RegenManager(GameServices *gameServices);

    /**
     * @brief Tick regen for every loaded character.
     *
     * For each character:
     *   1. Skip if dead (currentHealth == 0).
     *   2. Skip if the character was hit within disableInCombatMs.
     *   3. Compute hpGain = baseHpRegen + max(0, CON * hpRegenConCoeff).
     *   4. Compute mpGain = baseMpRegen + max(0, WIS * mpRegenWisCoeff).
     *   5. Apply gains (clamped to [current, max]).
     *   6. If anything changed, fire a stats_update packet via
     *      CharacterStatsNotificationService.
     */
    void tickRegen();

  private:
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};
