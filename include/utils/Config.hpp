#pragma once

#include <cstdlib>
#include <string>
#include <tuple>

struct GameServerConfig {
    std::string host;
    short port;
    short max_clients;
};

struct ChunkServerConfig {
    std::string host;
    std::string publicHost;
    short port;
    short max_clients;
};

class Config {
public:
    std::tuple<GameServerConfig, ChunkServerConfig> parseConfig();
};
