#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>

class ThreadPool
{
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    // Старая версия API – для задач без возвращаемого значения
    void enqueueTask(std::function<void()> task);

    // Новая версия API – для задач с возвращаемым значением
    template <class F, class... Args>
    auto enqueueTask(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.push([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
};
