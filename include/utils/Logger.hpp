#pragma once
#include <iostream>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <ctime>
#include "utils/TerminalColors.hpp"

class Logger {
public:
    void log(const std::string& message, const std::string& color = BLUE); 
    void logError(const std::string& message, const std::string& color = RED); 
    std::string getCurrentTimestamp();
};