#pragma once
// Pure respawn math (B2 extract).
//
// CharacterEventHandler::handlePlayerRespawnEvent resolves the destination
// (custom bind point → zone sample → death-position fallback) and the
// post-respawn vitals (Resurrection Sickness penalizes the effective max
// first; HP/MP restore to configured fractions of the penalized max, never
// exceeding it on the client HUD). Both are pure given resolved inputs;
// manager lookups, sickness expansion, persistence and teleports stay in
// the handler 1-1.
#include "data/DataStructs.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

class RespawnResolver
{
  public:
    enum class PositionSource
    {
        BindPoint,    ///< custom shrine bind (x!=0 || y!=0)
        ZoneSample,   ///< random point sampled from the nearest zone
        DeathFallback ///< no zones loaded — stay at death position
    };

    struct ResolvedPosition
    {
        PositionStruct pos;
        PositionSource source = PositionSource::DeathFallback;
    };

    /// @param bindPos   character's custom respawn bind (0,0 = unset)
    /// @param zoneId    nearest zone id (<=0 = no zones loaded)
    /// @param sampled   point already sampled from the zone
    /// @param deathPos  position of death
    static ResolvedPosition resolvePosition(const PositionStruct &bindPos,
        int zoneId,
        const PositionStruct &sampled,
        const PositionStruct &deathPos)
    {
        ResolvedPosition out;
        if (bindPos.positionX != 0.0f || bindPos.positionY != 0.0f)
        {
            out.pos = bindPos;
            out.source = PositionSource::BindPoint;
        }
        else if (zoneId > 0)
        {
            out.pos = sampled;
            out.source = PositionSource::ZoneSample;
        }
        else
        {
            out.pos = deathPos;
            out.source = PositionSource::DeathFallback;
        }
        return out;
    }

    struct VitalsResult
    {
        int effectiveMaxHealth = 1;
        int effectiveMaxMana = 0;
        int newHp = 1;
        int newMana = 0;
    };

    /// Effective max = base plus rounded, non-expired, non-periodic
    /// max_health/max_mana modifiers (floors 1/0); restore = configured
    /// fractions of the penalized max (HP at least 1, mana at least 0).
    static VitalsResult computeVitals(int baseMaxHealth,
        int baseMaxMana,
        const std::vector<ActiveEffectStruct> &effects,
        int64_t nowSec,
        float hpPct,
        float mpPct)
    {
        VitalsResult out;
        int effectiveMaxHealth = baseMaxHealth;
        int effectiveMaxMana = baseMaxMana;
        for (const auto &eff : effects)
        {
            if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
                continue;
            if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
                continue;
            if (eff.attributeSlug == "max_health")
                effectiveMaxHealth += static_cast<int>(std::round(eff.value));
            else if (eff.attributeSlug == "max_mana")
                effectiveMaxMana += static_cast<int>(std::round(eff.value));
        }
        out.effectiveMaxHealth = std::max(1, effectiveMaxHealth);
        out.effectiveMaxMana = std::max(0, effectiveMaxMana);
        out.newHp = std::max(1, static_cast<int>(out.effectiveMaxHealth * hpPct));
        out.newMana = std::max(0, static_cast<int>(out.effectiveMaxMana * mpPct));
        return out;
    }

    RespawnResolver() = delete;
};
