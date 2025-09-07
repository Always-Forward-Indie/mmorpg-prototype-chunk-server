#pragma once
#include "data/DataStructs.hpp"
#include "events/EventData.hpp"
#include <boost/asio.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

class Event
{
  public:
    enum EventType
    {
        PING_CLIENT,
        SET_CHUNK_DATA,
        GET_CONNECTED_CLIENTS,
        JOIN_CLIENT,
        JOIN_CHARACTER,
        SET_CHARACTER_DATA,
        SET_CHARACTER_ATTRIBUTES,
        DISCONNECT_CLIENT,
        SET_CONNECTED_CHARACTERS,
        GET_CONNECTED_CHARACTERS,
        MOVE_CHARACTER,
        LEAVE_GAME_CLIENT,
        LEAVE_GAME_CHUNK,
        GET_SPAWN_ZONE_DATA,
        GET_MOB_DATA,
        SET_ALL_SPAWN_ZONES,
        SET_ALL_MOBS_LIST,
        SET_ALL_MOBS_ATTRIBUTES,
        SET_ALL_MOBS_SKILLS, // Event to set skills for all mobs
        SET_ALL_ITEMS_LIST,
        SET_MOB_LOOT_INFO,
        SET_EXP_LEVEL_TABLE,
        SPAWN_MOBS_IN_ZONE,
        SPAWN_ZONE_MOVE_MOBS,
        MOVE_MOB,
        MOB_DEATH,           // Event to notify clients about mob death/removal
        MOB_TARGET_LOST,     // Event to notify clients when mob loses target
        MOB_LOOT_GENERATION, // Event to generate loot when mob dies
        // Item and loot events
        ITEM_DROP,
        ITEM_PICKUP,
        GET_NEARBY_ITEMS,
        INVENTORY_UPDATE,
        GET_PLAYER_INVENTORY,
        // Harvest system events
        HARVEST_START_REQUEST,   // Client requests to start harvesting
        HARVEST_START_RESPONSE,  // Server response with harvest info
        HARVEST_PROGRESS_UPDATE, // Server updates harvest progress
        HARVEST_COMPLETE,        // Harvest completed successfully
        HARVEST_CANCELLED,       // Harvest was cancelled/interrupted
        GET_NEARBY_CORPSES,      // Get harvestable corpses near player
        CORPSE_LOOT_PICKUP,      // Client requests to pickup specific items from corpse loot
        CORPSE_LOOT_INSPECT,     // Client requests to inspect available loot in corpse
        // Combat events
        INITIATE_COMBAT_ACTION,
        COMPLETE_COMBAT_ACTION,
        INTERRUPT_COMBAT_ACTION,
        COMBAT_ANIMATION,
        COMBAT_RESULT,
        // New attack events
        PLAYER_ATTACK,
        AI_ATTACK,
        ATTACK_TARGET_SELECTION,
        ATTACK_SEQUENCE_START,
        ATTACK_SEQUENCE_COMPLETE,
        // Experience events
        EXPERIENCE_GRANT,  // Grant experience to character
        EXPERIENCE_REMOVE, // Remove experience from character
        EXPERIENCE_UPDATE, // Update experience (broadcast to clients)
        LEVEL_UP,          // Level up event
        // Skill events
        INITIALIZE_PLAYER_SKILLS // Initialize player skills on client connection
    }; // Define more event types as needed

    Event() = default; // Default constructor
    Event(EventType type, int clientID, const EventData &data);
    Event(EventType type, int clientID, const EventData &data, const TimestampStruct &timestamps);

    // Copy constructor with validation
    Event(const Event &other);

    // Move constructor
    Event(Event &&other) noexcept;

    // Copy assignment with validation
    Event &operator=(const Event &other);

    // Move assignment
    Event &operator=(Event &&other) noexcept;

    // Destructor with safety checks
    ~Event();

    // Get Event Data
    const EventData &getData() const;
    // Get Client ID
    int getClientID() const;
    // Get Event Type
    EventType getType() const;
    // Get timestamps
    const TimestampStruct &getTimestamps() const;

  private:
    int clientID;
    EventType type;
    EventData eventData;
    TimestampStruct timestamps_;
};