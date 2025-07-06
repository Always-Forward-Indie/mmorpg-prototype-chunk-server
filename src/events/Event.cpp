#include "events/Event.hpp"
#include <variant>

Event::Event(EventType type, int clientID, const EventData &data)
    : type(type), clientID(clientID), eventData(data) {}

Event::Event(const Event &other)
    : type(other.type), clientID(other.clientID), eventData(other.eventData) {}

Event::Event(Event &&other) noexcept
    : type(other.type), clientID(other.clientID), eventData(std::move(other.eventData)) {}

Event &
Event::operator=(const Event &other)
{
    if (this != &other)
    {
        type = other.type;
        clientID = other.clientID;
        eventData = other.eventData;
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

// Getter for type
Event::EventType
Event::getType() const
{
    return type;
}