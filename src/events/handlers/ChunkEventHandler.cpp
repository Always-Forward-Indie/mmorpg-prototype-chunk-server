#include "events/handlers/ChunkEventHandler.hpp"
#include "events/EventData.hpp"

ChunkEventHandler::ChunkEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices)
{
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
            gameServices_.getLogger().log("Error with extracting data!");
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
            gameServices_.getLogger().log("Error with extracting data!");
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

            // Send disconnect response to game server
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Client disconnected!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", passedClientData.clientId)
                                          .setHeader("eventType", "disconnectClient")
                                          .setBody("", "")
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
        gameServices_.getLogger().log("Error here:" + std::string(ex.what()));
    }
}
