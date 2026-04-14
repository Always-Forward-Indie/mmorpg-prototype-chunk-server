#include "events/EventHandler.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/CombatSystem.hpp"
#include "services/ReputationManager.hpp"
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
    vendorEventHandler_ = std::make_unique<VendorEventHandler>(networkManager, gameServerWorker, gameServices);
    equipmentEventHandler_ = std::make_unique<EquipmentEventHandler>(networkManager, gameServerWorker, gameServices);
    chatEventHandler_ = std::make_unique<ChatEventHandler>(networkManager, gameServerWorker, gameServices);
    emoteEventHandler_ = std::make_unique<EmoteEventHandler>(networkManager, gameServerWorker, gameServices);

    // Set skill event handler reference in character event handler
    characterEventHandler_->setSkillEventHandler(skillEventHandler_.get());

    // Set NPC event handler reference in character event handler
    characterEventHandler_->setNPCEventHandler(npcEventHandler_.get());

    // Set item event handler reference in character event handler (ground items snapshot on join)
    characterEventHandler_->setItemEventHandler(itemEventHandler_.get());

    // Set mob event handler reference in character event handler (server-push spawn zones on join)
    characterEventHandler_->setMobEventHandler(mobEventHandler_.get());

    // Set equipment event handler so Phase 4 can broadcast the new player's own
    // equipment to all existing clients (covers the race where inventory arrives
    // before playerReady and the EventHandler guard skips the broadcast).
    characterEventHandler_->setEquipmentEventHandler(equipmentEventHandler_.get());
}

void
EventHandler::dispatchEvent(const Event &event)
{
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
        case Event::PLAYER_RESPAWN:
            characterEventHandler_->handlePlayerRespawnEvent(event);
            break;
        case Event::PLAYER_READY:
            characterEventHandler_->handlePlayerReadyEvent(event);
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
        case Event::SET_MOB_WEAKNESSES_RESISTANCES:
            mobEventHandler_->handleSetMobWeaknessesResistancesEvent(event);
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
        case Event::INVENTORY_UPDATE:
            itemEventHandler_->handleInventoryUpdateEvent(event);
            break;
        case Event::ITEM_DROP_BY_PLAYER:
            itemEventHandler_->handleItemDropByPlayerEvent(event);
            break;
        case Event::ITEM_REMOVE:
            itemEventHandler_->handleItemRemoveEvent(event);
            break;
        case Event::USE_ITEM:
            itemEventHandler_->handleUseItemEvent(event);
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

        // ── Zone Events ────────────────────────────────────────────────────────
        case Event::SET_ALL_SPAWN_ZONES:
            zoneEventHandler_->handleSetAllSpawnZonesEvent(event);
            break;
        case Event::GET_SPAWN_ZONE_DATA:
            zoneEventHandler_->handleGetSpawnZoneDataEvent(event);
            break;
        case Event::SET_RESPAWN_ZONES:
            zoneEventHandler_->handleSetRespawnZonesEvent(event);
            break;
        case Event::SET_STATUS_EFFECT_TEMPLATES:
            handleSetStatusEffectTemplatesEvent(event);
            break;
        case Event::SET_GAME_ZONES:
            zoneEventHandler_->handleSetGameZonesEvent(event);
            break;
        case Event::SET_TIMED_CHAMPION_TEMPLATES:
            zoneEventHandler_->handleSetTimedChampionTemplatesEvent(event);
            break;

        // Combat Events
        case Event::INITIATE_COMBAT_ACTION:
            combatEventHandler_->handleInitiateCombatAction(event);
            break;
        case Event::COMPLETE_COMBAT_ACTION:
            combatEventHandler_->handleCompleteCombatAction(event);
            break;
        case Event::INTERRUPT_COMBAT_ACTION:
            // Non-cancellable by design: skills execute instantly, no active cast window exists.
            // Packet is silently ignored.
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
        case Event::SET_PLAYER_INVENTORY:
            handleSetPlayerInventoryEvent(event);
            break;
        case Event::SET_PLAYER_PITY:
            handleSetPlayerPityEvent(event);
            break;
        case Event::SET_PLAYER_BESTIARY:
            handleSetPlayerBestiaryEvent(event);
            break;
        case Event::GET_BESTIARY_ENTRY:
            handleGetBestiaryEntryEvent(event);
            break;
        case Event::GET_BESTIARY_OVERVIEW:
            handleGetBestiaryOverviewEvent(event);
            break;

        // ── Stage-4 social systems ─────────────────────────────────────────────
        case Event::SET_PLAYER_REPUTATIONS:
            handleSetPlayerReputationsEvent(event);
            break;
        case Event::SET_PLAYER_MASTERIES:
            handleSetPlayerMasteriesEvent(event);
            break;
        case Event::SET_ZONE_EVENT_TEMPLATES:
            handleSetZoneEventTemplatesEvent(event);
            break;
        case Event::SAVE_REPUTATION:
            // outgoing only — no handler needed
            break;
        case Event::SAVE_MASTERY:
            // outgoing only — no handler needed
            break;

        // ── Title system ──────────────────────────────────────────────────────
        case Event::SET_TITLE_DEFINITIONS:
            handleSetTitleDefinitionsEvent(event);
            break;
        case Event::SET_PLAYER_TITLES:
            handleSetPlayerTitlesEvent(event);
            break;
        case Event::GET_PLAYER_TITLES:
            handleGetPlayerTitlesEvent(event);
            break;
        case Event::EQUIP_TITLE:
            handleEquipTitleEvent(event);
            break;
        case Event::SET_SKILL_BAR_SLOT:
            handleSetSkillBarSlotEvent(event);
            break;

        // ── Emote system ──────────────────────────────────────────────────────
        case Event::SET_EMOTE_DEFINITIONS:
            handleSetEmoteDefinitionsEvent(event);
            break;
        case Event::SET_PLAYER_EMOTES:
            handleSetPlayerEmotesEvent(event);
            break;
        case Event::USE_EMOTE:
            handleUseEmoteEvent(event);
            break;

        // Skill system
        case Event::SET_LEARNED_SKILL:
            handleSetLearnedSkillEvent(event);
            break;

        // ── Skill trainer events ───────────────────────────────────────────
        case Event::SET_TRAINER_DATA:
            skillEventHandler_->handleSetTrainerDataEvent(event);
            break;
        case Event::OPEN_SKILL_SHOP:
            skillEventHandler_->handleOpenSkillShopEvent(event);
            break;
        case Event::REQUEST_LEARN_SKILL:
            skillEventHandler_->handleRequestLearnSkillEvent(event);
            break;

        case Event::INVENTORY_ITEM_ID_SYNC:
            handleInventoryItemIdSyncEvent(event);
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

        // ── Vendor / Repair / Trade / Durability events ────────────────────
        case Event::SET_VENDOR_DATA:
            vendorEventHandler_->handleSetVendorDataEvent(event);
            break;
        case Event::VENDOR_STOCK_UPDATE:
            vendorEventHandler_->handleVendorStockUpdateEvent(event);
            break;
        case Event::OPEN_VENDOR_SHOP:
            vendorEventHandler_->handleOpenVendorShopEvent(event);
            break;
        case Event::BUY_ITEM:
            vendorEventHandler_->handleBuyItemEvent(event);
            break;
        case Event::SELL_ITEM:
            vendorEventHandler_->handleSellItemEvent(event);
            break;
        case Event::BUY_ITEM_BATCH:
            vendorEventHandler_->handleBuyItemBatchEvent(event);
            break;
        case Event::SELL_ITEM_BATCH:
            vendorEventHandler_->handleSellItemBatchEvent(event);
            break;
        case Event::OPEN_REPAIR_SHOP:
            vendorEventHandler_->handleOpenRepairShopEvent(event);
            break;
        case Event::REPAIR_ITEM:
            vendorEventHandler_->handleRepairItemEvent(event);
            break;
        case Event::REPAIR_ALL:
            vendorEventHandler_->handleRepairAllEvent(event);
            break;
        case Event::TRADE_REQUEST:
            vendorEventHandler_->handleTradeRequestEvent(event);
            break;
        case Event::TRADE_ACCEPT:
            vendorEventHandler_->handleTradeAcceptEvent(event);
            break;
        case Event::TRADE_DECLINE:
            vendorEventHandler_->handleTradeDeclineEvent(event);
            break;
        case Event::TRADE_OFFER_UPDATE:
            vendorEventHandler_->handleTradeOfferUpdateEvent(event);
            break;
        case Event::TRADE_CONFIRM:
            vendorEventHandler_->handleTradeConfirmEvent(event);
            break;
        case Event::TRADE_CANCEL:
            vendorEventHandler_->handleTradeCancelEvent(event);
            break;
        case Event::DURABILITY_UPDATE:
            // Outbound-only event (chunk → client), no handler needed
            break;

        // ── Equipment events ──────────────────────────────────────────────────
        case Event::EQUIP_ITEM:
            equipmentEventHandler_->handleEquipItemEvent(event);
            break;
        case Event::UNEQUIP_ITEM:
            equipmentEventHandler_->handleUnequipItemEvent(event);
            break;
        case Event::GET_EQUIPMENT:
            equipmentEventHandler_->handleGetEquipmentEvent(event);
            break;

        // ── Chat events ────────────────────────────────────────────────────────────
        case Event::CHAT_MESSAGE:
            chatEventHandler_->handleChatMessageEvent(event);
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

VendorEventHandler &
EventHandler::getVendorEventHandler()
{
    return *vendorEventHandler_;
}

EquipmentEventHandler &
EventHandler::getEquipmentEventHandler()
{
    return *equipmentEventHandler_;
}

ChatEventHandler &
EventHandler::getChatEventHandler()
{
    return *chatEventHandler_;
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

        // Push bestiary tier thresholds to BestiaryManager
        const auto &cfg = gameServices_.getGameConfigService();
        gameServices_.getBestiaryManager().setThresholds({cfg.getInt("bestiary.tier1_kills", 1),
            cfg.getInt("bestiary.tier2_kills", 5),
            cfg.getInt("bestiary.tier3_kills", 15),
            cfg.getInt("bestiary.tier4_kills", 30),
            cfg.getInt("bestiary.tier5_kills", 75),
            cfg.getInt("bestiary.tier6_kills", 150)});

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
        // Game server sets header clientId = characterId (same convention as inventory)
        int characterId = event.getClientID();

        if (characterId <= 0)
        {
            log_->error("[EH] SET_PLAYER_ACTIVE_EFFECTS: invalid characterId " +
                        std::to_string(characterId));
            return;
        }

        gameServices_.getCharacterManager().setCharacterActiveEffects(characterId, std::move(effects));

        // Re-apply permanent passive-skill effects so they survive every reload.
        const auto skills = gameServices_.getCharacterManager().getCharacterSkills(characterId);
        for (const auto &skill : skills)
        {
            if (!skill.isPassive)
                continue;
            for (const auto &ed : skill.effects)
            {
                ActiveEffectStruct eff;
                eff.effectSlug = ed.effectSlug;
                eff.effectTypeSlug = ed.effectTypeSlug;
                eff.attributeSlug = ed.attributeSlug;
                eff.value = ed.value;
                eff.sourceType = "skill_passive";
                eff.expiresAt = 0; // permanent
                eff.tickMs = 0;    // passives never tick
                gameServices_.getCharacterManager().addActiveEffect(characterId, eff);
            }
        }

        // Re-apply equipped title bonuses — may have been wiped by the setCharacterActiveEffects() call above.
        gameServices_.getTitleManager().reapplyEquippedBonuses(characterId);

        // Refresh HUD: active effects may modify attributes (stat buffs/debuffs).
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        log_->info("[EH] SET_PLAYER_ACTIVE_EFFECTS: stored effects for character " +
                   std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_ACTIVE_EFFECTS event: " + std::string(e.what()));
    }
}

void
EventHandler::handleSetPlayerInventoryEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<PlayerInventoryItemStruct>>(data))
        {
            log_->error("SET_PLAYER_INVENTORY: unexpected data type");
            return;
        }
        const auto &items = std::get<std::vector<PlayerInventoryItemStruct>>(data);
        int characterId = event.getClientID(); // game server sets clientId = characterId

        gameServices_.getInventoryManager().loadPlayerInventory(characterId, items);

        // Build equipment state from the freshly loaded inventory
        gameServices_.getEquipmentManager().buildFromInventory(characterId);

        log_->info("[EH] SET_PLAYER_INVENTORY: loaded " + std::to_string(items.size()) +
                   " items for character " + std::to_string(characterId));

        // Immediately push inventory to client to avoid race: client may have already
        // requested inventory before this DB response arrived (gets empty list).
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
            log_->info("[EH] SET_PLAYER_INVENTORY: character " + std::to_string(characterId) +
                       " has no active client yet, skipping push");
            return;
        }

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("[EH] SET_PLAYER_INVENTORY: socket not available for clientId " +
                        std::to_string(clientId));
            return;
        }

        nlohmann::json itemsArray = nlohmann::json::array();
        for (const auto &item : items)
        {
            itemsArray.push_back(gameServices_.getInventoryManager().inventoryItemToJson(item));
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Inventory retrieved successfully!")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "getPlayerInventory")
                                      .setBody("characterId", characterId)
                                      .setBody("items", itemsArray)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] SET_PLAYER_INVENTORY: pushed " + std::to_string(items.size()) +
                   " items to clientId=" + std::to_string(clientId) +
                   " (characterId=" + std::to_string(characterId) + ")");

        // Send current equipment state alongside inventory so the client
        // can display equipped items immediately on character join.
        equipmentEventHandler_->sendEquipmentState(clientId, characterId, TimestampStruct{});

        // Send carry weight so the client can display currentWeight/weightLimit
        // in the inventory UI immediately on connect.
        equipmentEventHandler_->sendWeightStatus(clientId, characterId);

        // Send full stats snapshot: inventory is now loaded so effective attributes
        // (base + equipment bonuses) and weight are calculated correctly.
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        // Notify all OTHER clients about this character's equipment so they can
        // render the correct gear visuals on this character's model.
        // Only broadcast once the joining client has sent playerReady — otherwise
        // the other clients haven't received the joinGameCharacter packet yet and
        // would try to render equipment on a character that doesn't exist in their
        // scene. If playerReady hasn't arrived yet, handlePlayerReadyEvent performs
        // this broadcast as a catch-up step (step 5, after pushConnectedCharactersToClient).
        if (gameServices_.getClientManager().isClientWorldReady(clientId))
        {
            equipmentEventHandler_->broadcastEquipmentUpdate(characterId, clientId);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_INVENTORY event: " + std::string(e.what()));
    }
}

void
EventHandler::handleInventoryItemIdSyncEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("[INVENTORY_ID_SYNC] unexpected data type");
            return;
        }
        const auto &j = std::get<nlohmann::json>(data);
        int characterId = j.value("characterId", 0);
        int itemId = j.value("itemId", 0);
        int64_t newId = j.value("inventoryItemId", (int64_t)0);

        if (characterId <= 0 || itemId <= 0 || newId <= 0)
            return;

        gameServices_.getInventoryManager().updateInventoryItemId(characterId, itemId, newId);

        log_->info("[INVENTORY_ID_SYNC] char=" + std::to_string(characterId) +
                   " itemId=" + std::to_string(itemId) +
                   " assignedId=" + std::to_string(newId));
    }
    catch (const std::exception &e)
    {
        log_->error("[INVENTORY_ID_SYNC] exception: " + std::string(e.what()));
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

void
EventHandler::handleSetStatusEffectTemplatesEvent(const Event &event)
{
    try
    {
        log_->info("Processing SET_STATUS_EFFECT_TEMPLATES event");

        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<StatusEffectTemplate>>(data))
        {
            log_->error("SET_STATUS_EFFECT_TEMPLATES: unexpected data type");
            return;
        }

        const auto &templates = std::get<std::vector<StatusEffectTemplate>>(data);
        gameServices_.getStatusEffectTemplateManager().loadTemplates(templates);

        gameServices_.getLogger().log(
            "Status effect templates loaded: " + std::to_string(templates.size()) + " entries.", GREEN);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(
            "Error processing SET_STATUS_EFFECT_TEMPLATES event: " + std::string(e.what()));
    }
}

// ── SET_PLAYER_PITY ────────────────────────────────────────────────────────
void
EventHandler::handleSetPlayerPityEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_PITY: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_PITY: missing characterId");
            return;
        }

        std::vector<std::pair<int, int>> counters;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                counters.emplace_back(e.value("itemId", 0), e.value("killCount", 0));
        }

        gameServices_.getPityManager().loadPityData(characterId, counters);
        log_->info("[EH] SET_PLAYER_PITY: loaded " + std::to_string(counters.size()) +
                   " counters for character " + std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_PITY: " + std::string(e.what()));
    }
}

// ── SET_PLAYER_BESTIARY ────────────────────────────────────────────────────
void
EventHandler::handleSetPlayerBestiaryEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_BESTIARY: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_BESTIARY: missing characterId");
            return;
        }

        std::vector<std::pair<int, int>> kills;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                kills.emplace_back(e.value("mobTemplateId", 0), e.value("killCount", 0));
        }

        gameServices_.getBestiaryManager().loadBestiaryData(characterId, kills);
        log_->info("[EH] SET_PLAYER_BESTIARY: loaded " + std::to_string(kills.size()) +
                   " bestiary entries for character " + std::to_string(characterId));

        // Auto-push overview to the client so it knows which mobs are discovered
        int clientId = event.getClientID();
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (clientSocket && clientSocket->is_open())
        {
            auto knownMobs = gameServices_.getBestiaryManager().getKnownMobs(characterId);
            nlohmann::json entries = nlohmann::json::array();
            for (const auto &[mobTemplateId, killCount] : knownMobs)
            {
                const std::string slug = gameServices_.getMobManager().getMobById(mobTemplateId).slug;
                if (slug.empty())
                    continue;
                nlohmann::json e;
                e["mobSlug"] = slug;
                e["killCount"] = killCount;
                entries.push_back(std::move(e));
            }

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Bestiary overview")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", clientId)
                                          .setHeader("eventType", "getBestiaryOverview")
                                          .setBody("characterId", characterId)
                                          .setBody("entries", entries)
                                          .build();

            networkManager_.sendResponse(clientSocket,
                networkManager_.generateResponseMessage("success", response));

            log_->info("[EH] SET_PLAYER_BESTIARY: pushed overview (" +
                       std::to_string(entries.size()) + " mobs) to clientId=" + std::to_string(clientId));
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_BESTIARY: " + std::string(e.what()));
    }
}

// ── GET_BESTIARY_OVERVIEW ──────────────────────────────────────────────────
void
EventHandler::handleGetBestiaryOverviewEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("GET_BESTIARY_OVERVIEW: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        int clientId = body.value("clientId", event.getClientID());

        if (characterId <= 0)
        {
            log_->error("GET_BESTIARY_OVERVIEW: invalid characterId");
            return;
        }

        auto knownMobs = gameServices_.getBestiaryManager().getKnownMobs(characterId);

        nlohmann::json entries = nlohmann::json::array();
        for (const auto &[mobTemplateId, killCount] : knownMobs)
        {
            const std::string slug = gameServices_.getMobManager().getMobById(mobTemplateId).slug;
            if (slug.empty())
                continue;
            nlohmann::json e;
            e["mobSlug"] = slug;
            e["killCount"] = killCount;
            entries.push_back(std::move(e));
        }

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("GET_BESTIARY_OVERVIEW: no socket for clientId=" + std::to_string(clientId));
            return;
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Bestiary overview")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "getBestiaryOverview")
                                      .setBody("characterId", characterId)
                                      .setBody("entries", entries)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] GET_BESTIARY_OVERVIEW: sent " + std::to_string(entries.size()) +
                   " entries to clientId=" + std::to_string(clientId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing GET_BESTIARY_OVERVIEW: " + std::string(e.what()));
    }
}

// ── GET_BESTIARY_ENTRY ─────────────────────────────────────────────────────
void
EventHandler::handleGetBestiaryEntryEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("GET_BESTIARY_ENTRY: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        int clientId = body.value("clientId", event.getClientID());
        std::string mobSlug = body.value("mobSlug", std::string{});

        if (characterId <= 0 || mobSlug.empty())
        {
            log_->error("GET_BESTIARY_ENTRY: invalid characterId or missing mobSlug");
            return;
        }

        // Resolve mob static data by slug
        MobDataStruct mobStatic = gameServices_.getMobManager().getMobBySlug(mobSlug);
        int mobTemplateId = mobStatic.id;

        if (mobTemplateId <= 0)
        {
            log_->error("GET_BESTIARY_ENTRY: unknown mobSlug=" + mobSlug);
            return;
        }

        // Resolve weaknesses and resistances
        std::vector<std::string> weaknesses = gameServices_.getMobManager().getWeaknessesForMob(mobTemplateId);
        std::vector<std::string> resistances = gameServices_.getMobManager().getResistancesForMob(mobTemplateId);

        // Resolve ability slugs (skills the mob can actively use)
        std::vector<std::string> abilities;
        for (const auto &sk : mobStatic.skills)
        {
            if (!sk.isPassive && !sk.skillSlug.empty())
                abilities.push_back(sk.skillSlug);
        }

        // Resolve loot rows for this mob template
        std::vector<MobLootInfoStruct> lootRows = gameServices_.getItemManager().getLootForMob(mobTemplateId);

        // Item slug resolver
        auto itemSlugFn = [this](int itemId) -> std::string
        {
            try
            {
                return gameServices_.getItemManager().getItemById(itemId).slug;
            }
            catch (...)
            {
                return "";
            }
        };

        nlohmann::json entry = gameServices_.getBestiaryManager().buildEntryJson(
            characterId, mobTemplateId, mobSlug, mobStatic, weaknesses, resistances, abilities, lootRows, itemSlugFn);

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("GET_BESTIARY_ENTRY: no socket for clientId=" + std::to_string(clientId));
            return;
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Bestiary entry retrieved")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "getBestiaryEntry")
                                      .setBody("characterId", characterId)
                                      .setBody("entry", entry)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] GET_BESTIARY_ENTRY: sent mob=" + std::to_string(mobTemplateId) +
                   " to clientId=" + std::to_string(clientId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing GET_BESTIARY_ENTRY: " + std::string(e.what()));
    }
}

// ── SET_PLAYER_REPUTATIONS ─────────────────────────────────────────────────
void
EventHandler::handleSetPlayerReputationsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_REPUTATIONS: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_REPUTATIONS: missing characterId");
            return;
        }

        std::unordered_map<std::string, int> reps;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                reps[e.value("factionSlug", "")] = e.value("value", 0);
        }

        gameServices_.getReputationManager().loadCharacterReputations(characterId, reps);
        log_->info("[EH] SET_PLAYER_REPUTATIONS: loaded " + std::to_string(reps.size()) +
                   " entries for character " + std::to_string(characterId));

        // Push reputation state to the client immediately (same pattern as SET_PLAYER_INVENTORY)
        int clientId = 0;
        {
            auto characters = gameServices_.getCharacterManager().getCharactersList();
            for (const auto &c : characters)
            {
                if (c.characterId == characterId)
                {
                    clientId = c.clientId;
                    break;
                }
            }
        }
        if (clientId > 0)
        {
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket && clientSocket->is_open())
            {
                nlohmann::json entriesArr = nlohmann::json::array();
                for (const auto &[faction, value] : reps)
                    entriesArr.push_back({{"factionSlug", faction}, {"value", value}, {"tier", ReputationManager::getTier(value)}});

                nlohmann::json response;
                response["header"]["eventType"] = "player_reputations";
                response["header"]["status"] = "success";
                response["body"]["characterId"] = characterId;
                response["body"]["entries"] = entriesArr;
                networkManager_.sendResponse(clientSocket,
                    networkManager_.generateResponseMessage("success", response));
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_REPUTATIONS: " + std::string(e.what()));
    }
}

// ── SET_PLAYER_MASTERIES ───────────────────────────────────────────────────
void
EventHandler::handleSetPlayerMasteriesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_MASTERIES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_MASTERIES: missing characterId");
            return;
        }

        std::unordered_map<std::string, float> masteries;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                masteries[e.value("masterySlug", "")] = e.value("value", 0.0f);
        }

        gameServices_.getMasteryManager().loadCharacterMasteries(characterId, masteries);
        log_->info("[EH] SET_PLAYER_MASTERIES: loaded " + std::to_string(masteries.size()) +
                   " entries for character " + std::to_string(characterId));

        // Push mastery state to the client immediately
        int clientId = 0;
        {
            auto characters = gameServices_.getCharacterManager().getCharactersList();
            for (const auto &c : characters)
            {
                if (c.characterId == characterId)
                {
                    clientId = c.clientId;
                    break;
                }
            }
        }
        if (clientId > 0)
        {
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket && clientSocket->is_open())
            {
                nlohmann::json entriesArr = nlohmann::json::array();
                for (const auto &[slug, value] : masteries)
                    entriesArr.push_back({{"masterySlug", slug}, {"value", value}});

                nlohmann::json response;
                response["header"]["eventType"] = "player_masteries";
                response["header"]["status"] = "success";
                response["body"]["characterId"] = characterId;
                response["body"]["entries"] = entriesArr;
                networkManager_.sendResponse(clientSocket,
                    networkManager_.generateResponseMessage("success", response));
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_MASTERIES: " + std::string(e.what()));
    }
}

// ── SET_ZONE_EVENT_TEMPLATES ───────────────────────────────────────────────
void
EventHandler::handleSetZoneEventTemplatesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_ZONE_EVENT_TEMPLATES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);

        std::vector<ZoneEventManager::ZoneEventTemplate> templates;
        if (body.contains("templates") && body["templates"].is_array())
        {
            for (const auto &t : body["templates"])
            {
                ZoneEventManager::ZoneEventTemplate tmpl;
                tmpl.id = t.value("id", 0);
                tmpl.slug = t.value("slug", "");
                tmpl.gameZoneId = t.value("gameZoneId", 0);
                tmpl.triggerType = t.value("triggerType", "manual");
                tmpl.durationSec = t.value("durationSec", 300);
                tmpl.lootMultiplier = t.value("lootMultiplier", 1.0f);
                tmpl.spawnRateMultiplier = t.value("spawnRateMultiplier", 1.0f);
                tmpl.mobSpeedMultiplier = t.value("mobSpeedMultiplier", 1.0f);
                tmpl.announceKey = t.value("announceKey", "");
                tmpl.intervalHours = t.value("intervalHours", 0);
                tmpl.randomChancePerHour = t.value("randomChancePerHour", 0.0f);
                tmpl.hasInvasionWave = t.value("hasInvasionWave", false);
                tmpl.invasionMobTemplateId = t.value("invasionMobTemplateId", 0);
                tmpl.invasionWaveCount = t.value("invasionWaveCount", 0);
                if (!tmpl.slug.empty())
                    templates.push_back(std::move(tmpl));
            }
        }

        gameServices_.getZoneEventManager().loadTemplates(templates);
        log_->info("[EH] SET_ZONE_EVENT_TEMPLATES: loaded " + std::to_string(templates.size()) + " templates");
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_ZONE_EVENT_TEMPLATES: " + std::string(e.what()));
    }
}

// ── SET_LEARNED_SKILL ─────────────────────────────────────────────────────
// Game server responds with full SkillStruct data for the newly learned skill.
// We update CharacterManager and notify the client.
void
EventHandler::handleSetLearnedSkillEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientId = event.getClientID();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_LEARNED_SKILL: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);

        if (characterId <= 0)
        {
            log_->error("[EH] SET_LEARNED_SKILL: invalid characterId");
            return;
        }

        // Build SkillStruct from the skillData object
        if (!body.contains("skillData") || !body["skillData"].is_object())
        {
            log_->error("[EH] SET_LEARNED_SKILL: missing skillData");
            return;
        }
        const auto &sd = body["skillData"];

        SkillStruct skill;
        skill.skillName = sd.value("skillName", "");
        skill.skillSlug = sd.value("skillSlug", "");
        skill.scaleStat = sd.value("scaleStat", "");
        skill.school = sd.value("school", "");
        skill.skillEffectType = sd.value("skillEffectType", "");
        skill.skillLevel = sd.value("skillLevel", 1);
        skill.coeff = sd.value("coeff", 0.0);
        skill.flatAdd = sd.value("flatAdd", 0.0);
        skill.cooldownMs = sd.value("cooldownMs", 0);
        skill.gcdMs = sd.value("gcdMs", 0);
        skill.castMs = sd.value("castMs", 0);
        skill.costMp = sd.value("costMp", 0);
        skill.maxRange = sd.value("maxRange", 0);
        skill.areaRadius = sd.value("areaRadius", 0);
        skill.swingMs = sd.value("swingMs", 300);
        skill.animationName = sd.value("animationName", "");
        skill.isPassive = sd.value("isPassive", false);

        // Parse effect definitions so passive bonuses can be applied immediately
        if (sd.contains("effects") && sd["effects"].is_array())
        {
            for (const auto &eff : sd["effects"])
            {
                SkillEffectDefinitionStruct ed;
                if (eff.contains("effectSlug") && eff["effectSlug"].is_string())
                    ed.effectSlug = eff["effectSlug"].get<std::string>();
                if (eff.contains("effectTypeSlug") && eff["effectTypeSlug"].is_string())
                    ed.effectTypeSlug = eff["effectTypeSlug"].get<std::string>();
                if (eff.contains("attributeSlug") && eff["attributeSlug"].is_string())
                    ed.attributeSlug = eff["attributeSlug"].get<std::string>();
                if (eff.contains("value") && eff["value"].is_number())
                    ed.value = eff["value"].get<float>();
                if (eff.contains("durationSeconds") && eff["durationSeconds"].is_number_integer())
                    ed.durationSeconds = eff["durationSeconds"].get<int>();
                if (eff.contains("tickMs") && eff["tickMs"].is_number_integer())
                    ed.tickMs = eff["tickMs"].get<int>();
                skill.effects.push_back(ed);
            }
        }

        // Persist in-memory
        gameServices_.getCharacterManager().addCharacterSkill(characterId, skill);

        // If this is a passive skill, apply its permanent stat modifiers immediately
        // (same logic as handleSetPlayerActiveEffectsEvent does on login)
        if (skill.isPassive)
        {
            for (const auto &ed : skill.effects)
            {
                ActiveEffectStruct eff;
                eff.effectSlug = ed.effectSlug;
                eff.effectTypeSlug = ed.effectTypeSlug;
                eff.attributeSlug = ed.attributeSlug;
                eff.value = ed.value;
                eff.sourceType = "skill_passive";
                eff.expiresAt = 0; // permanent
                eff.tickMs = 0;    // passives never tick
                gameServices_.getCharacterManager().addActiveEffect(characterId, eff);
            }
            // Push updated stats (with new passive bonus) to the client
            if (!skill.effects.empty())
                gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);
        }

        // Build and send skill_learned notification to client
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket)
        {
            log_->error("[EH] SET_LEARNED_SKILL: socket not available for clientId " + std::to_string(clientId));
            return;
        }

        int newSp = gameServices_.getCharacterManager().getCharacterFreeSkillPoints(characterId);

        nlohmann::json notif;
        notif["type"] = "skill_learned";
        notif["skillSlug"] = skill.skillSlug;
        notif["skillName"] = skill.skillName;
        notif["isPassive"] = skill.isPassive;
        notif["newFreeSkillPoints"] = newSp;
        notif["skillData"] = sd;

        nlohmann::json resp = ResponseBuilder()
                                  .setHeader("eventType", "skill_learned")
                                  .build();
        resp["body"] = notif;
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp));

        log_->info("[EH] SET_LEARNED_SKILL: char={} skill={} newSp={}", characterId, skill.skillSlug, newSp);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_LEARNED_SKILL: " + std::string(e.what()));
    }
}

// ── SET_TITLE_DEFINITIONS ──────────────────────────────────────────────────
void
EventHandler::handleSetTitleDefinitionsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_TITLE_DEFINITIONS: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);

        std::vector<TitleDefinitionStruct> defs;
        if (body.contains("titles") && body["titles"].is_array())
        {
            for (const auto &t : body["titles"])
            {
                TitleDefinitionStruct td;
                td.id = t.value("id", 0);
                td.slug = t.value("slug", "");
                td.displayName = t.value("displayName", "");
                td.description = t.value("description", "");
                td.earnCondition = t.value("earnCondition", "");
                if (t.contains("bonuses") && t["bonuses"].is_array())
                {
                    for (const auto &b : t["bonuses"])
                    {
                        TitleBonusStruct bonus;
                        bonus.attributeSlug = b.value("attributeSlug", "");
                        bonus.value = b.value("value", 0.0f);
                        if (!bonus.attributeSlug.empty())
                            td.bonuses.push_back(bonus);
                    }
                }
                if (!td.slug.empty())
                    defs.push_back(std::move(td));
            }
        }

        gameServices_.getTitleManager().loadTitleDefinitions(defs);
        log_->info("[EH] SET_TITLE_DEFINITIONS: loaded {} definitions", defs.size());
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_TITLE_DEFINITIONS: " + std::string(e.what()));
    }
}

// ── SET_PLAYER_TITLES ─────────────────────────────────────────────────────
void
EventHandler::handleSetPlayerTitlesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_TITLES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_TITLES: missing characterId");
            return;
        }

        PlayerTitleStateStruct state;
        state.characterId = characterId;
        state.equippedSlug = body.value("equippedSlug", "");
        if (body.contains("earnedSlugs") && body["earnedSlugs"].is_array())
        {
            for (const auto &s : body["earnedSlugs"])
                if (s.is_string())
                    state.earnedSlugs.push_back(s.get<std::string>());
        }

        gameServices_.getTitleManager().loadPlayerTitles(characterId, state);
        log_->info("[EH] SET_PLAYER_TITLES: loaded {} titles for char={}", state.earnedSlugs.size(), characterId);

        // Push title state to client immediately (same pattern as SET_PLAYER_INVENTORY)
        // notifyClientCallback_ fires sendTitleUpdateToClient → already handles this
        // But we need it explicitly if the callback isn't wired yet during login ordering.
        // The callback wired in ChunkServer will re-send on any future change.

        // If the player is already world-ready (titles arrived late — after playerReady),
        // broadcast their equipped title to all other zone clients so nameplates update.
        // If not yet ready, handlePlayerReadyEvent (Phase 4, step 6) does it as catch-up.
        auto chars = gameServices_.getCharacterManager().getCharactersList();
        int clientId = 0;
        for (const auto &c : chars)
            if (c.characterId == characterId)
            {
                clientId = c.clientId;
                break;
            }

        if (clientId > 0 && gameServices_.getClientManager().isClientWorldReady(clientId) && characterEventHandler_)
        {
            characterEventHandler_->broadcastTitleChanged(characterId, clientId);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_TITLES: " + std::string(e.what()));
    }
}

// ── GET_PLAYER_TITLES ─────────────────────────────────────────────────────
void
EventHandler::handleGetPlayerTitlesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("GET_PLAYER_TITLES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        int clientId = body.value("clientId", 0);

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("[EH] GET_PLAYER_TITLES: socket not available for clientId={}", clientId);
            return;
        }

        PlayerTitleStateStruct state = gameServices_.getTitleManager().getPlayerTitles(characterId);

        nlohmann::json earnedArr = nlohmann::json::array();
        for (const auto &slug : state.earnedSlugs)
        {
            TitleDefinitionStruct def = gameServices_.getTitleManager().getTitleDefinition(slug);
            nlohmann::json entry;
            entry["slug"] = slug;
            entry["displayName"] = def.displayName;
            entry["description"] = def.description;
            entry["earnCondition"] = def.earnCondition;
            nlohmann::json bonusArr = nlohmann::json::array();
            for (const auto &b : def.bonuses)
                bonusArr.push_back({{"attributeSlug", b.attributeSlug}, {"value", b.value}});
            entry["bonuses"] = bonusArr;
            earnedArr.push_back(entry);
        }

        nlohmann::json equippedEntry = nullptr;
        if (!state.equippedSlug.empty())
        {
            TitleDefinitionStruct def = gameServices_.getTitleManager().getTitleDefinition(state.equippedSlug);
            equippedEntry = {{"slug", state.equippedSlug}, {"displayName", def.displayName}, {"description", def.description}, {"earnCondition", def.earnCondition}};
            nlohmann::json bonusArr = nlohmann::json::array();
            for (const auto &b : def.bonuses)
                bonusArr.push_back({{"attributeSlug", b.attributeSlug}, {"value", b.value}});
            equippedEntry["bonuses"] = bonusArr;
        }

        nlohmann::json response;
        response["header"]["eventType"] = "player_titles_update";
        response["header"]["status"] = "success";
        response["body"]["characterId"] = characterId;
        response["body"]["equippedTitle"] = equippedEntry;
        response["body"]["earnedTitles"] = earnedArr;

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing GET_PLAYER_TITLES: " + std::string(e.what()));
    }
}

// ── EQUIP_TITLE ───────────────────────────────────────────────────────────
void
EventHandler::handleEquipTitleEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<EquipTitleRequestStruct>(data))
        {
            log_->error("EQUIP_TITLE: unexpected data type");
            return;
        }
        const auto &req = std::get<EquipTitleRequestStruct>(data);

        auto clientSocket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("[EH] EQUIP_TITLE: socket not available for clientId={}", req.clientId);
            return;
        }

        bool ok = gameServices_.getTitleManager().equipTitle(req.characterId, req.titleSlug);

        nlohmann::json response;
        response["header"]["eventType"] = "equip_title_result";
        response["header"]["status"] = ok ? "success" : "error";
        response["body"]["characterId"] = req.characterId;
        response["body"]["titleSlug"] = req.titleSlug;
        if (!ok)
            response["body"]["error"] = "title_not_earned";

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage(ok ? "success" : "error", response));

        // Broadcast the new equipped title (or empty = unequip) to all other zone
        // clients so they can update the nameplate display in real time.
        if (ok && characterEventHandler_)
            characterEventHandler_->broadcastTitleChanged(req.characterId, req.clientId);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing EQUIP_TITLE: " + std::string(e.what()));
    }
}

// ── SET_SKILL_BAR_SLOT ────────────────────────────────────────────────────
void
EventHandler::handleSetSkillBarSlotEvent(const Event &event)
{
    if (skillEventHandler_)
        skillEventHandler_->handleSetSkillBarSlotEvent(event);
    else
        log_->error("handleSetSkillBarSlotEvent: skillEventHandler_ is null");
}

// ── Emote system ──────────────────────────────────────────────────────────

void
EventHandler::handleSetEmoteDefinitionsEvent(const Event &event)
{
    if (emoteEventHandler_)
        emoteEventHandler_->handleSetEmoteDefinitionsEvent(event);
    else
        log_->error("handleSetEmoteDefinitionsEvent: emoteEventHandler_ is null");
}

void
EventHandler::handleSetPlayerEmotesEvent(const Event &event)
{
    if (emoteEventHandler_)
        emoteEventHandler_->handleSetPlayerEmotesEvent(event);
    else
        log_->error("handleSetPlayerEmotesEvent: emoteEventHandler_ is null");
}

void
EventHandler::handleUseEmoteEvent(const Event &event)
{
    if (emoteEventHandler_)
        emoteEventHandler_->handleUseEmoteEvent(event);
    else
        log_->error("handleUseEmoteEvent: emoteEventHandler_ is null");
}
