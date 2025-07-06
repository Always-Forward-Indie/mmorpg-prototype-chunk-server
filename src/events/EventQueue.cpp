#include "events/EventQueue.hpp"

EventQueue::EventQueue(size_t maxSize) : maxSize_(maxSize)
{
}

void
EventQueue::push(const Event &event)
{
    std::unique_lock<std::mutex> lock(mtx);
    try
    {
        queue.push(event);
        enforceLimit();
        cv.notify_one();
    }
    catch (const std::exception &e)
    {
        // Skip corrupted events
    }
}

void
EventQueue::push(Event &&event)
{
    std::unique_lock<std::mutex> lock(mtx);
    try
    {
        queue.emplace(std::move(event)); // Efficient move
        enforceLimit();
        cv.notify_one();
    }
    catch (const std::exception &e)
    {
        // Skip corrupted events
    }
}

bool
EventQueue::pop(Event &event)
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]
        { return !queue.empty(); });

    event = std::move(queue.front());
    queue.pop();
    return true;
}

void
EventQueue::pushBatch(const std::vector<Event> &events)
{
    std::unique_lock<std::mutex> lock(mtx);

    // Reserve space in the queue to avoid frequent reallocations
    if (queue.size() + events.size() > maxSize_)
    {
        // Drop oldest events to make room for new ones
        size_t eventsToRemove = (queue.size() + events.size()) - maxSize_;
        for (size_t i = 0; i < eventsToRemove && !queue.empty(); ++i)
        {
            queue.pop();
        }
    }

    for (const auto &event : events)
    {
        try
        {
            // Validate the event's variant state before accessing its data
            const auto &eventData = event.getData();

            // Check if the variant index is valid before proceeding
            if (eventData.index() >= std::variant_size_v<EventData>)
            {
                // Invalid variant, skip this event
                continue;
            }

            // Try to validate the variant by attempting to visit it
            std::visit([](const auto &value)
                {
                // Just access the value to trigger any potential corruption issues
                (void)value; },
                eventData);

            // If we get here, the variant is valid, so we can safely copy the event
            queue.emplace(event); // Safe copy operation
        }
        catch (const std::exception &e)
        {
            // Skip corrupted events and log the issue
            continue;
        }
    }
    enforceLimit();
    cv.notify_all();
}

// Helper method to enforce size limit and prevent memory bloat
void
EventQueue::enforceLimit()
{
    // Remove oldest events if queue exceeds maximum size
    while (queue.size() > maxSize_)
    {
        queue.pop(); // Drop oldest events
    }
}

bool
EventQueue::popBatch(std::vector<Event> &events, int batchSize)
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]
        { return !queue.empty(); });

    int actualSize = std::min(batchSize, static_cast<int>(queue.size()));

    // Reserve exact capacity to avoid over-allocation
    events.reserve(events.size() + actualSize);

    while (!queue.empty() && batchSize > 0)
    {
        events.emplace_back(std::move(queue.front())); // Use emplace_back with move
        queue.pop();
        batchSize--;
    }

    return !events.empty();
}

size_t
EventQueue::size()
{
    std::unique_lock<std::mutex> lock(mtx);
    return queue.size();
}

bool
EventQueue::empty()
{
    std::unique_lock<std::mutex> lock(mtx);
    return queue.empty();
}

void
EventQueue::forceCleanup()
{
    std::unique_lock<std::mutex> lock(mtx);
    if (queue.empty())
    {
        // When queue is empty, create a new queue to force memory cleanup
        std::queue<Event> newQueue;
        queue.swap(newQueue);
        // newQueue will be destroyed when it goes out of scope, releasing memory
    }
}