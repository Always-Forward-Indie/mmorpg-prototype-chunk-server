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

// Implement pushBatch to push a batch of events to the queue
void EventQueue::pushBatch(const std::vector<Event>& events) {
    std::unique_lock<std::mutex> lock(mtx);
    for (const auto& event : events) {
        queue.push(event);
    }
    cv.notify_one();
}

// Implement popBatch to pop a batch of events from the queue
bool EventQueue::popBatch(std::vector<Event>& events, int batchSize) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !queue.empty(); });

    while (!queue.empty() && batchSize > 0) {
        events.push_back(queue.front());
        queue.pop();
        batchSize--;
    }
    return !events.empty();
}