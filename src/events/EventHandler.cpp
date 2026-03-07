#include "events/EventHandler.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/CombatSystem.hpp"
#include <spdlog/logger.h>

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : networkManager_(networkManager), gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("events");
    // Initialize all specialized event handlers
    clientEventHandler_ = std::make_unique<ClientEventHandler>(networkManager, gameServerWorker, gameServices);
    characterEventHandler_ = std::make_unique<CharacterEventHandler>(networkManager, gameServerWorker, gameServices);
    mobEventHandler_ = std::make_unique<MobEventHandler>(networkManager, gameServerWorker, gameServices);
    npcEventHandler_ = std::make_unique<NPCEventHandler>(networkManager, gameServerWorker, gameServices);
    zoneEventHandler_ = std::make_unique<ZoneEventHandler>(networkManager, gameServerWorker, gameServices);
    chunkEventHandler_ = std::make_unique<ChunkEventHandler>(networkManager, gameServerWorker, gameServices);
    combatEventHandler_ = std::make_unique<CombatEventHandler>(networkManager, gameServerWorker, gameServices);
    itemEventHandler_ = std::make_unique<ItemEventHandler>(networkManager, gameServerWorker, gameServices);
    harvestEventHandler_ = std::make_unique<HarvestEventHandler>(networkManager, gameServerWorker, gameServices);
    skillEventHandler_ = std::make_unique<SkillEventHandler>(networkManager, gameServerWorker, gameServices);
    experienceEventHandler_ = std::make_unique<ExperienceEventHandler>(networkManager, gameServerWorker, gameServices);
    dialogueEventHandler_ = std::make_unique<DialogueEventHandler>(networkManager, gameServerWorker, gameServices);

    // Set skill event handler reference in character event handler
    characterEventHandler_->setSkillEventHandler(skillEventHandler_.get());

    // Set NPC event handler reference in character event handler
    characterEventHandler_->setNPCEventHandler(npcEventHandler_.get());
}

void
EventHandler::dispatchEvent(const Event &event)
{
    gameServices_.getLogger().log("EventHandler::dispatchEvent called with event type: " + std::to_string(static_cast<int>(event.getType())), GREEN);

    try
    {
        switch (event.getType())
        {
        // Chunk Events
        case Event::SET_CHUNK_DATA:
            chunkEventHandler_->handleInitChunkEvent(event);
            break;

        // Client Events
        case Event::PING_CLIENT:
            clientEventHandler_->handlePingClientEvent(event);
            break;
        case Event::JOIN_CLIENT:
            clientEventHandler_->handleJoinClientEvent(event);
            break;
        case Event::GET_CONNECTED_CLIENTS:
            clientEventHandler_->handleGetConnectedClientsEvent(event);
            break;
        case Event::DISCONNECT_CLIENT:
            clientEventHandler_->handleDisconnectClientEvent(event);
            break;

        // Character Events
        case Event::JOIN_CHARACTER:
            characterEventHandler_->handleJoinCharacterEvent(event);
            break;
        case Event::GET_CONNECTED_CHARACTERS:
            characterEventHandler_->handleGetConnectedCharactersEvent(event);
            break;
        case Event::SET_CHARACTER_DATA:
            characterEventHandler_->handleSetCharacterDataEvent(event);
            break;
        case Event::SET_CHARACTER_ATTRIBUTES:
            characterEventHandler_->handleSetCharacterAttributesEvent(event);
            break;
        case Event::MOVE_CHARACTER:
            characterEventHandler_->handleMoveCharacterEvent(event);
            break;

        // Mob Events
        case Event::SET_ALL_MOBS_LIST:
            mobEventHandler_->handleSetAllMobsListEvent(event);
            break;
        case Event::SET_ALL_MOBS_ATTRIBUTES:
            mobEventHandler_->handleSetMobsAttributesEvent(event);
            break;
        case Event::SET_ALL_MOBS_SKILLS:
            mobEventHandler_->handleSetMobsSkillsEvent(event);
            break;
        case Event::GET_MOB_DATA:
            mobEventHandler_->handleGetMobDataEvent(event);
            break;
        case Event::SPAWN_MOBS_IN_ZONE:
            mobEventHandler_->handleSpawnMobsInZoneEvent(event);
            break;
        case Event::SPAWN_ZONE_MOVE_MOBS:
            mobEventHandler_->handleZoneMoveMobsEvent(event);
            break;
        case Event::MOB_MOVE_UPDATE:
            mobEventHandler_->handleMobMoveUpdateEvent(event);
            break;
        case Event::MOB_DEATH:
            mobEventHandler_->handleMobDeathEvent(event);
            break;
        case Event::MOB_TARGET_LOST:
            mobEventHandler_->handleMobTargetLostEvent(event);
            break;
        case Event::MOB_HEALTH_UPDATE:
            mobEventHandler_->handleMobHealthUpdateEvent(event);
            break;
        case Event::MOB_LOOT_GENERATION:
            itemEventHandler_->handleMobLootGenerationEvent(event);
            break;

        // NPC Events
        case Event::SET_ALL_NPCS_LIST:
            npcEventHandler_->handleSetAllNPCsListEvent(event);
            break;
        case Event::SET_ALL_NPCS_ATTRIBUTES:
            npcEventHandler_->handleSetAllNPCsAttributesEvent(event);
            break;

        // Item Events
        case Event::SET_ALL_ITEMS_LIST:
            itemEventHandler_->handleSetItemsListEvent(event);
            break;
        case Event::SET_MOB_LOOT_INFO:
            itemEventHandler_->handleSetMobLootInfoEvent(event);
            break;
        case Event::SET_EXP_LEVEL_TABLE:
            handleSetExpLevelTableEvent(event);
            break;
        case Event::SET_GAME_CONFIG:
            handleSetGameConfigEvent(event);
            break;
        case Event::ITEM_DROP:
            itemEventHandler_->handleItemDropEvent(event);
            break;
        case Event::ITEM_PICKUP:
            itemEventHandler_->handleItemPickupEvent(event);
            break;
        case Event::GET_NEARBY_ITEMS:
            itemEventHandler_->handleGetNearbyItemsEvent(event);
            break;
        case Event::GET_PLAYER_INVENTORY:
            itemEventHandler_->handleGetPlayerInventoryEvent(event);
            break;

        // Harvest Events
        case Event::HARVEST_START_REQUEST:
            log_->info("EventHandler: Processing HARVEST_START_REQUEST event");
            harvestEventHandler_->handleHarvestStartRequest(event);
            break;
        case Event::HARVEST_CANCELLED:
            harvestEventHandler_->handleHarvestCancel(event);
            break;
        case Event::GET_NEARBY_CORPSES:
            harvestEventHandler_->handleGetNearbyCorpses(event);
            break;
        case Event::HARVEST_COMPLETE:
            if (std::holds_alternative<HarvestCompleteStruct>(event.getData()))
            {
                HarvestCompleteStruct completeData = std::get<HarvestCompleteStruct>(event.getData());
                harvestEventHandler_->handleHarvestComplete(completeData.playerId, completeData.corpseId);
            }
            break;
        case Event::CORPSE_LOOT_PICKUP:
            if (std::holds_alternative<CorpseLootPickupRequestStruct>(event.getData()))
            {
                CorpseLootPickupRequestStruct pickupData = std::get<CorpseLootPickupRequestStruct>(event.getData());
                harvestEventHandler_->handleCorpseLootPickup(pickupData);
            }
            break;
        case Event::CORPSE_LOOT_INSPECT:
            if (std::holds_alternative<CorpseLootInspectRequestStruct>(event.getData()))
            {
                CorpseLootInspectRequestStruct inspectData = std::get<CorpseLootInspectRequestStruct>(event.getData());
                harvestEventHandler_->handleCorpseLootInspect(inspectData);
            }
            break;

        // Zone Events
        case Event::SET_ALL_SPAWN_ZONES:
            zoneEventHandler_->handleSetAllSpawnZonesEvent(event);
            break;
        case Event::GET_SPAWN_ZONE_DATA:
            zoneEventHandler_->handleGetSpawnZoneDataEvent(event);
            break;

        // Combat Events
        case Event::INITIATE_COMBAT_ACTION:
            combatEventHandler_->handleInitiateCombatAction(event);
            break;
        case Event::COMPLETE_COMBAT_ACTION:
            combatEventHandler_->handleCompleteCombatAction(event);
            break;
        case Event::INTERRUPT_COMBAT_ACTION:
            combatEventHandler_->handleInterruptCombatAction(event);
            break;
        case Event::COMBAT_ANIMATION:
            combatEventHandler_->handleCombatAnimation(event);
            break;
        case Event::COMBAT_RESULT:
            combatEventHandler_->handleCombatResult(event);
            break;

        // Skill Events
        case Event::INITIALIZE_PLAYER_SKILLS:
            skillEventHandler_->handleInitializePlayerSkills(event);
            break;

        // New Attack Events
        case Event::PLAYER_ATTACK:
            combatEventHandler_->handlePlayerAttack(event);
            break;
        case Event::AI_ATTACK:
            // Legacy no-op: AI attacks are now driven entirely by
            // MobAIController → CombatSystem::processAIAttack(mobId, targetId).
            // This event is never pushed onto the queue.
            break;
        case Event::ATTACK_TARGET_SELECTION:
            // TODO: Implement target selection event handling
            break;
        case Event::ATTACK_SEQUENCE_START:
            // TODO: Implement attack sequence handling
            break;
        case Event::ATTACK_SEQUENCE_COMPLETE:
            // TODO: Implement attack sequence completion
            break;

        // Experience Events
        case Event::EXPERIENCE_GRANT:
            experienceEventHandler_->handleExperienceGrantEvent(event);
            break;
        case Event::EXPERIENCE_REMOVE:
            experienceEventHandler_->handleExperienceRemoveEvent(event);
            break;
        case Event::EXPERIENCE_UPDATE:
            experienceEventHandler_->handleExperienceUpdateEvent(event);
            break;
        case Event::LEVEL_UP:
            experienceEventHandler_->handleLevelUpEvent(event);
            break;

        // Legacy events that might not have direct mapping
        case Event::LEAVE_GAME_CLIENT:
            clientEventHandler_->handleDisconnectClientEvent(event);
            break;
        case Event::LEAVE_GAME_CHUNK:
            chunkEventHandler_->handleDisconnectChunkEvent(event);
            break;

        // Dialogue data events (game-server → chunk-server)
        case Event::SET_ALL_DIALOGUES:
            dialogueEventHandler_->handleSetAllDialoguesEvent(event);
            break;
        case Event::SET_NPC_DIALOGUE_MAPPINGS:
            dialogueEventHandler_->handleSetNPCDialogueMappingsEvent(event);
            break;
        case Event::SET_ALL_QUESTS:
            dialogueEventHandler_->handleSetAllQuestsEvent(event);
            break;
        case Event::SET_PLAYER_QUESTS:
            dialogueEventHandler_->handleSetPlayerQuestsEvent(event);
            break;
        case Event::SET_PLAYER_FLAGS:
            dialogueEventHandler_->handleSetPlayerFlagsEvent(event);
            break;
        case Event::SET_PLAYER_ACTIVE_EFFECTS:
            handleSetPlayerActiveEffectsEvent(event);
            break;
        case Event::SET_CHARACTER_ATTRIBUTES_REFRESH:
            handleSetCharacterAttributesRefreshEvent(event);
            break;

        // Client dialogue/quest interaction events
        case Event::NPC_INTERACT:
            dialogueEventHandler_->handleNPCInteractEvent(event);
            break;
        case Event::DIALOGUE_CHOICE:
            dialogueEventHandler_->handleDialogueChoiceEvent(event);
            break;
        case Event::DIALOGUE_CLOSE:
            dialogueEventHandler_->handleDialogueCloseEvent(event);
            break;

        default:
            gameServices_.getLogger().logError("Unknown event type: " + std::to_string(static_cast<int>(event.getType())));
            break;
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error dispatching event: " + std::string(ex.what()));
    }
}

CombatEventHandler &
EventHandler::getCombatEventHandler()
{
    return *combatEventHandler_;
}

SkillEventHandler &
EventHandler::getSkillEventHandler()
{
    return *skillEventHandler_;
}

NPCEventHandler &
EventHandler::getNPCEventHandler()
{
    return *npcEventHandler_;
}

DialogueEventHandler &
EventHandler::getDialogueEventHandler()
{
    return *dialogueEventHandler_;
}

void
EventHandler::handleSetExpLevelTableEvent(const Event &event)
{
    try
    {
        log_->info("Processing SET_EXP_LEVEL_TABLE event");

        const auto &data = event.getData();

        if (std::holds_alternative<std::vector<ExperienceLevelEntry>>(data))
        {
            std::vector<ExperienceLevelEntry> expLevelTable = std::get<std::vector<ExperienceLevelEntry>>(data);

            gameServices_.getLogger().log("Received experience level table with " +
                                              std::to_string(expLevelTable.size()) + " entries",
                GREEN);

            // Передаем данные в ExperienceCacheManager
            gameServices_.getExperienceCacheManager().setExperienceTable(expLevelTable);

            log_->info("Experience level table successfully loaded into cache");
        }
        else
        {
            log_->error("Invalid data type in SET_EXP_LEVEL_TABLE event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_EXP_LEVEL_TABLE event: " + std::string(e.what()));
    }
}

void
EventHandler::handleSetGameConfigEvent(const Event &event)
{
    try
    {
        log_->info("Processing SET_GAME_CONFIG event");

        const auto &data = event.getData();

        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_GAME_CONFIG: unexpected data type");
            return;
        }

        const auto &body = std::get<nlohmann::json>(data);

        if (!body.contains("configList") || !body["configList"].is_array())
        {
            log_->error("SET_GAME_CONFIG: missing or invalid 'configList' in body");
            return;
        }

        std::unordered_map<std::string, std::string> configMap;
        for (const auto &entry : body["configList"])
        {
            if (entry.contains("key") && entry.contains("value") &&
                entry["key"].is_string() && entry["value"].is_string())
            {
                configMap[entry["key"].get<std::string>()] = entry["value"].get<std::string>();
            }
        }

        gameServices_.getGameConfigService().setConfig(configMap);

        gameServices_.getLogger().log("Game config loaded: " +
                                          std::to_string(configMap.size()) + " entries.",
            GREEN);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_GAME_CONFIG event: " + std::string(e.what()));
    }
}

void
EventHandler::handleSetPlayerActiveEffectsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<ActiveEffectStruct>>(data))
        {
            log_->error("SET_PLAYER_ACTIVE_EFFECTS: unexpected data type");
            return;
        }

        auto effects = std::get<std::vector<ActiveEffectStruct>>(data);
        int clientId = event.getClientID();

        // Resolve characterId from clientId
        auto characters = gameServices_.getCharacterManager().getCharactersList();
        int characterId = 0;
        for (const auto &c : characters)
        {
            if (c.clientId == clientId)
            {
                characterId = c.characterId;
                break;
            }
        }

        if (characterId <= 0)
        {
            log_->error("[EH] SET_PLAYER_ACTIVE_EFFECTS: no character for clientId " +
                                               std::to_string(clientId));
            return;
        }

        gameServices_.getCharacterManager().setCharacterActiveEffects(characterId, std::move(effects));

        log_->info("[EH] SET_PLAYER_ACTIVE_EFFECTS: stored effects for character " +
                                          std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_ACTIVE_EFFECTS event: " + std::string(e.what()));
    }
}

void
EventHandler::handleSetCharacterAttributesRefreshEvent(const Event &event)
{
    using PairType = std::pair<int, std::vector<CharacterAttributeStruct>>;
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<PairType>(data))
        {
            log_->error("SET_CHARACTER_ATTRIBUTES_REFRESH: unexpected data type");
            return;
        }

        auto [characterId, attributes] = std::get<PairType>(data);

        if (characterId <= 0)
        {
            log_->error("[EH] SET_CHARACTER_ATTRIBUTES_REFRESH: invalid characterId");
            return;
        }

        // Replace stored attributes with fresh values from DB (includes equip bonuses & perm mods)
        gameServices_.getCharacterManager().replaceCharacterAttributes(characterId, attributes);

        // Find the client socket for this character so we can notify it
        auto characters = gameServices_.getCharacterManager().getCharactersList();
        int clientId = 0;
        for (const auto &c : characters)
        {
            if (c.characterId == characterId)
            {
                clientId = c.clientId;
                break;
            }
        }

        if (clientId <= 0)
        {
            log_->info("[EH] SET_CHARACTER_ATTRIBUTES_REFRESH: character " +
                                              std::to_string(characterId) + " has no active client, skipping push");
            return;
        }

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("[EH] SET_CHARACTER_ATTRIBUTES_REFRESH: socket not available for clientId " +
                                               std::to_string(clientId));
            return;
        }

        // Build charAttributesUpdate packet for the game client
        nlohmann::json attrsJson = nlohmann::json::array();
        for (const auto &attr : attributes)
        {
            nlohmann::json entry;
            entry["id"] = attr.id;
            entry["name"] = attr.name;
            entry["slug"] = attr.slug;
            entry["value"] = attr.value;
            attrsJson.push_back(std::move(entry));
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Attributes updated")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "charAttributesUpdate")
                                      .setBody("characterId", characterId)
                                      .setBody("attributesData", attrsJson)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Pushed " + std::to_string(attrsJson.size()) +
                                          " refreshed attributes to clientId=" + std::to_string(clientId) +
                                          " (characterId=" + std::to_string(characterId) + ")",
            GREEN);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_CHARACTER_ATTRIBUTES_REFRESH event: " + std::string(e.what()));
    }
}
