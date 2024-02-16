#include "events/EventQueue.hpp"

void EventQueue::push(const Event &event)
{
    std::unique_lock<std::mutex> lock(mtx);
    queue.push(event);
    cv.notify_one();
}

bool EventQueue::pop(Event &event)
{
    std::unique_lock<std::mutex> lock(mtx);
    // Wait until the queue is not empty (blocking the current thread)
    cv.wait(lock, [this]
            { return !queue.empty(); });
    event = queue.front();
    queue.pop();
    return true;
}