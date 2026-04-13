#include "services/RegenManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/CharacterStatsNotificationService.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameServices.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <spdlog/logger.h>

RegenManager::RegenManager(GameServices *gameServices)
    : gameServices_(gameServices)
{
    log_ = gameServices_->getLogger().getSystem("regen");
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
    auto &cfg = gameServices_->getGameConfigService();
    const int baseHpRegen = cfg.getInt("regen.baseHpRegen", 2);
    const int baseMpRegen = cfg.getInt("regen.baseMpRegen", 1);
    const float hpRegenConCoeff = cfg.getFloat("regen.hpRegenConCoeff", 0.3f);
    const float mpRegenWisCoeff = cfg.getFloat("regen.mpRegenWisCoeff", 0.5f);
    const int disableInCombatMs = cfg.getInt("regen.disableInCombatMs", 8000);

    auto &charMgr = gameServices_->getCharacterManager();
    auto &statsNotif = gameServices_->getStatsNotificationService();

    const auto now = std::chrono::steady_clock::now();
    const int64_t nowSec = static_cast<int64_t>(std::time(nullptr));

    // Snapshot the character list (returns copies – no lock held during iteration)
    auto characters = charMgr.getCharactersList();

    for (const auto &ch : characters)
    {
        const int cid = ch.characterId;
        if (cid <= 0)
            continue;

        // ── Skip dead characters ──────────────────────────────────────────────
        if (ch.characterCurrentHealth <= 0)
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

        // ── Compute effective attributes from snapshot ────────────────────────
        // Start with base attributes from the character snapshot (avoids N+1 lock
        // calls to CharacterManager) and layer active-effect modifiers on top.
        int conValue = findAttr(ch.attributes, "constitution");
        int wisValue = findAttr(ch.attributes, "wisdom");

        for (const auto &eff : ch.activeEffects)
        {
            // Permanent effects (expiresAt==0) or effects not yet expired
            if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
                continue;
            if (eff.tickMs > 0)
                continue; // DoT/HoT – not a stat modifier
            if (eff.attributeSlug == "constitution")
                conValue += static_cast<int>(eff.value);
            else if (eff.attributeSlug == "wisdom")
                wisValue += static_cast<int>(eff.value);
        }

        const int hpGain = baseHpRegen + std::max(0, static_cast<int>(conValue * hpRegenConCoeff));
        const int mpGain = baseMpRegen + std::max(0, static_cast<int>(wisValue * mpRegenWisCoeff));

        bool changed = false;

        // ── Apply HP regen ────────────────────────────────────────────────────
        if (ch.characterCurrentHealth < ch.characterMaxHealth && hpGain > 0)
        {
            const int newHp = std::min(ch.characterCurrentHealth + hpGain, ch.characterMaxHealth);
            charMgr.updateCharacterHealth(cid, newHp);
            changed = true;
        }

        // ── Apply MP regen ────────────────────────────────────────────────────
        if (ch.characterCurrentMana < ch.characterMaxMana && mpGain > 0)
        {
            const int newMp = std::min(ch.characterCurrentMana + mpGain, ch.characterMaxMana);
            charMgr.updateCharacterMana(cid, newMp);
            changed = true;
        }

        // ── Notify client if anything changed ────────────────────────────────
        if (changed)
        {
            statsNotif.sendStatsUpdate(cid, "regen");
        }
    }
}
