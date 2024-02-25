#include "chunk_server/ChunkServer.hpp"

ChunkServer::ChunkServer(EventQueue& eventQueue, NetworkManager& networkManager, Logger& logger)
    : networkManager_(networkManager),
      clientData_(),
      logger_(logger),
      eventQueue_(eventQueue),
      eventHandler_(networkManager, logger)
{
    // Start accepting new connections from Game Server as Client
    networkManager_.startAccept();
}

void ChunkServer::mainEventLoop() {
    logger_.log("Starting Main Event Loop...", YELLOW);
    constexpr int BATCH_SIZE = 10; // Process up to 10 events at a time

    while (true) {
        // Event event;
        // if (eventQueue_.pop(event)) {
        //     eventHandler_.dispatchEvent(event, clientData_);
        // }

        std::vector<Event> eventsBatch;
        if (eventQueue_.popBatch(eventsBatch, BATCH_SIZE)) {
            processBatch(eventsBatch);
        }

        // Optionally include a small delay or yield to prevent the loop from consuming too much CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Implement asynchronous processing of event batches
void ChunkServer::processBatch(const std::vector<Event>& eventsBatch) {
    // Asynchronously process events in the batch
    for (const auto& event : eventsBatch) {
        // Asynchronously process each event using std::async or std::thread
        std::async(std::launch::async | std::launch::deferred, [&]() {
            eventHandler_.dispatchEvent(event, clientData_);
        });
    }
}

void ChunkServer::startMainEventLoop()
{
    // Start the main event loop in a new thread
    event_thread_ = std::thread(&ChunkServer::mainEventLoop, this);
}

// destructor
ChunkServer::~ChunkServer()
{
    logger_.log("Shutting down Chunk server...", YELLOW);
    // Stop the main event loop
    event_thread_.join();
}