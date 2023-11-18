#include "chunk_server/GameServerWorker.hpp"

GameServerWorker::GameServerWorker(std::tuple<GameServerConfig, ChunkServerConfig> &configs, Logger &logger)
    : io_context_game_server_(),
      game_server_socket_(io_context_game_server_),
      retry_timer_(io_context_game_server_),
      work_(boost::asio::make_work_guard(io_context_game_server_)),
      logger_(logger)
{
    // Get the port and host from the config
    short port = std::get<0>(configs).port;
    std::string host = std::get<0>(configs).host;

    // Resolve the host and port into a TCP endpoint
    boost::asio::ip::tcp::resolver resolver(io_context_game_server_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    logger_.log("Connecting to the Game Server...", BLUE);

    connect(endpoints); // Start the connection attempt
}

void GameServerWorker::startIOEventLoop()
{
    logger_.log("Starting Game Server Worker IO Context", BLUE);
    // Start io_service in a separate thread
    io_thread_ = std::thread([this]()
                             { io_context_game_server_.run(); });
}

// In your destructor, join the io_thread to ensure it's properly cleaned up
GameServerWorker::~GameServerWorker()
{
    logger_.logError("GameServerWorker destructor called");
    // Clean up
    work_.reset(); // Allow io_context to exit
    if (io_thread_.joinable())
    {
        io_thread_.join();
    }
    // Close the socket
    if (game_server_socket_.is_open())
    {
        game_server_socket_.close();
    }
}

void GameServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints)
{
    boost::asio::async_connect(game_server_socket_, endpoints,
                               [this, endpoints](const boost::system::error_code &ec, boost::asio::ip::tcp::endpoint)
                               {
                                   if (!ec)
                                   {
                                       // Connection successful
                                       logger_.log("Connected to the Game Server!", GREEN);
                                   }
                                   else
                                   {
                                       // Handle connection error
                                       logger_.logError("Error connecting to the Game Server: " + ec.message(), RED);
                                       logger_.log("Retrying connection in 5 seconds...", BLUE);
                                       retry_timer_.expires_after(std::chrono::seconds(5));
                                       retry_timer_.async_wait([this, endpoints](const boost::system::error_code &ec)
                                                               {
                        if (!ec) {
                            connect(endpoints); // Retry the connection attempt
                        } });
                                   }
                               });
}

void GameServerWorker::sendDataToGameServer(const std::string &data)
{
    nlohmann::json response;
    response["status"] = "success";
    response["body"] = data;
    std::string responseString = response.dump();

    try
    {
        boost::asio::async_write(game_server_socket_, boost::asio::buffer(responseString),
                                 [this](const boost::system::error_code &error, size_t bytes_transferred)
                                 {
                                     {
                                         if (!error)
                                         {
                                         }
                                         else
                                         {
                                             // Handle error
                                             logger_.logError("Error in sending data to Game Server: " + error.message());
                                         }

                                         logger_.log("Data sent successfully to Game Server. Bytes transferred: " + std::to_string(bytes_transferred), GREEN);
                                     }
                                 });
    }
    catch (const std::exception &e)
    {
        logger_.logError("Exception: " + std::string(e.what()));
    }
}

void GameServerWorker::receiveDataFromGameServer(std::function<void(const boost::system::error_code &, std::size_t)> callback)
{
    // Receive data asynchronously using the existing socket
    boost::asio::streambuf receive_buffer;
    boost::asio::async_read_until(game_server_socket_, receive_buffer, '\n', callback);
}

// Close the connection when done
void GameServerWorker::closeConnection()
{
    logger_.logError("Closing connection to chunk server");
    game_server_socket_.close();
}
