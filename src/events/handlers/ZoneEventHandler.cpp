#include "events/handlers/ZoneEventHandler.hpp"
#include "events/EventData.hpp"
#include <spdlog/logger.h>

ZoneEventHandler::ZoneEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "zone")
{
    log_ = gameServices_.getLogger().getSystem("zone");
}

void
ZoneEventHandler::logSpawnZoneInfo(const SpawnZoneStruct &spawnZone)
{
    gameServices_.getLogger().log("Spawn Zone ID: " + std::to_string(spawnZone.zoneId) +
                                  ", Name: " + spawnZone.zoneName +
                                  ", MinX: " + std::to_string(spawnZone.posX) +
                                  ", MaxX: " + std::to_string(spawnZone.sizeX) +
                                  ", MinY: " + std::to_string(spawnZone.posY) +
                                  ", MaxY: " + std::to_string(spawnZone.sizeY) +
                                  ", MinZ: " + std::to_string(spawnZone.posZ) +
                                  ", MaxZ: " + std::to_string(spawnZone.sizeZ) +
                                  ", Spawn Mob ID: " + std::to_string(spawnZone.spawnMobId) +
                                  ", Max Spawn Count: " + std::to_string(spawnZone.spawnCount) +
                                  ", Respawn Time: " + std::to_string(spawnZone.respawnTime.count()) +
                                  ", Spawn Enabled: " + std::to_string(spawnZone.spawnEnabled));
}

void
ZoneEventHandler::handleSetAllSpawnZonesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<SpawnZoneStruct>>(data))
        {
            std::vector<SpawnZoneStruct> spawnZonesList = std::get<std::vector<SpawnZoneStruct>>(data);

            // Debug spawn zones
            for (const auto &spawnZone : spawnZonesList)
            {
                logSpawnZoneInfo(spawnZone);
            }

            // Set data to the spawn zone manager
            gameServices_.getSpawnZoneManager().loadMobSpawnZones(spawnZonesList);

            log_->info("Loaded all spawn zones data from the event handler!");
        }
        else
        {
            log_->info("Error with extracting data!");
        }
    }
    catch (const std::bad_variant_access &ex)
    {
        gameServices_.getLogger().log("Error here: " + std::string(ex.what()));
    }
}

void
ZoneEventHandler::handleGetSpawnZoneDataEvent(const Event &event)
{
    const auto &data = event.getData();

    // Implementation placeholder - can be extended based on requirements
    log_->info("HandleGetSpawnZoneDataEvent called - implementation placeholder");
}

void
ZoneEventHandler::handleSetRespawnZonesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<RespawnZoneStruct>>(data))
        {
            std::vector<RespawnZoneStruct> zones = std::get<std::vector<RespawnZoneStruct>>(data);
            gameServices_.getRespawnZoneManager().loadRespawnZones(zones);
            log_->info("[ZoneEventHandler] Loaded {} respawn zones", zones.size());
        }
        else
        {
            log_->error("[ZoneEventHandler] handleSetRespawnZonesEvent: unexpected data variant");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[ZoneEventHandler] handleSetRespawnZonesEvent: " + std::string(ex.what()));
    }
}

void
ZoneEventHandler::handleSetGameZonesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<GameZoneStruct>>(data))
        {
            std::vector<GameZoneStruct> zones = std::get<std::vector<GameZoneStruct>>(data);
            gameServices_.getGameZoneManager().loadGameZones(zones);
            log_->info("[ZoneEventHandler] Loaded {} game zones", zones.size());
        }
        else
        {
            log_->error("[ZoneEventHandler] handleSetGameZonesEvent: unexpected data variant");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[ZoneEventHandler] handleSetGameZonesEvent: " + std::string(ex.what()));
    }
}

void
ZoneEventHandler::handleSetTimedChampionTemplatesEvent(const Event &event)
{
    const auto &data = event.getData();

    try
    {
        if (std::holds_alternative<std::vector<TimedChampionTemplate>>(data))
        {
            auto templates = std::get<std::vector<TimedChampionTemplate>>(data);
            gameServices_.getChampionManager().loadTimedChampions(templates);
            log_->info("[ZoneEventHandler] Loaded {} timed champion templates", templates.size());
        }
        else
        {
            log_->error("[ZoneEventHandler] handleSetTimedChampionTemplatesEvent: unexpected data variant");
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[ZoneEventHandler] handleSetTimedChampionTemplatesEvent: " + std::string(ex.what()));
    }
}
