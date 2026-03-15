#include "events/handlers/MobEventHandler.hpp"
#include "events/EventData.hpp"
#include "utils/TimestampUtils.hpp"
#include <spdlog/logger.h>

MobEventHandler::MobEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "mob")
{
    log_ = gameServices_.getLogger().getSystem("mob");
}

nlohmann::json
MobEventHandler::mobToJson(const MobDataStruct &mobData)
{
    // Fetch movement data to include velocity info for client-side interpolation
    MobMovementData movementData = gameServices_.getMobMovementManager().getMobMovementData(mobData.uid);

    nlohmann::json mobJson = {
        {"id", mobData.id},
        {"uid", mobData.uid},
        {"zoneId", mobData.zoneId},
        {"name", mobData.name},
        {"slug", mobData.slug},
        {"race", mobData.raceName},
        {"level", mobData.level},
        {"isAggressive", mobData.isAggressive},
        {"isDead", mobData.isDead},
        {"stats", {{"health", {{"current", mobData.currentHealth}, {"max", mobData.maxHealth}}}, {"mana", {{"current", mobData.currentMana}, {"max", mobData.maxMana}}}}},
        {"position", {{"x", mobData.position.positionX}, {"y", mobData.position.positionY}, {"z", mobData.position.positionZ}, {"rotationZ", mobData.position.rotationZ}}},
        // Velocity data for client-side dead reckoning / interpolation.
        // dirX/dirY is a normalized direction vector; speed is in world-units/second.
        // The client should extrapolate: pos += dir * speed * deltaTime between packets,
        // then smoothly blend (lerp ~100-150ms) to the authoritative position on next update.
        {"velocity", {{"dirX", movementData.movementDirectionX}, {"dirY", movementData.movementDirectionY}, {"speed", movementData.currentSpeedUnitsPerSec}}},
        {"attributes", nlohmann::json::array()}};

    for (const auto &attr : mobData.attributes)
    {
        mobJson["attributes"].push_back({{"id", attr.id},
            {"name", attr.name},
            {"slug", attr.slug},
            {"value", attr.value}});
    }

    return mobJson;
}

nlohmann::json
MobEventHandler::spawnZoneToJson(const SpawnZoneStruct &spawnZoneData)
{
    return nlohmann::json{
        {"id", spawnZoneData.zoneId},
        {"name", spawnZoneData.zoneName},
        {"bounds", {{"minX", spawnZoneData.posX}, {"maxX", spawnZoneData.sizeX}, {"minY", spawnZoneData.posY}, {"maxY", spawnZoneData.sizeY}, {"minZ", spawnZoneData.posZ}, {"maxZ", spawnZoneData.sizeZ}}},
        {"spawnMobId", spawnZoneData.spawnMobId},
        {"maxSpawnCount", spawnZoneData.spawnCount},
        {"spawnedMobsCount", spawnZoneData.spawnedMobsList.size()},
        {"respawnTime", spawnZoneData.respawnTime.count()},
        {"spawnEnabled", true}};
}

void
MobEventHandler::handleSpawnMobsInZoneEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);
    const TimestampStruct timestamps = event.getTimestamps();

    try
    {
        if (std::holds_alternative<SpawnZoneStruct>(data))
        {
            SpawnZoneStruct spawnZoneData = std::get<SpawnZoneStruct>(data);

            // Build JSON for spawn zone
            nlohmann::json zoneJson = spawnZoneToJson(spawnZoneData);

            // Get current mobs data from MobInstanceManager instead of outdated spawnZoneData
            std::vector<MobDataStruct> mobsList = gameServices_.getMobInstanceManager().getMobInstancesInZone(spawnZoneData.zoneId);

            // Add mobs
            nlohmann::json mobsArray = nlohmann::json::array();
            for (const auto &mob : mobsList)
            {
                nlohmann::json mobJson = mobToJson(mob);
                mobJson["combatState"] = static_cast<int>(gameServices_.getMobMovementManager().getMobMovementData(mob.uid).combatState);
                mobsArray.push_back(mobJson);
            }

            if (clientID == 0)
            {
                sendErrorResponseWithTimestamps(clientSocket, "Spawning mobs failed!", "spawnMobsInZone", clientID, timestamps, "");
                return;
            }

            // Build success response with timestamps
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Spawning mobs success!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", clientID)
                                          .setHeader("eventType", "spawnMobsInZone")
                                          .setTimestamps(timestamps)
                                          .setBody("spawnZone", zoneJson)
                                          .setBody("mobs", mobsArray)
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response, timestamps);
            networkManager_.sendResponse(clientSocket, responseData);
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
MobEventHandler::sendSpawnZonesToClient(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    if (!clientSocket || !clientSocket->is_open())
        return;

    auto allSpawnZones = gameServices_.getSpawnZoneManager().getMobSpawnZones();
    log_->info("[MobEventHandler] Server-push: sending {} spawn zones to client {}", allSpawnZones.size(), clientID);

    for (const auto &[zoneId, spawnZone] : allSpawnZones)
    {
        nlohmann::json zoneJson = spawnZoneToJson(spawnZone);

        std::vector<MobDataStruct> mobsList = gameServices_.getMobInstanceManager().getMobInstancesInZone(spawnZone.zoneId);
        nlohmann::json mobsArray = nlohmann::json::array();
        for (const auto &mob : mobsList)
        {
            nlohmann::json mobJson = mobToJson(mob);
            mobJson["combatState"] = static_cast<int>(gameServices_.getMobMovementManager().getMobMovementData(mob.uid).combatState);
            mobsArray.push_back(mobJson);
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Spawning mobs success!")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "spawnMobsInZone")
                                      .setBody("spawnZone", zoneJson)
                                      .setBody("mobs", mobsArray)
                                      .build();

        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", response));
    }
}

void
MobEventHandler::handleZoneMoveMobsEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);
    const TimestampStruct timestamps = event.getTimestamps();

    try
    {
        nlohmann::json mobsArray = nlohmann::json::array();

        if (std::holds_alternative<int>(data))
        {
            // Legacy: zoneId - send all mobs in zone
            int zoneId = std::get<int>(data);
            std::vector<MobDataStruct> mobsList = gameServices_.getMobInstanceManager().getMobInstancesInZone(zoneId);

            for (const auto &mob : mobsList)
            {
                nlohmann::json mobJson = mobToJson(mob);
                mobJson["combatState"] = static_cast<int>(gameServices_.getMobMovementManager().getMobMovementData(mob.uid).combatState);
                mobsArray.push_back(mobJson);
            }
        }
        else if (std::holds_alternative<std::vector<MobDataStruct>>(data))
        {
            // New: specific moved mobs - send only these mobs
            const auto &movedMobs = std::get<std::vector<MobDataStruct>>(data);

            for (const auto &mob : movedMobs)
            {
                nlohmann::json mobJson = mobToJson(mob);
                mobJson["combatState"] = static_cast<int>(gameServices_.getMobMovementManager().getMobMovementData(mob.uid).combatState);
                mobsArray.push_back(mobJson);
            }
        }
        else
        {
            sendErrorResponseWithTimestamps(clientSocket, "Invalid data type for zone move mobs!", "zoneMoveMobs", clientID, timestamps, "");
            return;
        }

        if (clientID == 0)
        {
            sendErrorResponseWithTimestamps(clientSocket, "Moving mobs failed!", "zoneMoveMobs", clientID, timestamps, "");
            return;
        }

        sendSuccessResponseWithTimestamps(clientSocket, "Moving mobs success!", "zoneMoveMobs", clientID, timestamps, "mobs", mobsArray, "");
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleSetAllMobsListEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<MobDataStruct>>(data))
        {
            std::vector<MobDataStruct> mobsList = std::get<std::vector<MobDataStruct>>(data);
            gameServices_.getMobManager().setListOfMobs(mobsList);
            log_->info("Loaded all mobs data from the event handler!");
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
MobEventHandler::handleGetMobDataEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<MobDataStruct>(data))
        {
            MobDataStruct mobData = std::get<MobDataStruct>(data);

            // Get mob data from the mob manager
            MobDataStruct mobDataFromManager = gameServices_.getMobManager().getMobById(mobData.id);

            // Prepare mob data in json format
            nlohmann::json mobJson = {
                {"id", mobDataFromManager.id},
                {"uid", mobDataFromManager.uid},
                {"zoneId", mobDataFromManager.zoneId},
                {"name", mobDataFromManager.name}};

            // Send the response to the game server
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Getting mob data success!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", event.getClientID())
                                          .setHeader("eventType", "getMobData")
                                          .setBody("mob", mobJson)
                                          .build();

            sendGameServerResponse("success", response);
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
MobEventHandler::handleSetMobsAttributesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<MobAttributeStruct>>(data))
        {
            std::vector<MobAttributeStruct> mobAttributesList = std::get<std::vector<MobAttributeStruct>>(data);

            // Debug mob attributes
            for (const auto &mobAttribute : mobAttributesList)
            {
                gameServices_.getLogger().log("Mob Attribute ID: " + std::to_string(mobAttribute.id) +
                                              ", Name: " + mobAttribute.name +
                                              ", Slug: " + mobAttribute.slug +
                                              ", Value: " + std::to_string(mobAttribute.value));
            }

            gameServices_.getMobManager().setListOfMobsAttributes(mobAttributesList);
            // Also propagate attributes to already-spawned instances
            gameServices_.getMobInstanceManager().applyBulkAttributes(mobAttributesList);
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
MobEventHandler::handleMobDeathEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        // Event data should contain mobUID and zoneId
        if (std::holds_alternative<std::pair<int, int>>(data))
        {
            auto mobDeathData = std::get<std::pair<int, int>>(data);
            int mobUID = mobDeathData.first;
            int zoneId = mobDeathData.second;

            gameServices_.getLogger().log("[MOB_DEATH_EVENT] Broadcasting death notification for mob UID " +
                                          std::to_string(mobUID) + " in zone " + std::to_string(zoneId));

            // Build death notification response
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Mob died")
                                          .setHeader("hash", "")
                                          .setHeader("eventType", "mobDeath")
                                          .setBody("mobUID", mobUID)
                                          .setBody("zoneId", zoneId)
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            broadcastToAllClients(responseData);
        }
        else
        {
            log_->error("Invalid data format for MOB_DEATH event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob death event: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleMobTargetLostEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        // Event data should contain JSON with mob target lost information
        if (std::holds_alternative<nlohmann::json>(data))
        {
            auto targetLostData = std::get<nlohmann::json>(data);

            int mobUID = targetLostData["mobUID"];
            int mobId = targetLostData["mobId"];
            int lostTargetPlayerId = targetLostData["lostTargetPlayerId"];

            gameServices_.getLogger().log("[MOB_TARGET_LOST_EVENT] Broadcasting target lost notification for mob UID " +
                                          std::to_string(mobUID) + " (lost target player " + std::to_string(lostTargetPlayerId) + ")");

            // Build target lost notification response
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Mob lost target")
                                          .setHeader("hash", "")
                                          .setHeader("eventType", "mobTargetLost")
                                          .setBody("mobUID", mobUID)
                                          .setBody("mobId", mobId)
                                          .setBody("lostTargetPlayerId", lostTargetPlayerId)
                                          .setBody("positionX", targetLostData["positionX"])
                                          .setBody("positionY", targetLostData["positionY"])
                                          .setBody("positionZ", targetLostData["positionZ"])
                                          .setBody("rotationZ", targetLostData["rotationZ"])
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            broadcastToAllClients(responseData);
        }
        else
        {
            log_->error("Invalid data format for MOB_TARGET_LOST event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob target lost event: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing mob target lost event: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleSetMobsSkillsEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<std::pair<int, std::vector<SkillStruct>>>>(data))
        {
            auto mobSkillsMapping = std::get<std::vector<std::pair<int, std::vector<SkillStruct>>>>(data);
            gameServices_.getMobManager().setListOfMobsSkills(mobSkillsMapping);
            log_->info("Loaded mob skills data from the event handler!");
        }
        else
        {
            log_->error("Invalid data format for SET_ALL_MOBS_SKILLS event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob skills event: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing mob skills event: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleSetMobWeaknessesResistancesEvent(const Event &event)
{
    using WeakResMap = std::pair<std::unordered_map<int, std::vector<std::string>>,
        std::unordered_map<int, std::vector<std::string>>>;
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<WeakResMap>(data))
        {
            const auto &[weaknesses, resistances] = std::get<WeakResMap>(data);
            gameServices_.getMobManager().setWeaknessesResistances(weaknesses, resistances);
            log_->info("Loaded mob weaknesses and resistances from the event handler!");
        }
        else
        {
            log_->error("Invalid data format for SET_MOB_WEAKNESSES_RESISTANCES event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob weaknesses event: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing mob weaknesses event: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleMobHealthUpdateEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<nlohmann::json>(data))
        {
            auto healthData = std::get<nlohmann::json>(data);

            int mobUID = healthData["mobUID"];
            int mobId = healthData["mobId"];
            int currentHealth = healthData["currentHealth"];
            int maxHealth = healthData["maxHealth"];

            gameServices_.getLogger().log("[MOB_HEALTH_UPDATE] Mob UID " + std::to_string(mobUID) +
                                          " HP " + std::to_string(currentHealth) +
                                          "/" + std::to_string(maxHealth));

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Mob health updated")
                                          .setHeader("hash", "")
                                          .setHeader("eventType", "mobHealthUpdate")
                                          .setBody("mobUID", mobUID)
                                          .setBody("mobId", mobId)
                                          .setBody("currentHealth", currentHealth)
                                          .setBody("maxHealth", maxHealth)
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            broadcastToAllClients(responseData);
        }
        else
        {
            log_->error("Invalid data format for MOB_HEALTH_UPDATE event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob health update event: " + std::string(ex.what()));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error processing mob health update event: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleMobMoveUpdateEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (!std::holds_alternative<std::vector<MobMoveUpdateStruct>>(data))
        {
            log_->error("Invalid data type for MOB_MOVE_UPDATE event");
            return;
        }

        const auto &movedMobs = std::get<std::vector<MobMoveUpdateStruct>>(data);

        nlohmann::json mobsArray = nlohmann::json::array();
        for (const auto &mob : movedMobs)
        {
            mobsArray.push_back({{"uid", mob.uid},
                {"zoneId", mob.zoneId},
                {"position", {{"x", mob.position.positionX}, {"y", mob.position.positionY}, {"z", mob.position.positionZ}, {"rotationZ", mob.position.rotationZ}}},
                {"velocity", {{"dirX", mob.dirX}, {"dirY", mob.dirY}, {"speed", mob.speed}}},
                {"combatState", mob.combatState}});
        }

        if (clientID == 0 || !clientSocket || !clientSocket->is_open())
        {
            log_->error("MOB_MOVE_UPDATE: invalid client " + std::to_string(clientID));
            return;
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Mob movement update")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientID)
                                      .setHeader("eventType", "mobMoveUpdate")
                                      .setHeader("serverSendMs", TimestampUtils::getCurrentTimestampMs())
                                      .setBody("mobs", mobsArray)
                                      .build();

        std::string responseData = networkManager_.generateResponseMessage("success", response);
        networkManager_.sendResponse(clientSocket, responseData);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error in handleMobMoveUpdateEvent: " + std::string(ex.what()));
    }
}
