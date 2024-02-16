#pragma once
#include <iostream>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <chrono>
#include "utils/TerminalColors.hpp"

class Logger {
public:
    void log(const std::string& message, const std::string& color = BLUE); 
    //TODO - make message receive not only string
    void logError(const std::string& message, const std::string& color = RED); 
    std::string getCurrentTimestamp();

private:
    std::mutex logger_mutex_;
    std::mutex timeMutex;
};