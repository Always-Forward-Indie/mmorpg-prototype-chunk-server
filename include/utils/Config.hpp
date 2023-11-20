#pragma once  // Include guard to prevent multiple inclusions
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

struct ChunkServerConfig {
    std::string host;
    short port;
    short max_clients;
};

class Config {
public:
    std::tuple<ChunkServerConfig> parseConfig(const std::string& configFile);
};