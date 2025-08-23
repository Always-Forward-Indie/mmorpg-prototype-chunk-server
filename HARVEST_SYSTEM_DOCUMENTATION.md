# HARVEST SYSTEM GUIDE

## Overview

The Harvest System allows players to collect additional loot from mob corpses after they have been defeated. This system provides a secondary layer of resource gathering that requires player interaction with corpses through a timed harvesting process.

## System Architecture

The harvest system follows SOLID principles and integrates with existing game systems:

### Core Components

1. **HarvestManager** - Main service managing harvest logic
2. **HarvestEventHandler** - Handles client-server communication for harvest events
3. **Data Structures** - New structures to support harvest functionality
4. **Event System Integration** - New events for harvest operations

## Data Structures

### ItemDataStruct (Modified)
```cpp
struct ItemDataStruct {
    // ... existing fields ...
    bool isHarvest = false;  // NEW: Flag indicating harvest-only items
};
```

### MobLootInfoStruct (Modified)
```cpp
struct MobLootInfoStruct {
    // ... existing fields ...
    bool isHarvestOnly = false;  // NEW: Flag for harvest-exclusive loot
};
```

### New Structures

#### HarvestableCorpseStruct
```cpp
struct HarvestableCorpseStruct {
    int mobUID = 0;                  // Unique mob instance that died
    int mobId = 0;                   // Template mob ID
    PositionStruct position;         // Corpse position
    std::chrono::steady_clock::time_point deathTime;  // Death timestamp
    bool hasBeenHarvested = false;   // Harvest status
    int harvestedByCharacterId = 0;  // Who harvested it
    float interactionRadius = 150.0f; // Interaction distance
};
```

#### HarvestRequestStruct
```cpp
struct HarvestRequestStruct {
    int characterId = 0;      // Server-side character ID
    int playerId = 0;         // Client-side player ID
    int corpseUID = 0;        // Target corpse UID
    PositionStruct playerPosition;  // Player position
};
```

#### HarvestProgressStruct
```cpp
struct HarvestProgressStruct {
    int characterId = 0;
    int corpseUID = 0;
    std::chrono::steady_clock::time_point startTime;
    float harvestDuration = 3.0f;     // Harvest time in seconds
    bool isActive = false;
    PositionStruct startPosition;     // Starting position
    float maxMoveDistance = 50.0f;    // Max movement during harvest
};
```

## Events

### New Event Types
- `HARVEST_START_REQUEST` - Client requests to start harvesting
- `HARVEST_START_RESPONSE` - Server response with harvest info
- `HARVEST_PROGRESS_UPDATE` - Server updates harvest progress
- `HARVEST_COMPLETE` - Harvest completed successfully
- `HARVEST_CANCELLED` - Harvest was cancelled/interrupted
- `GET_NEARBY_CORPSES` - Get harvestable corpses near player

## Core Workflow

### 1. Mob Death and Corpse Registration
```cpp
// When a mob dies (in ItemEventHandler)
gameServices_.getLootManager().generateLootOnMobDeath(mobId, mobUID, position);
gameServices_.getHarvestManager().registerCorpse(mobUID, mobId, position);
```

### 2. Client Harvest Request
```json
{
  "eventType": "harvestStart",
  "body": {
    "corpseUID": 12345,
    "characterId": 67890
  }
}
```

### 3. Server Validation and Response
- Distance validation (player must be within interaction radius)
- Corpse availability check (not already harvested)
- Player state validation (not already harvesting)

### 4. Harvest Progress
- Scheduled task updates harvest progress every second
- Sends progress updates to client
- Completes harvest after duration expires
- Generates and awards harvest-specific loot

### 5. Harvest Completion
```json
{
  "eventType": "harvestComplete",
  "body": {
    "success": true,
    "corpseUID": 12345,
    "items": [
      {
        "itemId": 101,
        "quantity": 2,
        "name": "Beast Hide"
      }
    ]
  }
}
```

## Client-Server Communication

### 1. Start Harvest
**Client → Server:**
```json
{
  "eventType": "harvestStart",
  "body": {
    "corpseUID": 12345
  }
}
```

**Server → Client:**
```json
{
  "status": "success",
  "message": "Harvest started successfully",
  "body": {
    "success": true,
    "corpseUID": 12345,
    "harvestDuration": 3.0
  }
}
```

### 2. Get Nearby Corpses
**Client → Server:**
```json
{
  "eventType": "getNearbyCorpses"
}
```

**Server → Client:**
```json
{
  "status": "success",
  "body": {
    "corpses": [
      {
        "mobUID": 12345,
        "mobId": 10,
        "position": {"x": 100, "y": 200, "z": 0},
        "hasBeenHarvested": false,
        "interactionRadius": 150.0
      }
    ],
    "count": 1
  }
}
```

### 3. Cancel Harvest
**Client → Server:**
```json
{
  "eventType": "harvestCancel"
}
```

## System Configuration

### Default Values
- **Harvest Duration:** 3.0 seconds
- **Interaction Radius:** 150.0 units
- **Max Move Distance:** 50.0 units (during harvest)
- **Corpse Cleanup Time:** 600 seconds (10 minutes)

### Scheduled Tasks
- **Harvest Progress Update:** Every 1 second
- **Corpse Cleanup:** Every 10 seconds (checks for old corpses)

## Integration Points

### 1. Mob Death Integration
Located in `ItemEventHandler::handleMobLootGenerationEvent()`:
```cpp
// After generating normal loot
gameServices_.getHarvestManager().registerCorpse(mobUID, mobId, position);
```

### 2. Event System Integration
- EventDispatcher routes harvest requests to events
- EventHandler dispatches to HarvestEventHandler
- HarvestEventHandler processes and responds to clients

### 3. Scheduler Integration
Located in `ChunkServer` constructor:
```cpp
// Harvest update task
Task harvestUpdateTask([&] {
    gameServices_.getHarvestManager().updateHarvestProgress();
    gameServices_.getHarvestManager().cleanupOldCorpses(600);
}, 1, std::chrono::system_clock::now(), 7);
```

## Error Handling

### Validation Failures
- **Distance Check:** "Too far from corpse"
- **Already Harvested:** "Corpse has already been harvested"
- **Player Busy:** "Already harvesting"
- **Corpse Not Found:** "Corpse not found"

### Interruption Conditions
- Player moves too far from starting position
- Player disconnects
- Manual cancellation
- System errors

## Database Considerations

### Required Database Updates

1. **Items Table:**
```sql
ALTER TABLE items ADD COLUMN is_harvest BOOLEAN DEFAULT FALSE;
```

2. **Mob Loot Table:**
```sql
ALTER TABLE mob_loot ADD COLUMN is_harvest_only BOOLEAN DEFAULT FALSE;
```

### Example Harvest Items
```sql
INSERT INTO items (name, slug, is_harvest, ...) VALUES 
('Beast Hide', 'beast_hide', TRUE, ...),
('Monster Bone', 'monster_bone', TRUE, ...),
('Rare Scale', 'rare_scale', TRUE, ...);

INSERT INTO mob_loot (mob_id, item_id, drop_chance, is_harvest_only) VALUES
(10, 101, 0.8, TRUE),  -- Beast Hide from Wolf
(10, 102, 0.3, TRUE),  -- Monster Bone from Wolf
(15, 103, 0.1, TRUE);  -- Rare Scale from Dragon
```

## Performance Considerations

### Memory Management
- Corpses are automatically cleaned up after 10 minutes
- Active harvest sessions are limited by player count
- Efficient spatial queries for nearby corpse detection

### Network Optimization
- Progress updates sent only during active harvests
- Nearby corpse queries cached per region
- Minimal data in progress update packets

## Future Enhancements

### Planned Features
1. **Skill-Based Harvesting:** Different success rates based on player skills
2. **Tool Requirements:** Specific tools needed for different harvest types
3. **Harvest Recipes:** Complex harvesting requiring multiple materials
4. **Quality Variations:** Different quality harvests based on timing/skill

### Extensibility Points
- HarvestManager can be extended for complex harvest rules
- Event system easily supports new harvest-related events
- Database schema allows for additional harvest metadata

## Testing

### Unit Test Coverage
- HarvestManager validation logic
- Distance calculations
- Harvest timing mechanics
- Loot generation algorithms

### Integration Test Scenarios
- Full harvest workflow from mob death to completion
- Multiple players harvesting different corpses
- Harvest interruption and resumption
- Network disconnection during harvest

## Security Considerations

### Anti-Cheat Measures
- Server-side position validation
- Harvest timing verification
- Player ID consistency checks
- Rate limiting on harvest requests

### Data Validation
- All client requests validated server-side
- Position data sanitized
- Corpse availability double-checked
- Loot generation done server-side only

---

## Quick Start Guide

1. **Kill a mob** - Normal loot drops immediately
2. **Approach corpse** - Get within 150 units
3. **Request nearby corpses** - Client sends `getNearbyCorpses`
4. **Start harvest** - Client sends `harvestStart` with corpseUID
5. **Wait for completion** - Server processes harvest over 3 seconds
6. **Receive loot** - Harvest-specific items added to inventory

The system is now fully integrated and ready for use!
