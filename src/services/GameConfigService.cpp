#include "services/GameConfigService.hpp"
#include <mutex>
#include <stdexcept>
#include <spdlog/logger.h>

GameConfigService::GameConfigService(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("config");
}

void
GameConfigService::setConfig(const std::unordered_map<std::string, std::string> &config)
{
    std::unique_lock lock(mutex_);
    config_ = config;
    logger_.log("GameConfigService: received " + std::to_string(config_.size()) + " config entries.");
}

float
GameConfigService::getFloat(const std::string &key, float defaultValue) const
{
    std::shared_lock lock(mutex_);
    auto it = config_.find(key);
    if (it == config_.end())
        return defaultValue;
    try
    {
        return std::stof(it->second);
    }
    catch (...)
    {
        log_->error("GameConfigService::getFloat: invalid value for key '" + key +
                         "' = '" + it->second + "', using default " + std::to_string(defaultValue));
        return defaultValue;
    }
}

int
GameConfigService::getInt(const std::string &key, int defaultValue) const
{
    std::shared_lock lock(mutex_);
    auto it = config_.find(key);
    if (it == config_.end())
        return defaultValue;
    try
    {
        return std::stoi(it->second);
    }
    catch (...)
    {
        log_->error("GameConfigService::getInt: invalid value for key '" + key +
                         "' = '" + it->second + "', using default " + std::to_string(defaultValue));
        return defaultValue;
    }
}

bool
GameConfigService::getBool(const std::string &key, bool defaultValue) const
{
    std::shared_lock lock(mutex_);
    auto it = config_.find(key);
    if (it == config_.end())
        return defaultValue;
    const std::string &v = it->second;
    if (v == "true" || v == "1" || v == "yes")
        return true;
    if (v == "false" || v == "0" || v == "no")
        return false;
    log_->error("GameConfigService::getBool: invalid value for key '" + key +
                     "' = '" + v + "', using default");
    return defaultValue;
}

std::string
GameConfigService::getString(const std::string &key, const std::string &defaultValue) const
{
    std::shared_lock lock(mutex_);
    auto it = config_.find(key);
    if (it == config_.end())
        return defaultValue;
    return it->second;
}

bool
GameConfigService::isLoaded() const
{
    std::shared_lock lock(mutex_);
    return !config_.empty();
}
