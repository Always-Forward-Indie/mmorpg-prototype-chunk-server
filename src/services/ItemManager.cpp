#include "services/ItemManager.hpp"

ItemManager::ItemManager(Logger &logger)
    : logger_(logger)
{
}

void
ItemManager::setItemsList(const std::vector<ItemDataStruct> &items)
{
    try
    {
        if (items.empty())
        {
            logger_.logError("No items received from Game Server");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(itemsMutex_);
        items_.clear();

        for (const auto &item : items)
        {
            items_[item.id] = item;
        }

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
            logger_.logError("No mob loot information received from Game Server");
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
