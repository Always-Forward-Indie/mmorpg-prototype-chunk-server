#include "chunk_server/ChunkServer.hpp"

ChunkServer::ChunkServer(EventQueue& eventQueue, NetworkManager& networkManager, Logger& logger)
    : networkManager_(networkManager),
      clientData_(),
      logger_(logger),
      eventQueue_(eventQueue),
      eventHandler_(networkManager)
{
    // Start accepting new connections from Game Server as Client
    networkManager_.startAccept();
}

void ChunkServer::mainEventLoop() {
    logger_.log("Starting Main Event Loop...", YELLOW);

    while (true) {
        Event event;
        if (eventQueue_.pop(event)) {
            eventHandler_.dispatchEvent(event, clientData_);
        }

        // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ChunkServer::startMainEventLoop()
{
    // Start the main event loop in a new thread
    event_thread_ = std::thread(&ChunkServer::mainEventLoop, this);
}