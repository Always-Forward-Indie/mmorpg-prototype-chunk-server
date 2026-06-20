#include "utils/Config.hpp"

namespace {

const char* getEnvOrDefault(const char* name, const char* defaultVal) {
    const char* env = std::getenv(name);
    return (env && env[0] != '\0') ? env : defaultVal;
}

}

std::tuple<GameServerConfig, ChunkServerConfig> Config::parseConfig() {
    GameServerConfig GSConfig;
    GSConfig.host        = getEnvOrDefault("GAME_SERVER_HOST", "127.0.0.1");
    GSConfig.port        = static_cast<short>(std::stoi(getEnvOrDefault("GAME_SERVER_PORT", "27016")));
    GSConfig.max_clients = static_cast<short>(std::stoi(getEnvOrDefault("GAME_SERVER_MAX_CLIENTS", "3000")));

    ChunkServerConfig CSConfig;
    CSConfig.host        = getEnvOrDefault("SERVER_HOST", "0.0.0.0");
    CSConfig.publicHost  = getEnvOrDefault("CHUNK_PUBLIC_HOST", CSConfig.host.c_str());
    CSConfig.port        = static_cast<short>(std::stoi(getEnvOrDefault("SERVER_PORT", "27017")));
    CSConfig.max_clients = static_cast<short>(std::stoi(getEnvOrDefault("SERVER_MAX_CLIENTS", "3000")));

    return std::make_tuple(GSConfig, CSConfig);
}
