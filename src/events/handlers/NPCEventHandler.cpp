#include "events/handlers/NPCEventHandler.hpp"
#include "events/EventData.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"

NPCEventHandler::NPCEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
}

void
NPCEventHandler::handleSetAllNPCsListEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<NPCDataStruct>>(data))
        {
            const auto &npcs = std::get<std::vector<NPCDataStruct>>(data);

            // Store NPCs in NPCManager
            gameServices_.getNPCManager().setNPCsList(npcs);

            gameServices_.getLogger().log(
                "Received and stored " + std::to_string(npcs.size()) + " NPCs from game server",
                GREEN);
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data type in handleSetAllNPCsListEvent");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError(
            "Error in handleSetAllNPCsListEvent: " + std::string(ex.what()));
    }
}

void
NPCEventHandler::handleSetAllNPCsAttributesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<NPCAttributeStruct>>(data))
        {
            const auto &attributes = std::get<std::vector<NPCAttributeStruct>>(data);

            // Store NPC attributes in NPCManager
            gameServices_.getNPCManager().setNPCsAttributes(attributes);

            gameServices_.getLogger().log(
                "Received and stored attributes for NPCs from game server",
                GREEN);
        }
        else
        {
            gameServices_.getLogger().logError("Invalid data type in handleSetAllNPCsAttributesEvent");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError(
            "Error in handleSetAllNPCsAttributesEvent: " + std::string(ex.what()));
    }
}

void
NPCEventHandler::sendNPCSpawnDataToClient(int clientId, const PositionStruct &playerPosition, float spawnRadius)
{
    try
    {
        if (!gameServices_.getNPCManager().isNPCsLoaded())
        {
            gameServices_.getLogger().log("NPCs not loaded yet, cannot send spawn data to client " + std::to_string(clientId), YELLOW);
            return;
        }

        // Get NPCs in area around player
        std::vector<NPCDataStruct> nearbyNPCs = gameServices_.getNPCManager().getNPCsInArea(
            playerPosition.positionX,
            playerPosition.positionY,
            spawnRadius);

        if (nearbyNPCs.empty())
        {
            gameServices_.getLogger().log("No NPCs found near player " + std::to_string(clientId), BLUE);
            return;
        }

        // Convert NPCs to JSON array
        nlohmann::json npcsSpawnJson = nlohmann::json::array();
        for (const auto &npc : nearbyNPCs)
        {
            npcsSpawnJson.push_back(convertNPCToSpawnJson(npc));
        }

        // Build response message
        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "NPCs spawn data for area")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "spawnNPCs")
                                      .setBody("npcsSpawn", npcsSpawnJson)
                                      .setBody("spawnRadius", spawnRadius)
                                      .setBody("npcCount", nearbyNPCs.size())
                                      .build();

        // Send response to client
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (clientSocket)
        {
            std::string responseData = networkManager_.generateResponseMessage("success", response);
            networkManager_.sendResponse(clientSocket, responseData);
        }
        else
        {
            gameServices_.getLogger().logError("Client socket not found for client " + std::to_string(clientId));
        }

        gameServices_.getLogger().log(
            "Sent " + std::to_string(nearbyNPCs.size()) + " NPCs spawn data to client " + std::to_string(clientId),
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError(
            "Error in sendNPCSpawnDataToClient: " + std::string(ex.what()));
    }
}

nlohmann::json
NPCEventHandler::convertNPCToSpawnJson(const NPCDataStruct &npc) const
{
    nlohmann::json npcJson = {
        {"id", npc.id},
        {"name", npc.name},
        {"slug", npc.slug},
        {"race", npc.raceName},
        {"level", npc.level},
        {"npcType", npc.npcType},
        {"isInteractable", npc.isInteractable},
        {"dialogueId", npc.dialogueId},
        {"questId", npc.questId},
        {"stats", {{"health", {{"current", npc.currentHealth}, {"max", npc.maxHealth}}}, {"mana", {{"current", npc.currentMana}, {"max", npc.maxMana}}}}},
        {"position", {{"x", npc.position.positionX}, {"y", npc.position.positionY}, {"z", npc.position.positionZ}, {"rotationZ", npc.position.rotationZ}}},
        {"attributes", nlohmann::json::array()}};

    // Add attributes
    for (const auto &attr : npc.attributes)
    {
        npcJson["attributes"].push_back({{"id", attr.id},
            {"name", attr.name},
            {"slug", attr.slug},
            {"value", attr.value}});
    }

    return npcJson;
}