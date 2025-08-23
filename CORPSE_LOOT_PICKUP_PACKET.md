# Corpse Loot Pickup Packet Format

## Overview
This document describes the required format for `corpseLootPickup` packets sent from the client to the chunk server. This packet is used to pick up specific items from corpse loot after a harvest has been completed.

## Workflow
1. Client completes harvest (receives `HARVEST_COMPLETE` response with `availableLoot`)
2. Client decides which items to pick up from the loot
3. Client sends `corpseLootPickup` packet with specific items and quantities
4. Server validates the request and attempts to add items to player inventory
5. Server responds with success/failure and remaining loot information

## Security Enhancement
The `corpseLootPickup` packet requires the client to include their `playerId` for additional security verification. The server will:
1. Compare the client-provided `playerId` with the server-side `characterId` from the authenticated session
2. Reject the request if they don't match
3. Log security violations for monitoring

## Packet Format

### Header
```json
{
  "header": {
    "eventType": "corpseLootPickup",
    "clientId": <integer>,
    "hash": "<string>"
  }
}
```

### Body
```json
{
  "body": {
    "playerId": <integer>,     // REQUIRED: Must match server-side characterId
    "corpseUID": <integer>,    // REQUIRED: UID of the corpse to pickup loot from
    "requestedItems": [        // REQUIRED: Array of items to pickup
      {
        "itemId": <integer>,   // REQUIRED: Item ID to pickup
        "quantity": <integer>  // REQUIRED: Quantity to pickup (must be > 0)
      }
    ]
  }
}
```

### Complete Example
```json
{
  "header": {
    "eventType": "corpseLootPickup",
    "clientId": 12345,
    "hash": "abc123def456"
  },
  "body": {
    "playerId": 67890,
    "corpseUID": 11111,
    "requestedItems": [
      {
        "itemId": 101,
        "quantity": 1
      },
      {
        "itemId": 102,
        "quantity": 3
      }
    ]
  }
}
```

## Field Descriptions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `playerId` | integer | Yes | The ID of the player/character attempting to pick up loot. Must match the authenticated character ID on the server. |
| `corpseUID` | integer | Yes | The unique identifier of the corpse to pick up loot from. |
| `requestedItems` | array | Yes | Array of items the player wants to pick up from the corpse loot. |
| `requestedItems[].itemId` | integer | Yes | The item ID to pick up. |
| `requestedItems[].quantity` | integer | Yes | The quantity of the item to pick up. Must be greater than 0. |

## Server-Side Validation

1. **Authentication Check**: Verifies that the client is authenticated and has a valid session
2. **Player ID Verification**: Compares `playerId` from the packet with the authenticated `characterId`
3. **Corpse Existence**: Checks if the corpse with the given `corpseUID` exists
4. **Harvest Status**: Verifies that the corpse has been harvested and has loot available
5. **Distance Check**: Validates that the player is within pickup range of the corpse
6. **Loot Availability**: Ensures the requested items exist in the corpse loot with sufficient quantities
7. **Inventory Space**: Ensures the player has space in their inventory for the requested items

## Error Responses

### Security Violation
```json
{
  "header": {
    "status": "error",
    "message": "Security violation: player ID mismatch",
    "eventType": "corpseLootPickup"
  },
  "body": {
    "success": false,
    "errorCode": "SECURITY_VIOLATION"
  }
}
```

### Corpse Not Found
```json
{
  "header": {
    "status": "error",
    "message": "Corpse not found",
    "eventType": "corpseLootPickup"
  },
  "body": {
    "success": false,
    "errorCode": "CORPSE_NOT_FOUND"
  }
}
```

### Pickup Failed
```json
{
  "header": {
    "status": "error",
    "message": "Failed to pickup items",
    "eventType": "corpseLootPickup"
  },
  "body": {
    "success": false,
    "errorCode": "PICKUP_FAILED",
    "corpseUID": 11111
  }
}
```

## Success Response

```json
{
  "header": {
    "status": "success",
    "message": "Items picked up successfully",
    "eventType": "corpseLootPickup"
  },
  "body": {
    "success": true,
    "corpseUID": 11111,
    "itemsPickedUp": 2,
    "pickedUpItems": [
      {
        "itemId": 101,
        "itemSlug": "iron_sword",
        "quantity": 1,
        "name": "Iron Sword",
        "description": "A sturdy iron sword",
        "rarityId": 1,
        "rarityName": "Common",
        "itemType": "Weapon",
        "weight": 2.5
      }
    ],
    "remainingLoot": [
      {
        "itemId": 103,
        "itemSlug": "leather_boots",
        "quantity": 1,
        "name": "Leather Boots"
      }
    ]
  }
}
```

## Implementation Notes

- The client must always include `playerId` in corpse loot pickup requests
- The server will attempt to pick up as much as possible - if only partial quantities are available or can fit in inventory, it will pick up what it can
- Items that cannot be picked up (due to inventory space or other issues) will remain in the corpse loot
- If all loot is picked up from a corpse, the loot data for that corpse is removed from the server
- The server logs all pickup attempts for monitoring and debugging purposes
- Distance validation uses the same radius as harvest interaction (typically 150 units)

## Related Systems

- **Harvest System**: Corpse loot is generated when harvest completes successfully
- **Inventory System**: Picked up items are added to the player's inventory
- **Item System**: Item data is fetched and included in responses
