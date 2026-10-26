// Unit tests for ItemManager + InventoryManager (Logger only, no DB/sockets).
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "events/EventQueue.hpp"

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
    // Production wiring: deltas (and their versions) only publish with a
    // queue attached. Without it the manager stays silent at version 0.
    EventQueue queue{1024};

    void SetUp() override
    {
        ItemDataStruct gold = makeItem(999, "gold_coin", 1000000);
        ItemDataStruct potion = makeItem(100, "health_potion", 64);
        ItemDataStruct sword = makeItem(200, "iron_sword", 1);
        sword.isEquippable = true;
        sword.equipSlotSlug = "main_hand";
        items.setItemsList({gold, potion, sword});
        inv.setEventQueue(&queue);
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

TEST_F(ShopFixture, DeferredEquipSaveFlushesOnSync)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 200, 1));
    uint64_t tok = 0;
    for (const auto &r : inv.getPlayerInventory(1))
        if (r.itemId == 200 && r.id == 0)
            tok = r.syncToken;
    ASSERT_NE(tok, 0u);
    // Zero token is a no-op (caller sends immediately instead).
    inv.deferEquipSave(0, 1, "equip", "main_hand", 200);
    EXPECT_TRUE(inv.drainPendingEquipSaves().empty());
    inv.deferEquipSave(tok, 1, "equip", "main_hand", 200);
    // Nothing to emit before the echo (id still unknown).
    EXPECT_TRUE(inv.drainPendingEquipSaves().empty());
    inv.updateInventoryItemId(1, 200, 77, tok);
    auto out = inv.drainPendingEquipSaves();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].characterId, 1);
    EXPECT_EQ(out[0].action, "equip");
    EXPECT_EQ(out[0].inventoryItemId, 77);
    EXPECT_EQ(out[0].itemId, 200);
    EXPECT_EQ(out[0].syncToken, tok);
    // Drained: second call is empty (exactly-once flush).
    EXPECT_TRUE(inv.drainPendingEquipSaves().empty());
}

TEST_F(ShopFixture, LegacyEchoAlsoFlushesDeferredSaves)
{
    // Mixed-version path: a token-less echo clears the pending row —
    // stashed saves for its token must still flush with the real id.
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5));
    uint64_t tok = 0;
    for (const auto &r : inv.getPlayerInventory(1))
        if (r.itemId == 100 && r.id == 0)
            tok = r.syncToken;
    ASSERT_NE(tok, 0u);
    inv.deferEquipSave(tok, 1, "unequip", "main_hand", 100);
    inv.updateInventoryItemId(1, 100, 78); // legacy: token 0
    auto out = inv.drainPendingEquipSaves();
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].inventoryItemId, 78);
    EXPECT_EQ(out[0].action, "unequip");
}

TEST_F(ShopFixture, MultiStackRemoveSendsOneSavePerStack)
{
    // Two non-stackable instances (separate rows, same template — the A4
    // shape): removing both must persist EACH row, or the game resurrects
    // the unreported ones on the next login.
    PlayerInventoryItemStruct a, b;
    a.id = 5;
    a.characterId = 1;
    a.itemId = 200;
    a.quantity = 1;
    b.id = 6;
    b.characterId = 1;
    b.itemId = 200;
    b.quantity = 1;
    inv.loadPlayerInventory(1, {a, b});
    std::vector<std::pair<int, int>> sent; // (inventoryItemId, quantity)
    inv.setSaveInventoryCallback([&sent](const std::string &data)
        {
            auto j = nlohmann::json::parse(data);
            sent.emplace_back(j["body"].value("inventoryItemId", -1),
                j["body"].value("quantity", -1));
        });
    EXPECT_TRUE(inv.removeItemFromInventory(1, 200, 2));
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0], std::make_pair(5, 0));
    EXPECT_EQ(sent[1], std::make_pair(6, 0));
    EXPECT_EQ(inv.getItemQuantity(1, 200), 0);
}

TEST_F(ShopFixture, SyncRetryGivesUpToLegacyAfterBudget)
{
    EXPECT_TRUE(inv.addItemToInventory(1, 100, 5));
    // Burn the whole retry budget with fake clocks (no sleeping).
    int64_t nowMs = 0;
    for (int i = 0; i < InventoryManager::kSyncMaxAttempts; ++i)
    {
        auto due = inv.dueSyncResend(nowMs);
        ASSERT_EQ(due.size(), 1u);
        nowMs += InventoryManager::kSyncRetryMs;
    }
    // One more: budget exhausted -> token cleared, nothing returned.
    EXPECT_TRUE(inv.dueSyncResend(nowMs).empty());
    bool cleared = true;
    for (const auto &r : inv.getPlayerInventory(1))
        if (r.itemId == 100 && r.id == 0 && r.syncToken != 0)
            cleared = false;
    EXPECT_TRUE(cleared);
    // Row still id==0: legacy first-id==0 echo still heals it.
    inv.updateInventoryItemId(1, 100, 55);
    EXPECT_EQ(inv.getPlayerInventory(1)[0].id, 55);
}

// #132: gold derived from a snapshot copy must equal the manager read.
// Call sites send items[] + gold from one copy; any divergence means a
// mixed-generation response (gold 0 with present items).
TEST_F(ShopFixture, GoldFromSnapshotMatchesManager)
{
    EXPECT_TRUE(inv.addItemToInventory(7, 999, 310));
    EXPECT_TRUE(inv.addItemToInventory(7, 100, 5));
    const auto snapshot = inv.getPlayerInventory(7);
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(inv.getGoldFromSnapshot(snapshot), 310);
    EXPECT_EQ(inv.getGoldFromSnapshot(snapshot), inv.getGoldAmount(7));
}

TEST_F(ShopFixture, GoldFromSnapshotEmptyIsZero)
{
    EXPECT_TRUE(inv.getPlayerInventory(424242).empty());
    EXPECT_EQ(inv.getGoldFromSnapshot({}), 0);
    EXPECT_EQ(inv.getGoldFromSnapshot(inv.getPlayerInventory(424242)), 0);
}

// S15a: per-character generation — 0 until first publish, +1 per delta,
// +1 per pushed snapshot, isolated across characters.
TEST_F(ShopFixture, InventoryVersionStartsAtZero)
{
    InventoryManager bare{items, logger}; // no queue: silent, stays 0
    EXPECT_EQ(bare.getInventoryVersion(11), 0u);
    EXPECT_TRUE(bare.addItemToInventory(11, 999, 10));
    EXPECT_EQ(bare.getInventoryVersion(11), 0u);
}

TEST_F(ShopFixture, InventoryVersionBumpsPerDelta)
{
    EXPECT_TRUE(inv.addItemToInventory(11, 999, 10));
    EXPECT_EQ(inv.getInventoryVersion(11), 1u);
    EXPECT_TRUE(inv.addItemToInventory(11, 100, 2));
    EXPECT_EQ(inv.getInventoryVersion(11), 2u);
    // Other characters are unaffected.
    EXPECT_EQ(inv.getInventoryVersion(12), 0u);
}

TEST_F(ShopFixture, InventoryVersionBumpsOnPushedSnapshot)
{
    EXPECT_TRUE(inv.addItemToInventory(13, 999, 10));
    EXPECT_EQ(inv.trackSnapshotPublished(13), 2u);
    EXPECT_EQ(inv.getInventoryVersion(13), 2u);
    EXPECT_EQ(inv.trackSnapshotPublished(13), 3u);
}
