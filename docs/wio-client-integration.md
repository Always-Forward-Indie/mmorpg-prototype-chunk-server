# World Interactive Objects — Client Integration Guide

> **Server version:** WIO system (migration 043)  
> Audience: Unreal Engine 5 client developer

---

## Overview

World Interactive Objects (WIO) are static, interactable props placed in the game world: treasure chests, ancient altars, lore tablets, levers, etc.  
They are **not** NPCs and **not** mobs, but they can trigger dialogue, drop loot, advance quests, and change state.

### Object types

| `objectType`      | Description |
|-------------------|-------------|
| `examine`         | Read/inspect — opens a dialogue, no depletion |
| `search`          | Rummage — rolls and grants loot, depletes |
| `activate`        | Toggle / trigger — changes global/per-player state |
| `use_with_item`   | Requires a specific item; consumes it on use |
| `channeled`       | Progress bar interaction — depletion after casting |

### Scope

| `scope`       | Meaning |
|---------------|---------|
| `per_player`  | Each player has their own state (flag `wio_interacted_<id>`) |
| `global`      | Shared state broadcast to all players; respawns after `respawnSec` |

---

## Packets — Server → Client

### `spawnWorldObjects` (on join / zone enter)

Sent immediately after `playerReady` is acknowledged by the server.  
Lists all WIO objects relevant to the player's current zone.

```json
{
  "header": {
    "eventType": "spawnWorldObjects",
    "clientId": 42,
    "message": "World objects list"
  },
  "body": {
    "worldObjects": [
      {
        "id": 7,
        "slug": "ancient_altar_01",
        "nameKey": "wio.ancient_altar",
        "objectType": "examine",
        "scope": "per_player",
        "posX": 1234.5,
        "posY": -800.0,
        "posZ": 10.0,
        "rotZ": 45.0,
        "interactionRadius": 250.0,
        "channelTimeSec": 0,
        "state": "active"
      },
      {
        "id": 12,
        "slug": "herb_cache_forest",
        "nameKey": "wio.herb_cache",
        "objectType": "search",
        "scope": "global",
        "posX": 3000.0,
        "posY": 1500.0,
        "posZ": 5.0,
        "rotZ": 0.0,
        "interactionRadius": 150.0,
        "channelTimeSec": 0,
        "state": "depleted"
      }
    ]
  }
}
```

**Client action:** Use the `slug` field to look up the corresponding Blueprint actor class in a data table (e.g. a `DataTable` keyed by slug). Spawn the actor at the given world-space coordinates. The `slug` is the authoritative binding — no `meshId` is sent by the server. Set initial visual state (`state` field: `"active"` / `"depleted"` / `"disabled"`). Depleted and disabled objects should play their inactive appearance / collision variant. A depleted global object that has `respawnSec > 0` can optionally show a countdown.

---

### `worldObjectInteractResult` (response to player interact)

Sent in reply to a `worldObjectInteract` client request.

```json
{
  "header": {
    "eventType": "worldObjectInteractResult",
    "clientId": 42
  },
  "body": {
    "objectId": 7,
    "success": true,
    "errorCode": "",
    "interactionType": "examine",
    "channelTimeSec": 0,
    "lootItems": []
  }
}
```

**`success: false` response:**

```json
{
  "body": {
    "objectId": 12,
    "success": false,
    "errorCode": "DEPLETED",
    "interactionType": "search",
    "channelTimeSec": 0,
    "lootItems": []
  }
}
```

#### Error codes

| Code | Meaning |
|------|---------|
| `TOO_FAR` | Player is outside the object's `interactionRadius` |
| `CHAR_NOT_FOUND` | Server could not find character data (edge case) |
| `LEVEL_TOO_LOW` | Character level is below `minLevel` |
| `CONDITIONS_NOT_MET` | `condition_group` rules not satisfied |
| `DEPLETED` | Global-scope object is currently depleted |
| `ALREADY_DONE` | Per-player object already interacted by this character |
| `DISABLED` | Object has state `"disabled"` |
| `NO_REQUIRED_ITEM` | `use_with_item` but player lacks `requiredItemId` |
| `OBJECT_NOT_FOUND` | Object ID not loaded (config error) |

**For `interactionType: "examine"`** — a `dialogueStart` packet follows immediately (see the Dialogue integration docs).  
**For `interactionType: "search"`** — `lootItems` contains an array of granted item stacks:

```json
"lootItems": [
  { "itemId": 55, "count": 3 }
]
```

**For `interactionType: "channeled"`** — `channelTimeSec > 0` means the client should begin a cast bar. The server will send the result after the channel completes on its side.  When the channel finishes on the server the same `worldObjectInteractResult` arrives again with the final outcome.

---

### `worldObjectChannelCancelled`

Sent to confirm a channel was cancelled (may arrive even if channel had already completed).

```json
{
  "header": { "eventType": "worldObjectChannelCancelled", "clientId": 42 },
  "body": { "objectId": 15 }
}
```

**Client action:** Hide the cast bar.

---

### `worldObjectStateUpdate` (broadcast to all clients)

Sent when a global-scope object changes state: depleted on use, restored after respawn, changed via a dialogue `set_object_state` action.

```json
{
  "header": { "eventType": "worldObjectStateUpdate", "clientId": 0 },
  "body": {
    "objectId": 12,
    "state": "depleted",
    "respawnSec": 300
  }
}
```

| `state` value | Visual cue |
|---------------|-----------|
| `"active"` | Normal interactive appearance |
| `"depleted"` | Inactive variant, inaccessible. Show respawn timer if `respawnSec > 0` |
| `"disabled"` | Hidden or permanently inaccessible |

**`clientId: 0`** means this is a server broadcast, not addressed to a specific client.

---

## Packets — Client → Server

### `worldObjectInteract`

Sent when the player presses the interaction key on a WIO actor.

```json
{
  "header": {
    "eventType": "worldObjectInteract",
    "clientId": 42,
    "hash": "<session_hash>"
  },
  "body": {
    "characterId": 101,
    "objectId": 7
  }
}
```

Send this only when the player is within the visible interaction radius of the object (client-side distance check as first gate). The server will perform an authoritative radius check and may reject with `TOO_FAR`.

---

### `worldObjectChannelCancel`

Send if the player moves, presses Escape, or otherwise cancels a channeled interaction before `channelTimeSec` elapses.

```json
{
  "header": {
    "eventType": "worldObjectChannelCancel",
    "clientId": 42,
    "hash": "<session_hash>"
  },
  "body": {
    "characterId": 101,
    "objectId": 15
  }
}
```

**Do not** send this for non-channeled interactions — it will be silently ignored, but it wastes bandwidth.

---

## Client-side state machine (per-object actor)

```
ACTIVE ──[player interacts]──► INTERACTING
   │                               │
   │                        [result received]
   │                               │
   │         success=false         ▼
   │◄─────────────────────── INTERACTION_FAILED
   │
   │         success=true + type="channeled"
   │──────────────────────────────►CHANNELING
   │                               │
   │         worldObjectChannelCancelled  │
   │◄──────────────────────────────┘
   │                               │ worldObjectInteractResult (final)
   │◄──────────────────────── DEPLETED/DONE
   │
   │  worldObjectStateUpdate state="active"
   └──────────────────────────────► ACTIVE
```

For `per_player` scope, a successfully interacted object should be marked done locally and show the inactive appearance — it will never become "active" again for this character (no `worldObjectStateUpdate` will arrive for per-player objects).

---

## Interaction with Dialogue

When an `examine`-type object is successfully interacted with, the server opens a Dialogue session using a synthetic NPC id of `-objectId` (negative). The standard `dialogueStart` packet follows:

```json
{
  "header": { "eventType": "dialogueStart" },
  "body": {
    "sessionId": "...",
    "npcId": -7,
    "currentNodeId": 1,
    "nodes": [ ... ]
  }
}
```

The client should handle `npcId < 0` as a world object interaction (e.g., show an inspect panel instead of NPC portrait), but otherwise the dialogue flow is identical to NPC dialogue.

Dialogue conditions can check object state:
```json
{ "type": "object_state", "object_id": 12, "state": "active" }
```

Dialogue actions can set object state:
```json
{ "type": "set_object_state", "object_id": 12, "state": "depleted" }
```
When `set_object_state` is triggered from dialogue, a `worldObjectStateUpdate` broadcast is sent to all clients automatically.

---

## Interaction with Quests

An `"interact"` quest step is completed automatically by the server when a successful WIO interaction occurs.  
The client receives the standard `questProgressUpdate` packet — no special handling needed for WIO quests.

Quest step definition (server-side, for reference):
```json
{
  "stepType": "interact",
  "description": "Examine the ancient altar",
  "params": { "object_id": 7, "count": 1 }
}
```

---

## Blueprint Integration Checklist

1. **On `spawnWorldObjects`:** spawn `AWorldInteractiveObject` actors at given coordinates, set `ObjectId`, `ObjectType`, `State`, `InteractionRadius`.
2. **On approach (overlap or proximity):** show interaction prompt widget.
3. **On interact key press:** send `worldObjectInteract`. Disable prompt to prevent spam.
4. **On `worldObjectInteractResult`:**
   - `success=false` → show error toast, re-enable prompt if appropriate (object still active).
   - `success=true, channelTimeSec=0` → play feedback animation, mark as done if per-player.
   - `success=true, channelTimeSec>0` → start cast bar for `channelTimeSec` seconds.
5. **On `worldObjectChannelCancelled`:** hide cast bar.
6. **On `worldObjectStateUpdate`:** find actor by `objectId`, call `SetObjectState(state, respawnSec)`.  
   - `"active"` → reset to interactive appearance.  
   - `"depleted"` → play depletion VFX, switch to inactive mesh, optionally start respawn countdown.  
   - `"disabled"` → hide actor or show permanently disabled state.
7. **On `dialogueStart` with `npcId < 0`:** open WIO inspect panel (not NPC panel).
