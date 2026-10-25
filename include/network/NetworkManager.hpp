#pragma once
#include "data/DataStructs.hpp"
#include "events/EventQueue.hpp"
#include "network/ClientSession.hpp"
#include "services/GameServices.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include "utils/ProtocolLimits.hpp"
#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <cstdint>
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
    bool addActiveSession(std::shared_ptr<ClientSession> session);
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
        // P0: identity of the socket object this queue belongs to. A raw
        // socket* key alone is unsafe: after free+realloc a fresh socket may
        // sit at the same address and would inherit a stale queue (stuck
        // writing=true or wiped buffers => silently dropped broadcasts).
        std::weak_ptr<boost::asio::ip::tcp::socket> owner;

        explicit SocketWriteQueue(boost::asio::io_context &ioc)
            : strand(boost::asio::make_strand(ioc))
        {
        }
    };

    /// Get or create the write queue for a socket (thread-safe via writeQueuesMutex_).
    /// Same live object => same queue (one strand per socket, always).
    /// Raw-address reuse => the mapping is REPLACED with a fresh queue; the
    /// old object is never mutated (it may still serve in-flight callbacks).
    std::shared_ptr<SocketWriteQueue> getOrCreateWriteQueue(
        const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);
    /// Legacy removal hook: now only drops owner-expired entries (see
    /// gcWriteQueues). Never erases a live mapping — that could strand a
    /// second queue on one socket (concurrent async_write = UB).
    void removeWriteQueue(boost::asio::ip::tcp::socket *key,
        const std::shared_ptr<boost::asio::ip::tcp::socket> &expectedOwner);
    /// Reclaim idle queues (owner-expired only; provably no pending work).
    /// Called periodically (see cleanupInactiveSessions).
    void gcWriteQueues();
    size_t writeQueueCount() const;
    /// Enqueue data for writing; critical=true means CRITICAL queue, false means BULK.
    void enqueueWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        std::shared_ptr<const std::string> data,
        bool critical);
    /// Kick off the next async_write; must be called on the queue's strand.
    void doNextWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        std::shared_ptr<SocketWriteQueue> queue);

    std::unordered_map<boost::asio::ip::tcp::socket *, std::shared_ptr<SocketWriteQueue>> writeQueues_;
    mutable std::mutex writeQueuesMutex_;

    static constexpr size_t max_length = protocol::kMaxMessageBytes;
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

    // Transport fragmentation (wireFrag, issues #121/#122): per-connection
    // stream ids for oversize messages. Counters feed the ADMIN wireStats op.
    std::atomic<std::uint64_t> nextFragStreamId_{1};
    std::atomic<std::uint64_t> oversizeSeen_{0};
    std::atomic<std::uint64_t> fragStreams_{0};
    std::atomic<std::uint64_t> fragFrames_{0};
    mutable std::mutex oversizeByEventMutex_;
    std::unordered_map<std::string, std::uint64_t> oversizeByEvent_;

  public:
    /// Snapshot of wire-framing counters (ADMIN wireStats).
    struct WireStats
    {
        std::uint64_t oversizeSeen = 0;
        std::uint64_t fragStreams = 0;
        std::uint64_t fragFrames = 0;
        std::unordered_map<std::string, std::uint64_t> byEvent;
    };
    WireStats wireStats() const;

  private:
    // game services
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};
