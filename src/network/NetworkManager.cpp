#include "network/NetworkManager.hpp"
#include "events/EventDispatcher.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/TimestampUtils.hpp"
#include <netinet/tcp.h>
#include <spdlog/logger.h>

NetworkManager::NetworkManager(
    GameServices &gameServices,
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    std::tuple<GameServerConfig,
        ChunkServerConfig> &configs)
    : acceptor_(io_context_),
      configs_(configs),
      jsonParser_(),
      eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("network");
    boost::system::error_code ec;

    short customPort = std::get<1>(configs).port;
    std::string customIP = std::get<1>(configs).host;
    short maxClients = std::get<1>(configs).max_clients;

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        log_->info("Starting Chunk Server...");
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }
    if (ec)
    {
        log_->error("Error during server initialization: " + ec.message());
        return;
    }
    log_->info("Chunk Server started on IP: " + customIP + ", Port: " + std::to_string(customPort));
}

void
NetworkManager::startAccept()
{
    auto clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error)
        {
        if (!error) {
            boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
            std::string clientIP = remoteEndpoint.address().to_string();
            std::string portNumber = std::to_string(remoteEndpoint.port());

            {
                boost::system::error_code ec;
                clientSocket->set_option(boost::asio::socket_base::keep_alive(true), ec);
                if (ec)
                    log_->warn("Failed to set SO_KEEPALIVE on client socket: " + ec.message());
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
                int keepidle = 60;
                int keepintvl = 10;
                int keepcnt = 3;
                setsockopt(clientSocket->native_handle(), IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
                setsockopt(clientSocket->native_handle(), IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
                setsockopt(clientSocket->native_handle(), IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
#endif
            }

            // Create session and add to active sessions list
            auto session = std::make_shared<ClientSession>(clientSocket, chunkServer_, gameServices_, eventQueue_, eventQueuePing_, jsonParser_, *eventDispatcher_, *messageHandler_);

            // Setup cleanup callback safely
            setupSessionCallback(session);

            // Start the session only if it was accepted (not rejected due to max_clients limit)
            if (addActiveSession(session)) {
                log_->info("New Client with IP: " + clientIP + " Port: " + portNumber + " - connected!");
                session->start();
            } else {
                log_->warn("Rejected client connection from " + clientIP + ":" + portNumber +
                           " (max_clients limit reached)");
                boost::system::error_code ec;
                clientSocket->close(ec);
            }
        }
        else{
            log_->warn("Accept client connection error: " + error.message());
        }
        startAccept(); });
}

void
NetworkManager::startIOEventLoop()
{
    log_->info("Starting Chunk Server IO Context...");
    auto numThreads = std::thread::hardware_concurrency();
    for (size_t i = 0; i < numThreads; ++i)
    {
        threadPool_.emplace_back([this]()
            { 
                try {
                    io_context_.run();
                } catch (const std::exception& e) {
                    gameServices_.getLogger().logError("IO Context error: " + std::string(e.what()));
                } });
    }
}

NetworkManager::~NetworkManager()
{
    log_->warn("Network Manager destructor is called...");

    // Clean up all active sessions before shutdown
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        gameServices_.getLogger().log("Cleaning up " + std::to_string(activeSessions_.size()) + " active sessions...", YELLOW);
        activeSessions_.clear();
    }

    // Close the acceptor and stop the IO context
    acceptor_.close();
    io_context_.stop();

    for (auto &thread : threadPool_)
    {
        if (thread.joinable())
            thread.join();
    }

    log_->info("Network Manager cleanup completed.");
}

void
NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    if (!clientSocket)
    {
        log_->error("Attempted write on null socket.");
        return;
    }

    auto dataPtr = std::make_shared<const std::string>(responseString);
    enqueueWrite(std::move(clientSocket), std::move(dataPtr), true /* critical */);
}

// CRITICAL-8: shared_ptr overload — buffer stays alive until async_write completes.
// Used by broadcastToAllClients: one string allocation for N clients.
void
NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    std::shared_ptr<const std::string> data)
{
    if (!clientSocket || !data)
        return;

    enqueueWrite(std::move(clientSocket), std::move(data), true /* critical */);
}

void
NetworkManager::sendResponseBulk(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
    std::shared_ptr<const std::string> data)
{
    if (!clientSocket || !data)
        return;

    enqueueWrite(std::move(clientSocket), std::move(data), false /* bulk */);
}

std::shared_ptr<NetworkManager::SocketWriteQueue>
NetworkManager::getOrCreateWriteQueue(boost::asio::ip::tcp::socket *key)
{
    std::lock_guard<std::mutex> lock(writeQueuesMutex_);
    auto it = writeQueues_.find(key);
    if (it != writeQueues_.end())
        return it->second;
    auto q = std::make_shared<SocketWriteQueue>(io_context_);
    writeQueues_.emplace(key, q);
    return q;
}

void
NetworkManager::removeWriteQueue(boost::asio::ip::tcp::socket *key)
{
    std::lock_guard<std::mutex> lock(writeQueuesMutex_);
    writeQueues_.erase(key);
}

void
NetworkManager::enqueueWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    std::shared_ptr<const std::string> data,
    bool critical)
{
    auto queue = getOrCreateWriteQueue(socket.get());
    boost::asio::post(queue->strand,
        [this, socket = std::move(socket), queue, data = std::move(data), critical]() mutable
        {
            if (critical)
                queue->criticalQ.push_back(std::move(data));
            else
                queue->bulkQ.push_back(std::move(data));
            if (!queue->writing)
                doNextWrite(std::move(socket), std::move(queue));
        });
}

void
NetworkManager::doNextWrite(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    std::shared_ptr<SocketWriteQueue> queue)
{
    // Must be called on queue->strand.
    std::shared_ptr<const std::string> data;
    if (!queue->criticalQ.empty())
    {
        data = std::move(queue->criticalQ.front());
        queue->criticalQ.pop_front();
    }
    else if (!queue->bulkQ.empty())
    {
        data = std::move(queue->bulkQ.front());
        queue->bulkQ.pop_front();
    }
    else
    {
        queue->writing = false;
        return;
    }

    if (!socket || !socket->is_open())
    {
        queue->writing = false;
        queue->criticalQ.clear();
        queue->bulkQ.clear();
        return;
    }

    queue->writing = true;
    boost::asio::async_write(*socket,
        boost::asio::buffer(*data),
        boost::asio::bind_executor(queue->strand,
            [this, socket, queue, data](const boost::system::error_code &error, size_t bytes_transferred) mutable
            {
                if (error)
                {
                    log_->error("Priority write error: " + error.message());
                    queue->writing = false;
                    queue->criticalQ.clear();
                    queue->bulkQ.clear();
                    if (socket->is_open())
                    {
                        boost::system::error_code ec;
                        socket->close(ec);
                    }
                }
                else
                {
                    log_->debug("Bytes sent: " + std::to_string(bytes_transferred));
                    doNextWrite(std::move(socket), std::move(queue));
                }
            }));
}

std::string
NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message)
{
    nlohmann::json response;
    std::string currentTimestamp = TimestampUtils::getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = "1.0";
    response["body"] = message["body"];

    std::string responseString = response.dump();
    log_->info("Response generated: " + responseString);
    return responseString + "\n";
}

std::string
NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message, const TimestampStruct &timestamps)
{
    nlohmann::json response;
    std::string currentTimestamp = TimestampUtils::getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = "1.0";

    // Add lag compensation timestamps to header
    TimestampStruct finalTimestamps = timestamps;
    TimestampUtils::setServerSendTimestamp(finalTimestamps); // Set serverSendMs to current time
    TimestampUtils::addTimestampsToHeader(response, finalTimestamps);

    response["body"] = message["body"];

    std::string responseString = response.dump();
    log_->info("Response with timestamps generated: " + responseString);
    return responseString + "\n";
}

void
NetworkManager::setChunkServer(ChunkServer *chunkServer)
{
    if (!chunkServer)
    {
        throw std::runtime_error("Invalid ChunkServer pointer in NetworkManager!");
    }
    chunkServer_ = chunkServer;

    eventDispatcher_ = std::make_unique<EventDispatcher>(eventQueue_, eventQueuePing_, chunkServer_, gameServices_);
    messageHandler_ = std::make_unique<MessageHandler>(jsonParser_);
}

// Session management methods to prevent memory leaks
bool
NetworkManager::addActiveSession(std::shared_ptr<ClientSession> session)
{
    std::lock_guard<std::mutex> lock(sessionsMutex_);

    // Prevent DoS attacks by limiting maximum concurrent sessions
    size_t maxSessions = static_cast<size_t>(std::get<1>(configs_).max_clients);
    if (activeSessions_.size() >= maxSessions)
    {
        gameServices_.getLogger().logError("Maximum concurrent sessions reached (" +
                                               std::to_string(maxSessions) + "), rejecting new connection",
            RED);
        return false; // Don't add the session
    }

    activeSessions_.insert(session);
    gameServices_.getLogger().log("Added session to active sessions. Total active: " + std::to_string(activeSessions_.size()), GREEN);
    return true;
}

void
NetworkManager::removeActiveSession(std::shared_ptr<ClientSession> session)
{
    // Remove the per-socket write queue to free memory and stop future enqueues.
    if (session)
        removeWriteQueue(session->getSocket().get());

    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.erase(session);
    gameServices_.getLogger().log("Removed session from active sessions. Total active: " + std::to_string(activeSessions_.size()), GREEN);
}
void
NetworkManager::cleanupInactiveSessions()
{
    std::lock_guard<std::mutex> lock(sessionsMutex_);

    size_t initialSize = activeSessions_.size();

    // Remove expired sessions: sole reliable indicator is the socket being closed.
    // use_count() was intentionally removed — reference-count snapshots have an
    // inherent TOCTOU race on multi-threaded io_context threadpools and could
    // falsely evict sessions that are temporarily not holding a shared_ptr copy.
    for (auto it = activeSessions_.begin(); it != activeSessions_.end();)
    {
        bool shouldRemove = false;

        if (!(*it)->isSocketOpen())
        {
            shouldRemove = true;
        }

        if (shouldRemove)
        {
            log_->info("Cleaning up inactive session");
            it = activeSessions_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    size_t finalSize = activeSessions_.size();
    if (initialSize > finalSize)
    {
        gameServices_.getLogger().log("Session cleanup: removed " + std::to_string(initialSize - finalSize) +
                                          " inactive sessions. Active: " + std::to_string(finalSize),
            GREEN);
    }
}

void
NetworkManager::setupSessionCallback(std::shared_ptr<ClientSession> session)
{
    // Use raw pointer for cleanup callback to avoid circular references
    // This is safe because session lifetime is shorter than NetworkManager
    session->setCleanupCallback([this](std::shared_ptr<ClientSession> s)
        { removeActiveSession(s); });
}