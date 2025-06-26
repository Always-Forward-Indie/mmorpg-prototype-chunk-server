#pragma once

#include <functional>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <algorithm>
#include "data/SpecialStructs.hpp"
#include <thread>

// Компаратор для min-heap (задача с меньшим nextRunTime имеет больший приоритет)
struct TaskComparator {
    bool operator()(const Task &a, const Task &b) const {
        return a.nextRunTime > b.nextRunTime;
    }
};

class Scheduler {
public:
    Scheduler();
    ~Scheduler();

    void start();
    void stop();

    // Добавляем задачу в планировщик
    void scheduleTask(const Task &task);
    // Помечаем задачу для удаления (пересобираем кучу)
    void removeTask(int id);

private:
    void run();

    std::priority_queue<Task, std::vector<Task>, TaskComparator> tasksHeap;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> stopFlag;
    std::thread thread;
};