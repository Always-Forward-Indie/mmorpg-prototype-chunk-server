#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <tuple>

struct GameServerConfig {
    std::string host;
    std::uint16_t port = 27016;
    int max_clients = 3000;
};

struct ChunkServerConfig {
    std::string host;
    std::string publicHost;
    std::uint16_t port = 27017;
    int max_clients = 3000;
};

class Config {
public:
    /// Layered config: compiled defaults < config.json (if present) < ENV.
    /// Throws std::runtime_error with the variable name on invalid values.
    std::tuple<GameServerConfig, ChunkServerConfig> parseConfig();
    /// Override for tests / explicit paths.
    std::tuple<GameServerConfig, ChunkServerConfig> parseConfig(const std::string &jsonPath);
};
