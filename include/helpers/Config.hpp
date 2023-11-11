#pragma once  // Include guard to prevent multiple inclusions

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

struct DatabaseConfig {
    std::string dbname;
    std::string user;
    std::string password;
    std::string host;
    short port;
};

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
    std::tuple<DatabaseConfig, GameServerConfig, ChunkServerConfig> parseConfig(const std::string& configFile);
};