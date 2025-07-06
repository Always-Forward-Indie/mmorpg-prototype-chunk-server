#pragma once
#include "Event.hpp"
#include <condition_variable>
#include <mutex>
#include <queue>

class EventQueue
{
  public:
    EventQueue(size_t maxSize = 10000); // Default max size to prevent memory bloat

    void push(const Event &event);
    void push(Event &&event); // Add rvalue overload for efficient move
    bool pop(Event &event);

    void pushBatch(const std::vector<Event> &events);
    bool popBatch(std::vector<Event> &events, int batchSize);
    bool empty();
    size_t size(); // Add size method for monitoring

    // Force cleanup to shrink internal containers when queue is empty
    void forceCleanup();

  private:
    std::queue<Event> queue;
    std::mutex mtx;
    std::condition_variable cv;
    size_t maxSize_;

    // Helper method to enforce size limit
    void enforceLimit();
};
