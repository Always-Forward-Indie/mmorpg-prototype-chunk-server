#include "events/Event.hpp"

Event::Event(EventType type, int clientID, int characterID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
    : type(type), clientID(clientID), characterID(characterID), eventData(data), clientSocket(clientSocket)
{
}

// Getter for clientID
int Event::getClientID() const
{
    return clientID;
}

// Getter for characterID
int Event::getCharacterID() const
{
    return characterID;
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

// Getter for clientSocket
std::shared_ptr<boost::asio::ip::tcp::socket> Event::getClientSocket() const
{
    return clientSocket;
}