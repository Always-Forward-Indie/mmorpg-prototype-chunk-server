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

        // ── Compute effective regen and max values ────────────────────────────
        // Start from base character attributes, then layer active effects and
        // equipped item bonuses, exactly as CharacterStatsNotificationService does.
        int hpRegenBase = findAttr(ch.attributes, "hp_regen_per_s");
        int mpRegenBase = findAttr(ch.attributes, "mp_regen_per_s");
        int maxHpEff = findAttr(ch.attributes, "max_health");
        int maxMpEff = findAttr(ch.attributes, "max_mana");
        // Fallback to stored struct values if the attribute list is incomplete
        if (maxHpEff <= 0)
            maxHpEff = ch.characterMaxHealth;
        if (maxMpEff <= 0)
            maxMpEff = ch.characterMaxMana;

        // Layer active-effect stat modifiers (skip DoT/HoT ticks)
        for (const auto &eff : ch.activeEffects)
        {
            if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
                continue;
            if (eff.tickMs > 0)
                continue;
            const auto &slug = eff.attributeSlug;
            const int val = static_cast<int>(eff.value);
            if (slug == "hp_regen_per_s")
                hpRegenBase += val;
            else if (slug == "mp_regen_per_s")
                mpRegenBase += val;
            else if (slug == "max_health")
                maxHpEff += val;
            else if (slug == "max_mana")
                maxMpEff += val;
        }

        // Layer equipped-item bonuses (apply_on == "equip")
        try
        {
            const auto equipState =
                gameServices_->getEquipmentManager().getEquipmentState(cid);
            for (const auto &[slotSlug, slot] : equipState.slots)
            {
                if (slot.inventoryItemId == 0)
                    continue;
                const auto item =
                    gameServices_->getItemManager().getItemById(slot.itemId);
                for (const auto &attr : item.attributes)
                {
                    if (attr.apply_on != "equip")
                        continue;
                    const int v = attr.value;
                    if (attr.slug == "hp_regen_per_s")
                        hpRegenBase += v;
                    else if (attr.slug == "mp_regen_per_s")
                        mpRegenBase += v;
                    else if (attr.slug == "max_health")
                        maxHpEff += v;
                    else if (attr.slug == "max_mana")
                        maxMpEff += v;
                }
            }
        }
        catch (...)
        {
        }

        // Final per-tick gains.  Regen attributes are stored as per-second values;
        // tick interval is configurable (default 4 s) so multiply accordingly.
        // The old constitution/wisdom formula is kept as a fallback base bonus.
        const int conValue = findAttr(ch.attributes, "constitution");
        const int wisValue = findAttr(ch.attributes, "wisdom");
        const int hpFromStats = baseHpRegen + std::max(0, static_cast<int>(conValue * hpRegenConCoeff));
        const int mpFromStats = baseMpRegen + std::max(0, static_cast<int>(wisValue * mpRegenWisCoeff));

        const float tickSec = static_cast<float>(disableInCombatMs > 0
                                                     ? cfg.getInt("regen.tickIntervalMs", 4000)
                                                     : 4000) /
                              1000.0f;
        const int hpGain = std::max(hpFromStats,
            static_cast<int>(std::max(0, hpRegenBase) * tickSec));
        const int mpGain = std::max(mpFromStats,
            static_cast<int>(std::max(0, mpRegenBase) * tickSec));

        const int effectiveMaxHp = std::max(maxHpEff, ch.characterMaxHealth);
        const int effectiveMaxMp = std::max(maxMpEff, ch.characterMaxMana);

        bool changed = false;

        // ── Apply HP regen ────────────────────────────────────────────────────
        if (ch.characterCurrentHealth < effectiveMaxHp && hpGain > 0)
        {
            const int newHp = std::min(ch.characterCurrentHealth + hpGain, effectiveMaxHp);
            charMgr.updateCharacterHealth(cid, newHp);
            changed = true;
        }

        // ── Apply MP regen ────────────────────────────────────────────────────
        if (ch.characterCurrentMana < effectiveMaxMp && mpGain > 0)
        {
            const int newMp = std::min(ch.characterCurrentMana + mpGain, effectiveMaxMp);
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
