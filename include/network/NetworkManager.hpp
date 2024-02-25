#pragma once
#include <array>
#include <string>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include "utils/Config.hpp"
#include "utils/JSONParser.hpp"
#include "events/EventQueue.hpp"

class NetworkManager {
public:
    NetworkManager(EventQueue& eventQueue, std::tuple<ChunkServerConfig> &configs, Logger &logger);
    ~NetworkManager();
    void startAccept();
    void startIOEventLoop();
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string& responseString);
    std::string generateResponseMessage(const std::string &status, const nlohmann::json &message);

private:
    static constexpr size_t max_length = 1024; // Define the appropriate value
    void handleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const boost::system::error_code& error);
    void startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
    void handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length>& dataBuffer, size_t bytes_transferred);

    // Create a new events batch
    //std::vector<Event> eventsBatch;

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread networkManagerThread_;
    std::tuple<ChunkServerConfig>& configs_;
    EventQueue& eventQueue_;
    Logger& logger_;
    JSONParser jsonParser_;
};