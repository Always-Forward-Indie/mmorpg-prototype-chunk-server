#include "services/ItemManager.hpp"
#include <spdlog/logger.h>

ItemManager::ItemManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("item");
}

void
ItemManager::setItemsList(const std::vector<ItemDataStruct> &items)
{
    try
    {
        if (items.empty())
        {
            log_->error("No items received from Game Server");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(itemsMutex_);
        items_.clear();

        for (const auto &item : items)
        {
            items_[item.id] = item;
        }

        recomputeCatalogVersion();

        logger_.log("Loaded " + std::to_string(items_.size()) + " items from Game Server");
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading items: " + std::string(e.what()));
    }
}

void
ItemManager::setMobLootInfo(const std::vector<MobLootInfoStruct> &mobLootInfo)
{
    try
    {
        if (mobLootInfo.empty())
        {
            log_->error("No mob loot information received from Game Server");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(lootMutex_);
        mobLootInfo_.clear();

        for (const auto &lootInfo : mobLootInfo)
        {
            mobLootInfo_[lootInfo.mobId].push_back(lootInfo);
        }

        int totalLootEntries = mobLootInfo.size();
        logger_.log("Loaded loot information for " + std::to_string(mobLootInfo_.size()) +
                    " mobs with " + std::to_string(totalLootEntries) + " total loot entries");
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading mob loot: " + std::string(e.what()));
    }
}

uint32_t
ItemManager::getCatalogVersion() const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    return catalogVersion_;
}

void
ItemManager::recomputeCatalogVersion()
{
    // FNV-1a 32-bit over gameplay-relevant fields (display *Name strings and
    // DB row keys excluded on purpose: they never affect client behavior).
    // items_ is a std::map: iteration order is stable across runs.
    uint32_t h = 2166136261u;
    auto mix = [&h](const std::string &s)
    {
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 16777619u;
        }
        h ^= '|';
        h *= 16777619u;
    };
    for (const auto &[id, item] : items_)
    {
        mix(std::to_string(item.id));
        mix(item.slug);
        mix(std::to_string(item.isQuestItem));
        mix(std::to_string(item.itemType));
        mix(item.itemTypeSlug);
        mix(std::to_string(item.isContainer));
        mix(std::to_string(item.isDurable));
        mix(std::to_string(item.isTradable));
        mix(std::to_string(item.isEquippable));
        mix(std::to_string(item.isHarvest));
        mix(std::to_string(item.isUsable));
        mix(std::to_string(item.weight));
        mix(std::to_string(item.rarityId));
        mix(item.raritySlug);
        mix(std::to_string(item.stackMax));
        mix(std::to_string(item.durabilityMax));
        mix(std::to_string(item.vendorPriceBuy));
        mix(std::to_string(item.vendorPriceSell));
        mix(std::to_string(item.equipSlot));
        mix(item.equipSlotSlug);
        mix(std::to_string(item.levelRequirement));
        mix(std::to_string(item.isTwoHanded));
        for (int classId : item.allowedClassIds)
            mix(std::to_string(classId));
        mix(std::to_string(item.setId));
        mix(item.setSlug);
        for (const auto &attr : item.attributes)
        {
            mix(attr.slug);
            mix(std::to_string(attr.value));
            mix(attr.apply_on);
        }
        for (const auto &ue : item.useEffects)
        {
            mix(ue.effectSlug);
            mix(ue.attributeSlug);
            mix(std::to_string(ue.value));
            mix(std::to_string(ue.isInstant));
            mix(std::to_string(ue.durationSeconds));
            mix(std::to_string(ue.tickMs));
            mix(std::to_string(ue.cooldownSeconds));
        }
        mix(item.masterySlug);
    }
    // 31-bit only: the version crosses JSON as a number and lands in an int32
    // on the client (TryGetNumberField overflows past INT32_MAX and the client
    // would pin version 0 = "unknown" forever, re-downloading every join).
    // 0 is reserved for unknown: never emit it.
    catalogVersion_ = h & 0x7FFFFFFFu;
    if (catalogVersion_ == 0)
        catalogVersion_ = 1;
}

std::map<int, ItemDataStruct>
ItemManager::getItems() const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    return items_;
}

std::vector<ItemDataStruct>
ItemManager::getItemsAsVector() const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    std::vector<ItemDataStruct> itemsVector;

    for (const auto &item : items_)
    {
        itemsVector.push_back(item.second);
    }

    return itemsVector;
}

ItemDataStruct
ItemManager::getItemById(int itemId) const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    auto it = items_.find(itemId);
    if (it != items_.end())
    {
        return it->second;
    }
    return ItemDataStruct();
}

const ItemDataStruct *
ItemManager::getItemBySlug(const std::string &slug) const
{
    std::shared_lock<std::shared_mutex> lock(itemsMutex_);
    for (const auto &[id, item] : items_)
    {
        if (item.slug == slug)
        {
            // items_ is populated at startup and never mutated at runtime,
            // so returning a raw pointer is safe for the lifetime of the server.
            return &item;
        }
    }
    return nullptr;
}

std::map<int, std::vector<MobLootInfoStruct>>
ItemManager::getMobLootInfo() const
{
    std::shared_lock<std::shared_mutex> lock(lootMutex_);
    return mobLootInfo_;
}

std::vector<MobLootInfoStruct>
ItemManager::getLootForMob(int mobId) const
{
    std::shared_lock<std::shared_mutex> lock(lootMutex_);
    auto it = mobLootInfo_.find(mobId);
    if (it != mobLootInfo_.end())
    {
        return it->second;
    }
    return std::vector<MobLootInfoStruct>();
}
