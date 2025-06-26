#pragma once

#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <array>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "utils/Config.hpp"
#include "utils/Logger.hpp"
#include "events/EventQueue.hpp"
#include "network/NetworkManager.hpp"
#include "utils/JSONParser.hpp"

class GameServerWorker {
private:
    boost::asio::io_context io_context_game_server_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::shared_ptr<boost::asio::ip::tcp::socket> game_server_socket_;
    boost::asio::steady_timer retry_timer_;
    std::vector<std::thread> io_threads_;
    EventQueue &eventQueue_;
    Logger &logger_;
    int retryCount = 0;
    static constexpr int MAX_RETRY_COUNT = 5;
    static constexpr int RETRY_TIMEOUT = 5;
    JSONParser jsonParser_;
    GameServerConfig& gameServerConfig_;
    ChunkServerConfig& chunkServerConfig_;

    // Process received data from the Game Server
    void processGameServerData(const std::array<char, 1024>& buffer, std::size_t bytes_transferred);

public:
    GameServerWorker(EventQueue &eventQueue,
                        std::tuple<GameServerConfig, ChunkServerConfig>& configs, Logger &logger);
    ~GameServerWorker();
    void startIOEventLoop();
    void sendDataToGameServer(const std::string &data);
    void receiveDataFromGameServer();
    void connect(boost::asio::ip::tcp::resolver::results_type endpoints, int currentRetryCount = 0);
    void closeConnection();
};
