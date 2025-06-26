#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <variant>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include "data/DataStructs.hpp"

// Define the types of data that can be sent in an event
using EventData = std::variant<
    int, 
    float, 
    std::string, 
    nlohmann::json, 
    PositionStruct, 
    CharacterDataStruct, 
    ClientDataStruct, 
    SpawnZoneStruct, 
    MobDataStruct,
    ChunkInfoStruct,
    std::vector<MobDataStruct>, 
    std::vector<SpawnZoneStruct>,
    std::vector<MobAttributeStruct>,
    std::vector<CharacterDataStruct>,
    std::vector<CharacterAttributeStruct>,
    std::vector<ClientDataStruct>
/* other types */>;

class Event {
public:
    enum EventType { 
        PING_CLIENT,
        SET_CHUNK_DATA,
        JOIN_CLIENT, 
        JOIN_CLIENT_CHUNK, 
        SET_CHARACTER_DATA,
        SET_CHARACTER_ATTRIBUTES,
        DISCONNECT_CLIENT, 
        DISCONNECT_CLIENT_CHUNK, 
        SET_CONNECTED_CHARACTERS_LIST,
        GET_CONNECTED_CHARACTERS_CHUNK, 
        MOVE_CHARACTER_CLIENT, 
        LEAVE_GAME_CLIENT, 
        LEAVE_GAME_CHUNK, 
        GET_SPAWN_ZONE_DATA,
        GET_MOB_DATA,
        SET_ALL_SPAWN_ZONES,
        SET_ALL_MOBS_LIST,
        SET_ALL_MOBS_ATTRIBUTES,
        SPAWN_MOBS_IN_ZONE,
        SPAWN_ZONE_MOVE_MOBS,
        MOVE_MOB
    }; // Define more event types as needed
    Event() = default; // Default constructor
    Event(EventType type, int clientID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> currentSocket);

    // Get Event Data
    const EventData getData() const;
    // Get Client ID
    int getClientID() const;
    // Get Client Socket
    std::shared_ptr<boost::asio::ip::tcp::socket> getSocket() const;
    //Get Event Type
    EventType getType() const;

private:
    int clientID;
    EventType type;
    EventData eventData;
    std::shared_ptr<boost::asio::ip::tcp::socket> currentSocket;
};