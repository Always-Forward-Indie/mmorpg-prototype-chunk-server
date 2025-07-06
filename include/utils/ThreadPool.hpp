#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

class ThreadPool
{
  public:
    ThreadPool(size_t numThreads, size_t maxTasks = 10000);
    ~ThreadPool();

    // Старая версия API – для задач без возвращаемого значения
    void enqueueTask(std::function<void()> task);

    // Method to get current task queue size
    size_t getTaskQueueSize();

    // Новая версия API – для задач с возвращаемым значением
    template <class F, class... Args>
    auto enqueueTask(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();

        // Use internal method to avoid race conditions
        enqueueTaskInternal([task]()
            { (*task)(); });

        return res;
    }

  private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
    size_t maxTasks_;

    // Internal thread-safe method for adding tasks
    void enqueueTaskInternal(std::function<void()> task);
};
