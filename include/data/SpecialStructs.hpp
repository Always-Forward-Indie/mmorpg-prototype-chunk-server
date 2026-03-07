#pragma once

#include <chrono>
#include <functional>

struct Task
{
    std::function<void()> func;
    int64_t intervalMs;                                             // HIGH-3: interval in milliseconds (was int seconds)
    std::chrono::time_point<std::chrono::steady_clock> nextRunTime; // HIGH-2: steady_clock avoids NTP jumps
    bool stopFlag = false;                                          // Remove tasks flag
    int id;

    Task(std::function<void()> func, int64_t intervalMs, std::chrono::time_point<std::chrono::steady_clock> startTime, int id)
        : func(std::move(func)), intervalMs(intervalMs), nextRunTime(startTime), id(id) {}
};
