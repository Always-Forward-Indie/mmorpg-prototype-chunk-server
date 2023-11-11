#pragma once

#include <string>
#include <boost/asio.hpp>
#include "helpers/Config.hpp"

class GameServerWorker {
private:
    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;

public:
    GameServerWorker();
    void sendDataToGameServer(const std::string& data, std::function<void(const boost::system::error_code&, std::size_t)> callback);
    void receiveDataFromGameServer(std::function<void(const boost::system::error_code&, std::size_t)> callback);

    // Close the connection when done
    void closeConnection();
};
