#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for zone-related events
 *
 * Handles all events related to spawn zones and zone management.
 */
class ZoneEventHandler : public BaseEventHandler
{
  public:
    ZoneEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Handle set all spawn zones event
     *
     * Loads all spawn zones data into the spawn zone manager
     *
     * @param event Event containing list of spawn zones
     */
    void handleSetAllSpawnZonesEvent(const Event &event);

    /**
     * @brief Handle get spawn zone data event
     *
     * Retrieves specific spawn zone data
     *
     * @param event Event containing zone ID to retrieve
     */
    void handleGetSpawnZoneDataEvent(const Event &event);

  private:
    /**
     * @brief Log spawn zone information for debugging
     *
     * @param spawnZone Spawn zone data structure
     */
    void logSpawnZoneInfo(const SpawnZoneStruct &spawnZone);
};
