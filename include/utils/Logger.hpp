#pragma once

#include "utils/TerminalColors.hpp" // kept for backward-compat (callers pass color consts, we ignore them)
#include <memory>
#include <string>

// Forward declaration — spdlog headers are only in Logger.cpp, not pulled into every TU
namespace spdlog
{
class logger;
}

class Logger
{
  public:
    explicit Logger(const std::string &serverName = "server");
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // ── Backward-compatible API ──────────────────────────────────────────────
    // color param kept so existing call sites compile without changes; ignored internally
    void log(const std::string &message, const std::string &color = "");
    void logError(const std::string &message, const std::string &color = "");

    // ── Explicit-level methods ───────────────────────────────────────────────
    void debug(const std::string &message);
    void warn(const std::string &message);
    void info(const std::string &message);
    void error(const std::string &message);
    void critical(const std::string &message);

    void setLevel(const std::string &level);

    // ── Per-system loggers ───────────────────────────────────────────────────
    // Returns (creating if needed) a named child logger sharing the same sinks.
    // Level is read from LOG_LEVEL_<UPPER(system)> env var, falls back to global level.
    // Usage: auto log = logger_.getSystem("combat");
    //        log->debug("Player {} hit mob {}", playerId, mobId);
    std::shared_ptr<spdlog::logger> getSystem(const std::string &system);

  private:
    std::string serverName_;
    std::shared_ptr<spdlog::logger> logger_;
};
