#pragma once

#include <cmath>
#include <string>
#include <vector>

/**
 * @brief Pure repair-shop entry builder (Increment 9).
 *
 * The executor resolves (inventory slot × item template) pairs from managers;
 * this filters and prices them 1-1 with the pre-extraction loop: only durable
 * items with durabilityMax > 0 and missing > 0 are listed; cost is
 * ceil(vendorPriceBuy * missing / durabilityMax); durabilityCurrent falls back
 * to durabilityMax when the slot reports 0.
 */
struct RepairCostInput
{
    int inventoryItemId = 0;
    int itemId = 0;
    std::string itemSlug; // item template slug, surfaced as itemName (1-1)
    int durabilityCurrent = 0;
    bool isDurable = false;
    int durabilityMax = 0;
    int vendorPriceBuy = 0;
};

struct RepairCostEntry
{
    int inventoryItemId = 0;
    int itemId = 0;
    std::string itemName;
    int durabilityCurrent = 0;
    int durabilityMax = 0;
    int repairCost = 0;
};

// Single unit-cost formula (Wave 3.1): cost = ceil(vendorPriceBuy *
// missing / durabilityMax). Previously duplicated in
// VendorEventHandler::computeRepairCost. Callers normalize durabilityCurrent
// first (> 0, not full); the guards below make standalone misuse safe.
// NOTE: keep float32 arithmetic 1-1 — 100 * (30/100 in float32) = 30.000002
// → ceil = 31. Pinned by tests; do not "fix" without a product decision.
inline int repairUnitCost(int vendorPriceBuy, int durabilityMax, int durabilityCurrent)
{
    if (durabilityMax <= 0)
        return 0;
    const int missing = durabilityMax - durabilityCurrent;
    if (missing <= 0)
        return 0;
    return static_cast<int>(
        std::ceil(static_cast<float>(vendorPriceBuy) * (static_cast<float>(missing) / durabilityMax)));
}

inline std::vector<RepairCostEntry> computeRepairEntries(const std::vector<RepairCostInput> &items)
{
    std::vector<RepairCostEntry> out;
    for (const auto &it : items)
    {
        if (!it.isDurable || it.durabilityMax <= 0)
            continue;

        int durCurrent = (it.durabilityCurrent > 0) ? it.durabilityCurrent : it.durabilityMax;
        int missing = it.durabilityMax - durCurrent;
        if (missing <= 0)
            continue;

        // Cost proportional to missing durability
        int repairCost = repairUnitCost(it.vendorPriceBuy, it.durabilityMax, durCurrent);

        RepairCostEntry entry;
        entry.inventoryItemId = it.inventoryItemId;
        entry.itemId = it.itemId;
        entry.itemName = it.itemSlug;
        entry.durabilityCurrent = durCurrent;
        entry.durabilityMax = it.durabilityMax;
        entry.repairCost = repairCost;
        out.push_back(std::move(entry));
    }
    return out;
}
