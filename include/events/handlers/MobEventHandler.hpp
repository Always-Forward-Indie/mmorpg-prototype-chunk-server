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
