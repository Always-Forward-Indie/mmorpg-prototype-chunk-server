#include "network/GameServerWorker.hpp"
#include <chrono>
#include <cstdlib>

// Work with data from/to the Game Server
GameServerWorker::GameServerWorker(EventQueue &eventQueue,
                                     std::tuple<GameServerConfig, ChunkServerConfig>& configs, Logger &logger)
    : io_context_game_server_(),
      work_(boost::asio::make_work_guard(io_context_game_server_)),
      game_server_socket_(std::make_shared<boost::asio::ip::tcp::socket>(io_context_game_server_)),
      retry_timer_(io_context_game_server_),
      eventQueue_(eventQueue),
      logger_(logger),
      jsonParser_(),
      gameServerConfig_(std::get<0>(configs)),
      chunkServerConfig_(std::get<1>(configs))
{
    short port = std::get<0>(configs).port;
    std::string host = std::get<0>(configs).host;

    boost::asio::ip::tcp::resolver resolver(io_context_game_server_);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    logger_.log("Connecting to the Game Server on IP: " + host + " Port: " + std::to_string(port), YELLOW);
    connect(endpoints, 0); // Start connection to the Game Server
}

void GameServerWorker::startIOEventLoop() {
    logger_.log("Starting Game Server IO Context...", YELLOW);
    auto numThreads = std::max(1u, std::thread::hardware_concurrency());
    for (size_t i = 0; i < numThreads; ++i) {
        io_threads_.emplace_back([this]() { io_context_game_server_.run(); });
    }
}

GameServerWorker::~GameServerWorker() {
    logger_.logError("Game Server destructor is called...", RED);
    work_.reset();
    for (auto &thread : io_threads_) {
        if (thread.joinable())
            thread.join();
    }
    if (game_server_socket_->is_open())
    game_server_socket_->close();
}

void GameServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints, int currentRetryCount) {
    boost::asio::async_connect(*game_server_socket_, endpoints,
        [this, endpoints, currentRetryCount](const boost::system::error_code &ec, boost::asio::ip::tcp::endpoint) {
            if (!ec) {
                logger_.log("Connected to the Game Server!", GREEN);
                
                // Send handshake connection info to the game server
                nlohmann::json handshake;
                handshake["header"]["eventType"] = "chunkServerConnection";
                handshake["header"]["id"] = 1;
                handshake["header"]["ip"] = chunkServerConfig_.host;
                handshake["header"]["port"] = chunkServerConfig_.port;
                // add delimiter string to the dump
                std::string delimiter = "\r\n\r\n";
                std::string handshakeMessage = handshake.dump() += delimiter;

                sendDataToGameServer(handshakeMessage);


                receiveDataFromGameServer(); // Receive data from the Game Server
            } else {
                logger_.logError("Error connecting to the Game Server: " + ec.message());
                if (currentRetryCount < MAX_RETRY_COUNT) {
                    // Exponential backoff for retrying connection
                    int waitTime = RETRY_TIMEOUT * (1 << currentRetryCount);
                    retry_timer_.expires_after(std::chrono::seconds(waitTime));
                    retry_timer_.async_wait([this, endpoints, currentRetryCount](const boost::system::error_code &ecTimer) {
                        if (!ecTimer) {
                            logger_.log("Retrying connection to Game Server...", YELLOW);
                            connect(endpoints, currentRetryCount + 1);
                        }
                    });
                } else {
                    logger_.logError("Max retry count reached. Exiting...");
                    exit(1);
                }
            }
        });
}

void GameServerWorker::sendDataToGameServer(const std::string &data) {
    try {
        boost::asio::async_write(*game_server_socket_, boost::asio::buffer(data),
            [this, data](const boost::system::error_code &error, size_t bytes_transferred) {
                if (!error) {
                    logger_.log("Bytes sent: " + std::to_string(bytes_transferred), BLUE);

                    // Log the data that was sent
                    logger_.log("Data: " + data, BLUE);

                    // Log the sent data
                    logger_.log("Data sent successfully to the Game Server", BLUE);

                } else {
                    logger_.logError("Error in sending data to Game Server: " + error.message());
                }
            });
    } catch (const std::exception &e) {
        logger_.logError("Exception in sendDataToGameServer: " + std::string(e.what()));
    }
}

void GameServerWorker::processGameServerData(const std::array<char, 1024>& buffer, std::size_t bytes_transferred) {
    // Convert received data to string
    std::string receivedData(buffer.data(), bytes_transferred);
    logger_.log("Received data from Game Server: " + receivedData, YELLOW);

    // Client data
    std::string eventType = jsonParser_.parseEventType(buffer.data(), bytes_transferred);
    ClientDataStruct clientData = jsonParser_.parseClientData(buffer.data(), bytes_transferred);

    // Chunk data
    ChunkInfoStruct chunkInfo = jsonParser_.parseChunkInfo(buffer.data(), bytes_transferred);

    // Character data
    CharacterDataStruct characterData = jsonParser_.parseCharacterData(buffer.data(), bytes_transferred);
    std::vector<CharacterDataStruct> charactersList = jsonParser_.parseCharactersList(buffer.data(), bytes_transferred);
    std::vector<CharacterAttributeStruct> characterAttributesList = jsonParser_.parseCharacterAttributesList(buffer.data(), bytes_transferred);

    // Position data
    PositionStruct positionData = jsonParser_.parsePositionData(buffer.data(), bytes_transferred);

    //spawn zones
    std::vector<SpawnZoneStruct> spawnZonesList = jsonParser_.parseSpawnZonesList(buffer.data(), bytes_transferred);

    //mobs
    std::vector<MobDataStruct> mobsList = jsonParser_.parseMobsList(buffer.data(), bytes_transferred);
    std::vector<MobAttributeStruct> mobsAttributesList = jsonParser_.parseMobsAttributesList(buffer.data(), bytes_transferred);

    std::vector<Event> eventsBatch;
    constexpr int BATCH_SIZE = 10;

    // Update character data
    //characterData.characterPosition = positionData;
    //clientData.characterData = characterData;

    //TODO - Analyze code for events and refactor it
    if (eventType == "joinGame" && !clientData.hash.empty() && clientData.clientId != 0) {
        // Update joined client data
        Event joinClientEvent(Event::JOIN_CLIENT_CHUNK, clientData.clientId, clientData, game_server_socket_);
        eventsBatch.push_back(joinClientEvent);
    }

    if (eventType == "disconnectClient" && clientData.clientId != 0) {
        // remove client data
        Event disconnectClientEvent(Event::DISCONNECT_CLIENT, clientData.clientId, clientData, game_server_socket_);
        eventsBatch.push_back(disconnectClientEvent);
    }

    // set chunk data
    if (eventType == "setChunkData") {
        // set chunk data
        Event setChunkDataEvent(Event::SET_CHUNK_DATA, clientData.clientId, chunkInfo, game_server_socket_);
        eventsBatch.push_back(setChunkDataEvent);
    }

    if (eventType == "setConnectedCharacters") {
        // set connected characters list
        Event setConnectedCharactersEvent(Event::SET_CONNECTED_CHARACTERS_LIST, clientData.clientId, charactersList, game_server_socket_);
        eventsBatch.push_back(setConnectedCharactersEvent);
    }

    if (eventType == "setCharacterData") {
        // set character data
        Event setCharacterDataEvent(Event::SET_CHARACTER_DATA, clientData.clientId, characterData, game_server_socket_);
        eventsBatch.push_back(setCharacterDataEvent);
    }

    if (eventType == "setCharacterAttributes") {
        // set character attributes
        Event setCharacterAttributesEvent(Event::SET_CHARACTER_ATTRIBUTES, clientData.clientId, characterAttributesList, game_server_socket_);
        eventsBatch.push_back(setCharacterAttributesEvent);
    }

    if (eventType == "setAllSpawnZonesList") {
        // set spawn zones list
        Event setSpawnZonesEvent(Event::SET_ALL_SPAWN_ZONES, clientData.clientId, spawnZonesList, game_server_socket_);
        eventsBatch.push_back(setSpawnZonesEvent);
    }

    if (eventType == "setAllMobsList") {
        // set mobs list
        Event setMobsListEvent(Event::SET_ALL_MOBS_LIST, clientData.clientId, mobsList, game_server_socket_);
        eventsBatch.push_back(setMobsListEvent);
    }

    if (eventType == "setAllMobsAttributes") {
        // set mobs attributes
        Event setMobsAttributesEvent(Event::SET_ALL_MOBS_ATTRIBUTES, clientData.clientId, mobsAttributesList, game_server_socket_);
        eventsBatch.push_back(setMobsAttributesEvent);
    }

    // Push batched events to the event queue
    if (!eventsBatch.empty()) {
        eventQueue_.pushBatch(eventsBatch);
    }
}

void GameServerWorker::receiveDataFromGameServer() {
    auto dataBufferGameServer = std::make_shared<std::array<char, 1024>>();
    game_server_socket_->async_read_some(boost::asio::buffer(*dataBufferGameServer),
        [this, dataBufferGameServer](const boost::system::error_code &ec, std::size_t bytes_transferred) {
            if (!ec) {
                boost::system::error_code ecEndpoint;
                boost::asio::ip::tcp::endpoint remoteEndpoint = game_server_socket_->remote_endpoint(ecEndpoint);
                if (!ecEndpoint) {
                    std::string ipAddress = remoteEndpoint.address().to_string();
                    std::string portNumber = std::to_string(remoteEndpoint.port());
                    logger_.log("Bytes received: " + std::to_string(bytes_transferred), YELLOW);
                    logger_.log("Data received from Game Server - IP: " + ipAddress + " Port: " + portNumber, YELLOW);
                }
                // Process received data
                processGameServerData(*dataBufferGameServer, bytes_transferred);
                // Continue reading data from the Game Server
                receiveDataFromGameServer();
            } else {
                logger_.logError("Error in receiving data from Game Server: " + ec.message());
            }
        });
}

void GameServerWorker::closeConnection() {
    if (game_server_socket_->is_open()) {
        game_server_socket_->close();
    }
}
