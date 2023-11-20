#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "Event.hpp"

class EventQueue {
public:
    void push(const Event& event);
    bool pop(Event& event);

private:
    std::queue<Event> queue;
    std::mutex mtx;
    std::condition_variable cv;
};
