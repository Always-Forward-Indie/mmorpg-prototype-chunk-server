// Unit tests for VendorManager: shop queries, buy/sell, batch atomicity.
// Needs ItemManager + InventoryManager (Logger only, no DB/sockets).
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/TradeOfferValidator.hpp"
#include "services/TradeRequestValidator.hpp"
#include "services/VendorDiscountPolicy.hpp"
#include "services/VendorManager.hpp"

#include <gtest/gtest.h>

namespace
{

constexpr int kGold = 999;
constexpr int kPotion = 100;
constexpr int kSword = 200;
constexpr int kVendor = 11;

ItemDataStruct makeItem(int id, const std::string &slug, int buy = 10, int sell = 4)
{
    ItemDataStruct it;
    it.id = id;
    it.slug = slug;
    it.stackMax = 64;
    it.vendorPriceBuy = buy;
    it.vendorPriceSell = sell;
    return it;
}

VendorNPCDataStruct makeVendor()
{
    VendorNPCDataStruct v;
    v.npcId = kVendor;
    VendorInventoryItemStruct potion;
    potion.itemId = kPotion;
    potion.stockCurrent = 5;
    potion.stockMax = 5;
    VendorInventoryItemStruct sword;
    sword.itemId = kSword;
    sword.stockCurrent = -1; // unlimited
    sword.stockMax = -1;
    v.items = {potion, sword};
    return v;
}

struct VendorFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    VendorManager vendor{items, logger};

    void SetUp() override
    {
        ItemDataStruct gold = makeItem(kGold, "gold_coin", 1, 1);
        gold.stackMax = 1000000;
        ItemDataStruct potion = makeItem(kPotion, "health_potion", 10, 4);
        ItemDataStruct sword = makeItem(kSword, "iron_sword", 100, 40);
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        items.setItemsList({gold, potion, sword});
        vendor.setVendorData({makeVendor()});
    }

    void giveGold(int charId, int amount)
    {
        ASSERT_TRUE(inv.addItemToInventory(charId, kGold, amount));
    }

    int rowId(int charId, int itemId)
    {
        for (const auto &row : inv.getPlayerInventory(charId))
            if (row.itemId == itemId)
                return row.id;
        return 0;
    }

    // In-memory rows start with id == 0; mirror the DB upsert sync.
    int syncRowId(int charId, int itemId, int64_t newId)
    {
        inv.updateInventoryItemId(charId, itemId, newId);
        return rowId(charId, itemId);
    }
};

} // namespace

TEST_F(VendorFixture, ShopJsonFoundAndMissing)
{
    auto shop = vendor.buildShopJson(kVendor, 0.0f);
    EXPECT_FALSE(shop.is_null());
    auto missing = vendor.buildShopJson(424242, 0.0f);
    EXPECT_TRUE(missing.is_null());
}

TEST_F(VendorFixture, UpdateStockCount)
{
    vendor.updateStockCount(kVendor, kPotion, 2);
    giveGold(1, 1000);
    // Only 2 in stock now: buying 3 must fail.
    auto r = vendor.buyItem(1, kVendor, kPotion, 3, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 0);
}

TEST_F(VendorFixture, BuySuccessDeductsGoldAndStock)
{
    giveGold(1, 1000);
    auto r = vendor.buyItem(1, kVendor, kPotion, 2, kGold, inv, 0.0f);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.totalPrice, 20); // 2 x vendorPriceBuy 10, no markup
    EXPECT_EQ(inv.getGoldAmount(1), 980);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 2);
    // Stock is now 3: buying 4 must fail without mutation.
    auto r2 = vendor.buyItem(1, kVendor, kPotion, 4, kGold, inv, 0.0f);
    EXPECT_FALSE(r2.success);
    EXPECT_EQ(inv.getGoldAmount(1), 980);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 2);
}

TEST_F(VendorFixture, BuyUnlimitedStockAlwaysAvailable)
{
    giveGold(1, 10000);
    auto r = vendor.buyItem(1, kVendor, kSword, 3, kGold, inv, 0.0f);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kSword), 3);
}

TEST_F(VendorFixture, BuyFailsWithoutGold)
{
    auto r = vendor.buyItem(1, kVendor, kPotion, 1, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 0);
}

TEST_F(VendorFixture, BuyFailsForUnknownVendorOrItem)
{
    giveGold(1, 1000);
    EXPECT_FALSE(vendor.buyItem(1, 424242, kPotion, 1, kGold, inv, 0.0f).success);
    EXPECT_FALSE(vendor.buyItem(1, kVendor, 424242, 1, kGold, inv, 0.0f).success);
    EXPECT_EQ(inv.getGoldAmount(1), 1000); // untouched
}

TEST_F(VendorFixture, SellSuccessCreditsGold)
{
    EXPECT_TRUE(inv.addItemToInventory(1, kPotion, 4));
    int potionRow = syncRowId(1, kPotion, 77);
    ASSERT_NE(potionRow, 0);
    auto r = vendor.sellItem(1, kVendor, potionRow, 3, kGold, inv, 0.0f);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.goldReceived, 12); // 3 x vendorPriceSell 4
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 1);
    EXPECT_EQ(inv.getGoldAmount(1), 12);
}

TEST_F(VendorFixture, SellEquippedItemFails)
{
    EXPECT_TRUE(inv.addItemToInventory(1, kSword, 1));
    int swordRow = syncRowId(1, kSword, 88);
    ASSERT_NE(swordRow, 0);
    inv.setItemEquipped(1, swordRow, true);
    auto r = vendor.sellItem(1, kVendor, swordRow, 1, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kSword), 1);
}

TEST_F(VendorFixture, BuyBatchAtomicAllOrNothing)
{
    giveGold(1, 1000);
    // Second entry exceeds stock (5): whole batch must be rejected.
    std::vector<BuyBatchItemEntry> entries = {{kPotion, 2}, {kPotion, 5}};
    auto r = vendor.buyBatch(1, kVendor, entries, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 0);
    EXPECT_EQ(inv.getGoldAmount(1), 1000);
    // Valid batch goes through.
    std::vector<BuyBatchItemEntry> ok = {{kPotion, 2}, {kSword, 1}};
    auto r2 = vendor.buyBatch(1, kVendor, ok, kGold, inv, 0.0f);
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 2);
    EXPECT_EQ(inv.getItemQuantity(1, kSword), 1);
    EXPECT_EQ(inv.getGoldAmount(1), 1000 - r2.totalGoldSpent);
}

TEST_F(VendorFixture, BuyBatchDuplicateEntriesAggregatedStock)
{
    // Regression: entries for the same item share one stock pile.
    // Stock is 5; 2+5=7 must be rejected with zero mutation (no oversell).
    giveGold(1, 1000);
    std::vector<BuyBatchItemEntry> dup = {{kPotion, 2}, {kPotion, 5}};
    auto r = vendor.buyBatch(1, kVendor, dup, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 0);
    EXPECT_EQ(inv.getGoldAmount(1), 1000);
}

TEST_F(VendorFixture, SellBatchDuplicateRowsAggregatedQuantity)
{
    // Regression: entries for the same row share one quantity.
    // Row holds 5; 3+3=6 must be rejected with zero mutation.
    EXPECT_TRUE(inv.addItemToInventory(1, kPotion, 5));
    int potionRow = syncRowId(1, kPotion, 99);
    ASSERT_NE(potionRow, 0);
    std::vector<SellBatchItemEntry> dup = {{potionRow, 3}, {potionRow, 3}};
    auto r = vendor.sellBatch(1, kVendor, dup, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getItemQuantity(1, kPotion), 5);
    EXPECT_EQ(inv.getGoldAmount(1), 0);
}

TEST_F(VendorFixture, BatchSizeLimitEnforced)
{
    giveGold(1, 1000000);
    std::vector<BuyBatchItemEntry> tooMany(
        static_cast<size_t>(VendorManager::MAX_VENDOR_BATCH_SIZE) + 1, {kSword, 1});
    auto r = vendor.buyBatch(1, kVendor, tooMany, kGold, inv, 0.0f);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(inv.getGoldAmount(1), 1000000);
}

namespace
{

PlayerInventoryItemStruct offerSlot(int rowId, int itemId, int qty, bool equipped)
{
    PlayerInventoryItemStruct s;
    s.id = rowId;
    s.itemId = itemId;
    s.quantity = qty;
    s.isEquipped = equipped;
    return s;
}

TradeOfferItemStruct offerRow(int rowId, int itemId, int qty)
{
    TradeOfferItemStruct o;
    o.inventoryItemId = rowId;
    o.itemId = itemId;
    o.quantity = qty;
    return o;
}

} // namespace

TEST(TradeOfferValidator, OfferTimeShape)
{
    // Wave 3.2 pin: offer-time semantics (tradable required).
    const auto tradable = [](int itemId) { return itemId != 777; };
    std::vector<PlayerInventoryItemStruct> inv = {
        offerSlot(11, 100, 5, false),
        offerSlot(12, 200, 1, true),   // equipped: never matchable
        offerSlot(13, 777, 9, false),  // untradable template
    };
    EXPECT_TRUE(TradeOfferValidator::isOfferItemValid(offerRow(11, 100, 5), inv, true, tradable));
    EXPECT_TRUE(TradeOfferValidator::isOfferItemValid(offerRow(11, 100, 3), inv, true, tradable));
    EXPECT_FALSE(TradeOfferValidator::isOfferItemValid(offerRow(11, 100, 6), inv, true, tradable)); // short
    EXPECT_FALSE(TradeOfferValidator::isOfferItemValid(offerRow(99, 100, 1), inv, true, tradable)); // wrong row
    EXPECT_FALSE(TradeOfferValidator::isOfferItemValid(offerRow(12, 200, 1), inv, true, tradable)); // equipped
    EXPECT_FALSE(TradeOfferValidator::isOfferItemValid(offerRow(13, 777, 1), inv, true, tradable)); // untradable
    EXPECT_TRUE(TradeOfferValidator::isOfferValid({offerRow(11, 100, 2)}, inv, true, tradable));
    EXPECT_FALSE(TradeOfferValidator::isOfferValid({offerRow(11, 100, 2), offerRow(13, 777, 1)}, inv, true, tradable));
    EXPECT_TRUE(TradeOfferValidator::isOfferValid({}, inv, true, tradable)); // vacuous (matches loop)
}

TEST(TradeOfferValidator, TemplateResolvedPerSlot)
{
    // Tradability resolves per inventory slot's template (not the offered
    // itemId): an untradable row is rejected while a tradable row passes.
    const auto tradable = [](int itemId) { return itemId == 100; };
    std::vector<PlayerInventoryItemStruct> inv = {
        offerSlot(11, 777, 5, false),
        offerSlot(12, 100, 5, false),
    };
    EXPECT_TRUE(TradeOfferValidator::isOfferItemValid(offerRow(12, 100, 5), inv, true, tradable));
    EXPECT_FALSE(TradeOfferValidator::isOfferItemValid(offerRow(11, 777, 5), inv, true, tradable));
    EXPECT_FALSE(TradeOfferValidator::isOfferItemValid(offerRow(12, 100, 5), inv, true,
        [](int) { return false; })); // unknown template => untradable
}

TEST(TradeOfferValidator, ConfirmTimeShapeSkipsTemplateLookup)
{
    // Wave 3.2 pin: confirm-time semantics (slot match only, resolver unused).
    // An untradable template still validates here — do not "fix" without a
    // product decision (would strand in-flight sessions).
    std::vector<PlayerInventoryItemStruct> inv = {
        offerSlot(11, 100, 5, false),
        offerSlot(13, 777, 9, false),
    };
    EXPECT_TRUE(TradeOfferValidator::isOfferValid({offerRow(11, 100, 5)}, inv));
    EXPECT_TRUE(TradeOfferValidator::isOfferValid({offerRow(13, 777, 9)}, inv));
    EXPECT_FALSE(TradeOfferValidator::isOfferValid({offerRow(13, 777, 10)}, inv)); // short
    EXPECT_FALSE(TradeOfferValidator::isOfferValid({offerRow(99, 100, 1)}, inv));  // unknown row
}

namespace
{

TradeRequestValidator::Input allClearRequest()
{
    TradeRequestValidator::Input in;
    in.initiatorAlive = true;
    in.initiatorInSession = false;
    in.targetExists = true;
    in.inRange = true;
    in.targetBusy = false;
    in.targetOnline = true;
    return in;
}

} // namespace

TEST(TradeRequestValidator, AllClearProceeds)
{
    EXPECT_EQ(TradeRequestValidator::validate(allClearRequest()), "");
}

TEST(TradeRequestValidator, EachGateFiresItsCode)
{
    // Wave 3.4 pin: every gate maps to its exact client error code.
    auto in = allClearRequest();
    in.initiatorAlive = false;
    EXPECT_EQ(TradeRequestValidator::validate(in), "cannot_trade_while_dead");
    in = allClearRequest();
    in.initiatorInSession = true;
    EXPECT_EQ(TradeRequestValidator::validate(in), "already_in_trade");
    in = allClearRequest();
    in.targetExists = false;
    EXPECT_EQ(TradeRequestValidator::validate(in), "target_not_found");
    in = allClearRequest();
    in.inRange = false;
    EXPECT_EQ(TradeRequestValidator::validate(in), "out_of_range");
    in = allClearRequest();
    in.targetBusy = true;
    EXPECT_EQ(TradeRequestValidator::validate(in), "target_busy");
    in = allClearRequest();
    in.targetOnline = false;
    EXPECT_EQ(TradeRequestValidator::validate(in), "target_offline");
}

TEST(VendorDiscountPolicy, ThresholdGateAndApplication)
{
    // Below threshold: no discount; at/above: configured pct.
    EXPECT_FLOAT_EQ(VendorDiscountPolicy::resolvePct(199, 200, 0.05f), 0.0f);
    EXPECT_FLOAT_EQ(VendorDiscountPolicy::resolvePct(200, 200, 0.05f), 0.05f);
    EXPECT_FLOAT_EQ(VendorDiscountPolicy::resolvePct(9999, 200, 0.05f), 0.05f);
    // Buy side: straight subtraction (markup may go negative — handler's
    // existing behavior, pinned 1-1, not "fixed" here).
    EXPECT_FLOAT_EQ(VendorDiscountPolicy::applyToMarkup(0.10f, 0.05f), 0.05f);
    // Sell side: tax floored at zero.
    EXPECT_FLOAT_EQ(VendorDiscountPolicy::applyToTax(0.10f, 0.05f), 0.05f);
    EXPECT_FLOAT_EQ(VendorDiscountPolicy::applyToTax(0.02f, 0.05f), 0.0f);
}

TEST(TradeRequestValidator, FirstMatchWinsInHandlerOrder)
{
    // Order is load-bearing (1-1 with handleTradeRequestEvent): earlier gates
    // shadow later ones even when several fail at once.
    auto in = allClearRequest();
    in.initiatorAlive = false;
    in.targetExists = false;
    in.targetBusy = true;
    EXPECT_EQ(TradeRequestValidator::validate(in), "cannot_trade_while_dead");
    in = allClearRequest();
    in.initiatorInSession = true;
    in.inRange = false;
    EXPECT_EQ(TradeRequestValidator::validate(in), "already_in_trade");
    in = allClearRequest();
    in.targetExists = false;
    in.inRange = false;
    in.targetBusy = true;
    in.targetOnline = false;
    EXPECT_EQ(TradeRequestValidator::validate(in), "target_not_found");
}
