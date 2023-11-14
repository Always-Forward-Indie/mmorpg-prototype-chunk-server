#pragma once

#include <string>
#include <unordered_map>
#include <mutex>


class Event {
public:
    enum EventType { MOVE, INTERACT }; // Define more event types as needed
    Event(); // Default constructor
    Event(EventType type, int clientID, const std::unordered_map<std::string, int>& data);

    // ... Getter methods for type, playerID, and data

    //Get Event Type
    EventType getType() const {
        return type;
    }

private:
    EventType type;
    int clientID;
    std::unordered_map<std::string, int> eventData; // Example data structure; adjust as needed
};