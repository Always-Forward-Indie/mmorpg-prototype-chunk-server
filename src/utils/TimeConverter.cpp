#include "utils/TimeConverter.hpp"

// Parse a time string and return the total seconds as an int
int TimeConverter::getSeconds(const std::string& timeStr) {
    std::istringstream stream(timeStr);
    int hours, minutes, seconds;
    char delimiter;

    stream >> hours >> delimiter >> minutes >> delimiter >> seconds;

    return hours * 3600 + minutes * 60 + seconds;
}

// Optionally, you can create separate functions for hours and minutes if needed
int TimeConverter::getHours(const std::string& timeStr) {
    std::istringstream stream(timeStr);
    int hours;
    char delimiter;

    stream >> hours >> delimiter;

    return hours;
}

int TimeConverter::getMinutes(const std::string& timeStr) {
    std::istringstream stream(timeStr);
    int hours, minutes;
    char delimiter;

    stream >> hours >> delimiter >> minutes >> delimiter;

    return minutes;
}