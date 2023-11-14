#include "helpers/Config.hpp"

    std::tuple<GameServerConfig, ChunkServerConfig> Config::parseConfig(const std::string& configFile) {
    GameServerConfig GSConfig;
    ChunkServerConfig CSConfig;

    try {
        // Open the JSON configuration file
        std::ifstream ifs(configFile);
        if (!ifs.is_open()) {
            throw std::runtime_error("Failed to open configuration file: " + configFile);
        }

        // Parse the JSON data
        nlohmann::json root;
        ifs >> root;

        // Extract Game server connection details
        GSConfig.host = root["game_server"]["host"].get<std::string>();
        GSConfig.port = root["game_server"]["port"].get<short>();
        GSConfig.max_clients = root["game_server"]["max_clients"].get<short>();

        // Extract Chunk server connection details
        CSConfig.host = root["chunk_server"]["host"].get<std::string>();
        CSConfig.port = root["chunk_server"]["port"].get<short>();
        CSConfig.max_clients = root["chunk_server"]["max_clients"].get<short>();

    } catch (const std::exception& e) {
        // Handle any errors that occur during parsing or reading the configuration file
        std::cerr << "Error while parsing configuration: " << e.what() << std::endl;
        // You may want to throw or handle the error differently based on your application's needs
    }

    // Construct and return the tuple with the extracted values
    return std::make_tuple(GSConfig, CSConfig);
}