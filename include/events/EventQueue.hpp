#pragma once
#include "Event.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

class EventQueue
{
  public:
    EventQueue(size_t maxSize = 10000); // Default max size to prevent memory bloat

    void push(const Event &event);
    void push(Event &&event); // Add rvalue overload for efficient move
    bool pop(Event &event);   // returns false when stopped and queue is empty

    void pushBatch(const std::vector<Event> &events);
    bool popBatch(std::vector<Event> &events, int batchSize);
    bool empty();
    size_t size(); // Add size method for monitoring

    /// Number of events dropped due to queue overflow or validation failures
    /// (reset on next call; meant for monitoring/instrumentation).
    uint64_t getDroppedCount()
    {
        return droppedEvents_.exchange(0, std::memory_order_relaxed);
    }

    // Force cleanup to shrink internal containers when queue is empty
    void forceCleanup();

    /// HIGH-4: Signal all blocked pop/popBatch callers to return false so that
    ///         consumer threads can exit cleanly on shutdown.
    void stop();
    /// Drop everything still queued (S16a shutdown). Queued events may own
    /// sockets; the queue outlives NetworkManager's io_context at scope
    /// exit, so leftovers would destroy sockets after their service
    /// (ASan heap-use-after-free in asio internals, observed on game).
    /// Call after all producers are joined and before returning from main.
    void drain()
    {
        std::vector<Event> tmp;
        while (popBatch(tmp, 1024))
            tmp.clear();
    }
    bool isStopped() const
    {
        return stopped_.load(std::memory_order_acquire);
    }

  private:
    std::queue<Event> queue;
    std::mutex mtx;
    std::condition_variable cv;
    size_t maxSize_;
    /// HIGH-4: set to true by stop(); causes waiting consumers to wake and return false.
    std::atomic<bool> stopped_{false};

    /// Count of events silently dropped (overflow, corruption, invalid variant).
    std::atomic<uint64_t> droppedEvents_{0};

    // Helper method to enforce size limit
    void enforceLimit();
};
