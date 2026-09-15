#pragma once

#include "data/DataStructs.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <shared_mutex>
#include <unordered_map>
#include <utils/Logger.hpp>
#include <vector>

class ClientManager
{
  public:
    // A registered socket with a generation stamp. The generation is bumped
    // on every setClientSocket() so stale async events (disconnect/cleanup
    // arriving after a reconnect) can never wipe or reuse a live session.
    // gen == 0 means "no socket registered".
    struct SocketEntry
    {
        std::shared_ptr<boost::asio::ip::tcp::socket> sock;
        uint64_t gen{0};
    };

    // Snapshot of one live registration for broadcast fan-out.
    struct SocketSnapshot
    {
        std::shared_ptr<boost::asio::ip::tcp::socket> sock;
        uint64_t gen{0};
        int clientId{0};
    };

    // Constructor
    ClientManager(Logger &logger);

    // Load clients list
    void loadClientsList(std::vector<ClientDataStruct> clientsList);

    // Load client data
    void loadClientData(ClientDataStruct clientData);

    // Set client socket. Returns the new generation stamp for this
    // registration. Overwrites any previous entry (reconnect) and keeps the
    // reverse socket->client index in sync.
    uint64_t setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // set client character ID
    void setClientCharacterId(int clientID, int characterId);

    // Mark client as world-ready (scene loaded on client side)
    void setClientWorldReady(int clientID, bool ready);

    // Check if client has sent playerReady
    bool isClientWorldReady(int clientID) const;

    // Get clients list
    std::vector<ClientDataStruct> getClientsList();

    // Get clients list without cleanup (thread-safe read-only)
    std::vector<ClientDataStruct> getClientsListReadOnly();

    // CRITICAL-8: Snapshot of all live sockets for broadcast.
    // Uses shared_lock (read-only) — zero contention with other readers.
    // Pass excludeClientId = -1 to include all sockets.
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> getActiveSockets(int excludeClientId = -1) const;

    // P0 generation-stamped snapshot: same as getActiveSockets() but carries
    // the registration generation so senders can validate before use.
    std::vector<SocketSnapshot> getActiveSnapshots(int excludeClientId = -1) const;

    // Validate that a (clientId, generation) pair is still the live
    // registration. Used by senders holding a snapshot across locks.
    bool isLiveRegistration(int clientId, uint64_t gen) const;

    // Current generation for a client (0 = no socket registered).
    uint64_t getSocketGen(int clientId) const;

    // Remove dead sockets — call from Scheduler (e.g. every 30s), NOT from hot broadcast path
    void cleanupDeadSockets();

    // Get basic client data by client ID
    ClientDataStruct getClientData(int clientID);

    // Get client data by character ID (for accountId lookups)
    ClientDataStruct getClientDataByCharacterId(int characterId);

    // Get Client Socket by client ID
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket(int clientID);

    // Get client ID by socket
    int getClientIdBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // remove client by ID. When expectedGen != 0 the entry is removed only
    // if its generation still matches (stale disconnect/cleanup events from
    // a previous generation become safe no-ops). Returns true if removed.
    bool removeClientData(int clientID, uint64_t expectedGen = 0);

    // remove client by socket
    void removeClientDataBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    // cleanup clients with invalid sockets
    void cleanupInvalidClients();

    // Force cleanup of all disconnected clients and shrink containers to prevent memory leaks
    void forceCleanupMemory();

    // Ping timeout tracking (crashed-client detection)
    void recordPingTime(int clientID);
    std::vector<int> getInactiveClientIds(int timeoutSec) const;
    void removePingTime(int clientID);

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    // clients list
    std::vector<ClientDataStruct> clientsList_;
    // socket map - tracks socket references separately from client data.
    // Each entry carries a generation stamp bumped on every setClientSocket().
    std::unordered_map<int, SocketEntry> clientSockets_;
    // Reverse index socket* -> clientId for O(1) disconnect/by-socket lookup.
    // Always updated under the same unique_lock as clientSockets_.
    std::unordered_map<boost::asio::ip::tcp::socket *, int> socketToClient_;
    // Monotonic generation source (starts at 1; 0 = unregistered).
    std::atomic<uint64_t> nextGen_{1};

    // Mutex for clients list
    mutable std::shared_mutex mutex_;

    // Ping timeout tracking: clientId → last ping time (steady_clock)
    std::unordered_map<int, std::chrono::steady_clock::time_point> clientPingTimes_;
    mutable std::mutex pingMutex_;
};