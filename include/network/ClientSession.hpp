#pragma once

#include <memory>
#include <array>
#include <string>
#include <boost/asio.hpp>
#include "utils/Logger.hpp"
#include "events/EventQueue.hpp"
#include "utils/JSONParser.hpp"
#include "services/GameServices.hpp"
#include <nlohmann/json.hpp>

//Forward declarations
class ChunkServer;
class EventDispatcher;
class MessageHandler;

class ClientSession : public std::enable_shared_from_this<ClientSession> {
    public:
        // Accept a shared_ptr to a tcp::socket instead of a raw socket.
        ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                        ChunkServer *chunkServer,
                        GameServices &gameServices,
                        EventQueue &eventQueue, 
                        EventQueue &eventQueuePing,
                        JSONParser &jsonParser, 
                        EventDispatcher &eventDispatcher, 
                        MessageHandler &messageHandler);

        void start();

    private:
        void doRead();
        void processMessage(const std::string &message);
        void handleClientDisconnect();
        

        std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
        std::array<char, 1024> dataBuffer_;
        std::string accumulatedData_;
        EventQueue &eventQueue_;
        EventQueue &eventQueuePing_;
        JSONParser &jsonParser_;
        ChunkServer *chunkServer_;
        EventDispatcher &eventDispatcher_;
        MessageHandler &messageHandler_;
        GameServices &gameServices_;
};
