#pragma once
// Pure vendor reputation-discount policy (B1 extract).
//
// VendorEventHandler applied the same discount in five places: buys (and
// shop display) subtract it from the markup, sells subtract it from the
// tax with a zero floor. Threshold/pct come from game_config
// (reputation.vendor_discount_threshold / reputation.vendor_discount_pct),
// the reputation value from ReputationManager. The handler resolves those
// inputs (plus the empty-faction/character guard); the math lives here 1-1
// and is pinned by unit tests.
#include <algorithm>

class VendorDiscountPolicy
{
  public:
    /// Threshold gate: reputation at/above threshold earns the configured pct.
    static float resolvePct(int reputation, int threshold, float configuredPct)
    {
        if (reputation < threshold)
            return 0.0f;
        return configuredPct;
    }

    /// Buy side (open shop / buy / buy batch): discount reduces the markup.
    static float applyToMarkup(float markup, float discountPct)
    {
        return markup - discountPct;
    }

    /// Sell side (sell / sell batch): discount reduces the tax, floored at 0.
    static float applyToTax(float tax, float discountPct)
    {
        return std::max(0.0f, tax - discountPct);
    }

    /// Effective shop pricing for one NPC/character: discounted buy markup
    /// AND effective sell tax in a single call. Single owner of the pricing
    /// policy — the direct openVendorShop path (VendorEventHandler) and the
    /// dialogue open_vendor_shop path (DialogueActionExecutor) must both go
    /// through here so one NPC never shows two price lists.
    struct EffectivePricing
    {
        float markupPct = 0.0f;
        float sellTaxPct = 0.0f;
    };

    static EffectivePricing resolveEffective(
        float baseMarkupPct, float baseSellTaxPct, int reputation, int threshold, float configuredPct)
    {
        const float discount = resolvePct(reputation, threshold, configuredPct);
        EffectivePricing out;
        out.markupPct = applyToMarkup(baseMarkupPct, discount);
        out.sellTaxPct = applyToTax(baseSellTaxPct, discount);
        return out;
    }

    VendorDiscountPolicy() = delete;
};
