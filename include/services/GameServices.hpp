#pragma once
#include "utils/Logger.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/ClientManager.hpp"
#include "services/ChunkManager.hpp"

class GameServices {
public:
    // Constructor
    // Initializes all managers with the logger
    GameServices(Logger &logger)
        : logger_(logger),
            mobManager_(logger_),
            spawnZoneManager_(mobManager_, logger_),
            characterManager_(logger_),
            clientManager_(logger_),
            chunkManager_(logger_)
    {}

    Logger& getLogger() { return logger_; }
    MobManager& getMobManager() { return mobManager_; }
    SpawnZoneManager& getSpawnZoneManager() { return spawnZoneManager_; }
    CharacterManager& getCharacterManager() { return characterManager_; }
    ClientManager& getClientManager() { return clientManager_; }
    ChunkManager& getChunkManager() { return chunkManager_; }

private:
    Logger &logger_;
    MobManager mobManager_;
    SpawnZoneManager spawnZoneManager_;
    CharacterManager characterManager_;
    ClientManager clientManager_;
    ChunkManager chunkManager_;
};
