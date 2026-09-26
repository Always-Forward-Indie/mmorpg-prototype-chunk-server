#pragma once
#include "data/DataStructs.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>

// Forward declaration
class EventQueue;
class QuestManager;

class InventoryManager
{
  public:
    InventoryManager(ItemManager &itemManager, Logger &logger);

    /**
     * @brief Set event queue for sending inventory update events
     * @param eventQueue Event queue to send INVENTORY_UPDATE events
     */
    void setEventQueue(EventQueue *eventQueue);
    void setQuestManager(QuestManager *questManager);

    /**
     * @brief Add item to player's inventory
     * @param characterId Character ID
     * @param itemId Item ID to add
     * @param quantity Quantity to add
     * @return true if successfully added, false otherwise
     */
    bool addItemToInventory(int characterId, int itemId, int quantity = 1);

    /**
     * @brief Remove item from player's inventory
     * @param characterId Character ID
     * @param itemId Item ID to remove
     * @param quantity Quantity to remove
     * @return true if successfully removed, false otherwise
     */
    bool removeItemFromInventory(int characterId, int itemId, int quantity = 1);
    /** Remove a specific inventory row by its player_inventory.id (inventoryItemId). Avoids ambiguity when two rows share the same itemId. */
    bool removeItemFromInventoryById(int characterId, int inventoryItemId, int quantity);

    /**
     * @brief Get player's inventory
     * @param characterId Character ID
     * @return Vector of PlayerInventoryItemStruct
     */
    std::vector<PlayerInventoryItemStruct> getPlayerInventory(int characterId) const;

    /**
     * @brief Check if player has item
     * @param characterId Character ID
     * @param itemId Item ID to check
     * @param requiredQuantity Required quantity (default 1)
     * @return true if player has enough of the item
     */
    bool hasItem(int characterId, int itemId, int requiredQuantity = 1) const;

    /**
     * @brief Get item quantity in inventory
     * @param characterId Character ID
     * @param itemId Item ID
     * @return Quantity of item (0 if not found)
     */
    int getItemQuantity(int characterId, int itemId) const;

    /**
     * @brief Clear player's inventory (for testing or character deletion)
     * @param characterId Character ID
     */
    void clearPlayerInventory(int characterId);

    /**
     * @brief Get total number of different items in inventory
     * @param characterId Character ID
     * @return Number of different item types
     */
    int getInventoryItemCount(int characterId) const;

    /**
     * @brief Convert inventory item to full JSON with complete item data
     * @param item Player inventory item
     * @return JSON representation with full item data
     */
    nlohmann::json inventoryItemToJson(const PlayerInventoryItemStruct &item) const;

    /**
     * @brief Set callback for immediately persisting inventory changes to game server DB
     * @param callback Function that sends serialized packet to game server
     */
    void setSaveInventoryCallback(std::function<void(const std::string &)> callback);

    /**
     * @brief Load player inventory from DB response (called on character login).
     *        Replaces any in-memory inventory for the character without triggering saves.
     * @param characterId Character ID
     * @param items Items loaded from DB
     */
    void loadPlayerInventory(int characterId, const std::vector<PlayerInventoryItemStruct> &items);

    /**
     * @brief Get all equipped items for a character
     */
    std::vector<PlayerInventoryItemStruct> getEquippedItems(int characterId) const;

    /**
     * @brief Get the equipped weapon (equipSlotSlug == "main_hand" or "two_hand"), if any
     */
    std::optional<PlayerInventoryItemStruct> getEquippedWeapon(int characterId) const;

    /**
     * @brief Update durability in-memory (does NOT persist — caller must save separately)
     */
    void updateDurability(int characterId, int inventoryItemId, int newDurability);

    /**
     * @brief Update Item Soul kill_count in-memory (does NOT persist — caller must save separately)
     */
    void updateItemKillCount(int characterId, int inventoryItemId, int newKillCount);

    /**
     * @brief Mark an inventory item as equipped or unequipped in-memory
     */
    void setItemEquipped(int characterId, int inventoryItemId, bool equipped);

    /**
     * @brief Insert a fully-populated instanced item into the in-memory inventory without
     *        triggering a DB upsert. Used when picking up a player-dropped item that already
     *        has a player_inventory row (character_id was NULL while on ground).
     */
    void addInstancedItemToInventory(int characterId, const PlayerInventoryItemStruct &inst);

    /**
     * @brief Remove an item from in-memory inventory without sending any DB save event.
     *        Used when dropping an instanced item: the DB row stays alive (character_id
     *        will be nullified separately by LootManager).
     */
    void evictFromMemory(int characterId, int inventoryItemId);

    /**
     * @brief Calculate total weight of all items (equipped + unequipped) in character's inventory.
     * @return Sum of (item.weight * quantity) for every inventory slot.
     */
    float getTotalWeight(int characterId) const;

    /**
    * @brief Update the in-memory DB row ID for an inventory item after server-side upsert.
    *        Called when the game server responds with the assigned player_inventory.id.
    *        When syncToken != 0 the entry carrying that exact token is fixed
    *        (disambiguates several id==0 rows with the same itemId); 0 keeps
    *        the legacy first-id==0 fallback for token-less echoes.
    */
    void updateInventoryItemId(int characterId, int itemId, int64_t newId, uint64_t syncToken = 0);

    /**
    * @brief Rows still awaiting their DB id whose retry deadline has passed.
    *        The caller re-emits saveInventoryChange with the SAME syncToken
    *        (game-side upsert is absolute-SET, so resends converge instead of
    *        double-counting). nowMs is chunk steady-ms (fake clocks in tests).
    */
    static constexpr int64_t kSyncRetryMs = 5000;
    // Give-up: ~5 min of unacked retries, then clear the token (legacy
    // first-id==0 matching takes over). A permanently unsynced row is a
    // real bug — it deserves one warn, not infinite 5s noise.
    static constexpr int kSyncMaxAttempts = 60;
    std::vector<PlayerInventoryItemStruct> dueSyncResend(int64_t nowMs);

    /** One deferred equip/unequip persist, flushed once its row syncs. */
    struct PendingEquipSave
    {
        int characterId = 0;
        std::string action;   // "equip" | "unequip"
        std::string slotSlug; // equip only ("" for unequip)
        int inventoryItemId = 0; // real DB id, filled at flush time
        int itemId = 0;
        uint64_t syncToken = 0;
    };

    /**
    * @brief Stash an equip/unequip persist until its row syncs (no-op when
    *        syncToken == 0 — caller must send immediately instead).
    */
    void deferEquipSave(uint64_t syncToken, int characterId,
        const std::string &action, const std::string &slotSlug, int itemId);

    /**
    * @brief Take deferred saves whose rows just synced (clears the queue).
    *        Called right after updateInventoryItemId; the caller emits them.
    */
    std::vector<PendingEquipSave> drainPendingEquipSaves();

    /**
     * @brief Get the current gold (gold_coin) amount for a character.
     * @return Quantity of "gold_coin" items; 0 if none.
     */
    int getGoldAmount(int characterId) const;

  private:
    ItemManager &itemManager_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Event queue for sending inventory update events
    EventQueue *eventQueue_;
    QuestManager *questManager_ = nullptr;
    std::function<void(const std::string &)> saveInventoryCallback_;

    // Store inventories (characterId -> vector of inventory items)
    std::map<int, std::vector<PlayerInventoryItemStruct>> playerInventories_;

    // Monotonic pending-sync token source (0 is reserved = no pending sync).
    // Random start per process: tokens must not collide with a previous
    // boot's tokens (game keys idempotency on them after the sync_token
    // migration), same rationale as FactOutbox::bootId_.
    uint64_t nextSyncToken_ = 0;
    bool syncTokenSeeded_ = false;

    // Equip/unequip persists stashed while their row awaits its DB id,
    // keyed by the row's syncToken. Flushed with the real id once the
    // inventoryItemIdSync echo lands (see drainPendingEquipSaves).
    std::map<uint64_t, std::vector<PendingEquipSave>> pendingEquipSaves_;

    // Thread safety
    mutable std::shared_mutex inventoryMutex_;

    /**
     * @brief Find inventory item entry for character and item
     * @param characterId Character ID
     * @param itemId Item ID
     * @return Iterator to the item or end() if not found
     * NOTE: internal helper, call only with inventoryMutex_ held (see
     * addItemToInventory). There is deliberately no const overload: the old
     * one returned an iterator into a function-local static on miss (aliasing
     * hazard) and had zero callers, so it was removed (Wave 1.2).
     */
    std::vector<PlayerInventoryItemStruct>::iterator findInventoryItem(int characterId, int itemId);

    // Merge duplicate stacks of the same item (same itemId) into a single row
    void consolidateInventory(int characterId);
};
