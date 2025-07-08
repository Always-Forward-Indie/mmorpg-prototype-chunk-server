#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for character-related events
 *
 * Handles all events related to character operations such as
 * joining, moving, getting character lists, and managing character data.
 */
class CharacterEventHandler : public BaseEventHandler
{
  public:
    CharacterEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle character join event
     *
     * Validates character and broadcasts join notification to all clients
     *
     * @param event Event containing character data
     */
    void handleJoinCharacterEvent(const Event &event);

    /**
     * @brief Handle character movement event
     *
     * Updates character position and broadcasts movement to all clients
     *
     * @param event Event containing movement data
     */
    void handleMoveCharacterEvent(const Event &event);

    /**
     * @brief Handle get connected characters request
     *
     * Returns list of all currently connected characters
     *
     * @param event Event containing client request
     */
    void handleGetConnectedCharactersEvent(const Event &event);

    /**
     * @brief Handle set character data event
     *
     * Sets character data in the character manager
     *
     * @param event Event containing character data
     */
    void handleSetCharacterDataEvent(const Event &event);

    /**
     * @brief Handle set characters list event
     *
     * Loads multiple characters into the character manager
     *
     * @param event Event containing list of characters
     */
    void handleSetCharactersListEvent(const Event &event);

    /**
     * @brief Handle set character attributes event
     *
     * Loads character attributes into the character manager
     *
     * @param event Event containing character attributes
     */
    void handleSetCharacterAttributesEvent(const Event &event);

  private:
    /**
     * @brief Convert character data to JSON format
     *
     * @param characterData Character data structure
     * @return nlohmann::json Character data in JSON format
     */
    nlohmann::json characterToJson(const CharacterDataStruct &characterData);

    /**
     * @brief Validate character authentication
     *
     * @param clientId Client ID
     * @param characterId Character ID
     * @return true if valid, false otherwise
     */
    bool validateCharacterAuthentication(int clientId, int characterId);
};
