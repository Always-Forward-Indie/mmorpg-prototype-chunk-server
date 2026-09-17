#pragma once
// Pure P2P trade-offer validation (Wave 3.2).
//
// The "does the player really hold this offered stack" check lived twice:
// - handleTradeOfferUpdateEvent (offer time): slot match + tradable required.
// - handleTradeConfirmEvent's validateOfferor (confirm time): slot match only
//   (no tradable re-check) + gold sufficiency (stays at the call site).
// Both delegate here; requireTradable preserves the exact per-site semantics.
// Do NOT "fix" the confirm side to re-check tradable without a product
// decision — an item cannot change tradability mid-trade today, and tightening
// confirm-time validation can strand in-flight sessions.
#include "data/DataStructs.hpp"

#include <functional>
#include <vector>

struct TradeOfferValidator
{
    // One offered stack against a player's inventory snapshot.
    // isTradableOf resolves the item template; unknown template => false.
    // A non-tradable slot does NOT stop the scan (a later stack may match) —
    // 1-1 with the offer-time loop.
    static bool isOfferItemValid(const TradeOfferItemStruct &offer,
        const std::vector<PlayerInventoryItemStruct> &inventory,
        bool requireTradable,
        const std::function<bool(int itemId)> &isTradableOf)
    {
        for (const auto &s : inventory)
        {
            if (s.id == offer.inventoryItemId && s.quantity >= offer.quantity && !s.isEquipped)
            {
                if (requireTradable && !isTradableOf(s.itemId))
                    continue;
                return true;
            }
        }
        return false;
    }

    // Whole offer valid (every stack). Gold sufficiency is NOT part of this
    // helper — it lives at the confirm site (different concern, different data).
    static bool isOfferValid(const std::vector<TradeOfferItemStruct> &offer,
        const std::vector<PlayerInventoryItemStruct> &inventory,
        bool requireTradable,
        const std::function<bool(int itemId)> &isTradableOf)
    {
        for (const auto &oi : offer)
        {
            if (!isOfferItemValid(oi, inventory, requireTradable, isTradableOf))
                return false;
        }
        return true;
    }

    // Confirm-time shape: slot match only, no template lookup at all (the
    // resolver is never invoked — safe to omit).
    static bool isOfferValid(const std::vector<TradeOfferItemStruct> &offer,
        const std::vector<PlayerInventoryItemStruct> &inventory)
    {
        static const std::function<bool(int)> unused;
        return isOfferValid(offer, inventory, false, unused);
    }

    TradeOfferValidator() = delete;
};
