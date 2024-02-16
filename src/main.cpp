#include <iostream>
#include <csignal>
#include <atomic>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "chunk_server/ChunkServer.hpp"
#include "network/NetworkManager.hpp"

std::atomic<bool> running(true);

void signalHandler(int signal) {
    running = false;
}


int main() {
    try {
        // Initialize Config
        Config config;
        // Initialize Logger
        Logger logger;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("config.json");

        // Initialize EventQueue
        EventQueue eventQueue;
        
        // Initialize NetworkManager
        NetworkManager networkManager(eventQueue, configs, logger);
            
        // Initialize ChunkServer
        ChunkServer chunkServer(eventQueue, networkManager, logger);

        //Start the IO Networking event loop
        networkManager.startIOEventLoop();

        //Start the main event loop
        chunkServer.startMainEventLoop();

        //TODO fix issue where the server does not stop on Ctrl+C
        // Temporary commented out the signal handler
        // Register signal handler for graceful shutdown
        // signal(SIGINT, signalHandler);

        // while (running) {
        //     std::this_thread::sleep_for(std::chrono::seconds(1));
        // }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}