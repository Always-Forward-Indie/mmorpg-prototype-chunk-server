// Unit tests for ItemManager + InventoryManager (Logger only, no DB/sockets).
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>

namespace
{

ItemDataStruct makeItem(int id, const std::string &slug, int stackMax = 64)
{
    ItemDataStruct it;
    it.id = id;
    it.slug = slug;
    it.stackMax = stackMax;
    it.vendorPriceBuy = 10;
    it.vendorPriceSell = 4;
    return it;
}

struct ShopFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};

    void SetUp() override
    {
        ItemDataStruct gold = makeItem(999, "gold_coin", 1000000);
        ItemDataStruct potion = makeItem(100, "health_potion", 64);
        ItemDataStruct sword = makeItem(200, "iron_sword", 1);
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        items.setItemsList({gold, potion, sword});
    }
};

int rowId(InventoryManager &inv, int charId, int itemId)
{
    for (const auto &row : inv.getPlayerInventory(charId))
        if (row.itemId == itemId)
            return row.id;
    return 0;
}

// In-memory rows start with id == 0; the DB assigns real ids on upsert
// (INVENTORY_ITEM_ID_SYNC). Mirror that with updateInventoryItemId.
int syncRowId(InventoryManager &inv, int charId, int itemId, int64_t newId)
{
    inv.updateInventoryItemId(charId, itemId, newId);
    return rowId(inv, charId, itemId);
}

} // namespace

TEST_F(ShopFixture, ItemCatalogLookup)
{
    EXPECT_EQ(items.getItemById(100).slug, "health_potion");
    EXPECT_EQ(items.getItemById(424242).id, 0); // missing -> empty struct
    ASSERT_NE(items.getItemBySlug("gold_coin"), nullptr);
    EXPECT_EQ(items.getItemBySlug("nope"), nullptr);
}

TEST_F(ShopFixture, AddRemoveHasQuantity)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5));
    EXPECT_TRUE(inv.hasItem(1, 100, 5));
    EXPECT_EQ(inv.getItemQuantity(1, 100), 5);
    EXPECT_TRUE(inv.removeItemFromInventory(1, 100, 2));
    EXPECT_EQ(inv.getItemQuantity(1, 100), 3);
    EXPECT_FALSE(inv.hasItem(1, 100, 4));
}

TEST_F(ShopFixture, RemoveMoreThanOwnedFailsCleanly)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 3));
    EXPECT_FALSE(inv.removeItemFromInventory(1, 100, 10));
    EXPECT_EQ(inv.getItemQuantity(1, 100), 3); // unchanged
}

TEST_F(ShopFixture, UnknownItemRejected)
{
    EXPECT_FALSE(inv.addItemToInventory(1, 424242, 1));
    EXPECT_EQ(inv.getItemQuantity(1, 424242), 0);
}

TEST_F(ShopFixture, RemoveByRowIdDisambiguatesStacks)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 200, 1));
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 1));
    EXPECT_EQ(syncRowId(inv, 1, 200, 11), 11);
    EXPECT_EQ(syncRowId(inv, 1, 100, 22), 22);
    EXPECT_TRUE(inv.removeItemFromInventoryById(1, 22, 1));
    EXPECT_EQ(inv.getItemQuantity(1, 100), 0);
    EXPECT_EQ(inv.getItemQuantity(1, 200), 1);
}

TEST_F(ShopFixture, GoldTracking)
{
    EXPECT_EQ(inv.getGoldAmount(1), 0);
    EXPECT_TRUE(inv.addItemToInventory(1, 999, 250));
    EXPECT_EQ(inv.getGoldAmount(1), 250);
    EXPECT_TRUE(inv.removeItemFromInventory(1, 999, 50));
    EXPECT_EQ(inv.getGoldAmount(1), 200);
}

TEST_F(ShopFixture, ClearInventory)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5));
    EXPECT_TRUE(inv.addItemToInventory(1, 200, 1));
    inv.clearPlayerInventory(1);
    EXPECT_EQ(inv.getInventoryItemCount(1), 0);
    EXPECT_EQ(inv.getGoldAmount(1), 0);
}

TEST_F(ShopFixture, EquipFlagAndWeaponLookup)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 200, 1));
    int swordRow = syncRowId(inv, 1, 200, 33);
    ASSERT_NE(swordRow, 0);
    EXPECT_FALSE(inv.getEquippedWeapon(1).has_value());
    inv.setItemEquipped(1, swordRow, true);
    auto weapon = inv.getEquippedWeapon(1);
    ASSERT_TRUE(weapon.has_value());
    EXPECT_EQ(weapon->itemId, 200);
    auto equipped = inv.getEquippedItems(1);
    ASSERT_EQ(equipped.size(), 1u);
    EXPECT_EQ(equipped[0].itemId, 200);
}

TEST_F(ShopFixture, DurabilityUpdate)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 200, 1));
    int swordRow = syncRowId(inv, 1, 200, 44);
    ASSERT_NE(swordRow, 0);
    inv.updateDurability(1, swordRow, 37);
    for (const auto &row : inv.getPlayerInventory(1))
        if (row.id == swordRow)
            EXPECT_EQ(row.durabilityCurrent, 37);
}

TEST_F(ShopFixture, TotalWeight)
{
    ItemDataStruct heavy = makeItem(300, "anvil");
    heavy.weight = 25.5f;
    items.setItemsList({makeItem(999, "gold_coin", 1000000), makeItem(100, "health_potion"), heavy});
    EXPECT_TRUE(inv.addItemToInventory(1, 300, 2));
    EXPECT_DOUBLE_EQ(inv.getTotalWeight(1), 51.0);
}

TEST_F(ShopFixture, LoadReplacesSyncedRows)
{
    // Synced rows (real DB ids) are replaced by the push wholesale.
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5));
    EXPECT_EQ(syncRowId(inv, 1, 100, 7), 7);
    PlayerInventoryItemStruct row;
    row.id = 9;
    row.characterId = 1;
    row.itemId = 200;
    row.quantity = 1;
    inv.loadPlayerInventory(1, {row});
    EXPECT_EQ(inv.getItemQuantity(1, 100), 0);
    EXPECT_EQ(inv.getItemQuantity(1, 200), 1);
}

TEST_F(ShopFixture, LoadPreservesPendingSyncRows)
{
    // JOIN audit: a row still awaiting its id echo (save in flight) survives
    // the DB push instead of vanishing until the next mutation.
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5)); // id==0, token stamped
    PlayerInventoryItemStruct sword;
    sword.id = 7;
    sword.characterId = 1;
    sword.itemId = 200;
    sword.quantity = 1;
    inv.loadPlayerInventory(1, {sword});
    EXPECT_EQ(inv.getItemQuantity(1, 100), 5); // pending kept
    EXPECT_EQ(inv.getItemQuantity(1, 200), 1);
    EXPECT_EQ(rowId(inv, 1, 100), 0); // still awaiting sync
}

TEST_F(ShopFixture, LoadAbsorbsPendingWhenDbIsNewer)
{
    // Max-wins: the push already contains the picked-up quantity (save
    // applied before the push) -> pending folds into the DB row, no double.
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5)); // id==0, token stamped
    PlayerInventoryItemStruct dbRow;
    dbRow.id = 42;
    dbRow.characterId = 1;
    dbRow.itemId = 100;
    dbRow.quantity = 5;
    inv.loadPlayerInventory(1, {dbRow});
    EXPECT_EQ(inv.getItemQuantity(1, 100), 5); // not 10
    EXPECT_EQ(rowId(inv, 1, 100), 42);         // DB id wins
    EXPECT_TRUE(inv.dueSyncResend(0).empty()); // nothing left to retry
}

TEST_F(ShopFixture, SyncTokenDisambiguatesIdenticalPendings)
{
    // Two pending rows with the same itemId: the echo fixes exactly its own.
    ItemDataStruct herb = makeItem(300, "bitter_herb", 1); // non-stackable
    items.setItemsList({makeItem(999, "gold_coin", 1000000),
        makeItem(100, "health_potion", 64),
        makeItem(200, "iron_sword", 1), herb});
    EXPECT_TRUE(inv.addItemToInventory(1, 300, 1));
    EXPECT_TRUE(inv.addItemToInventory(1, 300, 1));
    uint64_t first = 0;
    for (const auto &r : inv.getPlayerInventory(1))
        if (r.itemId == 300 && r.id == 0)
        {
            first = r.syncToken;
            break;
        }
    ASSERT_NE(first, 0u);
    inv.updateInventoryItemId(1, 300, 101, first);
    int synced = 0, pending = 0;
    for (const auto &r : inv.getPlayerInventory(1))
        if (r.itemId == 300)
        {
            if (r.id == 101)
                ++synced;
            else if (r.id == 0)
                ++pending;
        }
    EXPECT_EQ(synced, 1);
    EXPECT_EQ(pending, 1);
    // Stale echo for the same token is now a no-op (already synced).
    inv.updateInventoryItemId(1, 300, 999, first);
    EXPECT_EQ(rowId(inv, 1, 300), 101);
}

TEST_F(ShopFixture, DueSyncResendHonoursDeadline)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5)); // syncNextMs == 0: due now
    auto due0 = inv.dueSyncResend(0);
    ASSERT_EQ(due0.size(), 1u);
    EXPECT_EQ(due0[0].itemId, 100);
    EXPECT_EQ(due0[0].quantity, 5);
    EXPECT_NE(due0[0].syncToken, 0u);
    // Next deadline pushed by kSyncRetryMs: silent until then, due after.
    EXPECT_TRUE(inv.dueSyncResend(4999).empty());
    EXPECT_EQ(inv.dueSyncResend(5000).size(), 1u);
    // Syncing clears the row from the retry set.
    inv.updateInventoryItemId(1, 100, 77, due0[0].syncToken);
    EXPECT_TRUE(inv.dueSyncResend(100000).empty());
}
