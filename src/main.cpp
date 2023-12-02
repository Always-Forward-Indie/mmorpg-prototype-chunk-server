#include <iostream>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "chunk_server/ChunkServer.hpp"
#include "network/NetworkManager.hpp"

int main() {
    try {
        // Initialize Config
        Config config;
        // Initialize Logger
        Logger logger;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("/home/shardanov/mmorpg-prototype-chunk-server/build/config.json");

        // Initialize EventQueue
        EventQueue eventQueue;
        
        // Initialize NetworkManager
        NetworkManager networkManager(eventQueue, configs, logger);
            
        // Initialize ChunkServer
        ChunkServer chunkServer(eventQueue, networkManager, logger);

        //Start the main event loop
        chunkServer.startMainEventLoop();

        //Start the IO Networking event loop
        networkManager.startIOEventLoop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}