#include "network/GameServerWorker.hpp"
#include <chrono>
#include <cstdlib>

// Work with data from/to the Game Server
GameServerWorker::GameServerWorker(EventQueue &eventQueue,
    std::tuple<GameServerConfig, ChunkServerConfig> &configs,
    Logger &logger)
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

void
GameServerWorker::startIOEventLoop()
{
    logger_.log("Starting Game Server IO Context...", YELLOW);
    auto numThreads = std::max(1u, std::thread::hardware_concurrency());
    for (size_t i = 0; i < numThreads; ++i)
    {
        io_threads_.emplace_back([this]()
            { io_context_game_server_.run(); });
    }
}

GameServerWorker::~GameServerWorker()
{
    logger_.logError("Game Server destructor is called...", RED);
    work_.reset();
    for (auto &thread : io_threads_)
    {
        if (thread.joinable())
            thread.join();
    }
    if (game_server_socket_->is_open())
        game_server_socket_->close();
}

void
GameServerWorker::connect(boost::asio::ip::tcp::resolver::results_type endpoints, int currentRetryCount)
{
    boost::asio::async_connect(*game_server_socket_, endpoints, [this, endpoints, currentRetryCount](const boost::system::error_code &ec, boost::asio::ip::tcp::endpoint)
        {
            if (!ec) {
                logger_.log("Connected to the Game Server!", GREEN);
                
                // Send handshake connection info to the game server
                nlohmann::json handshake;
                handshake["header"]["eventType"] = "chunkServerConnection";
                handshake["header"]["id"] = 1;
                handshake["header"]["ip"] = chunkServerConfig_.host;
                handshake["header"]["port"] = chunkServerConfig_.port;
                // add delimiter string to the dump
                std::string delimiter = "\n";
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
            } });
}

void
GameServerWorker::sendDataToGameServer(const std::string &data)
{
    try
    {
        boost::asio::async_write(*game_server_socket_, boost::asio::buffer(data), [this, data](const boost::system::error_code &error, size_t bytes_transferred)
            {
                if (!error) {
                    logger_.log("Bytes sent: " + std::to_string(bytes_transferred), BLUE);

                    // Log the data that was sent
                    logger_.log("Data: " + data, BLUE);

                    // Log the sent data
                    logger_.log("Data sent successfully to the Game Server", BLUE);

                } else {
                    logger_.logError("Error in sending data to Game Server: " + error.message());
                } });
    }
    catch (const std::exception &e)
    {
        logger_.logError("Exception in sendDataToGameServer: " + std::string(e.what()));
    }
}

void
GameServerWorker::processGameServerData(const std::array<char, 12096> &buffer, std::size_t bytes_transferred)
{
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

    // spawn zones
    std::vector<SpawnZoneStruct> spawnZonesList = jsonParser_.parseSpawnZonesList(buffer.data(), bytes_transferred);

    // mobs
    std::vector<MobDataStruct> mobsList = jsonParser_.parseMobsList(buffer.data(), bytes_transferred);
    std::vector<MobAttributeStruct> mobsAttributesList = jsonParser_.parseMobsAttributesList(buffer.data(), bytes_transferred);
    std::vector<std::pair<int, std::vector<SkillStruct>>> mobsSkillsMapping = jsonParser_.parseMobsSkillsMapping(buffer.data(), bytes_transferred);

    std::vector<Event> eventsBatch;
    constexpr int BATCH_SIZE = 10;

    // set chunk data
    if (eventType == "setChunkData")
    {
        // set chunk data
        Event setChunkDataEvent(Event::SET_CHUNK_DATA, clientData.clientId, chunkInfo);
        eventsBatch.push_back(setChunkDataEvent);
    }

    if (eventType == "setCharacterData")
    {
        characterData.characterPosition = positionData; // Set character position from parsed data
        characterData.clientId = clientData.clientId;   // Set client ID for the character
        // set character data
        Event setCharacterDataEvent(Event::SET_CHARACTER_DATA, clientData.clientId, characterData);
        eventsBatch.push_back(setCharacterDataEvent);
    }

    if (eventType == "setCharacterAttributes")
    {
        // set character attributes
        Event setCharacterAttributesEvent(Event::SET_CHARACTER_ATTRIBUTES, clientData.clientId, characterAttributesList);
        eventsBatch.push_back(setCharacterAttributesEvent);
    }

    if (eventType == "setSpawnZonesList")
    {
        // set spawn zones list
        Event setSpawnZonesEvent(Event::SET_ALL_SPAWN_ZONES, clientData.clientId, spawnZonesList);
        eventsBatch.push_back(setSpawnZonesEvent);
    }

    if (eventType == "setMobsList")
    {
        // set mobs list
        Event setMobsListEvent(Event::SET_ALL_MOBS_LIST, clientData.clientId, mobsList);
        eventsBatch.push_back(setMobsListEvent);
    }

    if (eventType == "setMobsAttributes")
    {
        // set mobs attributes
        Event setMobsAttributesEvent(Event::SET_ALL_MOBS_ATTRIBUTES, clientData.clientId, mobsAttributesList);
        eventsBatch.push_back(setMobsAttributesEvent);
    }

    if (eventType == "setMobsSkills")
    {
        // set mobs skills
        Event setMobsSkillsEvent(Event::SET_ALL_MOBS_SKILLS, clientData.clientId, mobsSkillsMapping);
        eventsBatch.push_back(setMobsSkillsEvent);
    }

    if (eventType == "getItemsList")
    {
        // Parse items list from the Game Server
        std::vector<ItemDataStruct> itemsList = jsonParser_.parseItemsList(buffer.data(), bytes_transferred);
        Event setItemsListEvent(Event::SET_ALL_ITEMS_LIST, clientData.clientId, itemsList);
        eventsBatch.push_back(setItemsListEvent);
    }

    if (eventType == "getMobLootInfo")
    {
        // Parse mob loot info from the Game Server
        std::vector<MobLootInfoStruct> mobLootInfo = jsonParser_.parseMobLootInfo(buffer.data(), bytes_transferred);
        Event setMobLootInfoEvent(Event::SET_MOB_LOOT_INFO, clientData.clientId, mobLootInfo);
        eventsBatch.push_back(setMobLootInfoEvent);
    }

    if (eventType == "getExpLevelTable")
    {
        // Parse experience level table from the Game Server
        std::vector<ExperienceLevelEntry> expLevelTable = jsonParser_.parseExpLevelTable(buffer.data(), bytes_transferred);
        Event setExpLevelTableEvent(Event::SET_EXP_LEVEL_TABLE, clientData.clientId, expLevelTable);
        eventsBatch.push_back(setExpLevelTableEvent);
    }

    // Push batched events to the event queue
    if (!eventsBatch.empty())
    {
        eventQueue_.pushBatch(eventsBatch);
    }
}

void
GameServerWorker::receiveDataFromGameServer()
{
    auto dataBufferGameServer = std::make_shared<std::array<char, 12096>>();
    game_server_socket_->async_read_some(boost::asio::buffer(*dataBufferGameServer),
        [this, dataBufferGameServer](const boost::system::error_code &ec, std::size_t bytes_transferred)
        {
            if (!ec)
            {
                std::string received(dataBufferGameServer->data(), bytes_transferred);
                receiveBuffer_ += received;

                std::size_t newlinePos = 0;
                while ((newlinePos = receiveBuffer_.find('\n')) != std::string::npos)
                {
                    std::string oneMessage = receiveBuffer_.substr(0, newlinePos);
                    receiveBuffer_.erase(0, newlinePos + 1); // удаляем обработанное

                    // Проверяем размер сообщения
                    if (oneMessage.size() > 12096)
                    {
                        logger_.logError("Message too large: " + std::to_string(oneMessage.size()) + " bytes. Truncating to 12096 bytes.");
                        logger_.logError("Message preview: " + oneMessage.substr(0, 100) + "...");
                    }

                    // безопасно копируем в std::array и передаём в старую логику
                    std::array<char, 12096> tempBuf{};
                    std::size_t copySize = std::min(oneMessage.size(), tempBuf.size());
                    std::memcpy(tempBuf.data(), oneMessage.data(), copySize);
                    processGameServerData(tempBuf, copySize);
                }

                receiveDataFromGameServer();
            }
            else
            {
                logger_.logError("Error in receiving data from Game Server: " + ec.message());
            }
        });
}

void
GameServerWorker::closeConnection()
{
    if (game_server_socket_->is_open())
    {
        game_server_socket_->close();
    }
}
