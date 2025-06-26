#pragma once

#include <chrono>
#include <functional>

struct Task {
    std::function<void()> func;
    int interval; // Interval in seconds
    std::chrono::time_point<std::chrono::system_clock> nextRunTime;
    bool stopFlag = false; // Remove tasks flag
    int id; 

    Task(std::function<void()> func, int interval, std::chrono::time_point<std::chrono::system_clock> startTime, int id)
        : func(std::move(func)), interval(interval), nextRunTime(startTime), id(id) {}
};
