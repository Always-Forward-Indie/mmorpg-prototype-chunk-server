#include "events/handlers/MobEventHandler.hpp"
#include "events/EventData.hpp"

MobEventHandler::MobEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
}

nlohmann::json
MobEventHandler::mobToJson(const MobDataStruct &mobData)
{
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
                mobsArray.push_back(mobToJson(mob));
            }

            if (clientID == 0)
            {
                sendErrorResponse(clientSocket, "Spawning mobs failed!", "spawnMobsInZone", clientID);
                return;
            }

            // Build success response
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Spawning mobs success!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", clientID)
                                          .setHeader("eventType", "spawnMobsInZone")
                                          .setBody("spawnZone", zoneJson)
                                          .setBody("mobs", mobsArray)
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
MobEventHandler::handleZoneMoveMobsEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

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
                mobsArray.push_back(mobToJson(mob));
            }
        }
        else if (std::holds_alternative<std::vector<MobDataStruct>>(data))
        {
            // New: specific moved mobs - send only these mobs
            const auto &movedMobs = std::get<std::vector<MobDataStruct>>(data);

            for (const auto &mob : movedMobs)
            {
                mobsArray.push_back(mobToJson(mob));
            }
        }
        else
        {
            sendErrorResponse(clientSocket, "Invalid data type for zone move mobs!", "zoneMoveMobs", clientID);
            return;
        }

        if (clientID == 0)
        {
            sendErrorResponse(clientSocket, "Moving mobs failed!", "zoneMoveMobs", clientID);
            return;
        }

        sendSuccessResponse(clientSocket, "Moving mobs success!", "zoneMoveMobs", clientID, "mobs", mobsArray);
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
            gameServices_.getLogger().log("Loaded all mobs data from the event handler!", GREEN);
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
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
            gameServices_.getLogger().log("Error with extracting data!");
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
        }
        else
        {
            gameServices_.getLogger().log("Error with extracting data!");
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
            gameServices_.getLogger().logError("Invalid data format for MOB_DEATH event");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Error processing mob death event: " + std::string(ex.what()));
    }
}
