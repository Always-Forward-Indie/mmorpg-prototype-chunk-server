#pragma once  // Include guard to prevent multiple inclusions

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

struct GameServerConfig {
    std::string host;
    short port;
    short max_clients;
};

struct ChunkServerConfig {
    std::string host;
    short port;
    short max_clients;
};

class Config {
public:
    std::tuple<GameServerConfig, ChunkServerConfig> parseConfig(const std::string& configFile);
};