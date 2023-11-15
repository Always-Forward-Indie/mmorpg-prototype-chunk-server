#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <variant>
#include "ClientData.hpp"

// Define the types of data that can be sent in an event
using EventData = std::variant<int, float, std::string, PositionStruct, CharacterDataStruct, ClientDataStruct /* other types */>;

class Event {
public:
    enum EventType { MOVE, INTERACT }; // Define more event types as needed
    Event() = default; // Default constructor
    Event(EventType type, int clientID, const EventData& data);

    // Get Event Data
    const EventData& getData() const;
    // Get Client ID
    int getClientID() const;
    //Get Event Type
    EventType getType() const;

private:
    int clientID;
    EventType type;
    EventData eventData;
};