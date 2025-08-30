#pragma once
#include "data/DataStructs.hpp"
#include "events/EventQueue.hpp"
#include "network/ClientSession.hpp"
#include "services/GameServices.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include <array>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
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
};
