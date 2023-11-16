#include "chunk_server/GameServerWorker.hpp"

    GameServerWorker::GameServerWorker() : io_context_(), socket_(io_context_), retry_timer_(io_context_) {
        // Initialize and establish the connection here (called only once)
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<0>(configs).port;
        std::string host = std::get<0>(configs).host;

        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        std::cout << BLUE << "Connecting to the Game Server..." << RESET << std::endl;

        connect(endpoints); // Start the connection attempt
        // Start the io_context in a separate thread
        std::thread io_thread([this] { io_context_.run(); });
        io_thread.detach();  // Or manage the thread's lifecycle more carefully
    }

    void GameServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints) {
        boost::asio::async_connect(socket_, endpoints,
            [this, endpoints](const boost::system::error_code& ec, boost::asio::ip::tcp::endpoint) {
                if (!ec) {
                    // Connection successful
                    std::cout << GREEN << "Connected to the Game Server!" << RESET << std::endl;
                } else {
                    // Handle connection error
                    std::cerr << RED << "Error connecting to the Game Server: " << ec.message() << RESET << std::endl;
                    std::cout << BLUE << "Retrying connection in 5 seconds..." << RESET << std::endl;
                    retry_timer_.expires_after(std::chrono::seconds(5));
                    retry_timer_.async_wait([this, endpoints](const boost::system::error_code& ec) {
                        if (!ec) {
                            connect(endpoints); // Retry the connection attempt
                        }
                    });
                }
            });
    }

    void GameServerWorker::sendDataToGameServer(const std::string& data, std::function<void(const boost::system::error_code&, std::size_t)> callback) {
        // Send data asynchronously using the existing socket
        boost::asio::async_write(socket_, boost::asio::buffer(data), callback);
    }


    void GameServerWorker::receiveDataFromGameServer(std::function<void(const boost::system::error_code&, std::size_t)> callback) {
        // Receive data asynchronously using the existing socket
        boost::asio::streambuf receive_buffer;
        boost::asio::async_read_until(socket_, receive_buffer, '\n', callback);
    }

    // Close the connection when done
    void GameServerWorker::closeConnection() {
        socket_.close();
    }
