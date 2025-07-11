#include "events/EventHandler.hpp"

EventHandler::EventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : gameServices_(gameServices)
{
    // Initialize all specialized event handlers
    clientEventHandler_ = std::make_unique<ClientEventHandler>(networkManager, gameServerWorker, gameServices);
    characterEventHandler_ = std::make_unique<CharacterEventHandler>(networkManager, gameServerWorker, gameServices);
    mobEventHandler_ = std::make_unique<MobEventHandler>(networkManager, gameServerWorker, gameServices);
    zoneEventHandler_ = std::make_unique<ZoneEventHandler>(networkManager, gameServerWorker, gameServices);
    chunkEventHandler_ = std::make_unique<ChunkEventHandler>(networkManager, gameServerWorker, gameServices);
    combatEventHandler_ = std::make_unique<CombatEventHandler>(networkManager, gameServerWorker, gameServices);
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

        // Mob Events
        case Event::SET_ALL_MOBS_LIST:
            mobEventHandler_->handleSetAllMobsListEvent(event);
            break;
        case Event::SET_ALL_MOBS_ATTRIBUTES:
            mobEventHandler_->handleSetMobsAttributesEvent(event);
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

        // New Attack Events
        case Event::PLAYER_ATTACK:
            combatEventHandler_->handlePlayerAttack(event);
            break;
        case Event::AI_ATTACK:
        {
            // Extract character ID for AI attack
            const auto &data = event.getData();
            if (std::holds_alternative<int>(data))
            {
                int characterId = std::get<int>(data);
                combatEventHandler_->handleAIAttack(characterId);
            }
            else
            {
                gameServices_.getLogger().logError("Invalid data for AI_ATTACK event");
            }
        }
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

        // Legacy events that might not have direct mapping
        case Event::LEAVE_GAME_CLIENT:
            clientEventHandler_->handleDisconnectClientEvent(event);
            break;
        case Event::LEAVE_GAME_CHUNK:
            chunkEventHandler_->handleDisconnectChunkEvent(event);
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
