#include "services/RegenManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/EquipmentManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/IStatsNotifier.hpp"
#include "services/ItemManager.hpp"
#include "services/StatsResolver.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <spdlog/logger.h>

RegenManager::RegenManager(CharacterManager &characters,
    GameConfigService &gameConfig,
    IStatsNotifier *statsNotify,
    EquipmentManager &equipment,
    ItemManager &items,
    Logger &logger)
    : characters_(characters),
      gameConfig_(gameConfig),
      statsNotify_(statsNotify),
      equipment_(equipment),
      items_(items),
      logger_(logger)
{
    log_ = logger_.getSystem("regen");
}

/// Returns the base value of a named attribute from a pre-built attribute list.
static int
findAttr(const std::vector<CharacterAttributeStruct> &attrs, const std::string &slug)
{
    for (const auto &a : attrs)
        if (a.slug == slug)
            return a.value;
    return 0;
}

void
RegenManager::tickRegen()
{
    // ── Read config (shared_lock inside getFloat/getInt – cheap) ──────────────
    const int baseHpRegen = gameConfig_.getInt("regen.baseHpRegen", 2);
    const int baseMpRegen = gameConfig_.getInt("regen.baseMpRegen", 1);
    const float hpRegenConCoeff = gameConfig_.getFloat("regen.hpRegenConCoeff", 0.3f);
    const float mpRegenWisCoeff = gameConfig_.getFloat("regen.mpRegenWisCoeff", 0.5f);
    const int disableInCombatMs = gameConfig_.getInt("regen.disableInCombatMs", 8000);

    const auto now = std::chrono::steady_clock::now();
    const int64_t nowSec = static_cast<int64_t>(std::time(nullptr));

    // Snapshot the character list (returns copies – no lock held during iteration)
    auto characters = characters_.getCharactersList();

    for (const auto &ch : characters)
    {
        const int cid = ch.characterId;
        if (cid <= 0)
            continue;

        // ── Skip dead characters ──────────────────────────────────────────────
        if (ch.isDead)
            continue;

        // ── Skip characters in combat window ─────────────────────────────────
        const auto msSinceLastCombat = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - ch.lastInCombatAt)
                                            .count();

        if (ch.lastInCombatAt.time_since_epoch().count() != 0 &&
            msSinceLastCombat < disableInCombatMs)
        {
            continue;
        }

        // ── Compute effective regen and max values ────────────────────────────
        // Single source of truth: StatsResolver over stored attributes (which
        // already contain equip/set/durability) + effects. The old code added
        // raw equip a second time (double-count vs combat) — never re-add.
        StatsResolverOptions convOpts;
        convOpts.agilityToEvasion = gameConfig_.getFloat("stats.conv_agility_evasion", 0.3f);
        convOpts.intellectToMagAttack = gameConfig_.getFloat("stats.conv_intellect_magattack", 0.5f);
        convOpts.strengthToPhysAttack = gameConfig_.getFloat("stats.conv_strength_physattack", 0.5f);
        const auto eff =
            StatsResolver::effectiveMap(ch.attributes, ch.activeEffects, nowSec, convOpts);
        const auto effInt = [&](const char *slug) -> int {
            const auto it = eff.find(slug);
            return it != eff.end() ? static_cast<int>(std::round(it->second)) : 0;
        };
        int hpRegenBase = effInt("hp_regen_per_s");
        int mpRegenBase = effInt("mp_regen_per_s");
        int maxHpEff = effInt("max_health");
        int maxMpEff = effInt("max_mana");
        // Fallback to stored struct values if the attribute list is incomplete
        if (maxHpEff <= 0)
            maxHpEff = ch.characterMaxHealth;
        if (maxMpEff <= 0)
            maxMpEff = ch.characterMaxMana;

        // (Effects already folded in by StatsResolver with combat-identical
        // rules — skip dot/hot. The old extra tickMs>0 filter is dropped on
        // purpose: display, regen and combat now share one rule set.)

        // Final per-tick gains.  Regen attributes are stored as per-second values;
        // tick interval is configurable (default 4 s) so multiply accordingly.
        // The old constitution/wisdom formula is kept as a fallback base bonus.
        const int conValue = findAttr(ch.attributes, "constitution");
        const int wisValue = findAttr(ch.attributes, "wisdom");
        const int hpFromStats = baseHpRegen + std::max(0, static_cast<int>(conValue * hpRegenConCoeff));
        const int mpFromStats = baseMpRegen + std::max(0, static_cast<int>(wisValue * mpRegenWisCoeff));

        const float tickSec = static_cast<float>(disableInCombatMs > 0
                                                     ? gameConfig_.getInt("regen.tickIntervalMs", 4000)
                                                     : 4000) /
                              1000.0f;
        const int hpGain = std::max(hpFromStats,
            static_cast<int>(std::max(0, hpRegenBase) * tickSec));
        const int mpGain = std::max(mpFromStats,
            static_cast<int>(std::max(0, mpRegenBase) * tickSec));

        // Use the fully computed effective max (base attrs + active effects + equipment).
        // Do NOT clamp up with ch.characterMaxHealth — that is the base stored value and
        // would override debuffs that legitimately reduce the maximum (e.g. death sickness).
        // The zero-fallback is already handled above.
        const int effectiveMaxHp = maxHpEff;
        const int effectiveMaxMp = maxMpEff;

        bool changed = false;

        // ── Apply HP regen ────────────────────────────────────────────────────
        if (ch.characterCurrentHealth < effectiveMaxHp && hpGain > 0)
        {
            const int newHp = std::min(ch.characterCurrentHealth + hpGain, effectiveMaxHp);
            characters_.updateCharacterHealth(cid, newHp);
            changed = true;
        }

        // ── Apply MP regen ────────────────────────────────────────────────────
        if (ch.characterCurrentMana < effectiveMaxMp && mpGain > 0)
        {
            const int newMp = std::min(ch.characterCurrentMana + mpGain, effectiveMaxMp);
            characters_.updateCharacterMana(cid, newMp);
            changed = true;
        }

        // ── Notify client if anything changed ────────────────────────────────
        if (changed && statsNotify_ != nullptr)
        {
            statsNotify_->sendStatsUpdate(cid, "regen");
        }
    }
}
