#pragma once

#include <array>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include "network/NetworkManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "events/EventHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/Scheduler.hpp"
#include "utils/ThreadPool.hpp"
#include "services/CharacterManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"


class ChunkServer {
public:
ChunkServer(GameServices &gameServices,
                EventHandler &eventHandler,
                EventQueue& eventQueueGameServer, 
                EventQueue& eventQueueChunkServer, 
                EventQueue& eventQueueGameServerPing,
                Scheduler& scheduler,
                GameServerWorker& gameServerWorker
            );
    
    ~ChunkServer();

    void processBatch(const std::vector<Event> &eventsBatch);
    void processPingBatch(const std::vector<Event>& pingEvents);

    void startMainEventLoop();
    void stop();

    void mainEventLoopGS();
    void mainEventLoopCH();
    void mainEventLoopPing();


private:
    std::atomic<bool> running_{true};

    std::thread event_game_server_thread_;
    std::thread event_chunk_server_thread_;
    std::thread event_ping_thread_;

    EventQueue& eventQueueGameServer_;
    EventQueue& eventQueueChunkServer_;
    EventQueue& eventQueueGameServerPing_;
    EventHandler& eventHandler_;
    
    Scheduler& scheduler_;

    std::mutex eventMutex;
    std::condition_variable eventCondition;

    ThreadPool threadPool_{std::thread::hardware_concurrency()};

    GameServices& gameServices_;
};
