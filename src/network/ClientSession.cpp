#include "network/ClientSession.hpp"
#include "chunk_server/ChunkServer.hpp"
#include "events/EventDispatcher.hpp"
#include "handlers/MessageHandler.hpp"

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
                // Check for accumulated buffer size limit to prevent memory leaks
                constexpr size_t MAX_BUFFER_SIZE = 64 * 1024; // 64KB limit
                if (accumulatedData_.size() + bytes_transferred > MAX_BUFFER_SIZE)
                {
                    gameServices_.getLogger().logError("Buffer overflow detected, disconnecting client", RED);
                    handleClientDisconnect();
                    return;
                }

                // Append new data to our session-specific buffer.
                accumulatedData_.append(dataBuffer_.data(), bytes_transferred);
                std::string delimiter = "\n";
                size_t pos;
                size_t processed_messages = 0;
                constexpr size_t MAX_MESSAGES_PER_READ = 10; // Limit messages per read cycle

                // Process all complete messages found.
                while ((pos = accumulatedData_.find(delimiter)) != std::string::npos && processed_messages < MAX_MESSAGES_PER_READ)
                {
                    std::string message = accumulatedData_.substr(0, pos);

                    // Check message size limit
                    constexpr size_t MAX_MESSAGE_SIZE = 8 * 1024; // 8KB per message
                    if (message.size() > MAX_MESSAGE_SIZE)
                    {
                        gameServices_.getLogger().logError("Message too large, skipping: " + std::to_string(message.size()) + " bytes", RED);
                        accumulatedData_.erase(0, pos + delimiter.size());
                        processed_messages++;
                        continue;
                    }

                    gameServices_.getLogger().log("Received data from Client: " + message, YELLOW);
                    processMessage(message);
                    accumulatedData_.erase(0, pos + delimiter.size());
                    processed_messages++;
                }

                // If accumulated buffer is getting large without complete messages, clear it
                if (accumulatedData_.size() > MAX_BUFFER_SIZE / 2 && accumulatedData_.find(delimiter) == std::string::npos)
                {
                    gameServices_.getLogger().logError("Clearing large incomplete buffer to prevent memory leak", RED);
                    accumulatedData_.clear();
                }

                // Periodically shrink the buffer to prevent memory fragmentation
                if (accumulatedData_.capacity() > MAX_BUFFER_SIZE && accumulatedData_.size() < MAX_BUFFER_SIZE / 4)
                {
                    std::string temp = accumulatedData_;
                    accumulatedData_.clear();
                    accumulatedData_.shrink_to_fit();
                    accumulatedData_ = std::move(temp);
                }

                doRead();
            }
            else if (ec == boost::asio::error::eof)
            {
                gameServices_.getLogger().logError("Client disconnected gracefully.", RED);
                handleClientDisconnect();
            }
            else
            {
                gameServices_.getLogger().logError("Error during async_read_some: " + ec.message(), RED);
                handleClientDisconnect();
            }
        });
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
                // Create minimal context for ping events with properly initialized empty structs
                CharacterDataStruct emptyCharacterData{};
                PositionStruct emptyPositionData{};
                MessageStruct emptyMessageData{};

                EventContext pingContext{eventType, clientData, emptyCharacterData, emptyPositionData, emptyMessageData, message};
                eventDispatcher_.dispatch(pingContext, socket_);
            }
            else
            {
                // Log that we're skipping ping for unauthenticated client (reduce log spam)
                static thread_local int logCounter = 0;
                if (++logCounter % 100 == 0)
                {
                    gameServices_.getLogger().log("Skipping ping for unauthenticated client (logged every 100th occurrence)", GREEN);
                }
            }
            return;
        }

        // For non-ping events, do full parsing using MessageHandler
        auto [fullEventType, clientData, characterData, positionData, messageStruct] = messageHandler_.parseMessage(message);

        // If client ID is not provided in the message (or is 0), try to look it up by socket
        if (clientData.clientId == 0 && socket_)
        {
            try
            {
                int lookupClientId = gameServices_.getClientManager().getClientIdBySocket(socket_);
                if (lookupClientId != 0)
                {
                    clientData.clientId = lookupClientId;
                    gameServices_.getLogger().log("Client ID " + std::to_string(lookupClientId) + " resolved by socket lookup for event: " + fullEventType, GREEN);
                }
                else
                {
                    gameServices_.getLogger().log("No client ID found for socket in event: " + fullEventType, YELLOW);
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
            try
            {
                ClientDataStruct serverClientData = gameServices_.getClientManager().getClientData(clientData.clientId);

                // For join events, get character ID from message body instead of stored data
                if (fullEventType == "joinGameClient" || fullEventType == "joinGameCharacter")
                {
                    try
                    {
                        nlohmann::json msgJson = nlohmann::json::parse(message);
                        if (msgJson.contains("body") && msgJson["body"].contains("id"))
                        {
                            int messageCharacterId = msgJson["body"]["id"];
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

        // Create full context for non-ping events
        EventContext context{fullEventType, clientData, characterData, positionData, messageStruct, message};
        eventDispatcher_.dispatch(context, socket_);
    }
    catch (const nlohmann::json::parse_error &e)
    {
        gameServices_.getLogger().logError("JSON parsing error: " + std::string(e.what()), RED);
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
                        gameServices_.getLogger().logError("Error closing socket: " + ec.message(), RED);
                    }
                    else
                    {
                        gameServices_.getLogger().log("Socket closed successfully during disconnect", GREEN);
                    }
                }
                else
                {
                    gameServices_.getLogger().log("Socket was already closed during disconnect", GREEN);
                }
            }
            else
            {
                gameServices_.getLogger().log("Socket reference count too low, avoiding access during disconnect", YELLOW);
            }
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Exception while handling socket in disconnect: " + std::string(e.what()), RED);
        }
    }
    else
    {
        gameServices_.getLogger().log("Socket is null during disconnect", GREEN);
    }

    // Get client ID before cleanup to prevent clientId=0 issues
    int clientId = 0;
    if (socket_)
    {
        clientId = gameServices_.getClientManager().getClientIdBySocket(socket_);
    }

    // Clean up the socket from ClientManager if it exists
    if (socket_)
    {
        gameServices_.getClientManager().removeClientDataBySocket(socket_);
    }

    // Only create disconnect event if we have valid client ID
    if (clientId > 0)
    {
        // Construct minimal disconnect event WITHOUT socket reference to avoid use-after-free
        ClientDataStruct clientData;
        clientData.clientId = clientId; // Use real client ID, not 0
        clientData.hash = "";

        std::vector<Event> eventsBatch;

        // SAFETY: Always construct EventData with in_place_type to avoid variant corruption
        EventData disconnectData{std::in_place_type<ClientDataStruct>, clientData};
        Event disconnectEvent(Event::DISCONNECT_CLIENT, clientId, disconnectData); // Use real clientId in event

        eventsBatch.push_back(disconnectEvent);
        // Push the disconnect event to the event queue
        eventQueue_.pushBatch(eventsBatch);

        gameServices_.getLogger().log("Disconnect event created for clientId: " + std::to_string(clientId), GREEN);
    }
    else
    {
        gameServices_.getLogger().log("No valid clientId found, skipping disconnect event", YELLOW);
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
ClientSession::notifyCleanup()
{
    if (cleanupCallback_)
    {
        // Use shared_from_this to ensure safe cleanup
        auto self = shared_from_this();
        cleanupCallback_(self);
    }
}
