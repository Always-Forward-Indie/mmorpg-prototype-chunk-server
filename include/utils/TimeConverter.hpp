#pragma once

#include <string>
#include <chrono>
#include <sstream>

// time converters class
class TimeConverter
{
public:
    static int getSeconds(const std::string& timeStr);
    static int getMinutes(const std::string& timeStr);
    static int getHours(const std::string& timeStr);
};