#include "events/handlers/NPCEventHandler.hpp"
#include "events/EventData.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"
#include <spdlog/logger.h>

NPCEventHandler::NPCEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "npc")
{
    log_ = gameServices_.getLogger().getSystem("npc");
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

            // Push NPC data to players who connected before this event arrived
            // (race condition: JOIN_CHARACTER processed before SET_ALL_NPCS_LIST)
            auto connectedClients = gameServices_.getClientManager().getClientsList();
            for (const auto &client : connectedClients)
            {
                if (client.characterId == 0)
                    continue;
                CharacterDataStruct charData = gameServices_.getCharacterManager().getCharacterData(client.characterId);
                if (charData.characterId == 0)
                    continue;
                sendNPCSpawnDataToClient(client.clientId, charData.characterPosition, 50000.0f);
                log_->info("Sent late NPC spawn data to already-connected client " + std::to_string(client.clientId));
            }
        }
        else
        {
            log_->error("Invalid data type in handleSetAllNPCsListEvent");
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

            log_->info(
                "Received and stored attributes for NPCs from game server");
        }
        else
        {
            log_->error("Invalid data type in handleSetAllNPCsAttributesEvent");
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
            log_->info("NPCs not loaded yet, cannot send spawn data to client " + std::to_string(clientId));
            return;
        }

        // Get NPCs in area around player
        std::vector<NPCDataStruct> nearbyNPCs = gameServices_.getNPCManager().getNPCsInArea(
            playerPosition.positionX,
            playerPosition.positionY,
            spawnRadius);

        if (nearbyNPCs.empty())
        {
            log_->debug("No NPCs found near player " + std::to_string(clientId));
            return;
        }

        // Convert NPCs to JSON array
        nlohmann::json npcsSpawnJson = nlohmann::json::array();

        // Get character ID for this client so we can compute per-player quest status
        const int characterId = gameServices_.getClientManager().getClientData(clientId).characterId;

        for (const auto &npc : nearbyNPCs)
        {
            npcsSpawnJson.push_back(convertNPCToSpawnJson(npc, characterId));
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
            log_->error("Client socket not found for client " + std::to_string(clientId));
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
NPCEventHandler::convertNPCToSpawnJson(const NPCDataStruct &npc, int characterId)
{
    // Build per-quest status array for this NPC x this character
    // Possible statuses: available | in_progress | completable | turned_in | failed
    nlohmann::json questsJson = nlohmann::json::array();
    for (const auto &slug : npc.questSlugs)
    {
        const std::string state = characterId > 0
                                      ? gameServices_.getQuestManager().getQuestStateBySlug(characterId, slug)
                                      : "";

        std::string status;
        if (state.empty() || state == "offered")
            status = "available";
        else if (state == "active")
            status = "in_progress";
        else if (state == "completed")
            status = "completable";
        else if (state == "turned_in")
            status = "turned_in";
        else // failed
            status = state;

        questsJson.push_back({{"slug", slug}, {"status", status}});
    }

    nlohmann::json npcJson = {
        {"id", npc.id},
        {"name", npc.name},
        {"slug", npc.slug},
        {"race", npc.raceName},
        {"level", npc.level},
        {"npcType", npc.npcType},
        {"isInteractable", npc.isInteractable},
        {"dialogueId", npc.dialogueId},
        {"quests", questsJson},
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