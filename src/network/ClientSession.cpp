#include "network/ClientSession.hpp"
#include "chunk_server/ChunkServer.hpp"
#include "events/EventDispatcher.hpp"
#include "handlers/MessageHandler.hpp"
#include "utils/ProtocolLimits.hpp"
#include "utils/TimestampUtils.hpp"
#include <spdlog/logger.h>

namespace {

std::string framingError(const std::string &eventType, const char *code) {
    nlohmann::json err;
    err["header"]["eventType"] = eventType;
    err["header"]["message"] = code;
    err["header"]["status"] = "error";
    err["header"]["timestamp"] = TimestampUtils::getCurrentTimestamp();
    err["header"]["version"] = protocol::kProtocolVersion;
    err["body"] = nlohmann::json::object();
    err["body"]["errorCode"] = code;
    return err.dump() + "\n";
}

} // namespace

ClientSession::ClientSession(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    ChunkServer *chunkServer,
    GameServices &gameServices,
    EventQueue &eventQueue,
    EventQueue &eventQueuePing,
    JSONParser &jsonParser,
    EventDispatcher &eventDispatcher,
    MessageHandler &messageHandler)
    : socket_(socket),
      eventQueue_(eventQueue),
      eventQueuePing_(eventQueuePing),
      chunkServer_(chunkServer),
      jsonParser_(jsonParser),
      eventDispatcher_(eventDispatcher),
      messageHandler_(messageHandler),
      gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("network");
}

void
ClientSession::start()
{
    doRead();
}

void
ClientSession::doRead()
{
    auto self(shared_from_this());
    socket_->async_read_some(boost::asio::buffer(dataBuffer_),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                // S14d: v2-only. The length IS the boundary; partial frames
                // wait for more TCP chunks (a split 4-byte prefix included).
                if (accumulatedData_.size() + bytes_transferred > protocol::kMaxAccumulatedBytesV2)
                {
                    log_->error("Buffer overflow detected, disconnecting client");
                    handleClientDisconnect();
                    return;
                }

                // Append new data to our session-specific buffer.
                accumulatedData_.append(dataBuffer_.data(), bytes_transferred);

                extractV2Messages();
                doRead();

            }
            else if (ec == boost::asio::error::eof)
            {
                log_->error("Client disconnected gracefully.");
                handleClientDisconnect();
            }
            else
            {
                log_->error("Error during async_read_some: " + ec.message());
                handleClientDisconnect();
            }
        });
}

// S14d: v2-only transport. Length-prefixed extraction; L > 4MB is a
// DoS attempt: explicit ERR_OVERSIZE + disconnect. Malformed JSON inside
// a well-formed frame follows the dispatcher contract (ERR_MALFORMED via
// processMessage, no disconnect).
void
ClientSession::extractV2Messages()
{
    size_t processed_messages = 0;
    constexpr size_t MAX_MESSAGES_PER_READ = 10;
    while (accumulatedData_.size() >= 4 && processed_messages < MAX_MESSAGES_PER_READ)
    {
        const auto *b = reinterpret_cast<const uint8_t *>(accumulatedData_.data());
        const std::uint32_t n = static_cast<std::uint32_t>(b[0]) |
                                (static_cast<std::uint32_t>(b[1]) << 8) |
                                (static_cast<std::uint32_t>(b[2]) << 16) |
                                (static_cast<std::uint32_t>(b[3]) << 24);
        if (n > protocol::kMaxFrameBytes)
        {
            gameServices_.getLogger().logError(
                std::string(protocol::kErrOversize) + ": v2 frame too large, disconnecting: " +
                    std::to_string(n) + " bytes",
                RED);
            if (sendCallback_)
            {
                try {
                    sendCallback_(socket_, framingError("unknown", protocol::kErrOversize));
                } catch (...) {}
            }
            handleClientDisconnect();
            return;
        }
        if (accumulatedData_.size() < 4 + n)
        {
            break; // partial frame: wait for more TCP chunks
        }
        processMessage(accumulatedData_.substr(4, n));
        accumulatedData_.erase(0, 4 + n);
        processed_messages++;
    }

    if (accumulatedData_.size() > protocol::kMaxAccumulatedBytesV2)
    {
        log_->error("Clearing large incomplete v2 buffer to prevent memory leak");
        accumulatedData_.clear();
    }
}

void
ClientSession::processMessage(const std::string &message)
{
    try
    {
        // First, parse only the event type to optimize for frequent ping events
        const char *data = message.data();
        size_t messageLength = message.size();

        // Create a single JSON object to avoid multiple parsing
        nlohmann::json jsonData = nlohmann::json::parse(data, data + messageLength);

        std::string eventType;
        if (jsonData.contains("header") && jsonData["header"].is_object() &&
            jsonData["header"].contains("eventType") && jsonData["header"]["eventType"].is_string())
        {
            eventType = jsonData["header"]["eventType"].get<std::string>();
        }

        // Skip ping events for unauthenticated clients to prevent memory leaks
        if (eventType == "pingClient")
        {
            // For ping events, only parse minimal client data
            ClientDataStruct clientData;
            if (jsonData.contains("header") && jsonData["header"].is_object())
            {
                if (jsonData["header"].contains("clientId") && jsonData["header"]["clientId"].is_number_integer())
                {
                    clientData.clientId = jsonData["header"]["clientId"].get<int>();
                }
                if (jsonData["header"].contains("hash") && jsonData["header"]["hash"].is_string())
                {
                    clientData.hash = jsonData["header"]["hash"].get<std::string>();
                }
            }

            // Try to look up client ID by socket if not provided
            if (clientData.clientId == 0 && socket_)
            {
                try
                {
                    int lookupClientId = gameServices_.getClientManager().getClientIdBySocket(socket_);
                    if (lookupClientId != 0)
                    {
                        clientData.clientId = lookupClientId;
                    }
                }
                catch (const std::exception &e)
                {
                    // Silent fail for ping lookups to reduce log spam
                }
            }

            // Only process ping events for authenticated clients (clientId != 0)
            if (clientData.clientId != 0)
            {
                // Liveness: any authenticated packet proves the client alive
                // (PING_TIMEOUT must never fire on chatting/moving players).
                gameServices_.getClientManager().recordPingTime(clientData.clientId);
                // Parse timestamps for ping events to support lag compensation
                TimestampStruct timestamps = jsonParser_.parseTimestamps(jsonData);
                std::string requestId = jsonParser_.parseRequestId(jsonData);
                TimestampStruct serverTimestamps = TimestampUtils::createReceiveTimestamp(timestamps.clientSendMsEcho, requestId);

                // Create minimal context for ping events with properly initialized empty structs
                CharacterDataStruct emptyCharacterData{};
                PositionStruct emptyPositionData{};
                MessageStruct emptyMessageData{};

                EventContext pingContext{eventType, clientData, emptyCharacterData, emptyPositionData, emptyMessageData, message, serverTimestamps, jsonData};
                eventDispatcher_.dispatch(pingContext, socket_);
            }
            else
            {
                // Log that we're skipping ping for unauthenticated client (reduce log spam)
                static thread_local int logCounter = 0;
                if (++logCounter % 100 == 0)
                {
                    log_->info("Skipping ping for unauthenticated client (logged every 100th occurrence)");
                }
            }
            return;
        }

        // For non-ping events, do full parsing using MessageHandler with timestamps.
        // Single-parse: jsonData was already parsed at the top of processMessage;
        // field extractors below must not re-parse the wire bytes.
        auto [fullEventType, clientData, characterData, positionData, messageStruct, timestamps] = messageHandler_.parseMessageWithTimestamps(jsonData);

        // If client ID is not provided in the message (or is 0), try to look it up by socket
        if (clientData.clientId == 0 && socket_)
        {
            try
            {
                int lookupClientId = gameServices_.getClientManager().getClientIdBySocket(socket_);
                if (lookupClientId != 0)
                {
                    clientData.clientId = lookupClientId;
                    log_->info("Client ID " + std::to_string(lookupClientId) + " resolved by socket lookup for event: " + fullEventType);
                }
                else
                {
                    log_->info("No client ID found for socket in event: " + fullEventType);
                }
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error looking up client ID by socket: " + std::string(e.what()), RED);
            }
        }

        // Populate character ID from ClientManager if client ID is valid
        if (clientData.clientId != 0)
        {
            // Liveness: any authenticated packet proves the client alive.
            gameServices_.getClientManager().recordPingTime(clientData.clientId);
            try
            {
                ClientDataStruct serverClientData = gameServices_.getClientManager().getClientData(clientData.clientId);

                // For join events, get character ID from message body instead of stored data
                if (fullEventType == "joinGameCharacter")
                {
                    try
                    {
                        // MEDIUM-3: reuse jsonData already parsed at the top of processMessage
                        // — avoids a second nlohmann::json::parse(message) call.
                        if (jsonData.contains("body") && jsonData["body"].contains("id"))
                        {
                            int messageCharacterId = jsonData["body"]["id"];
                            clientData.characterId = messageCharacterId;
                            characterData.characterId = messageCharacterId;
                            gameServices_.getLogger().log("Character ID " + std::to_string(messageCharacterId) + " extracted from message for client " + std::to_string(clientData.clientId) + " for event: " + fullEventType, GREEN);
                        }
                        else
                        {
                            clientData.characterId = serverClientData.characterId;
                            characterData.characterId = serverClientData.characterId;
                            gameServices_.getLogger().log("Character ID " + std::to_string(clientData.characterId) + " resolved from stored data for client " + std::to_string(clientData.clientId) + " for event: " + fullEventType, GREEN);
                        }
                    }
                    catch (const std::exception &parseEx)
                    {
                        // Fallback to stored data if parsing fails
                        clientData.characterId = serverClientData.characterId;
                        characterData.characterId = serverClientData.characterId;
                        gameServices_.getLogger().logError("Failed to parse character ID from message, using stored data: " + std::string(parseEx.what()), RED);
                    }
                }
                else
                {
                    // For other events, use stored character ID
                    clientData.characterId = serverClientData.characterId;
                    characterData.characterId = serverClientData.characterId;
                    gameServices_.getLogger().log("Character ID " + std::to_string(clientData.characterId) + " resolved for client " + std::to_string(clientData.clientId) + " for event: " + fullEventType, GREEN);
                }
            }
            catch (const std::exception &e)
            {
                gameServices_.getLogger().logError("Error looking up character ID for client " + std::to_string(clientData.clientId) + ": " + std::string(e.what()), RED);
            }
        }

        // Create full context for non-ping events with timestamps
        EventContext context{fullEventType, clientData, characterData, positionData, messageStruct, message, timestamps, jsonData};
        eventDispatcher_.dispatch(context, socket_);
    }
    catch (const nlohmann::json::parse_error &e)
    {
        gameServices_.getLogger().logError(
            std::string(protocol::kErrMalformed) + ": JSON parsing error: " + std::string(e.what()), RED);
        if (sendCallback_)
        {
            try {
                sendCallback_(socket_, framingError("unknown", protocol::kErrMalformed));
            } catch (...) {}
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(
            std::string(protocol::kErrMalformed) + ": message handling error: " + std::string(e.what()), RED);
        if (sendCallback_)
        {
            try {
                sendCallback_(socket_, framingError("unknown", protocol::kErrMalformed));
            } catch (...) {}
        }
    }
}

void
ClientSession::handleClientDisconnect()
{
    // Check if socket is valid before accessing it
    if (socket_)
    {
        try
        {
            // Use weak_ptr-like approach by checking reference count
            // If the socket is about to be freed, avoid accessing it
            if (socket_.use_count() > 1)
            {
                // First check if socket is still open before trying to close it
                if (socket_->is_open())
                {
                    // Try to access socket operations in a safe way
                    boost::system::error_code ec;
                    socket_->close(ec);
                    if (ec)
                    {
                        log_->error("Error closing socket: " + ec.message());
                    }
                    else
                    {
                        log_->info("Socket closed successfully during disconnect");
                    }
                }
                else
                {
                    log_->info("Socket was already closed during disconnect");
                }
            }
            else
            {
                log_->info("Socket reference count too low, avoiding access during disconnect");
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Exception while handling socket in disconnect: " + std::string(e.what()), RED);
        }
    }
    else
    {
        log_->info("Socket is null during disconnect");
    }

    // Get client ID and character ID before cleanup to prevent clientId/characterId=0 issues
    int clientId = 0;
    int characterId = 0;
    if (socket_)
    {
        clientId = gameServices_.getClientManager().getClientIdBySocket(socket_);
        if (clientId > 0)
        {
            // Read characterId while the client data is still present
            try
            {
                characterId = gameServices_.getClientManager().getClientData(clientId).characterId;
            }
            catch (const std::exception &)
            {
                // Leave characterId = 0 if lookup fails
            }
        }
    }

    // Only create disconnect event if we have valid client ID
    if (clientId > 0)
    {
        // Construct minimal disconnect event WITHOUT socket reference to avoid use-after-free
        ClientDataStruct clientData;
        clientData.clientId = clientId;       // Use real client ID, not 0
        clientData.characterId = characterId; // Carry character ID so handlers can clean up properly
        clientData.hash = "";

        std::vector<Event> eventsBatch;

        // SAFETY: Always construct EventData with in_place_type to avoid variant corruption
        EventData disconnectData{std::in_place_type<ClientDataStruct>, clientData};
        Event disconnectEvent(Event::DISCONNECT_CLIENT, clientId, disconnectData); // Use real clientId in event

        eventsBatch.push_back(disconnectEvent);
        // Push the disconnect event to the event queue FIRST,
        // before cleaning up ClientManager, so the disconnect handler
        // can still look up client data for proper cleanup.
        eventQueue_.pushBatch(eventsBatch);

        log_->info("Disconnect event created for clientId: " + std::to_string(clientId));
    }
    else
    {
        log_->info("No valid clientId found, skipping disconnect event");
    }

    // Clean up the socket from ClientManager AFTER the event is pushed
    if (socket_)
    {
        gameServices_.getClientManager().removeClientDataBySocket(socket_);
    }

    // Notify NetworkManager about session cleanup
    notifyCleanup();
}

void
ClientSession::setCleanupCallback(std::function<void(std::shared_ptr<ClientSession>)> callback)
{
    cleanupCallback_ = callback;
}

void
ClientSession::setSendCallback(
    std::function<void(std::shared_ptr<boost::asio::ip::tcp::socket>, const std::string &)> callback)
{
    sendCallback_ = std::move(callback);
}

void
ClientSession::notifyCleanup()
{
    if (cleanupCallback_)
    {
        // Use shared_from_this to ensure safe cleanup
        auto self = shared_from_this();
        cleanupCallback_(self);
    }
}
