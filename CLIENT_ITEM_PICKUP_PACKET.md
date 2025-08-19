# Item Pickup Packet Format

## Overview
This document describes the required format for `itemPickup` packets sent from the client to the chunk server.

## Security Enhancement
The `itemPickup` packet now requires the client to include their `playerId` for additional security verification. The server will:
1. Compare the client-provided `playerId` with the server-side `characterId` from the authenticated session
2. Reject the request if they don't match
3. Log security violations for monitoring

## Packet Format

### Header
```json
{
  "header": {
    "eventType": "itemPickup",
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
    "itemUID": <integer>,      // REQUIRED: UID of the dropped item to pick up
    "posX": <float>,          // OPTIONAL: Player's current X position
    "posY": <float>,          // OPTIONAL: Player's current Y position
    "posZ": <float>           // OPTIONAL: Player's current Z position
  }
}
```

### Complete Example
```json
{
  "header": {
    "eventType": "itemPickup",
    "clientId": 12345,
    "hash": "abc123def456"
  },
  "body": {
    "playerId": 67890,
    "itemUID": 11111,
    "posX": 150.5,
    "posY": 200.0,
    "posZ": 0.0
  }
}
```

## Field Descriptions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `playerId` | integer | Yes | The ID of the player/character attempting to pick up the item. Must match the authenticated character ID on the server. |
| `itemUID` | integer | Yes | The unique identifier of the dropped item to pick up. |
| `posX` | float | No | Player's current X coordinate (used for distance validation). |
| `posY` | float | No | Player's current Y coordinate (used for distance validation). |
| `posZ` | float | No | Player's current Z coordinate (used for distance validation). |

## Server-Side Validation

1. **Authentication Check**: Verifies that the client is authenticated and has a valid session
2. **Player ID Verification**: Compares `playerId` from the packet with the authenticated `characterId`
3. **Item Existence**: Checks if the item with the given `itemUID` exists and can be picked up
4. **Distance Check**: Validates that the player is within pickup range of the item
5. **Inventory Space**: Ensures the player has space in their inventory

## Error Responses

### Security Violation
```json
{
  "header": {
    "status": "error",
    "message": "Security violation: player ID mismatch",
    "eventType": "itemPickup"
  },
  "body": {
    "success": false,
    "errorCode": "SECURITY_VIOLATION"
  }
}
```

### Missing Player ID
```json
{
  "header": {
    "status": "error", 
    "message": "Missing required playerId field",
    "eventType": "itemPickup"
  },
  "body": {
    "success": false,
    "errorCode": "MISSING_PLAYER_ID"
  }
}
```

### Item Not Found
```json
{
  "header": {
    "status": "error",
    "message": "Item not found or already picked up", 
    "eventType": "itemPickup"
  },
  "body": {
    "success": false,
    "errorCode": "ITEM_NOT_FOUND"
  }
}
```

## Success Response

```json
{
  "header": {
    "status": "success",
    "message": "Item picked up successfully",
    "eventType": "itemPickup"
  },
  "body": {
    "success": true,
    "characterId": 67890,
    "droppedItemUID": 11111,
    "itemData": {
      "id": 42,
      "name": "Iron Sword",
      "quantity": 1
    }
  }
}
```

## Implementation Notes

- The client must always include `playerId` in itemPickup requests
- Position data is optional but recommended for better server-side validation
- All numeric fields should be properly typed (integers as integers, floats as floats)
- The server will log all security violations for monitoring purposes
