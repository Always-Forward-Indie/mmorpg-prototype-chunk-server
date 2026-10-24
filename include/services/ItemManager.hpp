#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>

class ItemManager
{
  public:
    ItemManager(Logger &logger);

    /**
     * @brief Set items list received from Game Server
     * @param items Vector of ItemDataStruct
     */
    void setItemsList(const std::vector<ItemDataStruct> &items);

    /**
     * @brief Set mob loot information received from Game Server
     * @param mobLootInfo Vector of MobLootInfoStruct
     */
    void setMobLootInfo(const std::vector<MobLootInfoStruct> &mobLootInfo);

    /**
     * @brief Get all items as map
     * @return Map of item ID to ItemDataStruct
     */
    std::map<int, ItemDataStruct> getItems() const;

    /**
     * @brief Get all items as vector
     * @return Vector of ItemDataStruct
     */
    std::vector<ItemDataStruct> getItemsAsVector() const;

    /**
     * @brief Get item by ID
     * @param itemId Item ID to retrieve
     * @return ItemDataStruct or empty struct if not found
     */
    ItemDataStruct getItemById(int itemId) const;

    /**
     * @brief Get item by slug
     * @param slug Item slug (e.g. "gold_coin")
     * @return Pointer to ItemDataStruct or nullptr if not found
     */
    const ItemDataStruct *getItemBySlug(const std::string &slug) const;

    /**
     * @brief Get mob loot information
     * @return Map of mob ID to vector of MobLootInfoStruct
     */
    std::map<int, std::vector<MobLootInfoStruct>> getMobLootInfo() const;

    /**
     * @brief Get loot for specific mob
     * @param mobId Mob ID
     * @return Vector of MobLootInfoStruct for the mob
     */
    std::vector<MobLootInfoStruct> getLootForMob(int mobId) const;

    /**
     * @brief Content version of the item stat catalog (FNV-1a over entries).
     * 0 = not loaded yet. Clients cache the catalog per version and send it
     * back in joinGameCharacter; mismatches trigger a fresh itemCatalog push.
     */
    uint32_t getCatalogVersion() const;

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Store items in memory
    std::map<int, ItemDataStruct> items_;

    // Catalog content version, recomputed on every setItemsList.
    uint32_t catalogVersion_ = 0;

    // Recompute catalogVersion_ from items_. Caller must hold itemsMutex_.
    void recomputeCatalogVersion();

    // Store mob loot information (mobId -> vector of loot entries)
    std::map<int, std::vector<MobLootInfoStruct>> mobLootInfo_;

    // Thread safety
    mutable std::shared_mutex itemsMutex_;
    mutable std::shared_mutex lootMutex_;
};
