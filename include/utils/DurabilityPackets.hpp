#pragma once

#include <nlohmann/json.hpp>

// Single source for the durabilityUpdate wire shape: the authoritative
// durability fact for one item instance. Sent on every server-side write
// (combat wear, death penalty, repair) to the owning client, which applies
// it to both its snapshots (equipment + inventory) and refreshes its views.
// Body: characterId, inventoryItemId, durabilityCurrent, durabilityMax,
// isDurabilityWarning (same threshold rule as EQUIPMENT_STATE builds).
struct DurabilityUpdatePacket
{
    static nlohmann::json build(int characterId,
        int inventoryItemId,
        int durabilityCurrent,
        int durabilityMax,
        bool isDurabilityWarning)
    {
        nlohmann::json packet;
        packet["header"]["eventType"] = "durabilityUpdate";
        packet["header"]["status"] = "success";
        packet["body"]["characterId"] = characterId;
        packet["body"]["inventoryItemId"] = inventoryItemId;
        packet["body"]["durabilityCurrent"] = durabilityCurrent;
        packet["body"]["durabilityMax"] = durabilityMax;
        packet["body"]["isDurabilityWarning"] = isDurabilityWarning;
        return packet;
    }
};
