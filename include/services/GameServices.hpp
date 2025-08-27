#pragma once
#include "services/CharacterManager.hpp"
#include "services/ChunkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/HarvestManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/LootManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"
#include "services/SkillManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Logger.hpp"

class GameServices
{
  public:
    // Constructor
    // Initializes all managers with the logger
    GameServices(Logger &logger)
        : logger_(logger),
          mobManager_(logger_),
          itemManager_(logger_),
          mobInstanceManager_(logger_),
          mobMovementManager_(logger_),
          spawnZoneManager_(mobManager_, logger_),
          characterManager_(logger_),
          clientManager_(logger_),
          chunkManager_(logger_),
          lootManager_(itemManager_, logger_),
          inventoryManager_(itemManager_, logger_),
          harvestManager_(itemManager_, logger_),
          skillManager_(this)
    {
        // Set up manager dependencies
        spawnZoneManager_.setMobInstanceManager(&mobInstanceManager_);
        mobMovementManager_.setMobInstanceManager(&mobInstanceManager_);
        mobMovementManager_.setSpawnZoneManager(&spawnZoneManager_);
        mobMovementManager_.setCharacterManager(&characterManager_);

        // Set up harvest manager dependencies
        harvestManager_.setInventoryManager(&inventoryManager_);
    }

    Logger &getLogger()
    {
        return logger_;
    }
    MobManager &getMobManager()
    {
        return mobManager_;
    }
    ItemManager &getItemManager()
    {
        return itemManager_;
    }
    MobInstanceManager &getMobInstanceManager()
    {
        return mobInstanceManager_;
    }
    MobMovementManager &getMobMovementManager()
    {
        return mobMovementManager_;
    }
    SpawnZoneManager &getSpawnZoneManager()
    {
        return spawnZoneManager_;
    }
    CharacterManager &getCharacterManager()
    {
        return characterManager_;
    }
    ClientManager &getClientManager()
    {
        return clientManager_;
    }
    ChunkManager &getChunkManager()
    {
        return chunkManager_;
    }
    LootManager &getLootManager()
    {
        return lootManager_;
    }
    InventoryManager &getInventoryManager()
    {
        return inventoryManager_;
    }
    HarvestManager &getHarvestManager()
    {
        return harvestManager_;
    }
    SkillManager &getSkillManager()
    {
        return skillManager_;
    }

  private:
    Logger &logger_;
    MobManager mobManager_;
    ItemManager itemManager_;
    MobInstanceManager mobInstanceManager_;
    MobMovementManager mobMovementManager_;
    SpawnZoneManager spawnZoneManager_;
    CharacterManager characterManager_;
    ClientManager clientManager_;
    ChunkManager chunkManager_;
    LootManager lootManager_;
    InventoryManager inventoryManager_;
    HarvestManager harvestManager_;
    SkillManager skillManager_;
};
