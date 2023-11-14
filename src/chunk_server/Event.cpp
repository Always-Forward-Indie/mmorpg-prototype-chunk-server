#include "chunk_server/Event.hpp"

Event::Event(){
    
}

Event::Event(EventType type, int clientID, const std::unordered_map<std::string, int> &data)
    : type(type), clientID(clientID), eventData(eventData)
{

}