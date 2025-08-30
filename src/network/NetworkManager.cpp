#include "network/NetworkManager.hpp"
#include "events/EventDispatcher.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/TimestampUtils.hpp"

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
    boost::system::error_code ec;

    short customPort = std::get<1>(configs).port;
    std::string customIP = std::get<1>(configs).host;
    short maxClients = std::get<1>(configs).max_clients;

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        gameServices_.getLogger().log("Starting Chunk Server...", YELLOW);
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }
    if (ec)
    {
        gameServices_.getLogger().logError("Error during server initialization: " + ec.message(), RED);
        return;
    }
    gameServices_.getLogger().log("Chunk Server started on IP: " + customIP + ", Port: " + std::to_string(customPort), GREEN);
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
            gameServices_.getLogger().log("New Client with IP: " + clientIP + " Port: " + portNumber + " - connected!", GREEN);
            
            // Create session and add to active sessions list
            auto session = std::make_shared<ClientSession>(clientSocket, chunkServer_, gameServices_, eventQueue_, eventQueuePing_, jsonParser_, *eventDispatcher_, *messageHandler_);
            
            // Setup cleanup callback safely
            setupSessionCallback(session);
            
            // Start the session and add it to active sessions
            addActiveSession(session);
            session->start();
        }
        else{
            gameServices_.getLogger().log("Accept client connection error: " + error.message(), RED);
        }
        startAccept(); });
}

void
NetworkManager::startIOEventLoop()
{
    gameServices_.getLogger().log("Starting Chunk Server IO Context...", YELLOW);
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
    gameServices_.getLogger().log("Network Manager destructor is called...", RED);

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

    gameServices_.getLogger().log("Network Manager cleanup completed.", GREEN);
}

void
NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    if (!clientSocket)
    {
        gameServices_.getLogger().logError("Attempted write on null socket.", RED);
        return;
    }

    try
    {
        if (!clientSocket->is_open())
        {
            gameServices_.getLogger().logError("Attempted write on closed socket.", RED);
            return;
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception while checking socket state: " + std::string(e.what()), RED);
        return;
    }

    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString), [this, clientSocket](const boost::system::error_code &error, size_t bytes_transferred)
        {
            if (error) {
                gameServices_.getLogger().logError("Error during async_write: " + error.message(), RED);
                if (clientSocket->is_open()) {
                    boost::system::error_code close_ec;
                    clientSocket->close(close_ec);
                    if (close_ec) {
                        gameServices_.getLogger().logError("Error closing socket after write failure: " + close_ec.message(), RED);
                    }
                }
            } else {
                gameServices_.getLogger().log("Bytes sent: " + std::to_string(bytes_transferred), BLUE);
                boost::system::error_code ec;
                auto remoteEndpoint = clientSocket->remote_endpoint(ec);
                if (!ec) {
                    gameServices_.getLogger().log("Data sent successfully to Client: " + remoteEndpoint.address().to_string() + ":" + std::to_string(remoteEndpoint.port()), BLUE);
                }
            } });
}

std::string
NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message)
{
    nlohmann::json response;
    std::string currentTimestamp = gameServices_.getLogger().getCurrentTimestamp();
    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = "1.0";
    response["body"] = message["body"];

    std::string responseString = response.dump();
    gameServices_.getLogger().log("Response generated: " + responseString, YELLOW);
    return responseString + "\n";
}

std::string
NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message, const TimestampStruct &timestamps)
{
    nlohmann::json response;
    std::string currentTimestamp = gameServices_.getLogger().getCurrentTimestamp();
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
    gameServices_.getLogger().log("Response with timestamps generated: " + responseString, YELLOW);
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
void
NetworkManager::addActiveSession(std::shared_ptr<ClientSession> session)
{
    std::lock_guard<std::mutex> lock(sessionsMutex_);

    // Prevent DoS attacks by limiting maximum concurrent sessions
    constexpr size_t MAX_CONCURRENT_SESSIONS = 1000;
    if (activeSessions_.size() >= MAX_CONCURRENT_SESSIONS)
    {
        gameServices_.getLogger().logError("Maximum concurrent sessions reached (" +
                                               std::to_string(MAX_CONCURRENT_SESSIONS) + "), rejecting new connection",
            RED);
        return; // Don't add the session
    }

    activeSessions_.insert(session);
    gameServices_.getLogger().log("Added session to active sessions. Total active: " + std::to_string(activeSessions_.size()), GREEN);
}

void
NetworkManager::removeActiveSession(std::shared_ptr<ClientSession> session)
{
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    activeSessions_.erase(session);
    gameServices_.getLogger().log("Removed session from active sessions. Total active: " + std::to_string(activeSessions_.size()), GREEN);
}

void
NetworkManager::cleanupInactiveSessions()
{
    std::lock_guard<std::mutex> lock(sessionsMutex_);

    size_t initialSize = activeSessions_.size();

    // Remove expired sessions (those that only exist in our set)
    for (auto it = activeSessions_.begin(); it != activeSessions_.end();)
    {
        bool shouldRemove = false;

        if (it->use_count() <= 1) // Only our reference remains
        {
            shouldRemove = true;
        }
        else
        {
            // Also check if the session's socket is still valid using public method
            if (!(*it)->isSocketOpen())
            {
                shouldRemove = true;
            }
        }

        if (shouldRemove)
        {
            gameServices_.getLogger().log("Cleaning up inactive session", GREEN);
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