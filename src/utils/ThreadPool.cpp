#include "utils/ThreadPool.hpp"
#include <stdexcept>

ThreadPool::ThreadPool(size_t numThreads, size_t maxTasks) : maxTasks_(maxTasks)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        workers.emplace_back([this]()
            {
            while (true)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    condition.wait(lock, [this]() { return stop || !tasks.empty(); });
                    if (stop && tasks.empty())
                        return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                try {
                    task();
                } catch (const std::exception& e) {
                    // Log task execution errors to prevent thread pool crashes
                    // Note: we don't have access to logger here, so errors may be silent
                    // This is better than crashing the worker thread
                }
            } });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers)
        worker.join();
}

void
ThreadPool::enqueueTask(std::function<void()> task)
{
    enqueueTaskInternal(std::move(task));
}

void
ThreadPool::enqueueTaskInternal(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        // Prevent task queue from growing too large
        if (tasks.size() >= maxTasks_)
        {
            throw std::runtime_error("ThreadPool task queue is full");
        }

        tasks.emplace(std::move(task));
    }
    condition.notify_one();
}

size_t
ThreadPool::getTaskQueueSize()
{
    std::unique_lock<std::mutex> lock(queueMutex);
    return tasks.size();
}
