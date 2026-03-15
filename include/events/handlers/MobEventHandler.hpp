#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for mob-related events
 *
 * Handles all events related to mobs such as spawning,
 * movement, data management, and attribute handling.
 */
class MobEventHandler : public BaseEventHandler
{
  public:
    MobEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle spawn mobs in zone event
     *
     * Spawns mobs in specified zone and sends response to client
     *
     * @param event Event containing spawn zone data
     */
    void handleSpawnMobsInZoneEvent(const Event &event);

    /**
     * @brief Handle zone move mobs event
     *
     * Handles mob movement within a zone
     *
     * @param event Event containing zone ID or mob movement data
     */
    void handleZoneMoveMobsEvent(const Event &event);

    /**
     * @brief Handle mob death event
     *
     * Sends notification to clients about mob death/removal
     *
     * @param event Event containing mob death data (mobUID and zoneId)
     */
    void handleMobDeathEvent(const Event &event);

    /**
     * @brief Handle mob target lost event
     *
     * Sends notification to clients when mob loses target
     *
     * @param event Event containing mob target lost data
     */
    void handleMobTargetLostEvent(const Event &event);

    /**
     * @brief Handle mob health update event (e.g. leash HP regeneration)
     *
     * Broadcasts current HP to all clients so health bars stay in sync.
     *
     * @param event Event containing mobUID, mobId, currentHealth, maxHealth
     */
    void handleMobHealthUpdateEvent(const Event &event);

    /**
     * @brief Handle set all mobs list event
     *
     * Loads all mobs data into the mob manager
     *
     * @param event Event containing list of mobs
     */
    void handleSetAllMobsListEvent(const Event &event);

    /**
     * @brief Handle get mob data event
     *
     * Retrieves specific mob data and sends to game server
     *
     * @param event Event containing mob ID to retrieve
     */
    void handleGetMobDataEvent(const Event &event);

    /**
     * @brief Handle set mobs attributes event
     *
     * Loads mob attributes into the mob manager
     *
     * @param event Event containing mob attributes
     */
    void handleSetMobsAttributesEvent(const Event &event);

    /**
     * @brief Handle set mobs skills event
     *
     * Loads mob skills into the mob manager
     *
     * @param event Event containing mob skills mapping
     */
    void handleSetMobsSkillsEvent(const Event &event);

    /**
     * @brief Handle set mob weaknesses and resistances event
     *
     * Loads mob weaknesses and resistances into the mob manager
     *
     * @param event Event containing per-mob weaknesses and resistances maps
     */
    void handleSetMobWeaknessesResistancesEvent(const Event &event);

    /**
     * @brief Handle lightweight per-tick mob movement update.
     *
     * Sends compact position+velocity packets to clients.
     * Full mob data is already known from spawnMobsInZone — only uid/pos/velocity
     * are transmitted here to minimize bandwidth.
     *
     * @param event Event containing vector<MobMoveUpdateStruct>
     */
    void handleMobMoveUpdateEvent(const Event &event);

    /**
     * @brief Push all spawn zones and their current live mobs to a single client.
     *
     * Called server-side on character join so the client does not need to issue
     * a separate getSpawnZones request.
     *
     * @param clientID    Target client ID (used in response header)
     * @param clientSocket Target client socket
     */
    void sendSpawnZonesToClient(int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);

  private:
    /**
     * @brief Convert mob data to JSON format
     *
     * @param mobData Mob data structure
     * @return nlohmann::json Mob data in JSON format
     */
    nlohmann::json mobToJson(const MobDataStruct &mobData);

    /**
     * @brief Convert spawn zone data to JSON format
     *
     * @param spawnZoneData Spawn zone data structure
     * @return nlohmann::json Spawn zone data in JSON format
     */
    nlohmann::json spawnZoneToJson(const SpawnZoneStruct &spawnZoneData);
};
