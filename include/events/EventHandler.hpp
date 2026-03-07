#pragma once

#include "Event.hpp"
#include "events/ExperienceEventHandler.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include "events/handlers/CharacterEventHandler.hpp"
#include "events/handlers/ChunkEventHandler.hpp"
#include "events/handlers/ClientEventHandler.hpp"
#include "events/handlers/CombatEventHandler.hpp"
#include "events/handlers/DialogueEventHandler.hpp"
#include "events/handlers/HarvestEventHandler.hpp"
#include "events/handlers/ItemEventHandler.hpp"
#include "events/handlers/MobEventHandler.hpp"
#include "events/handlers/NPCEventHandler.hpp"
#include "events/handlers/SkillEventHandler.hpp"
#include "events/handlers/ZoneEventHandler.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include <memory>

/**
 * @brief Main event handler that coordinates all specialized event handlers
 *
 * This class acts as a facade/coordinator that delegates events to the appropriate
 * specialized handlers based on event type. It follows the Single Responsibility
 * Principle by separating different categories of event handling into dedicated classes.
 */
class EventHandler
{
  public:
    /**
     * @brief Construct a new Event Handler object
     *
     * Creates and initializes all specialized event handlers
     *
     * @param networkManager Reference to network manager for client communication
     * @param gameServerWorker Reference to game server worker for server communication
     * @param gameServices Reference to game services for business logic
     */
    EventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Dispatch event to appropriate specialized handler
     *
     * Routes events to the correct handler based on event type using
     * the Command pattern for clean separation of concerns.
     *
     * @param event The event to be processed
     */
    void dispatchEvent(const Event &event);

    /**
     * @brief Get reference to combat event handler for ongoing actions update
     *
     * This allows external systems (like the game loop scheduler) to call
     * updateOngoingActions() to process timed combat events.
     *
     * @return Reference to the combat event handler
     */
    CombatEventHandler &getCombatEventHandler();

    /**
     * @brief Get reference to skill event handler for skill operations
     *
     * @return Reference to the skill event handler
     */
    SkillEventHandler &getSkillEventHandler();

    /**
     * @brief Get reference to NPC event handler for sending spawn data
     *
     * @return Reference to the NPC event handler
     */
    NPCEventHandler &getNPCEventHandler();

    /**
     * @brief Get reference to dialogue event handler.
     */
    DialogueEventHandler &getDialogueEventHandler();

  private:
    /**
     * @brief Handle experience level table data from game server
     * Sets up experience cache with level progression data
     */
    void handleSetExpLevelTableEvent(const Event &event);

    /**
     * @brief Handle game config data from game server.
     *        Populates GameConfigService so CombatCalculator and other
     *        services can read gameplay constants without hardcoding.
     */
    void handleSetGameConfigEvent(const Event &event);
    void handleSetPlayerActiveEffectsEvent(const Event &event);
    void handleSetCharacterAttributesRefreshEvent(const Event &event);

    // Specialized event handlers for different event categories
    std::unique_ptr<ClientEventHandler> clientEventHandler_;
    std::unique_ptr<CharacterEventHandler> characterEventHandler_;
    std::unique_ptr<MobEventHandler> mobEventHandler_;
    std::unique_ptr<NPCEventHandler> npcEventHandler_;
    std::unique_ptr<ZoneEventHandler> zoneEventHandler_;
    std::unique_ptr<ChunkEventHandler> chunkEventHandler_;
    std::unique_ptr<CombatEventHandler> combatEventHandler_;
    std::unique_ptr<ItemEventHandler> itemEventHandler_;
    std::unique_ptr<HarvestEventHandler> harvestEventHandler_;
    std::unique_ptr<SkillEventHandler> skillEventHandler_;
    std::unique_ptr<ExperienceEventHandler> experienceEventHandler_;
    std::unique_ptr<DialogueEventHandler> dialogueEventHandler_;

    // References for logging and error handling
    NetworkManager &networkManager_;
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};
