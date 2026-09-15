#include "services/ClientManager.hpp"
#include <spdlog/logger.h>

ClientManager::ClientManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("client");
}

void
ClientManager::loadClientsList(std::vector<ClientDataStruct> clientsList)
{
    try
    {
        if (clientsList.empty())
        {
            log_->error("No clients found in the GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : clientsList)
        {
            clientsList_.push_back(row);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading clients: " + std::string(e.what()));
    }
}

void
ClientManager::loadClientData(ClientDataStruct clientData)
{
    try
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        // Check if client already exists to avoid duplicates
        for (auto &existingClient : clientsList_)
        {
            if (existingClient.clientId == clientData.clientId)
            {
                // Update existing client data, preserve characterId if already set
                if (existingClient.characterId == 0 && clientData.characterId != 0)
                {
                    existingClient.characterId = clientData.characterId;
                }
                existingClient.hash = clientData.hash;
                log_->info("Updated existing client ID: " + std::to_string(clientData.clientId));
                return;
            }
        }

        // Client doesn't exist, add new one
        clientsList_.push_back(clientData);
        log_->info("Added new client ID: " + std::to_string(clientData.clientId));
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading client data: " + std::string(e.what()));
    }
}

std::vector<ClientDataStruct>
ClientManager::getClientsList()
{
    // Read-only: never mutate here. Dead-socket cleanup lives exclusively in
    // cleanupDeadSockets() (scheduler). Purging inside this getter previously
    // erased LIVE sessions when is_open() raced with reconnects, which broke
    // gameplay packets (silent characterId 0) for healthy clients.
    std::shared_lock<std::shared_mutex> lock(mutex_);

    // Return a copy of the client list (without socket references)
    return clientsList_;
}

std::vector<ClientDataStruct>
ClientManager::getClientsListReadOnly()
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return clientsList_; // Thread-safe read-only copy
}

ClientDataStruct
ClientManager::getClientData(int clientID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            return client;
        }
    }
    return ClientDataStruct();
}

ClientDataStruct
ClientManager::getClientDataByCharacterId(int characterId)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &client : clientsList_)
    {
        if (client.characterId == characterId)
            return client;
    }
    return ClientDataStruct();
}

std::shared_ptr<boost::asio::ip::tcp::socket>
ClientManager::getClientSocket(int clientID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = clientSockets_.find(clientID);
    if (it != clientSockets_.end())
    {
        return it->second.sock;
    }
    return nullptr;
}

int
ClientManager::getClientIdBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!socket)
        return 0;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = socketToClient_.find(socket.get());
    if (it != socketToClient_.end())
    {
        // Confirm the forward entry still points at this exact socket object
        // (guards against raw-pointer address reuse after free).
        auto fwd = clientSockets_.find(it->second);
        if (fwd != clientSockets_.end() && fwd->second.sock.get() == socket.get())
            return it->second;
    }
    return 0; // Return 0 if socket not found (0 means invalid client ID)
}

uint64_t
ClientManager::setClientSocket(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    // Drop the reverse index of any previous socket for this client first so
    // a stale disconnect for the old socket can never resolve to the new one.
    auto prev = clientSockets_.find(clientID);
    if (prev != clientSockets_.end() && prev->second.sock &&
        (!socket || prev->second.sock.get() != socket.get()))
    {
        socketToClient_.erase(prev->second.sock.get());
    }
    const uint64_t gen = nextGen_.fetch_add(1, std::memory_order_relaxed);
    clientSockets_[clientID] = SocketEntry{socket, gen};
    if (socket)
        socketToClient_[socket.get()] = clientID;
    return gen;
}

// Set client character ID
void
ClientManager::setClientCharacterId(int clientID, int characterId)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            client.characterId = characterId;
            logger_.log("Set character ID " + std::to_string(characterId) + " for client ID: " + std::to_string(clientID));
            return;
        }
    }

    // Client not found, create a minimal client entry
    logger_.log("Client ID " + std::to_string(clientID) + " not found, creating minimal client entry for character ID: " + std::to_string(characterId));
    ClientDataStruct newClient;
    newClient.clientId = clientID;
    newClient.accountId = clientID; // clientId == accountId in this system
    newClient.characterId = characterId;
    clientsList_.push_back(newClient);
    logger_.log("Created and set character ID " + std::to_string(characterId) + " for new client ID: " + std::to_string(clientID));
}

void
ClientManager::setClientWorldReady(int clientID, bool ready)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &client : clientsList_)
    {
        if (client.clientId == clientID)
        {
            client.isWorldReady = ready;
            return;
        }
    }
}

bool
ClientManager::isClientWorldReady(int clientID) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &client : clientsList_)
    {
        if (client.clientId == clientID)
            return client.isWorldReady;
    }
    return false;
}

// remove client by ID
bool
ClientManager::removeClientData(int clientID, uint64_t expectedGen)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (expectedGen != 0)
    {
        auto entry = clientSockets_.find(clientID);
        if (entry == clientSockets_.end() || entry->second.gen != expectedGen)
        {
            // Stale removal for a previous generation — the client has
            // reconnected since; never wipe the live session.
            return false;
        }
    }

    // Remove from client list
    for (auto it = clientsList_.begin(); it != clientsList_.end(); ++it)
    {
        if (it->clientId == clientID)
        {
            clientsList_.erase(it);
            break;
        }
    }

    // Remove from socket map + reverse index
    auto entry = clientSockets_.find(clientID);
    if (entry != clientSockets_.end())
    {
        if (entry->second.sock)
            socketToClient_.erase(entry->second.sock.get());
        clientSockets_.erase(entry);
    }
    return true;
}

void
ClientManager::removeClientDataBySocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (!socket)
        return;
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Resolve via the reverse index; confirm the forward entry still points
    // at this exact socket object (address reuse after free must not remove
    // a fresh registration that happens to sit at the same address).
    int clientIDToRemove = -1;
    auto rev = socketToClient_.find(socket.get());
    if (rev != socketToClient_.end())
    {
        auto fwd = clientSockets_.find(rev->second);
        if (fwd != clientSockets_.end() && fwd->second.sock.get() == socket.get())
            clientIDToRemove = rev->second;
        else
            socketToClient_.erase(rev); // dangling reverse entry, drop it
    }

    // Remove both from client list and socket map
    if (clientIDToRemove != -1)
    {
        for (auto it = clientsList_.begin(); it != clientsList_.end(); ++it)
        {
            if (it->clientId == clientIDToRemove)
            {
                clientsList_.erase(it);
                break;
            }
        }
        clientSockets_.erase(clientIDToRemove);
        socketToClient_.erase(socket.get());
    }
}

void
ClientManager::cleanupInvalidClients()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = clientsList_.begin(); it != clientsList_.end();)
    {
        bool shouldRemove = false;
        auto socketIt = clientSockets_.find(it->clientId);

        if (socketIt == clientSockets_.end() || !socketIt->second.sock)
        {
            shouldRemove = true;
        }
        else
        {
            try
            {
                if (!socketIt->second.sock->is_open())
                {
                    shouldRemove = true;
                }
            }
            catch (const std::exception &e)
            {
                // Socket is likely invalid/freed
                shouldRemove = true;
            }
        }

        if (shouldRemove)
        {
            log_->info("Removing client with invalid socket, ID: " + std::to_string(it->clientId));
            // Remove from both the list and socket map + reverse index
            if (socketIt != clientSockets_.end())
            {
                if (socketIt->second.sock)
                    socketToClient_.erase(socketIt->second.sock.get());
                clientSockets_.erase(socketIt);
            }
            it = clientsList_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// Force cleanup of all disconnected clients and shrink containers
void
ClientManager::forceCleanupMemory()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    size_t initialListSize = clientsList_.size();
    size_t initialSocketsSize = clientSockets_.size();

    // Remove all clients with invalid or closed sockets
    for (auto it = clientsList_.begin(); it != clientsList_.end();)
    {
        bool shouldRemove = false;
        auto socketIt = clientSockets_.find(it->clientId);

        if (socketIt == clientSockets_.end() || !socketIt->second.sock)
        {
            shouldRemove = true;
        }
        else
        {
            try
            {
                if (!socketIt->second.sock->is_open())
                {
                    shouldRemove = true;
                }
            }
            catch (const std::exception &e)
            {
                shouldRemove = true;
            }
        }

        if (shouldRemove)
        {
            log_->info("Force cleanup removing client ID: " + std::to_string(it->clientId));
            if (socketIt != clientSockets_.end())
            {
                if (socketIt->second.sock)
                    socketToClient_.erase(socketIt->second.sock.get());
                clientSockets_.erase(socketIt);
            }
            it = clientsList_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Shrink containers if they've grown too large
    if (clientsList_.capacity() > clientsList_.size() * 2 && clientsList_.capacity() > 100)
    {
        clientsList_.shrink_to_fit();
        log_->info("Shrunk clientsList capacity to reduce memory usage");
    }

    size_t finalListSize = clientsList_.size();
    size_t finalSocketsSize = clientSockets_.size();

    if (initialListSize > finalListSize || initialSocketsSize > finalSocketsSize)
    {
        logger_.log("Memory cleanup: removed " + std::to_string(initialListSize - finalListSize) +
                    " clients and " + std::to_string(initialSocketsSize - finalSocketsSize) + " sockets");
    }
}

// CRITICAL-8 fix: snapshot of live sockets under shared_lock (read-only).
// Callers (broadcast) take ONE lock instead of N individual getClientSocket() calls.
std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>>
ClientManager::getActiveSockets(int excludeClientId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>> result;
    result.reserve(clientSockets_.size());
    for (const auto &[id, entry] : clientSockets_)
    {
        if (id != excludeClientId && entry.sock)
        {
            result.push_back(entry.sock);
        }
    }
    return result;
}

// P0: generation-stamped snapshot so senders can validate registrations.
std::vector<ClientManager::SocketSnapshot>
ClientManager::getActiveSnapshots(int excludeClientId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<SocketSnapshot> result;
    result.reserve(clientSockets_.size());
    for (const auto &[id, entry] : clientSockets_)
    {
        if (id != excludeClientId && entry.sock)
        {
            result.push_back(SocketSnapshot{entry.sock, entry.gen, id});
        }
    }
    return result;
}

bool
ClientManager::isLiveRegistration(int clientId, uint64_t gen) const
{
    if (gen == 0)
        return false;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = clientSockets_.find(clientId);
    return it != clientSockets_.end() && it->second.gen == gen && it->second.sock;
}

uint64_t
ClientManager::getSocketGen(int clientId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = clientSockets_.find(clientId);
    return it != clientSockets_.end() ? it->second.gen : 0;
}

// Separated dead-socket cleanup from the hot broadcast path.
// Called by Scheduler every 30 seconds, NOT from broadcastToAllClients.
void
ClientManager::cleanupDeadSockets()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (auto it = clientSockets_.begin(); it != clientSockets_.end();)
    {
        bool dead = false;
        if (!it->second.sock)
        {
            dead = true;
        }
        else
        {
            try
            {
                dead = !it->second.sock->is_open();
            }
            catch (...)
            {
                dead = true;
            }
        }

        if (dead)
        {
            log_->info("[ClientManager] cleanupDeadSockets: removing dead socket for client " +
                       std::to_string(it->first));
            // Also remove from clientsList_ to keep both containers consistent
            for (auto jt = clientsList_.begin(); jt != clientsList_.end(); ++jt)
            {
                if (jt->clientId == it->first)
                {
                    clientsList_.erase(jt);
                    break;
                }
            }
            if (it->second.sock)
                socketToClient_.erase(it->second.sock.get());
            it = clientSockets_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
ClientManager::recordPingTime(int clientID)
{
    std::lock_guard<std::mutex> lock(pingMutex_);
    clientPingTimes_[clientID] = std::chrono::steady_clock::now();
}

void
ClientManager::removePingTime(int clientID)
{
    std::lock_guard<std::mutex> lock(pingMutex_);
    clientPingTimes_.erase(clientID);
}

std::vector<int>
ClientManager::getInactiveClientIds(int timeoutSec) const
{
    std::lock_guard<std::mutex> lock(pingMutex_);
    auto now = std::chrono::steady_clock::now();
    std::vector<int> inactive;
    for (const auto &[clientId, lastPing] : clientPingTimes_)
    {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count() >= timeoutSec)
            inactive.push_back(clientId);
    }
    return inactive;
}
