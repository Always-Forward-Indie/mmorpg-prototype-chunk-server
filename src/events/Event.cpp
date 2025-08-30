#include "events/Event.hpp"
#include <variant>

Event::Event(EventType type, int clientID, const EventData &data)
    : type(type), clientID(clientID), eventData(data) {}

Event::Event(EventType type, int clientID, const EventData &data, const TimestampStruct &timestamps)
    : type(type), clientID(clientID), eventData(data), timestamps_(timestamps) {}

Event::Event(const Event &other)
    : type(other.type), clientID(other.clientID), eventData(other.eventData), timestamps_(other.timestamps_) {}

Event::Event(Event &&other) noexcept
    : type(other.type), clientID(other.clientID), eventData(std::move(other.eventData)), timestamps_(other.timestamps_) {}

Event &
Event::operator=(const Event &other)
{
    if (this != &other)
    {
        type = other.type;
        clientID = other.clientID;
        eventData = other.eventData;
        timestamps_ = other.timestamps_;
    }
    return *this;
}

Event &
Event::operator=(Event &&other) noexcept
{
    if (this != &other)
    {
        type = other.type;
        clientID = other.clientID;
        eventData = std::move(other.eventData);
        timestamps_ = other.timestamps_;
    }
    return *this;
}

Event::~Event()
{
    // No need for any checks or assignments here.
}

// Getter for clientID
int
Event::getClientID() const
{
    return clientID;
}

// Getter for data
const EventData &
Event::getData() const
{
    return eventData;
}

// Getter for timestamps
const TimestampStruct &
Event::getTimestamps() const
{
    return timestamps_;
}

// Getter for type
Event::EventType
Event::getType() const
{
    return type;
}