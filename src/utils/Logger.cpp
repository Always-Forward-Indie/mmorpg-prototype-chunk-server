#include "utils/Logger.hpp"

std::string Logger::getCurrentTimestamp() {
    std::ostringstream ss;

    // Using chrono for time handling
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Safely handling the conversion to local time
    std::tm buf;
    #if defined(__unix__)
        localtime_r(&in_time_t, &buf);
    #elif defined(_MSC_VER)
        localtime_s(&buf, &in_time_t);
    #else
        buf = *std::localtime(&in_time_t);
    #endif

    // Formatting the timestamp
    ss << std::put_time(&buf, "%Y-%m-%d %X"); // X for HH:MM:SS
    return ss.str();
}

void Logger::log(const std::string& message, const std::string& color)
{
        std::lock_guard<std::mutex> lock(logger_mutex_);
        std::string timestamp = getCurrentTimestamp();
        std::cout << color  << "[INFO] [" << timestamp << "] " << message << RESET << std::endl;
}

void Logger::logError(const std::string& message, const std::string& color)
{
        std::lock_guard<std::mutex> lock(logger_mutex_);
        std::string timestamp = getCurrentTimestamp();
        std::cerr << color << "[ERROR] [" << timestamp << "] " << message << RESET << std::endl;
}