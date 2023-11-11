#include "helpers/Config.hpp"
#include <iostream>
#include "chunk_server/ChunkServer.hpp"
#include "chunk_server/GameServerWorker.hpp"

int main() {
    try {
        boost::asio::io_context io_context;
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<1>(configs).port;
        std::string ip = std::get<1>(configs).host;
        short maxClients = std::get<1>(configs).max_clients;

        ChunkServer chunkServer(io_context, ip, port, maxClients);

        io_context.run();  // Start the event loop

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Indicate an error exit status
    }
}