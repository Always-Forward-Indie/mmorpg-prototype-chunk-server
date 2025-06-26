#include "events/Event.hpp"

Event::Event(EventType type, int clientID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> currentSocket)
    : type(type), clientID(clientID), eventData(data), currentSocket(currentSocket)
{
}

// Getter for clientID
int Event::getClientID() const
{
    return clientID;
}

// Getter for data
const EventData Event::getData() const
{
    return eventData;
}

// Getter for type
Event::EventType Event::getType() const
{
    return type;
}

// Getter for currentSocket
std::shared_ptr<boost::asio::ip::tcp::socket> Event::getSocket() const
{
    return currentSocket;
}