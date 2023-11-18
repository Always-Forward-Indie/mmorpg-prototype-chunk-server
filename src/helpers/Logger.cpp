#include "helpers/Logger.hpp"

std::mutex logger_mutex_;

void Logger::log(const std::string& message, const std::string& color)
{
        std::lock_guard<std::mutex> lock(logger_mutex_);
        std::cout << color << message << RESET << std::endl;
}

void Logger::logError(const std::string& message, const std::string& color)
{
        std::lock_guard<std::mutex> lock(logger_mutex_);
        std::cerr << color << message << RESET << std::endl;
}