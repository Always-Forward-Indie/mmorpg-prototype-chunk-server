#pragma once

#include "chunk_server/ChunkServer.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"

class EventDispatcher
{
  public:
    EventDispatcher(EventQueue &eventQueue, EventQueue &eventQueuePing, ChunkServer *chunkServer, GameServices &gameServices);

    void dispatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

  private:
    void handleJoinGameClient(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleJoinGameCharacter(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleMoveCharacter(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleDisconnect(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePing(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetSpawnZones(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetConnectedClients(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePlayerAttack(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePickupDroppedItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerInventory(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleHarvestStart(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleHarvestCancel(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetNearbyCorpses(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleCorpseLootPickup(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleCorpseLootInspect(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    ChunkServer *chunkServer_;

    std::vector<Event> eventsBatch_;
    constexpr static int BATCH_SIZE = 10;

    GameServices &gameServices_;
};