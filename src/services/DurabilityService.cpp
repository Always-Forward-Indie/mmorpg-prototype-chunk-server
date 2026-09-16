#include "services/DurabilityService.hpp"
#include "services/GameConfigService.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <spdlog/logger.h>

DurabilityService::DurabilityService(InventoryManager &inventory,
    ItemManager &items,
    GameConfigService &gameConfig,
    Logger &logger)
    : inventory_(inventory), items_(items), gameConfig_(gameConfig), logger_(logger)
{
    log_ = logger_.getSystem("combat");
}

void
DurabilityService::setSaveCallback(SaveFn callback)
{
    saveCallback_ = std::move(callback);
}

void
DurabilityService::setRefreshAttributesCallback(RefreshFn callback)
{
    refreshAttributesCallback_ = std::move(callback);
}

void
DurabilityService::setNotifyCallback(NotifyFn callback)
{
    notifyCallback_ = std::move(callback);
}

void
DurabilityService::applyWeaponHitWear(int characterId)
{
    auto weapon = inventory_.getEquippedWeapon(characterId);
    if (!weapon.has_value())
        return;
    const auto &wItem = items_.getItemById(weapon->itemId);

    // Durability: weapon wears on every hit
    if (!wItem.isDurable || wItem.durabilityMax <= 0)
        return;
    int loss = static_cast<int>(
        std::lround(gameConfig_.getFloat("durability.weapon_loss_per_hit", 1.0f)));
    int cur = (weapon->durabilityCurrent > 0) ? weapon->durabilityCurrent : wItem.durabilityMax;
    int newDur = std::max(0, cur - loss);
    inventory_.updateDurability(characterId, weapon->id, newDur);
    saveDurabilityChange(characterId, weapon->id, newDur);
    checkAndTriggerDurabilityWarning(characterId, cur, newDur, wItem.durabilityMax);
}

void
DurabilityService::applyArmorHitWear(int characterId)
{
    int armorLoss = static_cast<int>(
        std::lround(gameConfig_.getFloat("durability.armor_loss_per_hit", 1.0f)));
    auto equipped = inventory_.getEquippedItems(characterId);
    for (const auto &invSlot : equipped)
    {
        const auto &iData = items_.getItemById(invSlot.itemId);
        if (!iData.isDurable || iData.equipSlotSlug == "main_hand" || iData.equipSlotSlug == "two_hand" ||
            iData.durabilityMax <= 0)
            continue;
        int cur = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : iData.durabilityMax;
        int newDur = std::max(0, cur - armorLoss);
        inventory_.updateDurability(characterId, invSlot.id, newDur);
        saveDurabilityChange(characterId, invSlot.id, newDur);
        checkAndTriggerDurabilityWarning(characterId, cur, newDur, iData.durabilityMax);
    }
}

void
DurabilityService::applyDeathPenalty(int characterId)
{
    float penaltyPct = gameConfig_.getFloat("durability.death_penalty_pct", 0.05f);
    auto equipped = inventory_.getEquippedItems(characterId);
    for (const auto &invSlot : equipped)
    {
        const auto &iData = items_.getItemById(invSlot.itemId);
        if (!iData.isDurable || iData.durabilityMax <= 0)
            continue;
        int penalty = static_cast<int>(std::ceil(iData.durabilityMax * penaltyPct));
        int cur = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : iData.durabilityMax;
        int newDur = std::max(0, cur - penalty);
        inventory_.updateDurability(characterId, invSlot.id, newDur);
        saveDurabilityChange(characterId, invSlot.id, newDur);
        checkAndTriggerDurabilityWarning(characterId, cur, newDur, iData.durabilityMax);
    }
}

void
DurabilityService::saveDurabilityChange(int characterId, int inventoryItemId, int durabilityCurrent)
{
    if (!saveCallback_)
        return;
    nlohmann::json packet;
    packet["header"]["eventType"] = "saveDurabilityChange";
    packet["header"]["clientId"] = 0;
    packet["header"]["hash"] = "";
    packet["body"]["characterId"] = characterId;
    packet["body"]["inventoryItemId"] = inventoryItemId;
    packet["body"]["durabilityCurrent"] = durabilityCurrent;
    saveCallback_(packet.dump() + "\n");
}

void
DurabilityService::checkAndTriggerDurabilityWarning(int characterId, int oldDur, int newDur, int maxDur)
{
    if (!refreshAttributesCallback_ || maxDur <= 0)
        return;

    const float t1 = gameConfig_.getFloat("durability.tier1_threshold_pct", 0.75f);
    const float t2 = gameConfig_.getFloat("durability.tier2_threshold_pct", 0.50f);
    const float t3 = gameConfig_.getFloat("durability.tier3_threshold_pct", 0.25f);

    const float oldRatio = static_cast<float>(oldDur) / maxDur;
    const float newRatio = static_cast<float>(newDur) / maxDur;

    // Check tier crossings from highest to lowest
    struct Tier
    {
        float threshold;
        int severity;
        const char *label;
    };
    const Tier tiers[] = {
        {t1, 1, "low"},
        {t2, 2, "medium"},
        {t3, 3, "high"},
    };

    bool attributesInvalidated = false;
    for (const auto &tier : tiers)
    {
        if (oldRatio >= tier.threshold && newRatio < tier.threshold)
        {
            // Crossing downward — emit world notification
            if (notifyCallback_)
            {
                nlohmann::json payload;
                payload["severity"] = tier.severity;
                payload["severityLabel"] = tier.label;
                payload["durabilityCurrent"] = newDur;
                payload["durabilityMax"] = maxDur;
                notifyCallback_(characterId, "durability_warning", payload, tier.label, "hud");
            }

            attributesInvalidated = true;
        }
    }

    // Broken (hits 0)
    if (oldDur > 0 && newDur == 0)
    {
        if (notifyCallback_)
        {
            nlohmann::json payload;
            payload["severity"] = 4;
            payload["severityLabel"] = "broken";
            payload["durabilityCurrent"] = 0;
            payload["durabilityMax"] = maxDur;
            notifyCallback_(characterId, "durability_warning", payload, "high", "hud");
        }

        attributesInvalidated = true;
    }

    if (attributesInvalidated)
        refreshAttributesCallback_(characterId);
}
