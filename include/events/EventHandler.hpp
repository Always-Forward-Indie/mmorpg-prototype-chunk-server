#pragma once

#include "Event.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <boost/asio.hpp>
#include <string>

class EventHandler
{
  public:
    EventHandler(NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);
    void dispatchEvent(const Event &event);

  private:
    void handlePingClientEvent(const Event &event);
    void handleJoinClientEvent(const Event &event);
    void handleJoinCharacterEvent(const Event &event);
    void handleGetConnectedClientsChunkEvent(const Event &event);
    void handleJoinChunkEvent(const Event &event);
    void handleLeaveClientEvent(const Event &event);
    void handleLeaveChunkEvent(const Event &event);
    void handleGetConnectedCharactersChunkEvent(const Event &event);
    void handleMoveCharacterClientEvent(const Event &event);
    void handleSpawnMobsInZoneEvent(const Event &event);
    void handleGetSpawnZoneDataEvent(const Event &event);
    void handleGetMobDataEvent(const Event &event);
    void handleZoneMoveMobsEvent(const Event &event);
    void handleSetAllMobsListEvent(const Event &event);
    void handleSetMobsAttributesEvent(const Event &event);
    void handleSetAllSpawnZonesEvent(const Event &event);
    void handleSetCharacterDataEvent(const Event &event);
    void handleSetCharactersListEvent(const Event &event);
    void handleSetCharacterAttributesEvent(const Event &event);
    void handleDisconnectChunkEvent(const Event &event);
    void handleDisconnectClientEvent(const Event &event);
    void handleInitChunkEvent(const Event &event);

    NetworkManager &networkManager_;
    GameServerWorker &gameServerWorker_;
    GameServices &gameServices_;
};