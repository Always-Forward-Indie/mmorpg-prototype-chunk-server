#include "utils/Logger.hpp"

std::string Logger::getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        std::lock_guard<std::mutex> lock(timeMutex);
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X"); // X for HH:MM:SS
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