#pragma once
#include <string>
#include <boost/asio.hpp>
#include "helpers/Config.hpp"
#include "helpers/Logger.hpp"

class GameServerWorker {
private:
    boost::asio::io_context io_context_game_server_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    boost::asio::ip::tcp::socket game_server_socket_;
    boost::asio::steady_timer retry_timer_;
    std::thread io_thread_;
    Logger& logger_;

public:
    GameServerWorker(std::tuple<GameServerConfig, ChunkServerConfig>& configs, Logger& logger);
    ~GameServerWorker();
    void startIOEventLoop();
    void sendDataToGameServer(const std::string& data);
    void receiveDataFromGameServer(std::function<void(const boost::system::error_code&, std::size_t)> callback);
    void connect(boost::asio::ip::tcp::resolver::results_type endpoints);

    // Close the connection when done
    void closeConnection();
};
