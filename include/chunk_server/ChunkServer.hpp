#pragma once

#include <boost/asio.hpp>
#include <array>
#include <string>
#include <nlohmann/json.hpp>
#include "Authenticator.hpp"
#include "GameServerWorker.hpp"
#include "CharacterManager.hpp"
#include "Event.hpp"
#include "EventQueue.hpp"
#include "EventHandler.hpp"
#include "helpers/TerminalColors.hpp"


class ChunkServer {
public:
    ChunkServer(GameServerWorker& gameServerWorker, std::tuple<GameServerConfig, ChunkServerConfig>& configs, Logger& logger);
    void startIOEventLoop();
    
private:
    static constexpr size_t max_length = 1024; // Define the appropriate value
    
    void startAccept();
    void handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length>& dataBuffer, size_t bytes_transferred);
    void startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
    void joinGame(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &hash, const int &characterId, const int &clientId);
    void sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string& responseString);
    std::string generateResponseMessage(const std::string& status, const nlohmann::json& message, const int& id);
    
    //Events
    void onPlayerMoveReceived(const int &clientId, float x, float y, float z);
    void mainEventLoop();

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    ClientData clientData_;
    Authenticator authenticator_;
    CharacterManager characterManager_;
    GameServerWorker& gameServerWorker_; // Reference to the GameServerWorker instance
    Logger& logger_; // Reference to the Logger instance
    std::tuple<GameServerConfig, ChunkServerConfig>& configs_;
    EventQueue _eventQueue;
    EventHandler _eventHandler;
};
