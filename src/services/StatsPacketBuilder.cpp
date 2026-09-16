#include "services/StatsPacketBuilder.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>

nlohmann::json
StatsPacketBuilder::build(const StatsPacketBuilderInput &in)
{
    const CharacterDataStruct &characterData = in.character;
    const int characterId = characterData.characterId;
    std::string requestId = "stats_update_" + std::to_string(characterId);

    // ── Build effective attributes: base + equipment bonuses + active effects ─
    // Start with base character attributes
    std::unordered_map<std::string, int> baseValues;
    std::unordered_map<std::string, float> effectiveValues;
    std::unordered_map<std::string, std::string> attrNames;

    for (const auto &a : characterData.attributes)
    {
        baseValues[a.slug] = a.value;
        effectiveValues[a.slug] = a.value;
        attrNames[a.slug] = a.name;
    }

    // Add pre-resolved item attribute bonuses from equipped gear
    for (const auto &[slug, bonus] : in.equipBonuses)
    {
        effectiveValues[slug] += bonus;
        if (attrNames.find(slug) == attrNames.end())
        {
            auto nameIt = in.equipNames.find(slug);
            attrNames[slug] = (nameIt != in.equipNames.end()) ? nameIt->second : slug;
        }
    }

    // Add non-expired stat-modifier active effects (skip dot/hot – they are damage ticks)
    for (const auto &eff : characterData.activeEffects)
    {
        if (eff.attributeSlug.empty())
            continue;
        if (eff.expiresAt != 0 && eff.expiresAt <= in.nowSec)
            continue;
        if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
            continue;
        effectiveValues[eff.attributeSlug] += static_cast<float>(eff.value);
        if (attrNames.find(eff.attributeSlug) == attrNames.end())
            attrNames[eff.attributeSlug] = eff.attributeSlug;
    }

    // Item Soul: pre-resolved kill-count tier bonus on the weapon's primary attribute
    if (in.soulBonusFlat > 0 && !in.soulAttrSlug.empty())
    {
        effectiveValues[in.soulAttrSlug] += in.soulBonusFlat;
        if (attrNames.find(in.soulAttrSlug) == attrNames.end())
            attrNames[in.soulAttrSlug] = in.soulAttrName.empty() ? in.soulAttrSlug : in.soulAttrName;
    }

    // Build attributes JSON array (base attrs + any extras added only by equipment/effects)
    nlohmann::json attributesJson = nlohmann::json::array();
    for (const auto &[slug, baseVal] : baseValues)
    {
        attributesJson.push_back({{"slug", slug},
            {"name", attrNames.count(slug) ? attrNames.at(slug) : slug},
            {"base", baseVal},
            {"effective", effectiveValues.at(slug)}});
    }
    for (const auto &[slug, effVal] : effectiveValues)
    {
        if (baseValues.count(slug))
            continue; // already included above
        attributesJson.push_back({{"slug", slug},
            {"name", attrNames.count(slug) ? attrNames.at(slug) : slug},
            {"base", 0},
            {"effective", effVal}});
    }

    // ── Active effects display list (all non-expired effects) ─────────────────
    nlohmann::json activeEffectsJson = nlohmann::json::array();
    for (const auto &eff : characterData.activeEffects)
    {
        if (eff.expiresAt != 0 && eff.expiresAt <= in.nowSec)
            continue;
        activeEffectsJson.push_back({{"slug", eff.effectSlug},
            {"effectTypeSlug", eff.effectTypeSlug},
            {"attributeSlug", eff.attributeSlug},
            {"value", eff.value},
            {"expiresAt", eff.expiresAt}});
    }

    // ── Build packet ──────────────────────────────────────────────────────────
    // Use effective (base + active-effect bonuses) max values in the health/mana objects
    // so the client bar is drawn against the real cap, not the stripped base value.
    // This matches what is reported in the attributes array and prevents false "current > max"
    // warnings when passive skills (e.g. mana_shield) raise the effective maximum.
    const int effectiveMaxHealth = effectiveValues.count("max_health")
                                        ? static_cast<int>(std::round(effectiveValues.at("max_health")))
                                        : characterData.characterMaxHealth;
    const int effectiveMaxMana = effectiveValues.count("max_mana")
                                      ? static_cast<int>(std::round(effectiveValues.at("max_mana")))
                                      : characterData.characterMaxMana;

    TimestampStruct timestamps = TimestampUtils::createReceiveTimestamp(0, requestId);
    ResponseBuilder builder;

    builder.setHeader("eventType", "stats_update")
        .setHeader("status", "success")
        .setHeader("requestId", requestId)
        .setTimestamps(timestamps);

    builder.setBody("characterId", characterId)
        .setBody("level", characterData.characterLevel)
        .setBody("freeSkillPoints", characterData.freeSkillPoints)
        .setBody("experience", nlohmann::json{{"current", characterData.characterExperiencePoints}, {"levelStart", in.levelStart}, {"nextLevel", characterData.expForNextLevel}, {"debt", characterData.experienceDebt}})
        .setBody("health", nlohmann::json{{"current", characterData.characterCurrentHealth}, {"max", effectiveMaxHealth}})
        .setBody("mana", nlohmann::json{{"current", characterData.characterCurrentMana}, {"max", effectiveMaxMana}})
        .setBody("weight", nlohmann::json{{"current", in.currentWeight}, {"max", in.weightLimit}})
        .setBody("attributes", attributesJson)
        .setBody("activeEffects", activeEffectsJson);

    return builder.build();
}
