#include "helpers/Config.hpp"
#include <iostream>
#include "chunk_server/ChunkServer.hpp"
#include "chunk_server/GameServerWorker.hpp"
#include "helpers/Logger.hpp"

int main() {
    try {
        // Initialize Config
        Config config;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("/home/shardanov/mmorpg-prototype-chunk-server/build/config.json");
        
        // Initialize Logger
        Logger logger;

        // Initialize GameServerWorker
        GameServerWorker gameServerWorker(configs, logger);

        // Start GameServerWorker IO Context in a separate thread
        gameServerWorker.startIOEventLoop(); 

        // Initialize ChunkServer
        ChunkServer chunkServer(gameServerWorker, configs, logger);

        //Start ChunkServer IO Context as the main thread
        chunkServer.startIOEventLoop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}