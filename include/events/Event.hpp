#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <variant>
#include <boost/asio.hpp>
#include "data/DataStructs.hpp"

// Define the types of data that can be sent in an event
using EventData = std::variant<int, float, std::string, PositionStruct, CharacterDataStruct, ClientDataStruct /* other types */>;

class Event {
public:
    enum EventType { 
        JOIN_GAME, 
        DISCONNECT_CLIENT, 
        PING_CLIENT, 
        GET_CONNECTED_CHARACTERS, 
        CHARACTER_MOVEMENT, 
        CHARACTER_INTERACT 
    }; // Define more event types as needed
    Event() = default; // Default constructor
    Event(EventType type, int clientID, int characterID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);

    // Get Event Data
    const EventData getData() const;
    // Get Client ID
    int getClientID() const;
    // Get Character ID
    int getCharacterID() const;
    // Get Client Socket
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket() const;
    //Get Event Type
    EventType getType() const;

private:
    int clientID;
    int characterID;
    EventType type;
    EventData eventData;
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket;
};