#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include <memory>

/**
 * @brief Handler for skill-related events
 *
 * Handles all events related to player skills such as
 * initialization, updates, learning new skills, and skill usage tracking.
 */
class SkillEventHandler : public BaseEventHandler
{
  public:
    SkillEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle player skills initialization
     *
     * Sends player's skills to client for initialization
     *
     * @param event Event containing PlayerSkillInitStruct data
     */
    void handleInitializePlayerSkills(const Event &event);

    /**
     * @brief Handle player skills initialization with direct socket access
     *
     * Alternative method for direct skill initialization without event system
     *
     * @param skillInitData Skill initialization data
     * @param clientID Client ID to send skills to
     * @param clientSocket Client socket for response
     */
    void handleInitializePlayerSkillsDirect(const PlayerSkillInitStruct &skillInitData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);

    /**
     * @brief Initialize player skills from character data
     *
     * Convenience method to initialize skills from character data
     *
     * @param characterData Character data containing skills
     * @param clientID Client ID to send skills to
     * @param clientSocket Client socket for response
     */
    void initializeFromCharacterData(const CharacterDataStruct &characterData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);

  private:
    /**
     * @brief Build skills JSON response
     *
     * @param skillInitData Skill initialization data
     * @return nlohmann::json JSON response with skills
     */
    nlohmann::json buildSkillsResponse(const PlayerSkillInitStruct &skillInitData);

    /**
     * @brief Send skills response to client
     *
     * @param response JSON response to send
     * @param clientID Client ID
     * @param clientSocket Client socket
     */
    void sendSkillsResponse(const nlohmann::json &response, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);

    /**
     * @brief Send error response to client
     *
     * @param errorMessage Error message to send
     * @param clientID Client ID
     * @param clientSocket Client socket
     */
    void sendErrorResponse(const std::string &errorMessage, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
};
