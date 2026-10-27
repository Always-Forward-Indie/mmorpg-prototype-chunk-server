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
    worldObjectEventHandler_ = std::make_unique<WorldObjectEventHandler>(networkManager, gameServerWorker, gameServices);
    catalogSync_ = std::make_unique<CatalogSyncService>(networkManager, gameServices);

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
    characterEventHandler_->setWorldObjectEventHandler(worldObjectEventHandler_.get());
    characterEventHandler_->setHarvestEventHandler(harvestEventHandler_.get());

    // Wire character event handler reference for pending join request cleanup on disconnect
    clientEventHandler_->setCharacterEventHandler(characterEventHandler_.get());

    // Catalog sync peers (extracted SET_* handlers delegate notifications).
    catalogSync_->setCharacterEventHandler(characterEventHandler_.get());
    catalogSync_->setCombatEventHandler(combatEventHandler_.get());
    catalogSync_->setEmoteEventHandler(emoteEventHandler_.get());
    catalogSync_->setEquipmentEventHandler(equipmentEventHandler_.get());
    catalogSync_->setSkillEventHandler(skillEventHandler_.get());

    // Visibility for server-spawned mobs (timed/threshold champions): thin
    // deltas carry no slug/name, so push a real spawn list to subscribers.
    // Same sender the admin spawn path uses (MobEventHandler::pushSpawnSnapshot).
    gameServices_.getChampionManager().setSpawnNotifyCallback(
        [this](const MobDataStruct &mob) { mobEventHandler_->pushSpawnSnapshot(mob); });
    // Trophy keys for champion announcements (slug + buy-back price).
    gameServices_.getChampionManager().setItemManager(&gameServices_.getItemManager());
    // Rare-spawn toasts (migration 108; silent without it, rares still spawn).
    gameServices_.getSpawnZoneManager().setStatsNotifier(&gameServices_.getStatsNotificationService());
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
            catalogSync_->handleSetExpLevelTableEvent(event);
            break;
        case Event::SET_GAME_CONFIG:
            catalogSync_->handleSetGameConfigEvent(event);
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
        case Event::SET_CLASS_SPAWN_ZONES:
            zoneEventHandler_->handleSetClassSpawnZonesEvent(event);
            break;
        case Event::SET_STATUS_EFFECT_TEMPLATES:
            catalogSync_->handleSetStatusEffectTemplatesEvent(event);
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
        case Event::ADMIN_KILL_MOB:
            combatEventHandler_->handleAdminKillMob(event);
            break;
        case Event::ADMIN_TELEPORT:
            characterEventHandler_->handleAdminTeleportEvent(event);
            break;
        case Event::ADMIN_SPAWN:
            mobEventHandler_->handleAdminSpawnEvent(event);
            break;
        case Event::AI_ATTACK:
            // Legacy no-op: AI attacks are now driven entirely by
            // MobAIController → CombatSystem::processAIAttack(mobId, targetId).
            // This event is never pushed onto the queue.
            break;
        case Event::ATTACK_TARGET_SELECTION:
        case Event::ATTACK_SEQUENCE_START:
        case Event::ATTACK_SEQUENCE_COMPLETE:
            // Retired no-ops (were TODO placeholders): target selection lives
            // client-side (lock-target) and the server validates per-skill via
            // CombatTargetResolver + SkillInitiationValidator. These events are
            // never pushed onto the queue.
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
            catalogSync_->handleSetPlayerActiveEffectsEvent(event);
            break;
        case Event::SET_PLAYER_SKILL_COOLDOWNS:
            catalogSync_->handleSetPlayerSkillCooldownsEvent(event);
            break;
        case Event::SET_PLAYER_INVENTORY:
            catalogSync_->handleSetPlayerInventoryEvent(event);
            break;
        case Event::SET_PLAYER_PITY:
            catalogSync_->handleSetPlayerPityEvent(event);
            break;
        case Event::SET_PLAYER_BESTIARY:
            catalogSync_->handleSetPlayerBestiaryEvent(event);
            break;
        case Event::GET_BESTIARY_ENTRY:
            handleGetBestiaryEntryEvent(event);
            break;
        case Event::GET_BESTIARY_OVERVIEW:
            handleGetBestiaryOverviewEvent(event);
            break;

        // ── Stage-4 social systems ─────────────────────────────────────────────
        case Event::SET_PLAYER_REPUTATIONS:
            catalogSync_->handleSetPlayerReputationsEvent(event);
            break;
        case Event::SET_PLAYER_MASTERIES:
            catalogSync_->handleSetPlayerMasteriesEvent(event);
            break;
        case Event::SET_MASTERY_DEFINITIONS:
            catalogSync_->handleSetMasteryDefinitionsEvent(event);
            break;
        case Event::SET_ZONE_EVENT_TEMPLATES:
            catalogSync_->handleSetZoneEventTemplatesEvent(event);
            break;
        case Event::SAVE_REPUTATION:
            // outgoing only — no handler needed
            break;
        case Event::SAVE_MASTERY:
            // outgoing only — no handler needed
            break;

        // ── Title system ──────────────────────────────────────────────────────
        case Event::SET_TITLE_DEFINITIONS:
            catalogSync_->handleSetTitleDefinitionsEvent(event);
            break;
        case Event::SET_PLAYER_TITLES:
            catalogSync_->handleSetPlayerTitlesEvent(event);
            break;
        case Event::GET_PLAYER_TITLES:
            handleGetPlayerTitlesEvent(event);
            break;
        case Event::EQUIP_TITLE:
            handleEquipTitleEvent(event);
            break;
        case Event::SET_SKILL_BAR_SLOT:
            catalogSync_->handleSetSkillBarSlotEvent(event);
            break;

        // ── Emote system ──────────────────────────────────────────────────────
        case Event::SET_EMOTE_DEFINITIONS:
            catalogSync_->handleSetEmoteDefinitionsEvent(event);
            break;
        case Event::SET_PLAYER_EMOTES:
            catalogSync_->handleSetPlayerEmotesEvent(event);
            break;
        case Event::USE_EMOTE:
            handleUseEmoteEvent(event);
            break;

        // ── NPC Ambient Speech system ─────────────────────────────────────────
        case Event::SET_NPC_AMBIENT_SPEECH:
            npcEventHandler_->handleSetNPCAmbientSpeechEvent(event);
            break;

        // ── World Interactive Objects (migration 043) ─────────────────────────
        case Event::SET_ALL_WORLD_OBJECTS:
            if (worldObjectEventHandler_)
                worldObjectEventHandler_->handleSetAllWorldObjectsEvent(event);
            break;
        case Event::WORLD_OBJECT_INTERACT:
            if (worldObjectEventHandler_)
                worldObjectEventHandler_->handleInteractEvent(event);
            break;
        case Event::WORLD_OBJECT_CHANNEL_CANCEL:
            if (worldObjectEventHandler_)
                worldObjectEventHandler_->handleChannelCancelEvent(event);
            break;

        // Skill system
        case Event::SET_LEARNED_SKILL:
            catalogSync_->handleSetLearnedSkillEvent(event);
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
            catalogSync_->handleSetCharacterAttributesRefreshEvent(event);
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

CharacterEventHandler &
EventHandler::getCharacterEventHandler()
{
    return *characterEventHandler_;
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

WorldObjectEventHandler &
EventHandler::getWorldObjectEventHandler()
{
    return *worldObjectEventHandler_;
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
        // Pending-sync token echo (0 = no token sent; UE never sends one,
        // the server-side token match then falls back to row lookup).
        uint64_t syncToken = j.value("syncToken", (uint64_t)0);

        if (characterId <= 0 || itemId <= 0 || newId <= 0)
            return;

        gameServices_.getInventoryManager().updateInventoryItemId(characterId, itemId, newId, syncToken);

        log_->info("[INVENTORY_ID_SYNC] char=" + std::to_string(characterId) +
                   " itemId=" + std::to_string(itemId) +
                   " assignedId=" + std::to_string(newId));

        // Flush equip/unequip persists stashed while this row awaited its
        // id (they now carry the real id assigned above).
        for (const auto &s : gameServices_.getInventoryManager().drainPendingEquipSaves())
            equipmentEventHandler_->sendPendingEquipSave(s.characterId, s.action,
                s.slotSlug, s.inventoryItemId, s.itemId);
    }
    catch (const std::exception &e)
    {
        log_->error("[INVENTORY_ID_SYNC] exception: " + std::string(e.what()));
    }
}

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

        // Item slug resolver: unknown item => "" (row renders without a name;
        // the entry itself still sends — a broken slug must not fail it).
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
EventHandler::handleUseEmoteEvent(const Event &event)
{
    if (emoteEventHandler_)
        emoteEventHandler_->handleUseEmoteEvent(event);
    else
        log_->error("handleUseEmoteEvent: emoteEventHandler_ is null");
}
