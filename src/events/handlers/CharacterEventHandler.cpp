#include "events/handlers/CharacterEventHandler.hpp"
#include "events/EventData.hpp"
#include "events/handlers/ItemEventHandler.hpp"
#include "events/handlers/MobEventHandler.hpp"
#include "events/handlers/NPCEventHandler.hpp"
#include "utils/TimestampUtils.hpp"
#include <algorithm>
#include <cmath>
#include <spdlog/logger.h>

CharacterEventHandler::CharacterEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "character"),
      skillEventHandler_(nullptr),
      npcEventHandler_(nullptr),
      itemEventHandler_(nullptr),
      mobEventHandler_(nullptr)
{
    log_ = gameServices_.getLogger().getSystem("character");
}

void
CharacterEventHandler::setMobEventHandler(MobEventHandler *mobEventHandler)
{
    mobEventHandler_ = mobEventHandler;
}

void
CharacterEventHandler::setSkillEventHandler(SkillEventHandler *skillEventHandler)
{
    skillEventHandler_ = skillEventHandler;
}

void
CharacterEventHandler::setNPCEventHandler(NPCEventHandler *npcEventHandler)
{
    npcEventHandler_ = npcEventHandler;
}

void
CharacterEventHandler::setItemEventHandler(ItemEventHandler *itemEventHandler)
{
    itemEventHandler_ = itemEventHandler;
}

bool
CharacterEventHandler::validateCharacterAuthentication(int clientId, int characterId)
{
    return clientId != 0 && characterId != 0;
}

nlohmann::json
CharacterEventHandler::characterToJson(const CharacterDataStruct &characterData)
{
    // Получаем опыт для текущего уровня (порог текущего уровня из DB)
    int expForCurrentLevel = gameServices_.getExperienceManager().getExperienceForLevelFromGameServer(characterData.characterLevel);

    nlohmann::json characterJson = {
        {"id", characterData.characterId},
        {"name", characterData.characterName},
        {"class", characterData.characterClass},
        {"race", characterData.characterRace},
        {"level", characterData.characterLevel},
        {"exp", {{"current", characterData.characterExperiencePoints}, {"levelStart", expForCurrentLevel}, {"levelEnd", characterData.expForNextLevel}}},
        {"stats", {{"health", {{"current", characterData.characterCurrentHealth}, {"max", characterData.characterMaxHealth}}}, {"mana", {{"current", characterData.characterCurrentMana}, {"max", characterData.characterMaxMana}}}}},
        {"position", {{"x", characterData.characterPosition.positionX}, {"y", characterData.characterPosition.positionY}, {"z", characterData.characterPosition.positionZ}, {"rotationZ", characterData.characterPosition.rotationZ}}},
        {"isDead", characterData.characterCurrentHealth <= 0}};

    // Note: attributes/stats are intentionally omitted here.
    // They are delivered to the owning client via a dedicated stats_update packet
    // (sendStatsUpdate), which includes effective values (base + equipment + effects).
    // Other clients receive only identity/position/health for nameplate rendering.

    return characterJson;
}

void
CharacterEventHandler::handleJoinCharacterEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    const auto &timestamps = event.getTimestamps();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct passedCharacterData = std::get<CharacterDataStruct>(data);

            log_->info("Passed Character ID: " + std::to_string(passedCharacterData.characterId));

            // set client character ID - now handles missing clients automatically
            gameServices_.getClientManager().setClientCharacterId(clientID, passedCharacterData.characterId);

            // Get character data
            CharacterDataStruct characterData = gameServices_.getCharacterManager().getCharacterData(passedCharacterData.characterId);

            // Check if character data exists in local storage
            if (characterData.characterId == 0)
            {
                log_->info("Character ID " + std::to_string(passedCharacterData.characterId) + " not found in local storage, adding to pending requests");

                // Add this request to pending list
                PendingJoinRequest pendingRequest;
                pendingRequest.clientID = clientID;
                pendingRequest.characterId = passedCharacterData.characterId;
                pendingRequest.timestamps = timestamps;
                pendingRequest.clientSocket = clientSocket;

                pendingJoinRequests_[passedCharacterData.characterId].push_back(pendingRequest);

                gameServices_.getLogger().log("Pending join request added for character ID: " + std::to_string(passedCharacterData.characterId) + ", total pending: " + std::to_string(pendingJoinRequests_[passedCharacterData.characterId].size()));
                return;
            }

            // Character data is available, process current request immediately
            log_->info("Character ID " + std::to_string(passedCharacterData.characterId) + " found in local storage, processing immediately");

            // Prepare character data in json format
            nlohmann::json characterJson = characterToJson(characterData);

            // Validate authentication
            if (!validateCharacterAuthentication(clientID, characterData.characterId))
            {
                sendErrorResponseWithTimestamps(clientSocket, "Authentication failed for character!", "joinGameCharacter", clientID, timestamps);
                return;
            }

            // Prepare and broadcast success response to all clients (including sender)
            nlohmann::json broadcastResponse = ResponseBuilder()
                                                   .setHeader("message", "Authentication success for character!")
                                                   .setHeader("hash", "")
                                                   .setHeader("clientId", clientID)
                                                   .setHeader("eventType", "joinGameCharacter")
                                                   .setTimestamps(timestamps)
                                                   .setBody("character", characterJson)
                                                   .build();

            broadcastToAllClientsWithTimestamps("success", broadcastResponse, timestamps);

            // Initialize player skills after successful character join
            if (skillEventHandler_)
            {
                skillEventHandler_->initializeFromCharacterData(characterData, clientID, clientSocket);
            }
            else
            {
                log_->error("SkillEventHandler not set in CharacterEventHandler");
            }

            // Send NPC spawn data to player after successful character join
            if (npcEventHandler_)
            {
                npcEventHandler_->sendNPCSpawnDataToClient(clientID, characterData.characterPosition, 50000.0f);
            }
            else
            {
                log_->error("NPCEventHandler not set in CharacterEventHandler");
            }

            // Send existing ground items snapshot to the joining player
            if (itemEventHandler_)
            {
                itemEventHandler_->sendGroundItemsToClient(clientID, clientSocket);
            }

            // Server-push all spawn zones with live mob state so client never needs to request them
            if (mobEventHandler_)
            {
                mobEventHandler_->sendSpawnZonesToClient(clientID, clientSocket);
            }
            else
            {
                log_->error("MobEventHandler not set in CharacterEventHandler");
            }

            // Notify client which game zone it is currently in
            try
            {
                auto zoneOpt = gameServices_.getGameZoneManager().getZoneForPosition(characterData.characterPosition);
                if (zoneOpt.has_value())
                {
                    lastZoneByCharacter_[characterData.characterId] = zoneOpt->id;
                    nlohmann::json zoneData = {
                        {"zoneSlug", zoneOpt->slug},
                        {"minLevel", zoneOpt->minLevel},
                        {"maxLevel", zoneOpt->maxLevel},
                        {"isPvp", zoneOpt->isPvp},
                        {"isSafeZone", zoneOpt->isSafeZone}};
                    gameServices_.getStatsNotificationService().sendWorldNotification(
                        characterData.characterId, "zone_entered", zoneData, "high", "zone_banner");
                }
            }
            catch (const std::exception &ex)
            {
                log_->error("[zone_entered on join] {}", ex.what());
            }

            // Request player quests and flags from game server
            {
                int cid = characterData.characterId;
                nlohmann::json questsReq;
                questsReq["header"]["eventType"] = "getPlayerQuests";
                questsReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(questsReq.dump() + "\n");

                nlohmann::json flagsReq;
                flagsReq["header"]["eventType"] = "getPlayerFlags";
                flagsReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(flagsReq.dump() + "\n");

                nlohmann::json effectsReq;
                effectsReq["header"]["eventType"] = "getPlayerActiveEffects";
                effectsReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(effectsReq.dump() + "\n");

                nlohmann::json inventoryReq;
                inventoryReq["header"]["eventType"] = "getPlayerInventory";
                inventoryReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(inventoryReq.dump() + "\n");

                nlohmann::json pityReq;
                pityReq["header"]["eventType"] = "getPlayerPityData";
                pityReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(pityReq.dump() + "\n");

                nlohmann::json bestiaryReq;
                bestiaryReq["header"]["eventType"] = "getPlayerBestiaryData";
                bestiaryReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(bestiaryReq.dump() + "\n");

                // Stage 4: request reputation and mastery data
                nlohmann::json reputationsReq;
                reputationsReq["header"]["eventType"] = "getPlayerReputationsData";
                reputationsReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(reputationsReq.dump() + "\n");

                nlohmann::json masteriesReq;
                masteriesReq["header"]["eventType"] = "getPlayerMasteriesData";
                masteriesReq["body"]["characterId"] = cid;
                gameServerWorker_.sendDataToGameServer(masteriesReq.dump() + "\n");
            }

            // Also process any pending requests for this character
            processPendingJoinRequests(passedCharacterData.characterId);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleMoveCharacterEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    const auto &timestamps = event.getTimestamps();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (std::holds_alternative<MovementDataStruct>(data))
        {
            const MovementDataStruct &movementData = std::get<MovementDataStruct>(data);

            if (!isPlayerAlive(movementData.characterId))
            {
                sendErrorResponseWithTimestamps(clientSocket, "Cannot move while dead", "moveCharacter", clientID, timestamps);
                return;
            }

            // ── Server-authoritative movement speed validation ────────────────────
            {
                CharacterDataStruct charData = gameServices_.getCharacterManager().getCharacterData(movementData.characterId);
                const int64_t srvNowMs = timestamps.serverRecvMs;

                if (charData.lastMoveSrvMs > 0 && srvNowMs > charData.lastMoveSrvMs)
                {
                    float moveSpeed = 5.0f;
                    bool moveSpeedFound = false;
                    for (const auto &attr : charData.attributes)
                    {
                        if (attr.slug == "move_speed")
                        {
                            moveSpeed = static_cast<float>(attr.value);
                            moveSpeedFound = true;
                            break;
                        }
                    }

                    // move_speed is an abstract stat (DB stores e.g. 5), not world-space units/s.
                    // The client applies the same scale to set MaxWalkSpeed.
                    // Scale factor deduced from logs: ~193 actual units/s / 5 stat = ~38.6.
                    // Adjust MOVE_SPEED_SCALE if the Unreal project changes the conversion formula.
                    static constexpr float MOVE_SPEED_SCALE = 40.0f;
                    const float moveSpeedUnits = moveSpeed * MOVE_SPEED_SCALE;

                    log_->debug("[MOVE_VALIDATE] char {} move_speed_stat={:.1f} (found={}, attrs={}) -> {:.1f} units/s",
                        movementData.characterId,
                        moveSpeed,
                        moveSpeedFound,
                        charData.attributes.size(),
                        moveSpeedUnits);

                    const float deltaMs = static_cast<float>(srvNowMs - charData.lastMoveSrvMs);
                    const float maxDist = moveSpeedUnits * (deltaMs / 1000.0f) * 1.3f;
                    const float dx = movementData.position.positionX - charData.lastValidatedPosition.positionX;
                    const float dy = movementData.position.positionY - charData.lastValidatedPosition.positionY;
                    // posZ is the vertical axis (altitude/terrain height); gravity and slopes are
                    // not bounded by move_speed, so the speed check uses the horizontal XY plane only.
                    const float actualDist = std::sqrt(dx * dx + dy * dy);

                    if (actualDist > maxDist)
                    {
                        log_->warn("[MOVE_VALIDATE] char {} moved {:.2f} units in {}ms (max {:.2f}) — rejected",
                            movementData.characterId,
                            actualDist,
                            static_cast<int>(deltaMs),
                            maxDist);

                        if (clientSocket)
                        {
                            nlohmann::json correction = ResponseBuilder()
                                                            .setHeader("status", "error")
                                                            .setHeader("message", "Position validation failed")
                                                            .setHeader("hash", "")
                                                            .setHeader("clientId", clientID)
                                                            .setHeader("eventType", "positionCorrection")
                                                            .setTimestamps(timestamps)
                                                            .setBody("characterId", movementData.characterId)
                                                            .setBody("position", nlohmann::json{{"x", charData.lastValidatedPosition.positionX}, {"y", charData.lastValidatedPosition.positionY}, {"z", charData.lastValidatedPosition.positionZ}, {"rotationZ", charData.lastValidatedPosition.rotationZ}})
                                                            .build();
                            networkManager_.sendResponse(clientSocket,
                                networkManager_.generateResponseMessage("error", correction));
                        }

                        // Reset lastMoveSrvMs so the next packet is treated as the first movement
                        // (no speed check). This breaks the rejection loop: the client may take
                        // one RTT to apply the teleport; without this reset every packet in flight
                        // during that RTT would be rejected and the loop would never terminate.
                        gameServices_.getCharacterManager().setLastValidatedMovement(
                            movementData.characterId, charData.lastValidatedPosition, 0);

                        return;
                    }
                }

                // Accept: persist validation state before applying position
                gameServices_.getCharacterManager().setLastValidatedMovement(
                    movementData.characterId, movementData.position, srvNowMs);
            }

            // Update character position on server
            gameServices_.getCharacterManager().setCharacterPosition(
                movementData.characterId, movementData.position);

            // Trigger reach-type quest step check
            gameServices_.getQuestManager().onPositionReached(
                movementData.characterId,
                movementData.position.positionX,
                movementData.position.positionY);

            // Exploration reward + zone_entered notification
            try
            {
                auto zoneOpt = gameServices_.getGameZoneManager().getZoneForPosition(movementData.position);
                if (zoneOpt.has_value())
                {
                    // Detect zone change and send zone_entered every time the player crosses a boundary
                    int prevZoneId = 0;
                    auto it = lastZoneByCharacter_.find(movementData.characterId);
                    if (it != lastZoneByCharacter_.end())
                        prevZoneId = it->second;

                    if (zoneOpt->id != prevZoneId)
                    {
                        lastZoneByCharacter_[movementData.characterId] = zoneOpt->id;
                        nlohmann::json zoneData = {
                            {"zoneSlug", zoneOpt->slug},
                            {"minLevel", zoneOpt->minLevel},
                            {"maxLevel", zoneOpt->maxLevel},
                            {"isPvp", zoneOpt->isPvp},
                            {"isSafeZone", zoneOpt->isSafeZone}};
                        gameServices_.getStatsNotificationService().sendWorldNotification(
                            movementData.characterId, "zone_entered", zoneData, "high", "zone_banner");
                    }

                    // One-time exploration XP reward
                    const std::string exploredKey = "explored_" + zoneOpt->slug;
                    const bool alreadyExplored =
                        gameServices_.getQuestManager().getFlagBool(movementData.characterId, exploredKey);
                    if (!alreadyExplored)
                    {
                        gameServices_.getQuestManager().setFlagBool(movementData.characterId, exploredKey, true);
                        if (zoneOpt->explorationXpReward > 0)
                        {
                            gameServices_.getExperienceManager().grantExperience(
                                movementData.characterId,
                                zoneOpt->explorationXpReward,
                                "zone_explored",
                                0);
                        }
                        nlohmann::json notifData = {{"zoneSlug", zoneOpt->slug}};
                        gameServices_.getStatsNotificationService().sendWorldNotification(
                            movementData.characterId,
                            "zone_explored",
                            notifData,
                            "medium",
                            "toast");
                    }
                }
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("[Exploration] " + std::string(ex.what()));
            }

            // Create minimal character data for response (only essential movement data)
            nlohmann::json characterJson = {
                {"id", movementData.characterId},
                {"position", {{"x", movementData.position.positionX}, {"y", movementData.position.positionY}, {"z", movementData.position.positionZ}, {"rotationZ", movementData.position.rotationZ}}}};

            // Validate authentication
            if (clientID == 0)
            {
                sendErrorResponseWithTimestamps(clientSocket, "Movement failed for character!", "moveCharacter", clientID, timestamps);
                return;
            }

            // Prepare and broadcast movement response
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Movement success for character!")
                                                 .setHeader("hash", "")
                                                 .setHeader("clientId", clientID)
                                                 .setHeader("eventType", "moveCharacter")
                                                 .setTimestamps(timestamps)
                                                 .setBody("character", characterJson)
                                                 .build();

            gameServices_.getLogger().log("Client data map size: " + std::to_string(gameServices_.getClientManager().getClientsList().size()));

            // Broadcast to all clients with timestamps
            broadcastToAllClientsWithTimestamps("success", successResponse, timestamps);
        }
        else
        {
            log_->info("Error with extracting data in moveCharacter - variant doesn't contain MovementDataStruct!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Bad variant access in handleMoveCharacterEvent: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("General error in handleMoveCharacterEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleGetConnectedCharactersEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    const auto &timestamps = event.getTimestamps();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        std::vector<CharacterDataStruct> charactersList = gameServices_.getCharacterManager().getCharactersList();

        nlohmann::json charactersListJson = nlohmann::json::array();
        for (const auto &character : charactersList)
        {
            nlohmann::json characterJson = characterToJson(character);

            nlohmann::json fullEntry = {
                {"clientId", character.clientId},
                {"character", characterJson}};

            charactersListJson.push_back(fullEntry);
        }

        if (clientID == 0)
        {
            sendErrorResponseWithTimestamps(clientSocket, "Getting connected characters failed!", "getConnectedCharacters", clientID, timestamps);
            return;
        }

        sendSuccessResponseWithTimestamps(clientSocket, "Getting connected characters success!", "getConnectedCharacters", clientID, timestamps, "characters", charactersListJson);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error in handleGetConnectedCharactersEvent: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSetCharacterDataEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<CharacterDataStruct>(data))
        {
            CharacterDataStruct characterData = std::get<CharacterDataStruct>(data);
            gameServices_.getCharacterManager().addCharacter(characterData);

            // Process any pending join requests for this character
            processPendingJoinRequests(characterData.characterId);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSetCharactersListEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<CharacterDataStruct>>(data))
        {
            std::vector<CharacterDataStruct> charactersList = std::get<std::vector<CharacterDataStruct>>(data);
            gameServices_.getCharacterManager().loadCharactersList(charactersList);
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::handleSetCharacterAttributesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<CharacterAttributeStruct>>(data))
        {
            std::vector<CharacterAttributeStruct> characterAttributesList = std::get<std::vector<CharacterAttributeStruct>>(data);
            gameServices_.getCharacterManager().loadCharacterAttributes(characterAttributesList);

            // Notify client of updated effective stats after attribute reload
            // (triggered by equip/unequip, level-up, or manual refresh)
            if (!characterAttributesList.empty())
            {
                int cid = characterAttributesList.front().character_id;
                gameServices_.getStatsNotificationService().sendStatsUpdate(cid);
            }
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
CharacterEventHandler::processPendingJoinRequests(int characterId)
{
    log_->info("processPendingJoinRequests called for character ID: " + std::to_string(characterId));
    gameServices_.getLogger().log("Current pendingJoinRequests_ size: " + std::to_string(pendingJoinRequests_.size()));

    auto it = pendingJoinRequests_.find(characterId);
    if (it == pendingJoinRequests_.end())
    {
        log_->info("No pending requests map entry found for character ID: " + std::to_string(characterId));
        return;
    }

    if (it->second.empty())
    {
        log_->info("Pending requests vector is empty for character ID: " + std::to_string(characterId));
        return;
    }

    gameServices_.getLogger().log("Processing " + std::to_string(it->second.size()) + " pending join requests for character ID: " + std::to_string(characterId));

    // Get character data
    CharacterDataStruct characterData = gameServices_.getCharacterManager().getCharacterData(characterId);

    if (characterData.characterId == 0)
    {
        log_->error("Character data still not available for ID: " + std::to_string(characterId));
        return;
    }

    // Process all pending requests for this character
    for (const auto &request : it->second)
    {
        // Prepare character data in json format
        nlohmann::json characterJson = characterToJson(characterData);

        // Validate authentication
        if (!validateCharacterAuthentication(request.clientID, characterData.characterId))
        {
            sendErrorResponseWithTimestamps(request.clientSocket, "Authentication failed for character!", "joinGameCharacter", request.clientID, request.timestamps);
            continue;
        }

        // Prepare and broadcast success response to all clients (including sender)
        nlohmann::json broadcastResponse = ResponseBuilder()
                                               .setHeader("message", "Authentication success for character!")
                                               .setHeader("hash", "")
                                               .setHeader("clientId", request.clientID)
                                               .setHeader("eventType", "joinGameCharacter")
                                               .setTimestamps(request.timestamps)
                                               .setBody("character", characterJson)
                                               .build();

        broadcastToAllClientsWithTimestamps("success", broadcastResponse, request.timestamps);

        // Initialize player skills after successful character join for pending request
        if (skillEventHandler_)
        {
            skillEventHandler_->initializeFromCharacterData(characterData, request.clientID, request.clientSocket);
        }
        else
        {
            log_->error("SkillEventHandler not set in CharacterEventHandler");
        }

        // Send NPC spawn data — this was missing from the pending path
        if (npcEventHandler_)
        {
            npcEventHandler_->sendNPCSpawnDataToClient(request.clientID, characterData.characterPosition, 50000.0f);
        }
        else
        {
            log_->error("NPCEventHandler not set in CharacterEventHandler");
        }

        // Send existing ground items snapshot to the joining player
        if (itemEventHandler_)
        {
            itemEventHandler_->sendGroundItemsToClient(request.clientID, request.clientSocket);
        }

        // Server-push all spawn zones with live mob state
        if (mobEventHandler_)
        {
            mobEventHandler_->sendSpawnZonesToClient(request.clientID, request.clientSocket);
        }

        // Notify client which game zone it is currently in
        try
        {
            auto zoneOpt = gameServices_.getGameZoneManager().getZoneForPosition(characterData.characterPosition);
            if (zoneOpt.has_value())
            {
                lastZoneByCharacter_[characterData.characterId] = zoneOpt->id;
                nlohmann::json zoneData = {
                    {"zoneSlug", zoneOpt->slug},
                    {"minLevel", zoneOpt->minLevel},
                    {"maxLevel", zoneOpt->maxLevel},
                    {"isPvp", zoneOpt->isPvp},
                    {"isSafeZone", zoneOpt->isSafeZone}};
                gameServices_.getStatsNotificationService().sendWorldNotification(
                    characterData.characterId, "zone_entered", zoneData, "high", "zone_banner");
            }
        }
        catch (const std::exception &ex)
        {
            log_->error("[zone_entered on join] {}", ex.what());
        }

        gameServices_.getLogger().log("Processed pending join request for client ID: " + std::to_string(request.clientID) + ", character ID: " + std::to_string(characterId));
    }

    // Request player quests and flags from game server (once per character)
    {
        nlohmann::json questsReq;
        questsReq["header"]["eventType"] = "getPlayerQuests";
        questsReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(questsReq.dump() + "\n");

        nlohmann::json flagsReq;
        flagsReq["header"]["eventType"] = "getPlayerFlags";
        flagsReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(flagsReq.dump() + "\n");

        nlohmann::json effectsReq;
        effectsReq["header"]["eventType"] = "getPlayerActiveEffects";
        effectsReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(effectsReq.dump() + "\n");

        nlohmann::json inventoryReq;
        inventoryReq["header"]["eventType"] = "getPlayerInventory";
        inventoryReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(inventoryReq.dump() + "\n");

        nlohmann::json pityReq;
        pityReq["header"]["eventType"] = "getPlayerPityData";
        pityReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(pityReq.dump() + "\n");

        nlohmann::json bestiaryReq;
        bestiaryReq["header"]["eventType"] = "getPlayerBestiaryData";
        bestiaryReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(bestiaryReq.dump() + "\n");

        // Stage 4: request reputation and mastery data
        nlohmann::json reputationsReq;
        reputationsReq["header"]["eventType"] = "getPlayerReputationsData";
        reputationsReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(reputationsReq.dump() + "\n");

        nlohmann::json masteriesReq;
        masteriesReq["header"]["eventType"] = "getPlayerMasteriesData";
        masteriesReq["body"]["characterId"] = characterId;
        gameServerWorker_.sendDataToGameServer(masteriesReq.dump() + "\n");
    }

    // Clear processed requests
    pendingJoinRequests_.erase(it);
    log_->info("Cleared pending requests for character ID: " + std::to_string(characterId));
}

void
CharacterEventHandler::handlePlayerRespawnEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientId = event.getClientID();

    if (!std::holds_alternative<RespawnRequestStruct>(data))
    {
        log_->error("[RESPAWN] unexpected data variant");
        return;
    }
    const auto &req = std::get<RespawnRequestStruct>(data);
    int characterId = req.characterId;

    auto clientSocket = getClientSocket(event);

    // Player must actually be dead
    if (isPlayerAlive(characterId))
    {
        log_->warn("[RESPAWN] character {} tried to respawn while alive", characterId);
        if (clientSocket)
            sendErrorResponseWithTimestamps(clientSocket, "You are not dead", "respawnRequest", clientId, req.timestamps);
        return;
    }

    try
    {
        auto &charMgr = gameServices_.getCharacterManager();
        CharacterDataStruct charData = charMgr.getCharacterData(characterId);

        // ── Find nearest respawn zone ────────────────────────────────────────
        const PositionStruct &deathPos = charData.characterPosition;
        RespawnZoneStruct zone = gameServices_.getRespawnZoneManager().findNearest(deathPos);

        PositionStruct respawnPos;
        if (charData.respawnPosition.positionX != 0.0f || charData.respawnPosition.positionY != 0.0f)
        {
            // Player has a custom respawn bind point (e.g. set via a shrine)
            respawnPos = charData.respawnPosition;
        }
        else if (zone.id > 0)
        {
            respawnPos = zone.position;
        }
        else
        {
            // No zones loaded — keep them at death position (will be fixed when zones are loaded)
            respawnPos = deathPos;
            log_->error("[RESPAWN] No respawn zones available for character {}", characterId);
        }

        // ── Restore HP and Mana to 30% ───────────────────────────────────────
        int newHp = std::max(1, static_cast<int>(charData.characterMaxHealth * 0.30f));
        int newMana = std::max(0, static_cast<int>(charData.characterMaxMana * 0.30f));

        charMgr.updateCharacterHealth(characterId, newHp);
        charMgr.updateCharacterMana(characterId, newMana);

        // ── Teleport to respawn zone ──────────────────────────────────────────
        charMgr.setCharacterPosition(characterId, respawnPos);
        charMgr.setLastValidatedMovement(characterId, respawnPos, 0); // reset: player teleported

        // ── Apply Resurrection Sickness (data-driven stat debuff from DB template) ──
        const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
                                   .count();

        // Remove any pre-existing resurrection_sickness rows (e.g. died again)
        auto currentEffects = charMgr.getCharacterData(characterId).activeEffects;
        currentEffects.erase(
            std::remove_if(currentEffects.begin(), currentEffects.end(), [](const ActiveEffectStruct &e)
                { return e.effectSlug == "resurrection_sickness"; }),
            currentEffects.end());

        const auto *tmpl = gameServices_.getStatusEffectTemplateManager().getTemplate("resurrection_sickness");
        if (tmpl && !tmpl->modifiers.empty())
        {
            const int64_t sicknessExpiresAt = nowSec + (tmpl->durationSec > 0 ? tmpl->durationSec : 120);

            for (const auto &mod : tmpl->modifiers)
            {
                // percent_all: expand over every character attribute
                // percent / flat: target only the specified attribute
                std::vector<std::pair<std::string, double>> targets;

                if (mod.modifierType == "percent_all")
                {
                    for (const auto &attr : charData.attributes)
                    {
                        if (attr.value == 0)
                            continue;
                        double flatValue = std::round(attr.value * (mod.value / 100.0));
                        targets.emplace_back(attr.slug, flatValue);
                    }
                }
                else if (mod.modifierType == "percent" && !mod.attributeSlug.empty())
                {
                    for (const auto &attr : charData.attributes)
                    {
                        if (attr.slug == mod.attributeSlug && attr.value != 0)
                        {
                            double flatValue = std::round(attr.value * (mod.value / 100.0));
                            targets.emplace_back(attr.slug, flatValue);
                            break;
                        }
                    }
                }
                else if (mod.modifierType == "flat" && !mod.attributeSlug.empty())
                {
                    targets.emplace_back(mod.attributeSlug, mod.value);
                }

                for (const auto &[attrSlug, flatValue] : targets)
                {
                    ActiveEffectStruct eff;
                    eff.effectSlug = tmpl->slug;
                    eff.effectTypeSlug = tmpl->category;
                    eff.attributeSlug = attrSlug;
                    eff.value = static_cast<float>(flatValue);
                    eff.sourceType = "death";
                    eff.expiresAt = sicknessExpiresAt;
                    currentEffects.push_back(eff);

                    nlohmann::json effectPacket;
                    effectPacket["header"]["eventType"] = "saveActiveEffect";
                    effectPacket["header"]["clientId"] = 0;
                    effectPacket["header"]["hash"] = "";
                    effectPacket["body"]["characterId"] = characterId;
                    effectPacket["body"]["effectSlug"] = tmpl->slug;
                    effectPacket["body"]["attributeSlug"] = attrSlug;
                    effectPacket["body"]["sourceType"] = "death";
                    effectPacket["body"]["value"] = flatValue;
                    effectPacket["body"]["expiresAt"] = sicknessExpiresAt;
                    effectPacket["body"]["tickMs"] = 0;
                    gameServerWorker_.sendDataToGameServer(effectPacket.dump() + "\n");
                }
            }
        }
        else
        {
            log_->warn("[RESPAWN] resurrection_sickness template not found or has no modifiers; skipping debuff.");
        }

        charMgr.setCharacterActiveEffects(characterId, currentEffects);

        // ── Persist experience debt to DB ────────────────────────────────────
        {
            nlohmann::json debtPacket;
            debtPacket["header"]["eventType"] = "saveExperienceDebt";
            debtPacket["header"]["clientId"] = 0;
            debtPacket["header"]["hash"] = "";
            debtPacket["body"]["characterId"] = characterId;
            debtPacket["body"]["experienceDebt"] = charMgr.getCharacterData(characterId).experienceDebt;
            gameServerWorker_.sendDataToGameServer(debtPacket.dump() + "\n");
        }

        // ── Save HP/Mana and position to DB via game server ───────────────────
        nlohmann::json hpManaPacket;
        hpManaPacket["header"]["eventType"] = "saveHpMana";
        hpManaPacket["header"]["clientId"] = 0;
        hpManaPacket["header"]["hash"] = "";
        hpManaPacket["body"]["characters"] = nlohmann::json::array();
        nlohmann::json hpEntry;
        hpEntry["characterId"] = characterId;
        hpEntry["currentHp"] = newHp;
        hpEntry["currentMana"] = newMana;
        hpManaPacket["body"]["characters"].push_back(hpEntry);
        gameServerWorker_.sendDataToGameServer(hpManaPacket.dump() + "\n");

        nlohmann::json savePacket;
        savePacket["header"]["eventType"] = "savePositions";
        savePacket["header"]["clientId"] = 0;
        savePacket["header"]["hash"] = "";
        savePacket["body"]["characters"] = nlohmann::json::array();
        nlohmann::json posEntry;
        posEntry["characterId"] = characterId;
        posEntry["posX"] = respawnPos.positionX;
        posEntry["posY"] = respawnPos.positionY;
        posEntry["posZ"] = respawnPos.positionZ;
        posEntry["rotZ"] = respawnPos.rotationZ;
        savePacket["body"]["characters"].push_back(posEntry);
        gameServerWorker_.sendDataToGameServer(savePacket.dump() + "\n");

        // ── Send stats update (HP/Mana/effects) ───────────────────────────────
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        // ── Send teleport packet to client ────────────────────────────────────
        if (clientSocket)
        {
            nlohmann::json teleportResponse = ResponseBuilder()
                                                  .setHeader("message", "Respawn successful")
                                                  .setHeader("hash", "")
                                                  .setHeader("clientId", clientId)
                                                  .setHeader("eventType", "respawnResult")
                                                  .setTimestamps(req.timestamps)
                                                  .setBody("characterId", characterId)
                                                  .setBody("position", nlohmann::json{{"x", respawnPos.positionX}, {"y", respawnPos.positionY}, {"z", respawnPos.positionZ}, {"rotationZ", respawnPos.rotationZ}})
                                                  .build();
            std::string msg = networkManager_.generateResponseMessage("success", teleportResponse);
            networkManager_.sendResponse(clientSocket, msg);
        }

        // Broadcast the character's new position to all players
        nlohmann::json broadcastJson = ResponseBuilder()
                                           .setHeader("message", "Character respawned")
                                           .setHeader("hash", "")
                                           .setHeader("clientId", clientId)
                                           .setHeader("eventType", "moveCharacter")
                                           .setTimestamps(req.timestamps)
                                           .setBody("character", nlohmann::json{{"id", characterId}, {"position", {{"x", respawnPos.positionX}, {"y", respawnPos.positionY}, {"z", respawnPos.positionZ}, {"rotationZ", respawnPos.rotationZ}}}})
                                           .build();
        broadcastToAllClientsWithTimestamps("success", broadcastJson, req.timestamps);

        log_->info("[RESPAWN] character {} respawned at ({},{},{})", characterId, respawnPos.positionX, respawnPos.positionY, respawnPos.positionZ);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[RESPAWN] exception for character " +
                                           std::to_string(characterId) + ": " + ex.what());
    }
}
