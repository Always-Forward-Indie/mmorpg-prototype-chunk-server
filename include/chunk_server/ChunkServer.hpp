#pragma once

#include "events/Event.hpp"
#include "events/EventHandler.hpp"
#include "events/EventQueue.hpp"
#include "network/NetworkManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "utils/Logger.hpp"
#include "utils/Scheduler.hpp"
#include "utils/ThreadPool.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

class ChunkServer
{
  public:
    ChunkServer(GameServices &gameServices,
        EventHandler &eventHandler,
        EventQueue &eventQueueGameServer,
        EventQueue &eventQueueChunkServer,
        EventQueue &eventQueueGameServerPing,
        Scheduler &scheduler,
        GameServerWorker &gameServerWorker,
        NetworkManager &networkManager);

    ~ChunkServer();

    void processBatch(const std::vector<Event> &eventsBatch);
    void processPingBatch(const std::vector<Event> &pingEvents);

    void startMainEventLoop();
    void stop();

    void mainEventLoopGS();
    void mainEventLoopCH();
    void mainEventLoopPing();

    NetworkManager &getNetworkManager()
    {
        return networkManager_;
    }

  private:
    std::atomic<bool> running_{true};

    std::thread event_game_server_thread_;
    std::thread event_chunk_server_thread_;
    std::thread event_ping_thread_;

    EventQueue &eventQueueGameServer_;
    EventQueue &eventQueueChunkServer_;
    EventQueue &eventQueueGameServerPing_;
    EventHandler &eventHandler_;

    Scheduler &scheduler_;

    std::mutex eventMutex;
    std::condition_variable eventCondition;

    ThreadPool threadPool_{std::thread::hardware_concurrency()};

    GameServices &gameServices_;
    NetworkManager &networkManager_;

    // Helper method for sending spawn events to all clients
    void sendSpawnEventsToClients(const SpawnZoneStruct &zone);
};
