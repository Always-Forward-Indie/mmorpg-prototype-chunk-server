// Unit tests for LootManager (explicit DI, no world).
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/LootManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/IStatsNotifier.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/ZoneEventManager.hpp"

#include <gtest/gtest.h>

namespace
{

PositionStruct pos(float x, float y, float z = 0.0f)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    p.positionZ = z;
    return p;
}

MobLootInfoStruct lootEntry(int mobId, int itemId, float chance, int qty = 1)
{
    MobLootInfoStruct e;
    e.mobId = mobId;
    e.itemId = itemId;
    e.dropChance = chance;
    e.minQuantity = qty;
    e.maxQuantity = qty;
    return e;
}

struct LootNotifier : IStatsNotifier
{
    void sendStatsUpdate(int) override
    {
    }
    void sendStatsUpdate(int, const std::string &) override
    {
    }
    void sendWorldNotification(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
    void sendWorldNotificationToGameZone(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
};

struct LootFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    MobInstanceManager instances{logger};
    GameZoneManager gameZones{logger};
    GameConfigService config{logger};
    LootNotifier notifier;
    ZoneEventManager zoneEvents{&notifier, logger};
    LootManager loot{items, instances, gameZones, zoneEvents, config, &notifier, logger};

    void SetUp() override
    {
        ItemDataStruct potion;
        potion.id = 100;
        potion.slug = "health_potion";
        ItemDataStruct sword;
        sword.id = 200;
        sword.slug = "iron_sword";
        items.setItemsList({potion, sword});
        loot.setInventoryManager(&inv); // pickup credits this inventory
    }
};

} // namespace

TEST_F(LootFixture, NoTableNoLoot)
{
    auto drops = loot.generateLootOnMobDeath(777, 1, pos(0, 0), 0);
    EXPECT_TRUE(drops.empty());
}

TEST_F(LootFixture, CertainDropAlwaysDrops)
{
    items.setMobLootInfo({lootEntry(7, 100, 1.0f, 2)});
    auto drops = loot.generateLootOnMobDeath(7, 1, pos(10, 20), 5);
    ASSERT_EQ(drops.size(), 1u);
    EXPECT_EQ(drops[0].itemId, 100);
    EXPECT_EQ(drops[0].quantity, 2);
    EXPECT_NE(drops[0].uid, 0);
    EXPECT_EQ(drops[0].droppedByMobUID, 1);
}

TEST_F(LootFixture, ImpossibleDropNeverDrops)
{
    items.setMobLootInfo({lootEntry(7, 100, 0.0f, 2)});
    for (int i = 0; i < 20; ++i)
        EXPECT_TRUE(loot.generateLootOnMobDeath(7, 1, pos(0, 0), 5).empty());
}

TEST_F(LootFixture, HarvestOnlySkippedInRegularDrop)
{
    MobLootInfoStruct e = lootEntry(7, 100, 1.0f, 1);
    e.isHarvestOnly = true;
    items.setMobLootInfo({e});
    EXPECT_TRUE(loot.generateLootOnMobDeath(7, 1, pos(0, 0), 5).empty());
}

TEST_F(LootFixture, DropPickupNearbyRoundtrip)
{
    DroppedItemStruct d = loot.dropItemByPlayer(1, 0, 100, 3, pos(100, 100));
    ASSERT_NE(d.uid, 0);
    EXPECT_EQ(d.itemId, 100);
    EXPECT_EQ(d.quantity, 3);

    auto near = loot.getDroppedItemsNearPosition(pos(100, 100), 200.0f);
    EXPECT_EQ(near.size(), 1u);
    auto far = loot.getDroppedItemsNearPosition(pos(10000, 10000), 200.0f);
    EXPECT_TRUE(far.empty());

    EXPECT_TRUE(loot.pickupDroppedItem(d.uid, 2, pos(100, 100)));
    EXPECT_EQ(loot.getDroppedItemByUID(d.uid).uid, 0); // gone after pickup
    EXPECT_EQ(inv.getItemQuantity(2, 100), 3);         // credited to picker
    EXPECT_FALSE(loot.pickupDroppedItem(d.uid, 2, pos(100, 100))); // twice fails
}

TEST_F(LootFixture, PickupUnknownUidFails)
{
    EXPECT_FALSE(loot.pickupDroppedItem(424242, 1, pos(0, 0)));
    EXPECT_EQ(loot.getDroppedItemByUID(424242).uid, 0);
}

TEST_F(LootFixture, CleanupOldDrops)
{
    loot.dropItemByPlayer(1, 0, 100, 1, pos(0, 0));
    loot.dropItemByPlayer(1, 0, 200, 1, pos(0, 0));
    EXPECT_EQ(loot.getAllDroppedItems().size(), 2u);
    loot.cleanupOldDroppedItems(0); // everything is older than 0s
    EXPECT_TRUE(loot.getAllDroppedItems().empty());
}
