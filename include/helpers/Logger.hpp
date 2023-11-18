#pragma once
#include <iostream>
#include <mutex>
#include "helpers/TerminalColors.hpp"

class Logger {
public:
    void log(const std::string& message, const std::string& color = BLUE); 
    void logError(const std::string& message, const std::string& color = RED); 
};