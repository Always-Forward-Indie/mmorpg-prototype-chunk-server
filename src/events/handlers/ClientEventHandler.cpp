#include "events/handlers/ClientEventHandler.hpp"
#include "events/EventData.hpp"
#include <spdlog/logger.h>

ClientEventHandler::ClientEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "client")
{
    log_ = gameServices_.getLogger().getSystem("client");
}

bool
ClientEventHandler::validateClientAuthentication(const ClientDataStruct &clientData)
{
    return clientData.clientId != 0 && !clientData.hash.empty();
}

void
ClientEventHandler::handlePingClientEvent(const Event &event)
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);
    int clientID = event.getClientID();

    log_->info("Handling PING event for client ID: " + std::to_string(clientID));

    if (!clientSocket || !clientSocket->is_open())
    {
        log_->info("Skipping ping - socket is closed for client ID: " + std::to_string(clientID));
        return;
    }

    const auto data = event.getData();
    const TimestampStruct timestamps = event.getTimestamps();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            const ClientDataStruct &clientData = std::get<ClientDataStruct>(data);
            sendSuccessResponseWithTimestamps(clientSocket, "Pong!", "pingClient", clientID, timestamps, "", nlohmann::json{}, clientData.hash);
            log_->info("Sending PING response with timestamps to Client ID: " + std::to_string(clientID));
        }
        else
        {
            log_->error("Error extracting data from ping event for client ID: " + std::to_string(clientID));
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().logError("Variant access error in ping event for client ID " + std::to_string(clientID) + ": " + std::string(ex.what()));
    }
}

void
ClientEventHandler::handleJoinClientEvent(const Event &event)
{
    const auto data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);
    const TimestampStruct timestamps = event.getTimestamps();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            // Validate authentication
            if (!validateClientAuthentication(passedClientData))
            {
                sendErrorResponseWithTimestamps(clientSocket, "Authentication failed for user!", "joinGameClient", clientID, timestamps, passedClientData.hash);
                return;
            }

            // Set the current client data
            gameServices_.getClientManager().loadClientData(passedClientData);

            // Set client socket
            gameServices_.getClientManager().setClientSocket(clientID, clientSocket);

            // Prepare success response and broadcast to all clients (including sender) with timestamps
            nlohmann::json broadcastResponse = ResponseBuilder()
                                                   .setHeader("message", "Authentication success for user!")
                                                   .setHeader("hash", passedClientData.hash)
                                                   .setHeader("clientId", passedClientData.clientId)
                                                   .setHeader("eventType", "joinGameClient")
                                                   .setTimestamps(timestamps)
                                                   .build();

            std::string responseData = networkManager_.generateResponseMessage("success", broadcastResponse, timestamps);
            broadcastToAllClients(responseData); // This includes the sender, so no need for separate sendSuccessResponse
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
ClientEventHandler::handleGetConnectedClientsEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);
    const TimestampStruct timestamps = event.getTimestamps();

    try
    {
        // Get client hash for response
        std::string clientHash = "";
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            const ClientDataStruct &clientData = std::get<ClientDataStruct>(data);
            clientHash = clientData.hash;
        }

        // Get all connected clients
        std::vector<ClientDataStruct> clientsList;
        try
        {
            clientsList = gameServices_.getClientManager().getClientsList();
        }
        catch (const std::exception &ex)
        {
            gameServices_.getLogger().logError("Error getting client list in handleGetConnectedClientsEvent: " + std::string(ex.what()));
            sendErrorResponseWithTimestamps(clientSocket, "Getting connected clients failed!", "getConnectedClients", clientID, timestamps, clientHash);
            return;
        }

        // Convert the clientsList to json
        nlohmann::json clientsListJson = nlohmann::json::array();
        for (const auto &client : clientsList)
        {
            // Check socket status using ClientManager
            auto clientSocket = gameServices_.getClientManager().getClientSocket(client.clientId);
            bool isConnected = clientSocket && clientSocket->is_open();

            nlohmann::json clientJson = {
                {"clientId", client.clientId},
                {"characterId", client.characterId},
                {"status", isConnected ? "connected" : "disconnected"}};
            clientsListJson.push_back(clientJson);
        }

        // Check if the authentication is not successful
        if (clientID == 0)
        {
            sendErrorResponseWithTimestamps(clientSocket, "Getting connected clients failed!", "getConnectedClients", clientID, timestamps, clientHash);
            return;
        }

        // Send success response with clients list and timestamps
        sendSuccessResponseWithTimestamps(clientSocket, "Getting connected clients success!", "getConnectedClients", clientID, timestamps, "clientsList", clientsListJson, clientHash);
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
ClientEventHandler::handleDisconnectClientEvent(const Event &event)
{
    const auto data = event.getData();

    try
    {
        if (std::holds_alternative<ClientDataStruct>(data))
        {
            ClientDataStruct passedClientData = std::get<ClientDataStruct>(data);

            gameServices_.getLogger().log("Handling disconnect event for client ID: " + std::to_string(passedClientData.clientId) +
                                          " and character ID: " + std::to_string(passedClientData.characterId));

            if (passedClientData.clientId == 0)
            {
                log_->info("Client ID is 0, handling graceful disconnect without specific client identification!");
                return;
            }

            // Idempotency guard: skip if client was already cleaned up by a previous disconnect event
            {
                ClientDataStruct existingClient = gameServices_.getClientManager().getClientData(passedClientData.clientId);
                if (existingClient.clientId == 0)
                {
                    log_->info("Client " + std::to_string(passedClientData.clientId) + " already cleaned up, skipping duplicate disconnect event.");
                    return;
                }
            }

            // Get the list of clients BEFORE removing the disconnecting client
            std::vector<ClientDataStruct> clientDataMap;
            try
            {
                clientDataMap = gameServices_.getClientManager().getClientsList();
            }
            catch (const std::exception &ex)
            {
                gameServices_.getLogger().logError("Error getting client list in handleDisconnectClientEvent: " + std::string(ex.what()));
                return;
            }

            // Remove the client data
            gameServices_.getClientManager().removeClientData(passedClientData.clientId);

            // Only save and clean up character data if the character is actually loaded in CharacterManager
            if (passedClientData.characterId > 0)
            {
                CharacterDataStruct charData = gameServices_.getCharacterManager().getCharacterData(passedClientData.characterId);
                if (charData.characterId == 0)
                {
                    log_->warn("[DISCONNECT] Character " + std::to_string(passedClientData.characterId) +
                               " not loaded in CharacterManager (incomplete join?), skipping save.");
                }
                else
                {
                    // Save last known position to game server
                    try
                    {
                        PositionStruct lastPos = charData.characterPosition;
                        nlohmann::json savePacket;
                        savePacket["header"]["eventType"] = "savePositions";
                        savePacket["header"]["clientId"] = 0;
                        savePacket["header"]["hash"] = "";
                        savePacket["body"]["characters"] = nlohmann::json::array();
                        nlohmann::json entry;
                        entry["characterId"] = passedClientData.characterId;
                        entry["posX"] = lastPos.positionX;
                        entry["posY"] = lastPos.positionY;
                        entry["posZ"] = lastPos.positionZ;
                        entry["rotZ"] = lastPos.rotationZ;
                        savePacket["body"]["characters"].push_back(entry);
                        gameServerWorker_.sendDataToGameServer(savePacket.dump() + "\n");
                        log_->info("[DISCONNECT] Saved position for characterId: " + std::to_string(passedClientData.characterId));
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError(
                            "[DISCONNECT] Failed to save position for characterId: " +
                            std::to_string(passedClientData.characterId) + " - " + ex.what());
                    }

                    // Save HP/Mana to game server
                    try
                    {
                        nlohmann::json hpManaPacket;
                        hpManaPacket["header"]["eventType"] = "saveHpMana";
                        hpManaPacket["header"]["clientId"] = 0;
                        hpManaPacket["header"]["hash"] = "";
                        hpManaPacket["body"]["characters"] = nlohmann::json::array();
                        nlohmann::json hpEntry;
                        hpEntry["characterId"] = passedClientData.characterId;
                        hpEntry["currentHp"] = charData.characterCurrentHealth;
                        hpEntry["currentMana"] = charData.characterCurrentMana;
                        hpManaPacket["body"]["characters"].push_back(hpEntry);
                        gameServerWorker_.sendDataToGameServer(hpManaPacket.dump() + "\n");
                        log_->info("[DISCONNECT] Saved HP/Mana for characterId: " + std::to_string(passedClientData.characterId));
                    }
                    catch (const std::exception &ex)
                    {
                        gameServices_.getLogger().logError(
                            "[DISCONNECT] Failed to save HP/Mana for characterId: " +
                            std::to_string(passedClientData.characterId) + " - " + ex.what());
                    }
                }
            }

            // Remove character data and flush state (only if character was loaded)
            if (passedClientData.characterId > 0)
            {
                CharacterDataStruct existingChar = gameServices_.getCharacterManager().getCharacterData(passedClientData.characterId);
                if (existingChar.characterId > 0)
                {
                    gameServices_.getCharacterManager().removeCharacter(passedClientData.characterId);
                }

                try
                {
                    gameServices_.getQuestManager().flushAllProgress(passedClientData.characterId);
                    gameServices_.getQuestManager().flushPendingFlags();
                    gameServices_.getQuestManager().unloadPlayerQuests(passedClientData.characterId);
                    gameServices_.getDialogueSessionManager().cleanupExpiredSessions();
                }
                catch (const std::exception &ex)
                {
                    gameServices_.getLogger().logError(
                        "[DISCONNECT] Quest flush error for characterId: " +
                        std::to_string(passedClientData.characterId) + " - " + ex.what());
                }

                // Stage 4: unload reputation and mastery data
                try
                {
                    gameServices_.getReputationManager().unloadCharacterReputations(passedClientData.characterId);
                    gameServices_.getMasteryManager().unloadCharacterMasteries(passedClientData.characterId);
                    gameServices_.getQuestManager().clearFlagsLoaded(passedClientData.characterId);
                    gameServices_.getEmoteManager().unloadPlayerEmotes(passedClientData.characterId);
                }
                catch (const std::exception &ex)
                {
                    gameServices_.getLogger().logError(
                        "[DISCONNECT] Rep/Mastery unload error for characterId: " +
                        std::to_string(passedClientData.characterId) + " - " + ex.what());
                }

                // Flush Item Soul kill_count for equipped weapon so unsaved kills are not lost
                try
                {
                    auto weapon = gameServices_.getInventoryManager().getEquippedWeapon(passedClientData.characterId);
                    if (weapon.has_value() && weapon->killCount > 0)
                    {
                        nlohmann::json pkt;
                        pkt["header"]["eventType"] = "saveItemKillCount";
                        pkt["header"]["clientId"] = 0;
                        pkt["header"]["hash"] = "";
                        pkt["body"]["characterId"] = passedClientData.characterId;
                        pkt["body"]["inventoryItemId"] = weapon->id;
                        pkt["body"]["killCount"] = weapon->killCount;
                        gameServerWorker_.sendDataToGameServer(pkt.dump() + "\n");
                        log_->info("[DISCONNECT] Flushed ItemSoul killCount=" +
                                   std::to_string(weapon->killCount) +
                                   " for invId=" + std::to_string(weapon->id) +
                                   " charId=" + std::to_string(passedClientData.characterId));
                    }
                }
                catch (const std::exception &ex)
                {
                    gameServices_.getLogger().logError(
                        "[DISCONNECT] ItemSoul flush error for characterId: " +
                        std::to_string(passedClientData.characterId) + " - " + ex.what());
                }
            }

            // Prepare disconnect notification
            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Client disconnected!")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", passedClientData.clientId)
                                          .setHeader("eventType", "disconnectClient")
                                          .setBody("", "")
                                          .build();

            std::string responseData = networkManager_.generateResponseMessage("success", response);

            // Broadcast to all existing clients except the disconnecting one
            broadcastToAllClients(responseData, passedClientData.clientId);
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
