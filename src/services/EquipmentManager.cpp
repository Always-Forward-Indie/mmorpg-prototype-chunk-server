#include "services/EquipmentManager.hpp"
#include <algorithm>
#include <spdlog/logger.h>

// All 12 slot slugs in display order
const std::vector<std::string> EquipmentManager::ALL_SLOTS = {
    "head", "chest", "legs", "feet", "hands", "waist", "necklace", "ring_1", "ring_2", "main_hand", "off_hand", "cloak"};

EquipmentManager::EquipmentManager(
    InventoryManager &inventoryManager,
    ItemManager &itemManager,
    CharacterManager &characterManager,
    Logger &logger)
    : inventoryManager_(inventoryManager),
      itemManager_(itemManager),
      characterManager_(characterManager),
      logger_(logger)
{
    log_ = logger.getSystem("equipment");
}

// ─── Private helpers ──────────────────────────────────────────────────────────

float
EquipmentManager::getWarningThreshold() const
{
    if (configService_)
        return configService_->getFloat("durability.warning_threshold_pct", 0.30f);
    return 0.30f;
}

EquipmentSlotItemStruct
EquipmentManager::slotFromInventoryItem(
    const PlayerInventoryItemStruct &invItem,
    const ItemDataStruct &itemData,
    float warningThreshold) const
{
    EquipmentSlotItemStruct slot;
    slot.inventoryItemId = invItem.id;
    slot.itemId = invItem.itemId;
    slot.itemSlug = itemData.slug;
    slot.durabilityCurrent = invItem.durabilityCurrent;
    slot.durabilityMax = itemData.durabilityMax;
    slot.blockedByTwoHanded = false;

    if (itemData.isDurable && itemData.durabilityMax > 0)
    {
        float ratio = static_cast<float>(invItem.durabilityCurrent) / itemData.durabilityMax;
        slot.isDurabilityWarning = (ratio < warningThreshold);
    }
    return slot;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void
EquipmentManager::buildFromInventory(int characterId)
{
    const float threshold = getWarningThreshold();

    // 1. Fetch inventory OUTSIDE the equipment lock to avoid deadlock
    //    (InventoryManager has its own mutex).
    auto items = inventoryManager_.getPlayerInventory(characterId);

    // 2. Lock and rebuild equipment state ATOMICALLY — clear + populate in
    //    a single critical section so concurrent buildEquipmentStateJson()
    //    calls never observe an intermediate "cleared but not yet populated"
    //    state (which would broadcast empty equipment to other clients).
    std::unique_lock lock(mutex_);

    CharacterEquipmentStruct &equip = equipment_[characterId];
    equip.characterId = characterId;
    equip.slots.clear();
    equip.twoHandedActive = false;

    for (const auto &invItem : items)
    {
        if (!invItem.isEquipped)
            continue;

        const ItemDataStruct itemData = itemManager_.getItemById(invItem.itemId);
        if (itemData.id == 0 || itemData.equipSlotSlug.empty())
            continue;

        EquipmentSlotItemStruct slot = slotFromInventoryItem(invItem, itemData, threshold);
        equip.slots[itemData.equipSlotSlug] = std::move(slot);

        // Detect two-handed state
        if (itemData.equipSlotSlug == "main_hand" && itemData.isTwoHanded)
            equip.twoHandedActive = true;
    }

    log_->info("[EquipmentManager] Built equipment for char=" + std::to_string(characterId) +
               " slots=" + std::to_string(equip.slots.size()) +
               " 2h=" + std::to_string(equip.twoHandedActive));
}

EquipmentManager::EquipResult
EquipmentManager::equipItem(int characterId, int inventoryItemId)
{
    EquipResult result;

    // ── 1. Fetch inventory item ──────────────────────────────────────────────
    auto inventory = inventoryManager_.getPlayerInventory(characterId);
    const PlayerInventoryItemStruct *invItemPtr = nullptr;
    for (const auto &item : inventory)
    {
        if (item.id == inventoryItemId)
        {
            invItemPtr = &item;
            break;
        }
    }

    // Re-validate: take a copy because we'll unlock below
    if (!invItemPtr || invItemPtr->characterId != characterId)
    {
        result.error = EquipError::ITEM_NOT_IN_INVENTORY;
        log_->warn("[EquipmentManager] equipItem: item " + std::to_string(inventoryItemId) +
                   " not in inventory of char " + std::to_string(characterId));
        return result;
    }

    PlayerInventoryItemStruct invItem = *invItemPtr;

    // ── 2. Fetch item data ───────────────────────────────────────────────────
    const ItemDataStruct itemData = itemManager_.getItemById(invItem.itemId);
    if (!itemData.isEquippable || itemData.equipSlotSlug.empty())
    {
        result.error = EquipError::ITEM_NOT_EQUIPPABLE;
        return result;
    }

    // ── 3. Level requirement ─────────────────────────────────────────────────
    if (itemData.levelRequirement > 0)
    {
        const CharacterDataStruct charData = characterManager_.getCharacterData(characterId);
        if (charData.characterLevel < itemData.levelRequirement)
        {
            result.error = EquipError::LEVEL_REQUIREMENT_NOT_MET;
            log_->info("[EquipmentManager] equipItem: level req not met char=" +
                       std::to_string(characterId) + " need=" + std::to_string(itemData.levelRequirement) +
                       " have=" + std::to_string(charData.characterLevel));
            return result;
        }
    }

    // ── 4. Class restriction ─────────────────────────────────────────────────
    if (!itemData.allowedClassIds.empty())
    {
        const CharacterDataStruct charData = characterManager_.getCharacterData(characterId);
        bool allowed = std::find(
                           itemData.allowedClassIds.begin(),
                           itemData.allowedClassIds.end(),
                           charData.classId) != itemData.allowedClassIds.end();
        if (!allowed)
        {
            result.error = EquipError::CLASS_RESTRICTION;
            return result;
        }
    }

    const std::string &targetSlug = itemData.equipSlotSlug;

    // ── 5. Two-handed checks ─────────────────────────────────────────────────
    {
        std::shared_lock rlock(mutex_);
        auto it = equipment_.find(characterId);
        if (it != equipment_.end())
        {
            const CharacterEquipmentStruct &equip = it->second;

            // Equipping into off_hand while two-handed active → blocked
            if (targetSlug == "off_hand" && equip.twoHandedActive)
            {
                result.error = EquipError::SLOT_BLOCKED_BY_TWO_HANDED;
                return result;
            }
        }
    }

    const float threshold = getWarningThreshold();

    std::unique_lock lock(mutex_);
    CharacterEquipmentStruct &equip = equipment_[characterId];
    equip.characterId = characterId;

    // ── 6. Auto-swap if slot occupied ────────────────────────────────────────

    // If equipping a two-handed weapon and off_hand is occupied — swap it out first
    if (targetSlug == "main_hand" && itemData.isTwoHanded)
    {
        auto offIt = equip.slots.find("off_hand");
        if (offIt != equip.slots.end() && offIt->second.inventoryItemId != 0)
        {
            inventoryManager_.setItemEquipped(characterId, offIt->second.inventoryItemId, false);
            equip.slots.erase(offIt);
            // Note: caller (handler) must persist this swap-out as well
        }
    }

    // Swap out whatever was previously in the target slot
    auto existingIt = equip.slots.find(targetSlug);
    if (existingIt != equip.slots.end() && existingIt->second.inventoryItemId != 0)
    {
        result.swappedOutInventoryItemId = existingIt->second.inventoryItemId;
        inventoryManager_.setItemEquipped(characterId, existingIt->second.inventoryItemId, false);
        equip.slots.erase(existingIt);
    }

    // ── 7. Place item into slot ───────────────────────────────────────────────
    EquipmentSlotItemStruct slot = slotFromInventoryItem(invItem, itemData, threshold);
    equip.slots[targetSlug] = slot;

    inventoryManager_.setItemEquipped(characterId, inventoryItemId, true);

    // Update two-handed state
    if (targetSlug == "main_hand")
        equip.twoHandedActive = itemData.isTwoHanded;

    result.equipSlotSlug = targetSlug;
    log_->info("[EquipmentManager] equipItem: char=" + std::to_string(characterId) +
               " item=" + std::to_string(inventoryItemId) + " slot=" + targetSlug);
    return result;
}

EquipmentManager::UnequipResult
EquipmentManager::unequipItem(int characterId, const std::string &slotSlug)
{
    UnequipResult result;

    std::unique_lock lock(mutex_);
    auto equipIt = equipment_.find(characterId);
    if (equipIt == equipment_.end())
    {
        result.error = UnequipError::SLOT_EMPTY;
        return result;
    }

    CharacterEquipmentStruct &equip = equipIt->second;
    auto slotIt = equip.slots.find(slotSlug);
    if (slotIt == equip.slots.end() || slotIt->second.inventoryItemId == 0)
    {
        result.error = UnequipError::SLOT_EMPTY;
        return result;
    }

    result.inventoryItemId = slotIt->second.inventoryItemId;
    result.equipSlotSlug = slotSlug;

    inventoryManager_.setItemEquipped(characterId, result.inventoryItemId, false);
    equip.slots.erase(slotIt);

    // Reset two-handed state when main_hand is cleared
    if (slotSlug == "main_hand")
        equip.twoHandedActive = false;

    log_->info("[EquipmentManager] unequipItem: char=" + std::to_string(characterId) +
               " slot=" + slotSlug + " item=" + std::to_string(result.inventoryItemId));
    return result;
}

nlohmann::json
EquipmentManager::buildEquipmentStateJson(int characterId) const
{
    std::shared_lock lock(mutex_);

    nlohmann::json slots = nlohmann::json::object();

    const CharacterEquipmentStruct *equipPtr = nullptr;
    auto it = equipment_.find(characterId);
    if (it != equipment_.end())
        equipPtr = &it->second;

    for (const auto &slug : ALL_SLOTS)
    {
        if (!equipPtr)
        {
            slots[slug] = nlohmann::json(nullptr);
            continue;
        }

        // Special sentinel for blocked off_hand
        if (slug == "off_hand" && equipPtr->twoHandedActive)
        {
            auto slotIt = equipPtr->slots.find(slug);
            if (slotIt == equipPtr->slots.end() || slotIt->second.inventoryItemId == 0)
            {
                nlohmann::json blocked;
                blocked["blockedByTwoHanded"] = true;
                slots[slug] = blocked;
                continue;
            }
        }

        auto slotIt = equipPtr->slots.find(slug);
        if (slotIt == equipPtr->slots.end() || slotIt->second.inventoryItemId == 0)
        {
            slots[slug] = nlohmann::json(nullptr);
        }
        else
        {
            const auto &s = slotIt->second;
            nlohmann::json entry;
            entry["inventoryItemId"] = s.inventoryItemId;
            entry["itemId"] = s.itemId;
            entry["itemSlug"] = s.itemSlug;
            entry["durabilityCurrent"] = s.durabilityCurrent;
            entry["durabilityMax"] = s.durabilityMax;
            entry["isDurabilityWarning"] = s.isDurabilityWarning;
            entry["blockedByTwoHanded"] = false;
            slots[slug] = entry;
        }
    }

    return slots;
}

CharacterEquipmentStruct
EquipmentManager::getEquipmentState(int characterId) const
{
    std::shared_lock lock(mutex_);
    auto it = equipment_.find(characterId);
    if (it != equipment_.end())
        return it->second;
    return CharacterEquipmentStruct{};
}

void
EquipmentManager::clearCharacter(int characterId)
{
    std::unique_lock lock(mutex_);
    equipment_.erase(characterId);
    log_->info("[EquipmentManager] Cleared equipment for char=" + std::to_string(characterId));
}

std::optional<std::pair<std::string, int>>
EquipmentManager::findSlotForItemId(int characterId, int itemId) const
{
    std::shared_lock lock(mutex_);
    auto it = equipment_.find(characterId);
    if (it == equipment_.end())
        return std::nullopt;

    for (const auto &[slug, slotData] : it->second.slots)
    {
        if (slotData.itemId == itemId && slotData.inventoryItemId != 0)
            return std::make_pair(slug, slotData.inventoryItemId);
    }
    return std::nullopt;
}

float
EquipmentManager::getCarryWeightLimit(int characterId) const
{
    float base = 50.0f;
    float perStrength = 3.0f;

    if (configService_)
    {
        base = static_cast<float>(configService_->getInt("carry_weight.base", 50));
        perStrength = static_cast<float>(configService_->getInt("carry_weight.per_strength", 3));
    }

    int strength = 0;
    auto attrs = characterManager_.getCharacterAttributes(characterId);
    for (const auto &a : attrs)
    {
        if (a.slug == "strength")
        {
            strength = a.value;
            break;
        }
    }

    return base + perStrength * static_cast<float>(strength);
}
