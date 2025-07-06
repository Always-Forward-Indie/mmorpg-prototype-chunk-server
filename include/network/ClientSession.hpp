#pragma once

#include "events/EventQueue.hpp"
#include "services/GameServices.hpp"
#include "utils/JSONParser.hpp"
#include "utils/Logger.hpp"
#include <array>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

// Forward declarations
class ChunkServer;
class EventDispatcher;
class MessageHandler;

class ClientSession : public std::enable_shared_from_this<ClientSession>
{
  public:
    // Accept a shared_ptr to a tcp::socket instead of a raw socket.
    ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        ChunkServer *chunkServer,
        GameServices &gameServices,
        EventQueue &eventQueue,
        EventQueue &eventQueuePing,
        JSONParser &jsonParser,
        EventDispatcher &eventDispatcher,
        MessageHandler &messageHandler);

    void start();

    // Set cleanup callback to notify NetworkManager when session ends
    void setCleanupCallback(std::function<void(std::shared_ptr<ClientSession>)> callback);

    // Check if socket is open and valid
    bool isSocketOpen() const
    {
        try
        {
            return socket_ && socket_->is_open();
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    ~ClientSession()
    {
        // Explicitly clear the accumulated data buffer to free memory
        accumulatedData_.clear();
        accumulatedData_.shrink_to_fit();
    }

  private:
    void doRead();
    void processMessage(const std::string &message);
    void handleClientDisconnect();
    void notifyCleanup(); // Called when session is ending

    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    std::array<char, 1024> dataBuffer_;
    std::string accumulatedData_;

    // Cleanup callback for NetworkManager
    std::function<void(std::shared_ptr<ClientSession>)> cleanupCallback_;

    // references
    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    ChunkServer *chunkServer_;
    JSONParser &jsonParser_;
    EventDispatcher &eventDispatcher_;
    MessageHandler &messageHandler_;
    GameServices &gameServices_;
};
