#include "utils/Config.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

const char *getEnvOrDefault(const char *name, const char *defaultVal) {
    const char *env = std::getenv(name);
    return (env && env[0] != '\0') ? env : defaultVal;
}

bool envPresent(const char *name) {
    const char *env = std::getenv(name);
    return env && env[0] != '\0';
}

std::uint16_t parsePort(const char *varName, const std::string &raw) {
    try {
        int v = std::stoi(raw);
        if (v < 1 || v > 65535)
            throw std::out_of_range("port out of range 1..65535");
        return static_cast<std::uint16_t>(v);
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(std::string("Config: invalid integer in ") + varName + "='" + raw + "'");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(std::string("Config: value out of range in ") + varName + "='" + raw + "'");
    }
}

int parsePositiveInt(const char *varName, const std::string &raw, int lo, int hi) {
    try {
        int v = std::stoi(raw);
        if (v < lo || v > hi)
            throw std::out_of_range("out of range");
        return v;
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(std::string("Config: invalid integer in ") + varName + "='" + raw + "'");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(std::string("Config: value out of range in ") + varName + "='" + raw +
                                 "' (expected " + std::to_string(lo) + ".." + std::to_string(hi) + ")");
    }
}

nlohmann::json loadJsonFile(const std::string &path) {
    std::ifstream f(path);
    if (!f.good())
        return nlohmann::json::object();
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object())
            throw std::runtime_error("Config: '" + path + "' root must be a JSON object");
        return j;
    } catch (const nlohmann::json::exception &e) {
        throw std::runtime_error(std::string("Config: cannot parse '") + path + "': " + e.what());
    }
}

std::string jsonStr(const nlohmann::json &j, const char *key, const std::string &fallback) {
    if (j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return fallback;
}

} // namespace

std::tuple<GameServerConfig, ChunkServerConfig> Config::parseConfig() {
    return parseConfig("config.json");
}

std::tuple<GameServerConfig, ChunkServerConfig> Config::parseConfig(const std::string &jsonPath) {
    const nlohmann::json j = loadJsonFile(jsonPath);

    GameServerConfig GSConfig;
    GSConfig.host = jsonStr(j, "game_server_host", "127.0.0.1");
    GSConfig.port = 27016;
    GSConfig.max_clients = 3000;
    if (j.contains("game_server_port") && j["game_server_port"].is_number_integer())
        GSConfig.port = parsePort("game_server_port(config.json)", std::to_string(j["game_server_port"].get<int>()));

    ChunkServerConfig CSConfig;
    CSConfig.host = jsonStr(j, "server_host", "0.0.0.0");
    CSConfig.publicHost = jsonStr(j, "chunk_public_host", CSConfig.host);
    CSConfig.port = 27017;
    CSConfig.max_clients = 3000;
    if (j.contains("server_port") && j["server_port"].is_number_integer())
        CSConfig.port = parsePort("server_port(config.json)", std::to_string(j["server_port"].get<int>()));
    if (j.contains("server_max_clients") && j["server_max_clients"].is_number_integer())
        CSConfig.max_clients =
            parsePositiveInt("server_max_clients(config.json)", std::to_string(j["server_max_clients"].get<int>()), 1, 100000);

    if (envPresent("GAME_SERVER_HOST"))
        GSConfig.host = getEnvOrDefault("GAME_SERVER_HOST", "127.0.0.1");
    if (envPresent("GAME_SERVER_PORT"))
        GSConfig.port = parsePort("GAME_SERVER_PORT", getEnvOrDefault("GAME_SERVER_PORT", "27016"));
    if (envPresent("GAME_SERVER_MAX_CLIENTS"))
        GSConfig.max_clients =
            parsePositiveInt("GAME_SERVER_MAX_CLIENTS", getEnvOrDefault("GAME_SERVER_MAX_CLIENTS", "3000"), 1, 100000);

    if (envPresent("SERVER_HOST"))
        CSConfig.host = getEnvOrDefault("SERVER_HOST", "0.0.0.0");
    // CHUNK_PUBLIC_HOST defaults to SERVER_HOST (Docker/NAT friendly).
    if (envPresent("CHUNK_PUBLIC_HOST"))
        CSConfig.publicHost = getEnvOrDefault("CHUNK_PUBLIC_HOST", CSConfig.host.c_str());
    else if (!envPresent("SERVER_HOST") && CSConfig.publicHost.empty())
        CSConfig.publicHost = CSConfig.host;
    if (envPresent("SERVER_PORT"))
        CSConfig.port = parsePort("SERVER_PORT", getEnvOrDefault("SERVER_PORT", "27017"));
    if (envPresent("SERVER_MAX_CLIENTS"))
        CSConfig.max_clients = parsePositiveInt("SERVER_MAX_CLIENTS", getEnvOrDefault("SERVER_MAX_CLIENTS", "3000"), 1, 100000);

    if (CSConfig.publicHost.empty())
        CSConfig.publicHost = CSConfig.host;

    return std::make_tuple(GSConfig, CSConfig);
}
