#pragma once
#include <array>
#include <string>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <memory>
#include "data/DataStructs.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include "events/EventQueue.hpp"
#include "services/GameServices.hpp"
#include "network/ClientSession.hpp"

// Forward declarations
class ChunkServer;
class EventDispatcher;
class MessageHandler;

class NetworkManager {
public:
    NetworkManager(GameServices& gameServices, EventQueue& eventQueue, EventQueue& eventQueuePing, std::tuple<GameServerConfig, ChunkServerConfig>& configs);
    ~NetworkManager();
    void startAccept();
    void startIOEventLoop();
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message);
    void setChunkServer(ChunkServer* ChunkServer);

private:
    static constexpr size_t max_length = 1024;
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> threadPool_;
    std::tuple<GameServerConfig, ChunkServerConfig>& configs_;
    ChunkServer* chunkServer_;
    EventQueue& eventQueue_;
    EventQueue& eventQueuePing_;
    JSONParser jsonParser_;

    // These are declared but NOT initialized here!
    std::unique_ptr<EventDispatcher> eventDispatcher_;
    std::unique_ptr<MessageHandler> messageHandler_;


    //game services
    GameServices& gameServices_;
};
