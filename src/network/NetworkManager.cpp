#include "network/NetworkManager.hpp"

NetworkManager::NetworkManager(EventQueue& eventQueue, std::tuple<ChunkServerConfig> &configs, Logger &logger)
    : acceptor_(io_context_),
      logger_(logger),
      configs_(configs),
      jsonParser_(),
      eventQueue_(eventQueue)
{
    boost::system::error_code ec;

    // Get the custom port and IP address from the configs
    short customPort = std::get<0>(configs).port;
    std::string customIP = std::get<0>(configs).host;
    short maxClients = std::get<0>(configs).max_clients;

    // Create an endpoint with the custom IP and port
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(customIP), customPort);

    // Open the acceptor and bind it to the endpoint
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec)
    {
        logger_.log("Starting Chunk Server...", YELLOW);
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(maxClients, ec);
    }

    if (ec)
    {
        logger.logError("Error during server initialization: " + ec.message(), RED);
        return;
    }

    // Print IP address and port when the server starts
    logger_.log("Chunk Server started on IP: " + customIP + ", Port: " + std::to_string(customPort), GREEN);
}

void NetworkManager::startAccept()
{
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*clientSocket, [this, clientSocket](const boost::system::error_code &error)
                           {
            if (!error) {
                // Get the Game Server as Client remote endpoint (IP address and port)
                boost::asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
                std::string clientIP = remoteEndpoint.address().to_string();

                // Print the Game Server as Client IP address
                logger_.log("New Game Server with IP: " + clientIP + " connected!", GREEN);
                
                // Start reading data from the client
                startReadingFromClient(clientSocket);
            }

            // Continue accepting new connections even if there's an error
            startAccept(); });
}

    void NetworkManager::startIOEventLoop()
    {
        logger_.log("Starting Chunk Server IO Context...", YELLOW);

        // Start io_service in a separate thread
        networkManagerThread_ = std::thread([this]()
                                { io_context_.run(); });

    }

    NetworkManager::~NetworkManager()
    {
        logger_.log("Network Manager destructor is called...", RED);
        // Close the acceptor and all client sockets
        acceptor_.close();
        io_context_.stop();
        networkManagerThread_.join();
    }


void NetworkManager::handleClientData(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::array<char, max_length> &dataBuffer, size_t bytes_transferred)
{
    try
    {
        logger_.log("Data received from Game Server. Bytes transferred: " + std::to_string(bytes_transferred), YELLOW);

        // Parse the data received from the client using JSONParser
        std::string eventType = jsonParser_.parseEventType(dataBuffer, bytes_transferred);
        ClientDataStruct clientData = jsonParser_.parseClientData(dataBuffer, bytes_transferred);
        CharacterDataStruct characterData = jsonParser_.parseCharacterData(dataBuffer, bytes_transferred);
        PositionStruct positionData = jsonParser_.parsePositionData(dataBuffer, bytes_transferred);
        MessageStruct message = jsonParser_.parseMessage(dataBuffer, bytes_transferred);
        // Create a new events batch
        std::vector<Event> eventsBatch;

        // Define a constant for the batch size
        constexpr int BATCH_SIZE = 10;

        // Check if the type of request is joinGame
        if (eventType == "joinGame" && message.status == "success")
        {
            // Set the client data
            characterData.characterPosition = positionData;
            clientData.characterData = characterData;

            // Create a new event and push it to the queue
            Event joinGameEvent(Event::JOIN_GAME, clientData.clientId, characterData.characterId, clientData, clientSocket);
            //eventQueue_.push(joinGameEvent);

            // Add the event to the current batch
            eventsBatch.push_back(joinGameEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE) {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is getConnectedCharacters
        if (eventType == "getConnectedCharacters" && message.status == "success")
        {
            // Set the client data
           // characterData.characterPosition = positionData;
            //clientData.characterData = characterData;

            // Create a new event and push it to the queue
            Event getConnectedCharactersEvent(Event::GET_CONNECTED_CHARACTERS, clientData.clientId, characterData.characterId, clientData, clientSocket);
            //eventQueue_.push(getConnectedCharactersEvent);

            // Add the event to the current batch
            eventsBatch.push_back(getConnectedCharactersEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE) {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is moveCharacter
        if (eventType == "moveCharacter" && message.status == "success")
        {
            // Set the client data
            //characterData.characterPosition = positionData;
            //clientData.characterData = characterData;

            // Create a new event and push it to the queue
            Event characterMovementEvent(Event::CHARACTER_MOVEMENT, clientData.clientId, characterData.characterId, positionData, clientSocket);
            //eventQueue_.push(characterMovementEvent);

            // Add the event to the current batch
            eventsBatch.push_back(characterMovementEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE) {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is disconnectClient
        if (eventType == "disconnectClient" && message.status == "success")
        {
            // Set the client data
            //characterData.characterPosition = positionData;
            //clientData.characterData = characterData;

            // Create a new event and push it to the queue
            Event disconnectClientEvent(Event::DISCONNECT_CLIENT, clientData.clientId, characterData.characterId, clientData, clientSocket);
           // eventQueue_.push(disconnectClientEvent);

            // Add the event to the current batch
            eventsBatch.push_back(disconnectClientEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE) {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }

        // Check if the type of request is pingClient
        if (eventType == "pingClient" && message.status == "success")
        {
            // Set the client data
            //characterData.characterPosition = positionData;
            //clientData.characterData = characterData;

            // Create a new event and push it to the queue
            Event pingClientEvent(Event::PING_CLIENT, clientData.clientId, characterData.characterId, clientData, clientSocket);

            //eventQueue_.push(pingClientEvent);

            // Add the event to the current batch
            eventsBatch.push_back(pingClientEvent);

            // If the batch size has been reached, push the batch to the event queue
            if (eventsBatch.size() >= BATCH_SIZE) {
                eventQueue_.pushBatch(eventsBatch);
                // Clear the batch for the next set of events
                eventsBatch.clear();
            }
        }


        // Push any remaining events in the batch to the event queue
        if (!eventsBatch.empty()) {
            eventQueue_.pushBatch(eventsBatch);
        }
    }
    catch (const nlohmann::json::parse_error &e)
    {
        logger_.logError("JSON parsing error: " + std::string(e.what()));
    }
}

void NetworkManager::sendResponse(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket, const std::string &responseString)
{
    boost::asio::async_write(*clientSocket, boost::asio::buffer(responseString),
                             [this, clientSocket](const boost::system::error_code &error, size_t bytes_transferred)
                             {
                                 logger_.log("Data sent successfully to Game Server. Bytes transferred: " + std::to_string(bytes_transferred), BLUE);

                                 if (!error)
                                 {
                                     // Response sent successfully, now start listening for the client's next message
                                     startReadingFromClient(clientSocket);
                                 }
                                 else
                                 {
                                     logger_.logError("Error during async_write: " + error.message());
                                 }
                             });
}

void NetworkManager::startReadingFromClient(std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    auto dataBuffer = std::make_shared<std::array<char, max_length>>();
    clientSocket->async_read_some(boost::asio::buffer(*dataBuffer),
                                  [this, clientSocket, dataBuffer](const boost::system::error_code &error, size_t bytes_transferred)
                                  {
                                      if (!error)
                                      {
                                          // Data has been read successfully, handle it
                                          handleClientData(clientSocket, *dataBuffer, bytes_transferred);

                                          // Continue reading from the client
                                          startReadingFromClient(clientSocket);
                                      }
                                      else if (error == boost::asio::error::eof)
                                      {

                                          // The client has closed the connection
                                          logger_.logError("Client disconnected gracefully.");

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else if (error == boost::asio::error::operation_aborted)
                                      {
                                          // The read operation was canceled, likely due to the client disconnecting
                                          logger_.logError("Read operation canceled (client disconnected).");

                                          // You can perform any cleanup or logging here if needed

                                          // Close the client socket
                                          clientSocket->close();
                                      }
                                      else
                                      {
                                          // Handle other errors
                                          logger_.logError("Error during async_read_some: " + error.message());

                                          // You can also close the socket in case of other errors if needed
                                          clientSocket->close();
                                      }
                                  });
}

std::string NetworkManager::generateResponseMessage(const std::string &status, const nlohmann::json &message)
{
    nlohmann::json response;
    std::string currentTimestamp = logger_.getCurrentTimestamp();

    response["header"] = message["header"];
    response["header"]["status"] = status;
    response["header"]["timestamp"] = currentTimestamp;
    response["header"]["version"] = "1.0";
    response["body"] = message["body"];

    std::string responseString = response.dump();

    //logger_.log("Response generated: " + responseString, YELLOW);

    return responseString+ "\n";
}