#include "chunk_server/Event.hpp"

Event::Event(EventType type, int clientID, const EventData &data)
    : type(type), clientID(clientID), eventData(eventData)
{
}

// Getter for clientID
int Event::getClientID() const
{
    return clientID;
}

// Getter for data
const EventData& Event::getData() const
{
    return eventData;
}

// Getter for type
Event::EventType Event::getType() const
{
    return type;
}