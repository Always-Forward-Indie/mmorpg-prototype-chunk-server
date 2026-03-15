#pragma once
#include "data/DataStructs.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Manages in-memory equipment state for all active characters.
 *
 * Responsibilities:
 *  - Build equipment state from inventory items marked isEquipped=true (on character join)
 *  - Validate and execute equipItem / unequipItem operations
 *  - Track two-handed weapon blocking of off_hand slot
 *  - Build EQUIPMENT_STATE JSON payloads for the client
 *  - Clear equipment on character disconnect
 *
 * DB persistence is NOT done here — the EquipmentEventHandler is responsible
 * for sending SAVE_EQUIPMENT_CHANGE to the game server after calling mutating ops here.
 */
class EquipmentManager
{
  public:
    enum class EquipError
    {
        NONE,
        ITEM_NOT_IN_INVENTORY,
        ITEM_NOT_EQUIPPABLE,
        LEVEL_REQUIREMENT_NOT_MET,
        CLASS_RESTRICTION,
        SLOT_BLOCKED_BY_TWO_HANDED,
        EQUIP_FAILED
    };

    enum class UnequipError
    {
        NONE,
        SLOT_EMPTY,
        UNEQUIP_FAILED
    };

    struct EquipResult
    {
        EquipError error = EquipError::NONE;
        std::string equipSlotSlug;
        int swappedOutInventoryItemId = 0; // 0 = slot was empty before equip
    };

    struct UnequipResult
    {
        UnequipError error = UnequipError::NONE;
        int inventoryItemId = 0; // item that was removed from the slot
        std::string equipSlotSlug;
    };

    EquipmentManager(
        InventoryManager &inventoryManager,
        ItemManager &itemManager,
        CharacterManager &characterManager,
        Logger &logger);

    void setGameConfigService(GameConfigService *cfg)
    {
        configService_ = cfg;
    }

    /**
     * @brief Called after SET_PLAYER_INVENTORY loads items for a character.
     *        Scans inventory for isEquipped=true items and rebuilds the slot map.
     */
    void buildFromInventory(int characterId);

    /**
     * @brief Equip an item by player_inventory.id.
     * @return EquipResult with error code and filled slot name on success.
     *         If the slot was already occupied the old item is auto-swapped to
     *         inventory and its inventoryItemId is placed in swappedOutInventoryItemId.
     */
    EquipResult equipItem(int characterId, int inventoryItemId);

    /**
     * @brief Unequip the item in a specific slot by slug.
     * @return UnequipResult with error code and unequipped inventoryItemId on success.
     */
    UnequipResult unequipItem(int characterId, const std::string &slotSlug);

    /**
     * @brief Build full EQUIPMENT_STATE body JSON for a character.
     *        All 12 slots are present; empty slots are null.
     *        off_hand shows {blockedByTwoHanded:true} when applicable.
     */
    nlohmann::json buildEquipmentStateJson(int characterId) const;

    /** Returns the CharacterEquipmentStruct copy for external use (read-only snapshot). */
    CharacterEquipmentStruct getEquipmentState(int characterId) const;

    /** Remove all equipment state for a character (call on disconnect). */
    void clearCharacter(int characterId);

    /**
     * @brief Calculate carry weight limit: base + strength * per_strength_config.
     * Reads carry_weight.base and carry_weight.per_strength from GameConfigService.
     */
    float getCarryWeightLimit(int characterId) const;

    /**
     * @brief Find the equipped slot and inventoryItemId for a given itemId (template ID).
     * @return pair<slotSlug, inventoryItemId> if found, std::nullopt if item is not equipped.
     */
    std::optional<std::pair<std::string, int>> findSlotForItemId(int characterId, int itemId) const;

  private:
    InventoryManager &inventoryManager_;
    ItemManager &itemManager_;
    CharacterManager &characterManager_;
    Logger &logger_;
    GameConfigService *configService_ = nullptr;

    std::shared_ptr<spdlog::logger> log_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<int, CharacterEquipmentStruct> equipment_; // characterId → state

    static const std::vector<std::string> ALL_SLOTS;

    // Build a slot item from an inventory item (must hold itemManager_ data).
    EquipmentSlotItemStruct slotFromInventoryItem(
        const PlayerInventoryItemStruct &invItem,
        const ItemDataStruct &itemData,
        float warningThreshold) const;

    float getWarningThreshold() const;

    // Inline helpers
    static std::string errorToString(EquipError e);
    static std::string errorToString(UnequipError e);
};
