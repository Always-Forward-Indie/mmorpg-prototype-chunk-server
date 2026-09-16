// Unit tests for DurabilityService (real Inventory/Item/Config + Logger;
// save/notify/refresh captured via std::function seams).
#include "services/DurabilityService.hpp"
#include "services/GameConfigService.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

struct NotifyRecord
{
    int characterId = 0;
    std::string type;
    nlohmann::json data;
    std::string priority;
    std::string channel;
};

struct DurFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    GameConfigService cfg{logger};
    DurabilityService dur{inv, items, cfg, logger};

    std::vector<std::string> saved;
    std::vector<NotifyRecord> notified;
    std::vector<int> refreshed;

    void SetUp() override
    {
        cfg.setConfig({
            {"durability.weapon_loss_per_hit", "1"},
            {"durability.armor_loss_per_hit", "1"},
            {"durability.death_penalty_pct", "0.05"},
            {"durability.tier1_threshold_pct", "0.75"},
            {"durability.tier2_threshold_pct", "0.50"},
            {"durability.tier3_threshold_pct", "0.25"},
        });
        dur.setSaveCallback([this](const std::string &pkt) { saved.push_back(pkt); });
        dur.setNotifyCallback(
            [this](int cid, const std::string &type, const nlohmann::json &data,
                const std::string &prio, const std::string &channel)
            { notified.push_back({cid, type, data, prio, channel}); });
        dur.setRefreshAttributesCallback([this](int cid) { refreshed.push_back(cid); });

        ItemDataStruct sword;
        sword.id = 200;
        sword.slug = "iron_sword";
        sword.stackMax = 1;
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        sword.isDurable = true;
        sword.durabilityMax = 100;
        ItemDataStruct chest;
        chest.id = 201;
        chest.slug = "iron_chest";
        chest.stackMax = 1;
        chest.isEquippable = true;
        chest.equipSlotSlug = "chest";
        chest.isDurable = true;
        chest.durabilityMax = 100;
        ItemDataStruct bread;
        bread.id = 202;
        bread.slug = "bread";
        bread.stackMax = 10;
        items.setItemsList({sword, chest, bread});
    }

    void equip(int charId, int itemId, int rowId)
    {
        EXPECT_TRUE(inv.addItemToInventory(charId, itemId, 1));
        inv.updateInventoryItemId(charId, itemId, rowId);
        inv.setItemEquipped(charId, rowId, true);
    }

    int slotDur(int charId, int rowId)
    {
        for (const auto &s : inv.getEquippedItems(charId))
            if (s.id == rowId)
                return s.durabilityCurrent;
        return -1;
    }
};

} // namespace

TEST_F(DurFixture, WeaponHitWearAndSave)
{
    equip(1, 200, 11);
    dur.applyWeaponHitWear(1);
    // durabilityCurrent 0 -> fallback max 100 -> 99
    EXPECT_EQ(slotDur(1, 11), 99);
    ASSERT_EQ(saved.size(), 1u);
    auto pkt = nlohmann::json::parse(saved[0]);
    EXPECT_EQ(pkt["header"]["eventType"], "saveDurabilityChange");
    EXPECT_EQ(pkt["body"]["characterId"], 1);
    EXPECT_EQ(pkt["body"]["inventoryItemId"], 11);
    EXPECT_EQ(pkt["body"]["durabilityCurrent"], 99);
    // 1.00 -> 0.99 stays above t1: no warning, no refresh
    EXPECT_TRUE(notified.empty());
    EXPECT_TRUE(refreshed.empty());
}

TEST_F(DurFixture, NoWeaponEquippedIsNoOp)
{
    equip(1, 201, 12); // chest only, no main_hand weapon
    dur.applyWeaponHitWear(1);
    EXPECT_TRUE(saved.empty());
    EXPECT_TRUE(notified.empty());
}

TEST_F(DurFixture, TierCrossingLow)
{
    equip(1, 200, 11);
    inv.updateDurability(1, 11, 75);
    dur.applyWeaponHitWear(1);
    EXPECT_EQ(slotDur(1, 11), 74);
    ASSERT_EQ(notified.size(), 1u);
    EXPECT_EQ(notified[0].type, "durability_warning");
    EXPECT_EQ(notified[0].data["severity"], 1);
    EXPECT_EQ(notified[0].data["severityLabel"], "low");
    EXPECT_EQ(notified[0].data["durabilityCurrent"], 74);
    EXPECT_EQ(notified[0].data["durabilityMax"], 100);
    EXPECT_EQ(notified[0].priority, "low");
    EXPECT_EQ(notified[0].channel, "hud");
    ASSERT_EQ(refreshed.size(), 1u);
    EXPECT_EQ(refreshed[0], 1);
}

TEST_F(DurFixture, BrokenAtZero)
{
    equip(1, 200, 11);
    inv.updateDurability(1, 11, 1);
    dur.applyWeaponHitWear(1);
    EXPECT_EQ(slotDur(1, 11), 0);
    ASSERT_EQ(notified.size(), 1u);
    EXPECT_EQ(notified[0].data["severity"], 4);
    EXPECT_EQ(notified[0].data["severityLabel"], "broken");
    EXPECT_EQ(notified[0].priority, "high");
    ASSERT_EQ(refreshed.size(), 1u);
}

TEST_F(DurFixture, ArmorHitWearSkipsWeapon)
{
    equip(1, 200, 11); // main_hand: excluded from armor wear
    equip(1, 201, 12); // chest: wears
    dur.applyArmorHitWear(1);
    EXPECT_EQ(slotDur(1, 11), 0); // untouched
    EXPECT_EQ(slotDur(1, 12), 99);
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_EQ(nlohmann::json::parse(saved[0])["body"]["inventoryItemId"], 12);
}

TEST_F(DurFixture, DeathPenaltyAllEquipped)
{
    equip(1, 200, 11);
    equip(1, 201, 12);
    dur.applyDeathPenalty(1);
    // ceil(100 * 0.05) = 5 on each, warnings none (1.00 -> 0.95)
    EXPECT_EQ(slotDur(1, 11), 95);
    EXPECT_EQ(slotDur(1, 12), 95);
    EXPECT_EQ(saved.size(), 2u);
    EXPECT_TRUE(notified.empty());
    EXPECT_TRUE(refreshed.empty());
}

TEST_F(DurFixture, NonDurableSkipped)
{
    equip(1, 202, 13); // bread: not durable
    dur.applyDeathPenalty(1);
    dur.applyArmorHitWear(1);
    EXPECT_TRUE(saved.empty());
}

TEST_F(DurFixture, NoCallbacksNoCrash)
{
    DurabilityService silent{inv, items, cfg, logger};
    equip(1, 200, 11);
    silent.applyWeaponHitWear(1); // wear applies, callbacks null-guarded
    silent.applyArmorHitWear(1);  // main_hand skipped
    silent.applyDeathPenalty(1);  // 99 - 5
    EXPECT_EQ(slotDur(1, 11), 94);
    SUCCEED();
}
