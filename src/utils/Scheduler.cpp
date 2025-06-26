#include "utils/Scheduler.hpp"
#include <algorithm>

Scheduler::Scheduler() : stopFlag(false) {}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    thread = std::thread(&Scheduler::run, this);
}

void Scheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopFlag.store(true);
    }
    cv.notify_all();
    if (thread.joinable()) {
        thread.join();
    }
}

void Scheduler::scheduleTask(const Task &task) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        tasksHeap.push(task);
    }
    cv.notify_all();
}

void Scheduler::removeTask(int id) {
    std::lock_guard<std::mutex> lock(mutex);
    // Пересобираем кучу, помечая задачи с заданным id
    std::vector<Task> temp;
    while (!tasksHeap.empty()) {
        Task t = tasksHeap.top();
        tasksHeap.pop();
        if (t.id == id) {
            t.stopFlag = true;
        }
        temp.push_back(t);
    }
    for (const auto &t : temp) {
        tasksHeap.push(t);
    }
    cv.notify_all();
}

void Scheduler::run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stopFlag.load()) {
        if (tasksHeap.empty()) {
            cv.wait(lock, [this] { return stopFlag.load() || !tasksHeap.empty(); });
            if (stopFlag.load())
                break;
        }
        // Получаем задачу с минимальным nextRunTime
        Task t = tasksHeap.top();
        // Если задача помечена на удаление, просто удаляем её
        if (t.stopFlag) {
            tasksHeap.pop();
            continue;
        }
        auto now = std::chrono::system_clock::now();
        if (now >= t.nextRunTime) {
            // Готовая задача – удаляем её из кучи, освобождаем мьютекс и выполняем
            tasksHeap.pop();
            lock.unlock();
            t.func();
            lock.lock();
            // Обновляем время следующего запуска и возвращаем задачу в кучу
            t.nextRunTime = now + std::chrono::seconds(t.interval);
            tasksHeap.push(t);
            cv.notify_all();
        } else {
            // Ждём до следующего времени выполнения или появления новой задачи с более ранним временем
            cv.wait_until(lock, t.nextRunTime, [this, &t] {
                return stopFlag.load() || (!tasksHeap.empty() && tasksHeap.top().nextRunTime < t.nextRunTime);
            });
        }
    }
}
