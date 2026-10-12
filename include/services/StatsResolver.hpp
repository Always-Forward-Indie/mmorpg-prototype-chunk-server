#pragma once
// Single source of truth for effective attributes (P0-1 extract).
//
// Before: three places computed "base + extras" three different ways —
//   StatsPacketBuilder: stored + raw equip + effects + soul,
//   RegenManager:      stored + raw equip + effects subset,
//   CombatCalculator::mergeEffects: stored + effects.
// Stored attributes already contain equip/set/durability (DB computes them
// in get_character_attributes), so the first two DOUBLE-counted equipment
// (display/regen disagreed with combat). All three now delegate here:
// effective = stored + effects; equipment is never re-added.
//
// Stat conversions (agi/int/str revival) ride the same options struct so
// display, regen and combat stay identical: default 0.0 (off) until the
// design decision lands, then one game_config flip enables all three.
#include "data/DataStructs.hpp"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

struct StatsResolverOptions
{
    float agilityToEvasion = 0.0f;
    float intellectToMagAttack = 0.0f;
    float strengthToPhysAttack = 0.0f;
};

class StatsResolver
{
  public:
    // Effective values keyed by slug. Effects: skip empty slug, expired,
    // dot/hot (damage ticks, not modifiers) — same rules as the old
    // mergeEffects, pinned by test_stats_resolver.cpp.
    static std::unordered_map<std::string, float> effectiveMap(
        const std::vector<CharacterAttributeStruct> &stored,
        const std::vector<ActiveEffectStruct> &effects,
        int64_t nowSec,
        const StatsResolverOptions &opts = StatsResolverOptions{})
    {
        std::unordered_map<std::string, float> base;
        for (const auto &a : stored)
            base[a.slug] = static_cast<float>(a.value);
        std::unordered_map<std::string, float> out = base;
        auto live = [&](const ActiveEffectStruct &eff) {
            if (eff.attributeSlug.empty())
                return false;
            if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
                return false;
            // Ticking effects are damage ticks, never stat modifiers — skip
            // by TYPE (dot/hot) and defensively by tickMs (a poison with an
            // empty type must not become +100 regen).
            if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
                return false;
            if (eff.tickMs > 0)
                return false;
            return true;
        };
        // Pass 1: flats (in effect order). Pass 2: percents of STORED base
        // (order-independent: +8% max_health is always 8% of base, never of
        // a flat-boosted total).
        for (const auto &eff : effects)
        {
            if (!live(eff) || eff.modifierType == "percent")
                continue;
            out[eff.attributeSlug] += static_cast<float>(eff.value);
        }
        for (const auto &eff : effects)
        {
            if (!live(eff) || eff.modifierType != "percent")
                continue;
            const auto it = base.find(eff.attributeSlug);
            const float baseVal = (it != base.end()) ? it->second : 0.0f;
            out[eff.attributeSlug] += baseVal * static_cast<float>(eff.value) / 100.0f;
        }
        if (opts.agilityToEvasion != 0.0f)
        {
            const auto it = out.find("agility");
            if (it != out.end())
                out["evasion"] += it->second * opts.agilityToEvasion;
        }
        if (opts.intellectToMagAttack != 0.0f)
        {
            const auto it = out.find("intelligence");
            if (it != out.end())
                out["magical_attack"] += it->second * opts.intellectToMagAttack;
        }
        if (opts.strengthToPhysAttack != 0.0f)
        {
            const auto it = out.find("strength");
            if (it != out.end())
                out["physical_attack"] += it->second * opts.strengthToPhysAttack;
        }
        return out;
    }

    // Vector form for the combat hot path. 1-1 with the old mergeEffects
    // when conversions are off: stored order preserved, effect values
    // rounded, unknown slugs appended in effect order.
    static std::vector<CharacterAttributeStruct> mergedVector(
        const std::vector<CharacterAttributeStruct> &stored,
        const std::vector<ActiveEffectStruct> &effects,
        int64_t nowSec,
        const StatsResolverOptions &opts = StatsResolverOptions{})
    {
        std::unordered_map<std::string, float> base;
        for (const auto &a : stored)
            base[a.slug] = static_cast<float>(a.value);
        std::vector<CharacterAttributeStruct> attrs = stored;
        auto find = [&](const std::string &slug) -> CharacterAttributeStruct * {
            for (auto &a : attrs)
                if (a.slug == slug)
                    return &a;
            return nullptr;
        };
        auto live = [&](const ActiveEffectStruct &eff) {
            if (eff.attributeSlug.empty())
                return false;
            if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
                return false;
            // Ticking effects are damage ticks, never stat modifiers — skip
            // by TYPE (dot/hot) and defensively by tickMs (a poison with an
            // empty type must not become +100 regen).
            if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
                return false;
            if (eff.tickMs > 0)
                return false;
            return true;
        };
        auto add = [&](const std::string &slug, float val) {
            if (auto *slot = find(slug))
                slot->value += static_cast<int>(std::round(val));
            else
            {
                CharacterAttributeStruct created;
                created.slug = slug;
                created.value = static_cast<int>(std::round(val));
                attrs.push_back(std::move(created));
            }
        };
        // Pass 1: flats (effect order). Pass 2: percents of STORED base
        // (order-independent, same rule as effectiveMap).
        for (const auto &eff : effects)
        {
            if (!live(eff) || eff.modifierType == "percent")
                continue;
            add(eff.attributeSlug, eff.value);
        }
        for (const auto &eff : effects)
        {
            if (!live(eff) || eff.modifierType != "percent")
                continue;
            const auto it = base.find(eff.attributeSlug);
            const float baseVal = (it != base.end()) ? it->second : 0.0f;
            add(eff.attributeSlug, baseVal * eff.value / 100.0f);
        }
        auto convert = [&](const char *from, const char *to, float rate) {
            if (rate == 0.0f)
                return;
            int base = 0;
            for (const auto &a : stored)
                if (a.slug == from)
                {
                    base = a.value;
                    break;
                }
            if (base == 0)
                return;
            const int add = static_cast<int>(std::round(base * rate));
            if (auto *slot = find(to))
                slot->value += add;
            else
            {
                CharacterAttributeStruct created;
                created.slug = to;
                created.value = add;
                attrs.push_back(std::move(created));
            }
        };
        convert("agility", "evasion", opts.agilityToEvasion);
        convert("intelligence", "magical_attack", opts.intellectToMagAttack);
        convert("strength", "physical_attack", opts.strengthToPhysAttack);
        return attrs;
    }

    // Mob-attribute twin of mergedVector (same two-pass flat/percent rules,
    // same skip rules). Lets weakened mobs hit softer through the existing
    // calculateMobSkillDamage path without a parallel formula.
    static std::vector<MobAttributeStruct> mergedMobVector(
        const std::vector<MobAttributeStruct> &stored,
        const std::vector<ActiveEffectStruct> &effects,
        int64_t nowSec,
        const StatsResolverOptions &opts = StatsResolverOptions{})
    {
        std::unordered_map<std::string, float> base;
        for (const auto &a : stored)
            base[a.slug] = static_cast<float>(a.value);
        std::vector<MobAttributeStruct> attrs = stored;
        auto find = [&](const std::string &slug) -> MobAttributeStruct * {
            for (auto &a : attrs)
                if (a.slug == slug)
                    return &a;
            return nullptr;
        };
        auto live = [&](const ActiveEffectStruct &eff) {
            if (eff.attributeSlug.empty())
                return false;
            if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
                return false;
            if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
                return false;
            if (eff.tickMs > 0)
                return false;
            return true;
        };
        auto add = [&](const std::string &slug, float val) {
            if (auto *slot = find(slug))
                slot->value += static_cast<int>(std::round(val));
            else
            {
                MobAttributeStruct created;
                created.slug = slug;
                created.value = static_cast<int>(std::round(val));
                attrs.push_back(std::move(created));
            }
        };
        for (const auto &eff : effects)
        {
            if (!live(eff) || eff.modifierType == "percent")
                continue;
            add(eff.attributeSlug, eff.value);
        }
        for (const auto &eff : effects)
        {
            if (!live(eff) || eff.modifierType != "percent")
                continue;
            const auto it = base.find(eff.attributeSlug);
            const float baseVal = (it != base.end()) ? it->second : 0.0f;
            add(eff.attributeSlug, baseVal * eff.value / 100.0f);
        }
        // Conversions (agility/intellect/strength) apply to player scale
        // stats only; mob templates carry no such primaries — skip.
        (void)opts;
        return attrs;
    }

    StatsResolver() = delete;
};
