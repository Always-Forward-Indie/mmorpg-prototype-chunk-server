#include "chunk_server/ChunkServer.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "utils/Scheduler.hpp"
#include "utils/TimeConverter.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

std::atomic<bool> running(true);

void
signalHandler(int signal)
{
    running = false;
}

int
main()
{
    try
    {
        // Устанавливаем обработчик сигналов (Ctrl+C)
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Initialize Config
        Config config;
        // Initialize Logger
        Logger logger;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig("config.json");

        // Initialize EventQueue
        EventQueue eventQueueChunkServer;
        EventQueue eventQueueGameServer;
        EventQueue eventQueueChunkServerPing;

        // Initialize Scheduler
        Scheduler scheduler;

        // Initialize GameServices
        GameServices gameServices(logger);

        // Initialize GameServerWorker
        GameServerWorker gameServerWorker(eventQueueGameServer, configs, logger);

        // Initialize NetworkManager
        NetworkManager networkManager(gameServices, eventQueueChunkServer, eventQueueChunkServerPing, configs);

        // Event Handler
        EventHandler eventHandler(networkManager, gameServerWorker, gameServices);

        // Initialize GameServer
        ChunkServer chunkServer(gameServices, eventHandler, eventQueueChunkServer, eventQueueGameServer, eventQueueChunkServerPing, scheduler, gameServerWorker, networkManager);

        // Set the ChunkServer object in the NetworkManager
        networkManager.setChunkServer(&chunkServer);

        // Start accepting connections
        networkManager.startAccept();

        // Start the IO Networking event loop in the main thread
        networkManager.startIOEventLoop();

        // Start GameServerWorker IO Context in a separate thread
        gameServerWorker.startIOEventLoop();

        // Start Chunk Server main event loop in a separate thread
        chunkServer.startMainEventLoop();

        // Start Scheduler loop in a separate thread
        scheduler.start();

        // wait for the signal to stop the server
        while (running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "Shutting down gracefully..." << std::endl;

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
