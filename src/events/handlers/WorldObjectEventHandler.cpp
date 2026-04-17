#include "events/handlers/WorldObjectEventHandler.hpp"
#include "events/EventData.hpp"
#include "services/DialogueConditionEvaluator.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <algorithm>
#include <cmath>
#include <random>

WorldObjectEventHandler::WorldObjectEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "wio")
{
    log_ = gameServices_.getLogger().getSystem("wio");
}

// =============================================================================
// SET_ALL_WORLD_OBJECTS — game-server → chunk-server
// =============================================================================

void
WorldObjectEventHandler::handleSetAllWorldObjectsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<WorldObjectDataStruct>>(data))
        {
            log_->error("[WIO] handleSetAllWorldObjectsEvent: unexpected data type");
            return;
        }
        auto objects = std::get<std::vector<WorldObjectDataStruct>>(data);
        gameServices_.getWorldObjectManager().setWorldObjects(objects);
    }
    catch (const std::exception &ex)
    {
        log_->error("[WIO] handleSetAllWorldObjectsEvent: {}", ex.what());
    }
}

// =============================================================================
// WORLD_OBJECT_INTERACT — client → chunk-server
// =============================================================================

void
WorldObjectEventHandler::handleInteractEvent(const Event &event)
{
    int clientId = event.getClientID();

    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<WorldObjectInteractRequestStruct>(data))
        {
            log_->error("[WIO] handleInteractEvent: unexpected data type");
            return;
        }
        const auto &req = std::get<WorldObjectInteractRequestStruct>(data);

        const int characterId = req.characterId;
        const int objectId = req.objectId;

        if (characterId <= 0 || objectId <= 0)
        {
            sendInteractResult(clientId, objectId, false, "INVALID_OBJECT");
            return;
        }

        WorldObjectDataStruct obj = gameServices_.getWorldObjectManager().getObjectById(objectId);
        if (obj.id == 0)
        {
            sendInteractResult(clientId, objectId, false, "OBJECT_NOT_FOUND");
            return;
        }

        // ── Range check ────────────────────────────────────────────────────────
        const PositionStruct &pp = req.playerPosition;
        const PositionStruct &op = obj.position;
        const float dx = pp.positionX - op.positionX;
        const float dy = pp.positionY - op.positionY;
        const float dz = pp.positionZ - op.positionZ;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > obj.interactionRadius * 1.25f)
        {
            sendInteractResult(clientId, objectId, false, "TOO_FAR");
            return;
        }

        // ── Level check ────────────────────────────────────────────────────────
        const CharacterDataStruct charData =
            gameServices_.getCharacterManager().getCharacterById(req.characterId);
        if (charData.characterId == 0)
        {
            sendInteractResult(clientId, objectId, false, "CHAR_NOT_FOUND");
            return;
        }
        if (charData.characterLevel < obj.minLevel)
        {
            sendInteractResult(clientId, objectId, false, "LEVEL_TOO_LOW");
            return;
        }

        // ── Build player context for condition evaluation ──────────────────────
        PlayerContextStruct ctx;
        ctx.characterId = charData.characterId;
        ctx.characterLevel = charData.characterLevel;
        ctx.classId = charData.classId;
        for (const auto &flag : charData.flags)
        {
            if (flag.boolValue.has_value())
                ctx.flagsBool[flag.flagKey] = flag.boolValue.value();
            if (flag.intValue.has_value())
                ctx.flagsInt[flag.flagKey] = flag.intValue.value();
        }
        gameServices_.getQuestManager().fillQuestContext(charData.characterId, ctx);
        const auto &inventory = gameServices_.getInventoryManager().getPlayerInventory(charData.characterId);
        for (const auto &inv : inventory)
            ctx.flagsInt["item_" + std::to_string(inv.itemId)] = inv.quantity;
        gameServices_.getReputationManager().fillReputationContext(charData.characterId, ctx);
        gameServices_.getMasteryManager().fillMasteryContext(charData.characterId, ctx);

        // ── Custom condition_group check ───────────────────────────────────────
        if (!obj.conditionGroup.is_null() && !obj.conditionGroup.empty())
        {
            if (!DialogueConditionEvaluator::evaluate(obj.conditionGroup, ctx))
            {
                sendInteractResult(clientId, objectId, false, "CONDITIONS_NOT_MET");
                return;
            }
        }

        // ── Global state check (search / activate / channeled) ─────────────────
        if (obj.scope == "global")
        {
            const std::string gState = gameServices_.getWorldObjectManager().getGlobalState(objectId);
            if (gState == "depleted")
            {
                sendInteractResult(clientId, objectId, false, "DEPLETED");
                return;
            }
            if (gState == "disabled")
            {
                sendInteractResult(clientId, objectId, false, "DISABLED");
                return;
            }
        }

        // ── Per-player state check (examine / per-player channeled) ────────────
        if (obj.scope == "per_player" && obj.objectType != "examine")
        {
            const std::string flagKey = "wio_interacted_" + std::to_string(objectId);
            const bool alreadyDone = ctx.flagsBool.count(flagKey) && ctx.flagsBool.at(flagKey);
            if (alreadyDone)
            {
                sendInteractResult(clientId, objectId, false, "ALREADY_DONE");
                return;
            }
        }

        // ── Route to type handler ─────────────────────────────────────────────
        if (obj.objectType == "examine")
            processExamine(clientId, characterId, obj, ctx);
        else if (obj.objectType == "search")
            processSearch(clientId, characterId, obj, ctx);
        else if (obj.objectType == "activate")
            processActivate(clientId, characterId, obj, ctx);
        else if (obj.objectType == "use_with_item")
            processUseWithItem(clientId, characterId, obj, ctx);
        else if (obj.objectType == "channeled")
            processChanneledStart(clientId, characterId, obj, ctx);
        else
            sendInteractResult(clientId, objectId, false, "UNKNOWN_TYPE");

        // ── Quest progress ─────────────────────────────────────────────────────
        gameServices_.getQuestManager().onWorldObjectInteracted(characterId, objectId);
    }
    catch (const std::exception &ex)
    {
        log_->error("[WIO] handleInteractEvent: {}", ex.what());
    }
}

// =============================================================================
// WORLD_OBJECT_CHANNEL_CANCEL — client → chunk-server
// =============================================================================

void
WorldObjectEventHandler::handleChannelCancelEvent(const Event &event)
{
    int clientId = event.getClientID();

    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<WorldObjectChannelCancelStruct>(data))
            return;

        const auto &req = std::get<WorldObjectChannelCancelStruct>(data);
        gameServices_.getWorldObjectManager().removeChannelSession(req.characterId);

        auto socket = getClientSocket(event);
        if (!socket)
            return;

        ResponseBuilder builder;
        auto resp = builder
                        .setHeader("eventType", "worldObjectChannelCancelled")
                        .setHeader("clientId", clientId)
                        .setHeader("message", "Channel cancelled")
                        .setBody("objectId", req.objectId)
                        .build();
        networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", resp));
    }
    catch (const std::exception &ex)
    {
        log_->error("[WIO] handleChannelCancelEvent: {}", ex.what());
    }
}

// =============================================================================
// Outgoing helpers
// =============================================================================

void
WorldObjectEventHandler::sendWorldObjectsToClient(int clientId, int zoneId)
{
    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket)
        return;

    try
    {
        const auto objects = zoneId > 0
                                 ? gameServices_.getWorldObjectManager().getObjectsInZone(zoneId)
                                 : gameServices_.getWorldObjectManager().getAllObjects();

        nlohmann::json arr = nlohmann::json::array();
        for (const auto &obj : objects)
            arr.push_back(buildObjectJson(obj));

        ResponseBuilder builder;
        auto resp = builder
                        .setHeader("eventType", "spawnWorldObjects")
                        .setHeader("clientId", clientId)
                        .setHeader("message", "World objects list")
                        .setBody("worldObjects", arr)
                        .build();
        networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", resp));
    }
    catch (const std::exception &ex)
    {
        log_->error("[WIO] sendWorldObjectsToClient: {}", ex.what());
    }
}

void
WorldObjectEventHandler::broadcastStateUpdate(int objectId, const std::string &newState, int respawnSec)
{
    try
    {
        ResponseBuilder builder;
        auto resp = builder
                        .setHeader("eventType", "worldObjectStateUpdate")
                        .setHeader("clientId", 0)
                        .setHeader("message", "State changed")
                        .setBody("objectId", objectId)
                        .setBody("state", newState)
                        .setBody("respawnSec", respawnSec)
                        .build();
        const std::string msg = networkManager_.generateResponseMessage("success", resp);

        const auto sockets = gameServices_.getClientManager().getActiveSockets(-1);
        for (const auto &s : sockets)
            if (s)
                networkManager_.sendResponse(s, msg);
    }
    catch (const std::exception &ex)
    {
        log_->error("[WIO] broadcastStateUpdate: {}", ex.what());
    }
}

void
WorldObjectEventHandler::tick()
{
    // ── Complete finished channel sessions ─────────────────────────────────
    auto done = gameServices_.getWorldObjectManager().pollCompletedChannels();
    for (const auto &[charId, objId] : done)
        completeChannel(charId, objId);

    // ── Respawn depleted global objects ────────────────────────────────────
    auto respawned = gameServices_.getWorldObjectManager().tickRespawns();
    for (int objId : respawned)
    {
        const WorldObjectDataStruct obj =
            gameServices_.getWorldObjectManager().getObjectById(objId);
        broadcastStateUpdate(objId, "active", 0);
        log_->info("[WIO] Object {} respawned", objId);
    }
}

// =============================================================================
// Type-specific interaction helpers
// =============================================================================

void
WorldObjectEventHandler::processExamine(
    int clientId, int characterId, const WorldObjectDataStruct &obj, PlayerContextStruct &ctx)
{
    if (obj.dialogueId <= 0)
    {
        // No dialogue — just ack success
        sendInteractResult(clientId, obj.id, true);
        return;
    }

    const auto *dialogue = gameServices_.getDialogueManager().getDialogueById(obj.dialogueId);
    if (!dialogue)
    {
        sendInteractResult(clientId, obj.id, false, "NO_DIALOGUE");
        return;
    }

    // Use negative npcId to distinguish WIO sessions from NPC sessions.
    const int syntheticNpcId = -obj.id;

    gameServices_.getDialogueSessionManager().closeSessionByCharacter(characterId);
    DialogueSessionStruct &session = gameServices_.getDialogueSessionManager().createSession(
        clientId, characterId, syntheticNpcId, obj.dialogueId, dialogue->startNodeId);

    // Let DialogueEventHandler helpers drive the first node send.
    // We just signal success — the dialogue packet will follow from DialogueEventHandler.
    sendInteractResult(clientId, obj.id, true);
    (void)session; // session used by DialogueSessionManager; may be needed by handler later
}

void
WorldObjectEventHandler::processSearch(
    int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct & /*ctx*/)
{
    // Roll loot directly into player inventory
    nlohmann::json lootItems = nlohmann::json::array();

    if (obj.lootTableId > 0)
    {
        auto lootTable =
            gameServices_.getItemManager().getLootForMob(obj.lootTableId);

        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (const auto &entry : lootTable)
        {
            if (dist(rng) <= entry.dropChance)
            {
                std::uniform_int_distribution<int> qtyDist(entry.minQuantity, entry.maxQuantity);
                int qty = qtyDist(rng);
                gameServices_.getInventoryManager().addItemToInventory(characterId, entry.itemId, qty);
                lootItems.push_back({{"itemId", entry.itemId}, {"quantity", qty}});
            }
        }
    }

    // Deplete global-scope objects
    if (obj.scope == "global")
    {
        gameServices_.getWorldObjectManager().depleteGlobalObject(obj.id);
        broadcastStateUpdate(obj.id, "depleted", obj.respawnSec);
    }
    else
    {
        // Per-player: set flag so it can't be re-searched
        gameServices_.getQuestManager().setFlagBool(
            characterId, "wio_interacted_" + std::to_string(obj.id), true);
    }

    sendInteractResult(clientId, obj.id, true, "", {{"lootItems", lootItems}});
}

void
WorldObjectEventHandler::processActivate(
    int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct & /*ctx*/)
{
    if (obj.scope == "global")
    {
        gameServices_.getWorldObjectManager().depleteGlobalObject(obj.id);
        broadcastStateUpdate(obj.id, "depleted", obj.respawnSec);
    }
    else
    {
        gameServices_.getQuestManager().setFlagBool(
            characterId, "wio_interacted_" + std::to_string(obj.id), true);
    }

    sendInteractResult(clientId, obj.id, true);
}

void
WorldObjectEventHandler::processUseWithItem(
    int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct & /*ctx*/)
{
    if (obj.requiredItemId <= 0)
    {
        sendInteractResult(clientId, obj.id, false, "NO_REQUIRED_ITEM");
        return;
    }
    if (!gameServices_.getInventoryManager().hasItem(characterId, obj.requiredItemId))
    {
        sendInteractResult(clientId, obj.id, false, "ITEM_NOT_IN_INVENTORY");
        return;
    }

    // Consume the required item
    gameServices_.getInventoryManager().removeItemFromInventory(characterId, obj.requiredItemId, 1);

    if (obj.scope == "global")
    {
        gameServices_.getWorldObjectManager().depleteGlobalObject(obj.id);
        broadcastStateUpdate(obj.id, "depleted", obj.respawnSec);
    }
    else
    {
        gameServices_.getQuestManager().setFlagBool(
            characterId, "wio_interacted_" + std::to_string(obj.id), true);
    }

    sendInteractResult(clientId, obj.id, true);
}

void
WorldObjectEventHandler::processChanneledStart(
    int clientId, int characterId, const WorldObjectDataStruct &obj, const PlayerContextStruct & /*ctx*/)
{
    if (obj.channelTimeSec <= 0)
    {
        // Degenerate: treat as instant
        processActivate(clientId, characterId, obj, {});
        return;
    }

    // Cancel any existing channel for this character first
    gameServices_.getWorldObjectManager().removeChannelSession(characterId);
    gameServices_.getWorldObjectManager().startChannelSession(characterId, obj.id, obj.channelTimeSec);

    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket)
        return;

    ResponseBuilder builder;
    auto resp = builder
                    .setHeader("eventType", "worldObjectInteractResult")
                    .setHeader("clientId", clientId)
                    .setHeader("message", "Channeling started")
                    .setBody("objectId", obj.id)
                    .setBody("success", true)
                    .setBody("interactionType", std::string("channeled"))
                    .setBody("channelTimeSec", obj.channelTimeSec)
                    .build();
    networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", resp));
}

void
WorldObjectEventHandler::completeChannel(int characterId, int objectId)
{
    WorldObjectDataStruct obj =
        gameServices_.getWorldObjectManager().getObjectById(objectId);

    if (obj.id == 0)
        return;

    int clientId = gameServices_.getClientManager().getClientDataByCharacterId(characterId).clientId;
    if (clientId <= 0)
        return;

    if (obj.scope == "global")
    {
        gameServices_.getWorldObjectManager().depleteGlobalObject(objectId);
        broadcastStateUpdate(objectId, "depleted", obj.respawnSec);
    }
    else
    {
        gameServices_.getQuestManager().setFlagBool(
            characterId, "wio_interacted_" + std::to_string(objectId), true);
    }

    gameServices_.getQuestManager().onWorldObjectInteracted(characterId, objectId);

    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket)
        return;

    ResponseBuilder builder;
    auto resp = builder
                    .setHeader("eventType", "worldObjectInteractResult")
                    .setHeader("clientId", clientId)
                    .setHeader("message", "Channel complete")
                    .setBody("objectId", objectId)
                    .setBody("success", true)
                    .setBody("interactionType", std::string("channeled_complete"))
                    .build();
    networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", resp));
}

// =============================================================================
// Private helpers
// =============================================================================

nlohmann::json
WorldObjectEventHandler::buildObjectJson(const WorldObjectDataStruct &obj) const
{
    const std::string state =
        obj.scope == "global"
            ? gameServices_.getWorldObjectManager().getGlobalState(obj.id)
            : "active"; // per_player state is not broadcast globally

    return {
        {"id", obj.id},
        {"slug", obj.slug},
        {"nameKey", obj.nameKey},
        {"objectType", obj.objectType},
        {"scope", obj.scope},
        {"posX", obj.position.positionX},
        {"posY", obj.position.positionY},
        {"posZ", obj.position.positionZ},
        {"rotZ", obj.position.rotationZ},
        {"zoneId", obj.zoneId},
        {"interactionRadius", obj.interactionRadius},
        {"channelTimeSec", obj.channelTimeSec},
        {"respawnSec", obj.respawnSec},
        {"minLevel", obj.minLevel},
        {"dialogueId", obj.dialogueId},
        {"requiredItemId", obj.requiredItemId},
        {"currentState", state},
    };
}

void
WorldObjectEventHandler::sendInteractResult(
    int clientId, int objectId, bool success, const std::string &errorCode, const nlohmann::json &extra)
{
    auto socket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!socket)
        return;

    ResponseBuilder builder;
    builder.setHeader("eventType", "worldObjectInteractResult")
        .setHeader("clientId", clientId)
        .setHeader("message", success ? "OK" : errorCode)
        .setBody("objectId", objectId)
        .setBody("success", success);

    if (!errorCode.empty())
        builder.setBody("errorCode", errorCode);

    if (!extra.is_null() && extra.is_object())
    {
        for (const auto &[key, val] : extra.items())
            builder.setBody(key, val);
    }

    auto resp = builder.build();
    networkManager_.sendResponse(socket, networkManager_.generateResponseMessage("success", resp));
}
