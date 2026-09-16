#pragma once

#include "data/DataStructs.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

/**
 * @brief Pure stats_update packet builder (Increment 9).
 *
 * Owns the attribute merge (base attrs → equipped-item bonuses → active
 * effects → item-soul bonus → JSON) with no manager dependencies: the service
 * resolves live data (items, thresholds, weights) into StatsPacketInput and
 * this builds the packet deterministically. Unit-testable on structs.
 */
struct StatsPacketBuilderInput
{
    CharacterDataStruct character;
    int levelStart = 0;
    float currentWeight = 0.0f;
    float weightLimit = 0.0f;
    /// Pre-resolved equipped-item bonuses (apply_on == "equip" only): slug → total
    std::unordered_map<std::string, int> equipBonuses;
    /// Display names for slugs that have no base character attribute
    std::unordered_map<std::string, std::string> equipNames;
    /// Pre-resolved item-soul tier bonus (empty soulAttrSlug = none)
    std::string soulAttrSlug;
    std::string soulAttrName;
    int soulBonusFlat = 0;
    int64_t nowSec = 0;
};

class StatsPacketBuilder
{
  public:
    static nlohmann::json build(const StatsPacketBuilderInput &in);
};
