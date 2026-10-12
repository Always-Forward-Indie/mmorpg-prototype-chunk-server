#pragma once

#include "events/EventQueue.hpp"
#include "network/NetworkManager.hpp"
#include "services/FactOutbox.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include "utils/Logger.hpp"
#include <array>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class GameServerWorker
{
  private:
    boost::asio::io_context io_context_game_server_;
    /// CRITICAL-2: strand serialises all send operations so async_write is
    /// never called concurrently on the same socket from different threads.
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::shared_ptr<boost::asio::ip::tcp::socket> game_server_socket_;
    boost::asio::steady_timer retry_timer_;
    // Registration heartbeat: re-asserts the chunkServerConnection handshake
    // every 60s so the game never permanently loses the chunk registration
    // (stale-disconnect races, missed events). Idempotent server-side.
    boost::asio::steady_timer heartbeat_timer_;
    // Outbox flush: resends unacked facts every 5s (strand-bound).
    boost::asio::steady_timer flush_timer_;
    void scheduleFlush();
    /// Chunk→game fact outbox (at-least-once + idempotent receivers).
    /// Strand-confined: touched only on strand_ (or single-threaded tests).
    FactOutbox outbox_;
    void scheduleHeartbeat();
    std::string buildHandshakeMessage() const;
    std::vector<std::thread> io_threads_;
    EventQueue &eventQueue_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    int retryCount = 0;
    static constexpr int RETRY_TIMEOUT = 5;
    // Infinite-reconnect ceiling: backoff never exceeds this (was: exit(1)
    // after MAX_RETRY_COUNT — a game outage wiped the in-memory world).
    static constexpr int MAX_RETRY_WAIT_SEC = 60;
    JSONParser jsonParser_;
    GameServerConfig &gameServerConfig_;
    ChunkServerConfig &chunkServerConfig_;
    std::string receiveBuffer_; // буфер для накопления данных
    /// CRITICAL-2: serialised send queue — only accessed via strand_
    std::queue<std::string> sendQueue_;
    bool writePending_{false};
    /// LOW-8: stored so receiveDataFromGameServer can reconnect on disconnect
    boost::asio::ip::tcp::resolver::results_type endpoints_;

    /// Called after successful connection handshake to restore online status
    std::function<void()> onReconnect_;

    // Process received data from the Game Server
    void processGameServerData(std::string_view data);
    /// Dequeue and async_write the next pending message; must run on strand_.
    void doNextWrite();
    /// Reconcile sender (strand context): report unacked keys to game after
    /// a new-link setChunkData ack carried outboxGlobalSeq. Bounded batches.
    void sendOutboxUnacked();

  public:
    GameServerWorker(EventQueue &eventQueue,
        std::tuple<GameServerConfig, ChunkServerConfig> &configs,
        Logger &logger);
    ~GameServerWorker();
    void startIOEventLoop();
    void sendDataToGameServer(const std::string &data);
    /// Send an idempotent fact and wait (bounded) for the game factAck.
    /// Returns "applied", "duplicate", or "timeout". Blocks the caller —
    /// only for rare synchronous admin ops (ban/unban), never hot paths.
    std::string sendFactAndWait(const std::string &eventType,
        const nlohmann::json &body,
        int timeoutMs = 8000);
    /// Lock-free outbox snapshot for the periodic status task.
    OutboxSnapshot outboxSnapshot() const { return outbox_.snapshot(); }
    void receiveDataFromGameServer();
    void connect(boost::asio::ip::tcp::resolver::results_type endpoints, int currentRetryCount = 0);
    void closeConnection();

    // Callback invoked after connection (or reconnection) handshake completes.
    // ChunkServer uses this to restore online status for loaded characters.
    void setOnReconnectCallback(std::function<void()> callback) { onReconnect_ = std::move(callback); }
};
