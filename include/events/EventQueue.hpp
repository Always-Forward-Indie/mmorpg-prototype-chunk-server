#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "Event.hpp"

class EventQueue {
public:
    void push(const Event& event);
    bool pop(Event& event);

    void pushBatch(const std::vector<Event> &events);
    bool popBatch(std::vector<Event> &events, int batchSize);
    bool empty();

private:
    std::queue<Event> queue;
    std::mutex mtx;
    std::condition_variable cv;
};
