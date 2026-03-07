#include "events/handlers/ChunkEventHandler.hpp"
#include "events/EventData.hpp"
#include <spdlog/logger.h>

ChunkEventHandler::ChunkEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "chunk")
{
    log_ = gameServices_.getLogger().getSystem("chunk");
}

bool
ChunkEventHandler::validateChunkAuthentication(int chunkId)
{
    return chunkId != 0;
}

bool
ChunkEventHandler::validateClientAuthentication(const ClientDataStruct &clientData)
{
    return clientData.clientId != 0 && !clientData.hash.empty();
}

void
ChunkEventHandler::handleInitChunkEvent(const Event &event)
{
    const auto data = event.getData();
    int clientID = event.getClientID();

    try
    {
        if (std::holds_alternative<ChunkInfoStruct>(data))
        {
            ChunkInfoStruct passedChunkData = std::get<ChunkInfoStruct>(data);

            // Set the current chunk data
            gameServices_.getChunkManager().loadChunkInfo(passedChunkData);

            // Validate chunk authentication
            if (!validateChunkAuthentication(passedChunkData.id))
            {
                // Send error response to game server
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Init failed for chunk!")
                                                   .setHeader("chunkId", passedChunkData.id)
                                                   .setHeader("eventType", "chunkServerData")
                                                   .setBody("", "")
                                                   .build();

                sendGameServerResponse("error", errorResponse);
                return;
            }

            // Send success response to game server
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Init success for chunk!")
                                                 .setHeader("chunkId", passedChunkData.id)
                                                 .setHeader("eventType", "chunkServerData")
                                                 .setBody("", "")
                                                 .build();

            sendGameServerResponse("success", successResponse);
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
ChunkEventHandler::handleJoinChunkEvent(const Event &event)
{
    const auto data = event.getData();
    int clientID = event.getClientID();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Set the current client data
            gameServices_.getClientManager().loadClientData(passedClientData);

            // Validate client authentication
            if (!validateClientAuthentication(passedClientData))
            {
                // Send error response to game server
                nlohmann::json errorResponse = ResponseBuilder()
                                                   .setHeader("message", "Authentication failed for user!")
                                                   .setHeader("hash", passedClientData.hash)
                                                   .setHeader("clientId", passedClientData.clientId)
                                                   .setHeader("eventType", "joinGame")
                                                   .setBody("", "")
                                                   .build();

                sendGameServerResponse("error", errorResponse);
                return;
            }

            // Send success response to game server
            nlohmann::json successResponse = ResponseBuilder()
                                                 .setHeader("message", "Authentication success for user!")
                                                 .setHeader("hash", passedClientData.hash)
                                                 .setHeader("clientId", passedClientData.clientId)
                                                 .setHeader("eventType", "joinGame")
                                                 .setBody("characterId", passedClientData.characterId)
                                                 .build();

            sendGameServerResponse("success", successResponse);
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
ChunkEventHandler::handleDisconnectChunkEvent(const Event &event)
{
    const auto data = event.getData();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Retrieve the last known position from in-memory store so game server can persist it
            PositionStruct lastPos = gameServices_.getCharacterManager()
                                         .getCharacterPosition(passedClientData.characterId);

            // Send disconnect response to game server, carrying characterId and last position
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Client disconnected!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", passedClientData.clientId)
                                          .setHeader("eventType", "disconnectClient")
                                          .setBody("characterId", passedClientData.characterId)
                                          .setBody("posX", lastPos.positionX)
                                          .setBody("posY", lastPos.positionY)
                                          .setBody("posZ", lastPos.positionZ)
                                          .setBody("rotZ", lastPos.rotationZ)
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
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}
