#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include "events/handlers/SkillEventHandler.hpp"
#include <memory>
#include <mutex>
#include <unordered_map>

// Forward declaration to avoid circular include
class NPCEventHandler;
class ItemEventHandler;
class MobEventHandler;
class EquipmentEventHandler;

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
     * @brief Set NPC event handler for NPC spawn data
     *
     * @param npcEventHandler Pointer to NPC event handler
     */
    void setNPCEventHandler(NPCEventHandler *npcEventHandler);

    /**
     * @brief Set item event handler for sending ground items snapshot on join
     */
    void setItemEventHandler(ItemEventHandler *itemEventHandler);

    /**
     * @brief Set mob event handler for server-push spawn zones on join
     */
    void setMobEventHandler(MobEventHandler *mobEventHandler);

    /**
     * @brief Set equipment event handler for broadcasting equipment updates on join
     */
    void setEquipmentEventHandler(EquipmentEventHandler *equipmentEventHandler);

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

  private:
    /**
     * @brief Push connected characters list + equipment to one client socket.
     *
     * Sends a single `getConnectedCharacters` response followed by one
     * `PLAYER_EQUIPMENT_UPDATE` per character. Used by both
     * handleGetConnectedCharactersEvent (client-requested) and
     * handlePlayerReadyEvent (Phase 4 server push).
     *
     * @param clientID      Target client ID
     * @param clientSocket  Target socket
     * @param excludeCharacterId  Skip this character (pass own characterId in Phase 4,
     *                            or -1 to include all)
     */
    void pushConnectedCharactersToClient(
        int clientID,
        std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket,
        int excludeCharacterId);

  public:
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
     * @brief Handle player ready event (Phase 4 world-state push)
     *
     * Called when the client sends "playerReady" — scene has finished loading.
     * Pushes mobs, NPCs, ground items, and other players' equipment to the client.
     *
     * @param event Event containing CharacterDataStruct (characterId only)
     */
    void handlePlayerReadyEvent(const Event &event);

    /**
     * @brief Handle player respawn request
     *
     * Verifies player is dead, teleports to nearest respawn zone, restores HP/Mana,
     * applies Resurrection Sickness, and converts pending XP penalty to experience debt.
     *
     * @param event Event containing RespawnRequestStruct
     */
    void handlePlayerRespawnEvent(const Event &event);

    /**
     * @brief Broadcast a PLAYER_TITLE_CHANGED packet to all zone clients except one.
     *
     * Sends the currently equipped title slug of @p characterId to every connected
     * client, excluding @p excludeClientId (use the owner's clientId so they don't
     * receive a redundant update on top of the private player_titles_update).
     * Called from:
     *  – handlePlayerReadyEvent (Phase 4 catch-up, in case titles loaded before ready)
     *  – EventHandler::handleSetPlayerTitlesEvent (catch-up if titles load after ready)
     *  – EventHandler::handleEquipTitleEvent (live title change by the owner)
     *
     * @param characterId      Owner whose equipped title is broadcast.
     * @param excludeClientId  Client to skip (-1 = include everyone).
     */
    void broadcastTitleChanged(int characterId, int excludeClientId = -1);

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
     * @brief Evict a stale session for a character that is already loaded.
     *
     * If `characterId` is already present in CharacterManager (left over from a
     * previous connection that did not cleanly disconnect), this method:
     *   1. Flushes position, HP/Mana, quests, flags, reputation, mastery and
     *      ItemSoul kill-count to the game-server.
     *   2. Removes the character and old client entry from their respective managers.
     *   3. Broadcasts a `disconnectClient` notification to all currently connected
     *      clients, excluding both the dead old socket and `newClientId` (the
     *      reconnecting client that has not yet joined).
     *
     * @param characterId  The character being reclaimed.
     * @param newClientId  The clientId of the incoming (re)connect — excluded from
     *                     the disconnect broadcast so it does not confuse itself.
     */
    void evictStaleSession(int characterId, int newClientId);

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
    std::mutex pendingJoinRequestsMutex_;

    // Reference to skill event handler for skill initialization
    SkillEventHandler *skillEventHandler_;

    // Reference to NPC event handler for NPC spawn data
    NPCEventHandler *npcEventHandler_;

    // Reference to item event handler for ground items snapshot on join
    ItemEventHandler *itemEventHandler_;

    // Reference to mob event handler for server-push spawn zones on join
    MobEventHandler *mobEventHandler_;

    // Reference to equipment event handler for broadcasting equipment on player join
    EquipmentEventHandler *equipmentEventHandler_;

    // Last known GameZone id per characterId — used to detect zone transitions
    std::unordered_map<int, int> lastZoneByCharacter_;
};
