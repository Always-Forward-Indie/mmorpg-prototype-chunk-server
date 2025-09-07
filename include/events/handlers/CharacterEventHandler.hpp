#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include "events/handlers/SkillEventHandler.hpp"
#include <memory>
#include <unordered_map>

/**
 * @brief Structure to hold pending join requests
 */
struct PendingJoinRequest
{
    int clientID;
    int characterId;
    TimestampStruct timestamps;
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket;
};

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
     * @brief Set skill event handler for skill initialization
     *
     * @param skillEventHandler Pointer to skill event handler
     */
    void setSkillEventHandler(SkillEventHandler *skillEventHandler);

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

    /**
     * @brief Initialize player skills after successful character join
     *
     * @param characterData Character data containing skills
     * @param clientID Client ID to send skills to
     * @param clientSocket Client socket for response
     */
    void initializePlayerSkills(const CharacterDataStruct &characterData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);

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

    /**
     * @brief Process pending join requests for a character
     *
     * @param characterId Character ID that just became available
     */
    void processPendingJoinRequests(int characterId);

    /**
     * @brief Get character data by ID
     */
    CharacterDataStruct getCharacterDataById(int characterId) const;

    // Store pending join requests while waiting for character data from Game Server
    std::unordered_map<int, std::vector<PendingJoinRequest>> pendingJoinRequests_;

    // Pointer to skill event handler for skill initialization
    SkillEventHandler *skillEventHandler_ = nullptr;
};
