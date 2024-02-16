#pragma once
#include <array>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include "network/NetworkManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include "events/EventHandler.hpp"
#include "utils/Logger.hpp"


class ChunkServer {
public:
    ChunkServer(EventQueue& eventQueue, NetworkManager& networkManager, Logger& logger);
    ~ChunkServer();
    void startMainEventLoop();
    
private:
    //Events
    void mainEventLoop();

    std::thread event_thread_;
    ClientData clientData_;
    Logger& logger_;
    EventQueue& eventQueue_;
    EventHandler eventHandler_;
    NetworkManager& networkManager_;
};
