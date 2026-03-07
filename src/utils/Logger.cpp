#include "utils/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace
{
spdlog::level::level_enum
parseLevel(const std::string &level)
{
    if (level == "trace")
        return spdlog::level::trace;
    if (level == "debug")
        return spdlog::level::debug;
    if (level == "info")
        return spdlog::level::info;
    if (level == "warn" || level == "warning")
        return spdlog::level::warn;
    if (level == "error")
        return spdlog::level::err;
    if (level == "critical")
        return spdlog::level::critical;
    return spdlog::level::info;
}
} // namespace

Logger::Logger(const std::string &serverName) : serverName_(serverName)
{
    std::filesystem::create_directories("logs");

    spdlog::init_thread_pool(8192, 1);

    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_sink->set_color_mode(spdlog::color_mode::always);
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/" + serverName + ".log", 10 * 1024 * 1024, 5);

    logger_ = std::make_shared<spdlog::async_logger>(
        serverName,
        spdlog::sinks_init_list{stdout_sink, file_sink},
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    const char *envLevel = std::getenv("LOG_LEVEL");
    logger_->set_level(parseLevel(envLevel ? envLevel : "info"));
    logger_->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");

    spdlog::register_logger(logger_);
}

Logger::~Logger()
{
    spdlog::drop(logger_->name());
    spdlog::shutdown();
}

void
Logger::log(const std::string &message, const std::string &)
{
    logger_->info(message);
}
void
Logger::logError(const std::string &message, const std::string &)
{
    logger_->error(message);
}
void
Logger::debug(const std::string &message)
{
    logger_->debug(message);
}
void
Logger::warn(const std::string &message)
{
    logger_->warn(message);
}
void
Logger::info(const std::string &message)
{
    logger_->info(message);
}
void
Logger::error(const std::string &message)
{
    logger_->error(message);
}
void
Logger::critical(const std::string &message)
{
    logger_->critical(message);
}

void
Logger::setLevel(const std::string &level)
{
    logger_->set_level(parseLevel(level));
}

std::shared_ptr<spdlog::logger>
Logger::getSystem(const std::string &system)
{
    const std::string name = serverName_ + "." + system;

    // Return existing logger if already registered
    if (auto existing = spdlog::get(name))
        return existing;

    // Create child logger sharing the same sinks as the parent
    auto child = std::make_shared<spdlog::async_logger>(
        name,
        logger_->sinks().begin(),
        logger_->sinks().end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    child->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");

    // Per-system level: LOG_LEVEL_COMBAT, LOG_LEVEL_MOB, LOG_LEVEL_NETWORK, ...
    std::string envKey = "LOG_LEVEL_";
    std::transform(system.begin(), system.end(), std::back_inserter(envKey), ::toupper);
    const char *envLevel = std::getenv(envKey.c_str());
    child->set_level(parseLevel(envLevel ? envLevel : spdlog::level::to_string_view(logger_->level()).data()));

    spdlog::register_logger(child);
    return child;
}
