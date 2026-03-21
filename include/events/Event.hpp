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
        SET_ALL_MOBS_SKILLS,     // Event to set skills for all mobs
        SET_ALL_NPCS_LIST,       // Event to receive NPC data from game server
        SET_ALL_NPCS_ATTRIBUTES, // Event to receive NPC attributes from game server
        SET_ALL_ITEMS_LIST,
        SET_MOB_LOOT_INFO,
        SET_EXP_LEVEL_TABLE,
        SPAWN_MOBS_IN_ZONE,
        SPAWN_ZONE_MOVE_MOBS,
        MOVE_MOB,
        MOB_MOVE_UPDATE,     // Lightweight per-tick position+velocity update (replaces SPAWN_ZONE_MOVE_MOBS for movement)
        MOB_DEATH,           // Event to notify clients about mob death/removal
        MOB_TARGET_LOST,     // Event to notify clients when mob loses target
        MOB_HEALTH_UPDATE,   // Event to notify clients when mob HP changes (e.g. leash regen)
        MOB_LOOT_GENERATION, // Event to generate loot when mob dies
        // Item and loot events
        ITEM_DROP,
        ITEM_DROP_BY_PLAYER, // Player drops item from inventory onto the ground
        ITEM_PICKUP,
        ITEM_REMOVE, // Broadcast: one or more ground items have been removed (cleanup/pickup)
        GET_NEARBY_ITEMS,
        INVENTORY_UPDATE,
        GET_PLAYER_INVENTORY,
        USE_ITEM, // Player uses an item (potion, scroll, food, etc.)
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
        INITIALIZE_PLAYER_SKILLS, // Initialize player skills on client connection
        // Dialogue events (game-server → chunk-server, static data load)
        SET_ALL_DIALOGUES,                // Load all dialogue graphs from game-server
        SET_NPC_DIALOGUE_MAPPINGS,        // Load all npc_dialogue bindings
        SET_ALL_QUESTS,                   // Load all quest static definitions
        SET_PLAYER_QUESTS,                // Load active quests for a specific character (on join)
        SET_PLAYER_FLAGS,                 // Load player flags for a specific character (on join)
        SET_PLAYER_ACTIVE_EFFECTS,        // Load non-expired active effects for a character (on join)
        SET_PLAYER_INVENTORY,             // Load player inventory items from DB (on join)
        SET_CHARACTER_ATTRIBUTES_REFRESH, // Updated attributes after level-up or equip change
        INVENTORY_ITEM_ID_SYNC,           // Game server sends back assigned player_inventory.id after upsert
        // Dialogue events (client → chunk-server)
        NPC_INTERACT,    // Player clicks on NPC to start dialogue
        DIALOGUE_CHOICE, // Player selects a dialogue edge
        DIALOGUE_CLOSE,  // Player closes dialogue
        // Persistence events (chunk-server → game-server)
        UPDATE_PLAYER_QUEST_PROGRESS, // Flush quest progress to DB
        UPDATE_PLAYER_FLAG,           // Flush a flag change to DB
        FLUSH_PLAYER_QUESTS,          // Flush all quests for a character (on disconnect)
        SET_GAME_CONFIG,              // Receive gameplay constants from game-server
        // Vendor / Trade / Repair events (game-server → chunk-server, static data)
        SET_VENDOR_DATA,     // Load vendor NPC inventory from game-server on chunk startup
        VENDOR_STOCK_UPDATE, // Restock notification from game-server scheduler
        // Vendor events (client → chunk-server)
        OPEN_VENDOR_SHOP, // Player opens NPC vendor window
        BUY_ITEM,         // Player buys item from NPC vendor
        SELL_ITEM,        // Player sells item to NPC vendor
        BUY_ITEM_BATCH,   // Player buys multiple items (cart purchase) from NPC vendor
        SELL_ITEM_BATCH,  // Player sells multiple items (cart sell) to NPC vendor
        // Repair shop events (client → chunk-server)
        OPEN_REPAIR_SHOP, // Player opens blacksmith repair window
        REPAIR_ITEM,      // Player repairs one item
        REPAIR_ALL,       // Player repairs all equipped items
        // P2P trade events (client → chunk-server)
        TRADE_REQUEST,      // Initiate P2P trade with another player
        TRADE_ACCEPT,       // Accept incoming trade invite
        TRADE_DECLINE,      // Decline incoming trade invite
        TRADE_OFFER_UPDATE, // Update own offer items/gold in session
        TRADE_CONFIRM,      // Confirm current offer (lock it in)
        TRADE_CANCEL,       // Cancel / abort active trade session
        // Durability event (chunk-server → clients)
        DURABILITY_UPDATE, // Notify client(s) of changed item durabilities
        // Equipment events (client → chunk-server)
        EQUIP_ITEM,    // Player equips an inventory item
        UNEQUIP_ITEM,  // Player removes item from a slot
        GET_EQUIPMENT, // Player requests current equipment state
        // Equipment persistence (chunk-server → game-server)
        SAVE_EQUIPMENT_CHANGE, // Persist equip/unequip to character_equipment table
        // Respawn events
        PLAYER_RESPAWN,              // Client requests respawn after death
        SET_RESPAWN_ZONES,           // Game-server sends respawn zone list to chunk-server
        SET_STATUS_EFFECT_TEMPLATES, // Game-server sends status effect template list to chunk-server
        SET_GAME_ZONES,              // Game-server sends game zone list (AABB + exploration XP) to chunk-server
        SET_PLAYER_PITY,             // Load pity kill counters for a character (on join)
        SET_PLAYER_BESTIARY,         // Load bestiary kill counts for a character (on join)
        GET_BESTIARY_ENTRY,          // Client requests a single bestiary entry for a mob template
        GET_BESTIARY_OVERVIEW,       // Client requests list of all discovered mob IDs with kill counts
        // Stage 3: Champion system
        SET_TIMED_CHAMPION_TEMPLATES, // Game-server sends timed champion template list to chunk-server
        TIMED_CHAMPION_KILLED,        // Chunk-server notifies game-server that a timed champion was killed

        // Stage 4: Reputation system
        SET_PLAYER_REPUTATIONS, // Load reputation values for a character (on join)
        SAVE_REPUTATION,        // Persist a reputation change to game-server

        // Stage 4: Skill Mastery system
        SET_PLAYER_MASTERIES, // Load mastery values for a character (on join)
        SAVE_MASTERY,         // Persist a mastery value change to game-server

        // Stage 4: Zone Events
        SET_ZONE_EVENT_TEMPLATES,       // Game-server sends zone event template list to chunk-server
        SET_MOB_WEAKNESSES_RESISTANCES, // Game-server sends mob weaknesses/resistances tables

        // Chat events (client → chunk-server)
        CHAT_MESSAGE ///< Player sends a chat message (local / zone / whisper)
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