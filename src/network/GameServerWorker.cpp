#include "network/GameServerWorker.hpp"
#include <chrono>
#include <cstdlib>
#include <spdlog/logger.h>

// Work with data from/to the Game Server
GameServerWorker::GameServerWorker(EventQueue &eventQueue,
    std::tuple<GameServerConfig, ChunkServerConfig> &configs,
    Logger &logger)
    : io_context_game_server_(),
      strand_(boost::asio::make_strand(io_context_game_server_)), // CRITICAL-2
      work_(boost::asio::make_work_guard(io_context_game_server_)),
      game_server_socket_(std::make_shared<boost::asio::ip::tcp::socket>(io_context_game_server_)),
      retry_timer_(io_context_game_server_),
      eventQueue_(eventQueue),
      logger_(logger),
      jsonParser_(),
      gameServerConfig_(std::get<0>(configs)),
      chunkServerConfig_(std::get<1>(configs))
{
    log_ = logger.getSystem("network");
    short port = std::get<0>(configs).port;
    std::string host = std::get<0>(configs).host;

    boost::asio::ip::tcp::resolver resolver(io_context_game_server_);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    endpoints_ = endpoints; // LOW-8: store for reconnect

    log_->info("Connecting to the Game Server on IP: " + host + " Port: " + std::to_string(port));
    connect(endpoints_, 0); // Start connection to the Game Server
}

void
GameServerWorker::startIOEventLoop()
{
    log_->info("Starting Game Server IO Context...");
    auto numThreads = std::max(1u, std::thread::hardware_concurrency());
    for (size_t i = 0; i < numThreads; ++i)
    {
        io_threads_.emplace_back([this]()
            { io_context_game_server_.run(); });
    }
}

GameServerWorker::~GameServerWorker()
{
    log_->error("Game Server destructor is called...");
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
                log_->info("Connected to the Game Server!");
                
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
                log_->error("Error connecting to the Game Server: " + ec.message());
                if (currentRetryCount < MAX_RETRY_COUNT) {
                    // Exponential backoff for retrying connection
                    int waitTime = RETRY_TIMEOUT * (1 << currentRetryCount);
                    retry_timer_.expires_after(std::chrono::seconds(waitTime));
                    retry_timer_.async_wait([this, endpoints, currentRetryCount](const boost::system::error_code &ecTimer) {
                        if (!ecTimer) {
                            log_->info("Retrying connection to Game Server...");
                            connect(endpoints, currentRetryCount + 1);
                        }
                    });
                } else {
                    log_->error("Max retry count reached. Exiting...");
                    exit(1);
                }
            } });
}

void
GameServerWorker::sendDataToGameServer(const std::string &data)
{
    // CRITICAL-2: post onto strand so sendQueue_ and writePending_ are only
    // accessed from one logical thread, eliminating concurrent async_write.
    boost::asio::post(strand_, [this, data]()
        {
        sendQueue_.push(data);
        if (!writePending_)
            doNextWrite(); });
}

void
GameServerWorker::doNextWrite()
{
    if (sendQueue_.empty())
    {
        writePending_ = false;
        return;
    }
    writePending_ = true;
    auto payload = std::make_shared<std::string>(std::move(sendQueue_.front()));
    sendQueue_.pop();
    boost::asio::async_write(
        *game_server_socket_,
        boost::asio::buffer(*payload),
        boost::asio::bind_executor(strand_, [this, payload](const boost::system::error_code &error, std::size_t bytes_transferred)
            {
            if (!error) {
                log_->debug("Bytes sent: " + std::to_string(bytes_transferred));
                log_->debug("Data sent successfully to the Game Server");
            } else {
                log_->error("Error in sending data to Game Server: " + error.message());
            }
            doNextWrite(); }));
}

void
GameServerWorker::processGameServerData(std::string_view data)
{
    // CRITICAL-7: data is now a string_view — no fixed-size copy, no truncation.
    log_->info("Received data from Game Server: " + std::string(data));

    // Parse only the header fields that are always required.
    std::string eventType = jsonParser_.parseEventType(data.data(), data.size());
    ClientDataStruct clientData = jsonParser_.parseClientData(data.data(), data.size());

    std::vector<Event> eventsBatch;
    constexpr int BATCH_SIZE = 10;

    // MEDIUM-4: lazy per-event parsing — parse only the payload needed for each
    // event type, avoiding 10+ unnecessary parse calls per message.
    if (eventType == "setChunkData")
    {
        ChunkInfoStruct chunkInfo = jsonParser_.parseChunkInfo(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_CHUNK_DATA, clientData.clientId, chunkInfo);
    }
    else if (eventType == "setCharacterData")
    {
        CharacterDataStruct characterData = jsonParser_.parseCharacterData(data.data(), data.size());
        PositionStruct positionData = jsonParser_.parsePositionData(data.data(), data.size());
        characterData.characterPosition = positionData;
        characterData.clientId = clientData.clientId;
        eventsBatch.emplace_back(Event::SET_CHARACTER_DATA, clientData.clientId, characterData);
    }
    else if (eventType == "setCharacterAttributes")
    {
        auto attrList = jsonParser_.parseCharacterAttributesList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_CHARACTER_ATTRIBUTES, clientData.clientId, attrList);
    }
    else if (eventType == "setSpawnZonesList")
    {
        auto spawnZones = jsonParser_.parseSpawnZonesList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_SPAWN_ZONES, clientData.clientId, spawnZones);
    }
    else if (eventType == "setMobsList")
    {
        auto mobsList = jsonParser_.parseMobsList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_MOBS_LIST, clientData.clientId, mobsList);
    }
    else if (eventType == "setMobsAttributes")
    {
        auto mobsAttr = jsonParser_.parseMobsAttributesList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_MOBS_ATTRIBUTES, clientData.clientId, mobsAttr);
    }
    else if (eventType == "setMobsSkills")
    {
        auto mobsSkills = jsonParser_.parseMobsSkillsMapping(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_MOBS_SKILLS, clientData.clientId, mobsSkills);
    }
    else if (eventType == "setNPCsList")
    {
        auto npcsList = jsonParser_.parseNPCsList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_NPCS_LIST, clientData.clientId, npcsList);
    }
    else if (eventType == "setNPCsAttributes")
    {
        auto npcsAttr = jsonParser_.parseNPCsAttributes(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_NPCS_ATTRIBUTES, clientData.clientId, npcsAttr);
    }
    else if (eventType == "getItemsList")
    {
        auto itemsList = jsonParser_.parseItemsList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_ITEMS_LIST, clientData.clientId, itemsList);
    }
    else if (eventType == "getMobLootInfo")
    {
        auto lootInfo = jsonParser_.parseMobLootInfo(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_MOB_LOOT_INFO, clientData.clientId, lootInfo);
    }
    else if (eventType == "setMobWeaknessesResistances")
    {
        auto weakRes = jsonParser_.parseMobWeaknessesResistances(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_MOB_WEAKNESSES_RESISTANCES, clientData.clientId, std::move(weakRes));
    }
    else if (eventType == "getExpLevelTable")
    {
        auto expTable = jsonParser_.parseExpLevelTable(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_EXP_LEVEL_TABLE, clientData.clientId, expTable);
    }
    else if (eventType == "setDialoguesData")
    {
        auto dialogues = jsonParser_.parseDialoguesList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_DIALOGUES, clientData.clientId, dialogues);
    }
    else if (eventType == "setNPCDialogueMappings")
    {
        auto mappings = jsonParser_.parseNPCDialogueMappings(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_NPC_DIALOGUE_MAPPINGS, clientData.clientId, mappings);
    }
    else if (eventType == "setQuestsData")
    {
        auto quests = jsonParser_.parseQuestsList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ALL_QUESTS, clientData.clientId, quests);
    }
    else if (eventType == "setPlayerQuestsData")
    {
        auto quests = jsonParser_.parsePlayerQuestProgress(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_QUESTS, clientData.clientId, quests);
    }
    else if (eventType == "setPlayerFlagsData")
    {
        auto flags = jsonParser_.parsePlayerFlags(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_FLAGS, clientData.clientId, flags);
    }
    else if (eventType == "setGameConfig")
    {
        nlohmann::json bodyJson = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_GAME_CONFIG, clientData.clientId, bodyJson);
    }
    else if (eventType == "setVendorData")
    {
        nlohmann::json bodyJson = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_VENDOR_DATA, clientData.clientId, bodyJson);
    }
    else if (eventType == "setTrainerData")
    {
        nlohmann::json bodyJson = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_TRAINER_DATA, clientData.clientId, bodyJson);
    }
    else if (eventType == "setPlayerActiveEffects")
    {
        auto effects = jsonParser_.parsePlayerActiveEffects(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_ACTIVE_EFFECTS, clientData.clientId, effects);
    }
    else if (eventType == "setPlayerInventoryData")
    {
        auto items = jsonParser_.parsePlayerInventory(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_INVENTORY, clientData.clientId, items);
    }
    else if (eventType == "inventoryItemIdSync")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::INVENTORY_ITEM_ID_SYNC, clientData.clientId, body);
    }
    else if (eventType == "setCharacterAttributesRefresh")
    {
        auto payload = jsonParser_.parseCharacterAttributesRefresh(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_CHARACTER_ATTRIBUTES_REFRESH, clientData.clientId, payload);
    }
    else if (eventType == "setRespawnZonesList")
    {
        auto zones = jsonParser_.parseRespawnZonesList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_RESPAWN_ZONES, clientData.clientId, zones);
    }
    else if (eventType == "setGameZonesList")
    {
        auto zones = jsonParser_.parseGameZonesList(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_GAME_ZONES, clientData.clientId, zones);
    }
    else if (eventType == "setStatusEffectTemplates")
    {
        auto templates = jsonParser_.parseStatusEffectTemplates(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_STATUS_EFFECT_TEMPLATES, clientData.clientId, templates);
    }
    else if (eventType == "setPlayerPityData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_PITY, clientData.clientId, body);
    }
    else if (eventType == "setPlayerBestiaryData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_BESTIARY, clientData.clientId, body);
    }
    else if (eventType == "setTimedChampionTemplatesList")
    {
        auto templates = jsonParser_.parseTimedChampionTemplates(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_TIMED_CHAMPION_TEMPLATES, clientData.clientId, templates);
    }
    else if (eventType == "setPlayerReputationsData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_REPUTATIONS, clientData.clientId, body);
    }
    else if (eventType == "setPlayerMasteriesData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_MASTERIES, clientData.clientId, body);
    }
    else if (eventType == "setZoneEventTemplatesList")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_ZONE_EVENT_TEMPLATES, clientData.clientId, body);
    }
    else if (eventType == "setLearnedSkill")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_LEARNED_SKILL, clientData.clientId, body);
    }
    else if (eventType == "setTitleDefinitionsData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_TITLE_DEFINITIONS, clientData.clientId, body);
    }
    else if (eventType == "setPlayerTitlesData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_TITLES, clientData.clientId, body);
    }
    else if (eventType == "setEmoteDefinitionsData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_EMOTE_DEFINITIONS, clientData.clientId, body);
    }
    else if (eventType == "setPlayerEmotesData")
    {
        nlohmann::json body = jsonParser_.parseCombatActionData(data.data(), data.size());
        eventsBatch.emplace_back(Event::SET_PLAYER_EMOTES, clientData.clientId, body);
    }
    else
    {
        log_->info("Unknown event type from Game Server: " + eventType);
    }

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

                    // CRITICAL-7: pass string_view directly — no fixed-size copy, no truncation
                    processGameServerData(oneMessage);
                }

                receiveDataFromGameServer();
            }
            else
            {
                // LOW-8: on disconnect, close socket and reconnect from the beginning
                log_->error("Connection to Game Server lost: " + ec.message() + ". Reconnecting...");
                if (game_server_socket_->is_open())
                    game_server_socket_->close();
                // Reset socket and drain pending sends
                game_server_socket_ = std::make_shared<boost::asio::ip::tcp::socket>(io_context_game_server_);
                boost::asio::post(strand_, [this]()
                    {
                    while (!sendQueue_.empty()) sendQueue_.pop();
                    writePending_ = false; });
                connect(endpoints_, 0);
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
