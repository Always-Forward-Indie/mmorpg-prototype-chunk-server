#pragma once
#include "data/DataStructs.hpp"
#include "events/EventQueue.hpp"
#include "network/ClientSession.hpp"
#include "services/GameServices.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include <array>
#include <boost/asio.hpp>
#include <deque>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations
class ChunkServer;
class EventDispatcher;
class MessageHandler;

class NetworkManager
{
  public:
    NetworkManager(GameServices &gameServices, EventQueue &eventQueue, EventQueue &eventQueuePing, std::tuple<GameServerConfig, ChunkServerConfig> &configs);
    ~NetworkManager();
    void startAccept();
    void startIOEventLoop();
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString);
    /// CRITICAL-8: shared_ptr overload for broadcast — one allocation for N clients, zero copies
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, std::shared_ptr<const std::string> data);
    /// Bulk priority send — mob position updates go here so combat packets always reach clients first
    void sendResponseBulk(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, std::shared_ptr<const std::string> data);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message, const TimestampStruct &timestamps);
    void setChunkServer(ChunkServer *ChunkServer);

    // Session management methods to prevent memory leaks
    void addActiveSession(std::shared_ptr<ClientSession> session);
    void removeActiveSession(std::shared_ptr<ClientSession> session);
    void cleanupInactiveSessions();

    // Helper method to setup session callback safely
    void setupSessionCallback(std::shared_ptr<ClientSession> session);

  private:
    // Per-socket priority write queue — serialises all writes to a socket and
    // ensures that CRITICAL packets (combat, stats) are always delivered before
    // BULK packets (mob position updates), even when bulk data is already queued.
    struct SocketWriteQueue
    {
        boost::asio::strand<boost::asio::io_context::executor_type> strand;
        std::deque<std::shared_ptr<const std::string>> criticalQ;
        std::deque<std::shared_ptr<const std::string>> bulkQ;
        bool writing{false};

        explicit SocketWriteQueue(boost::asio::io_context &ioc)
            : strand(boost::asio::make_strand(ioc))
        {
        }
    };

    /// Get or create the write queue for a socket (thread-safe via writeQueuesMutex_).
    std::shared_ptr<SocketWriteQueue> getOrCreateWriteQueue(boost::asio::ip::tcp::socket *key);
    /// Remove write queue on disconnect to free memory.
    void removeWriteQueue(boost::asio::ip::tcp::socket *key);
    /// Enqueue data for writing; critical=true means CRITICAL queue, false means BULK.
    void enqueueWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        std::shared_ptr<const std::string> data,
        bool critical);
    /// Kick off the next async_write; must be called on the queue's strand.
    void doNextWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        std::shared_ptr<SocketWriteQueue> queue);

    std::unordered_map<boost::asio::ip::tcp::socket *, std::shared_ptr<SocketWriteQueue>> writeQueues_;
    std::mutex writeQueuesMutex_;

    static constexpr size_t max_length = 1024;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> threadPool_;
    std::tuple<GameServerConfig, ChunkServerConfig> &configs_;
    ChunkServer *chunkServer_;
    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    JSONParser jsonParser_;

    // These are declared but NOT initialized here!
    std::unique_ptr<EventDispatcher> eventDispatcher_;
    std::unique_ptr<MessageHandler> messageHandler_;

    // Active sessions tracking to prevent memory leaks
    std::unordered_set<std::shared_ptr<ClientSession>> activeSessions_;
    std::mutex sessionsMutex_;

    // game services
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};
