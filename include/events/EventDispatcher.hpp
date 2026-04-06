#pragma once

#include "chunk_server/ChunkServer.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include <mutex>

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
    void handleGetConnectedClients(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePlayerAttack(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePickupDroppedItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetPlayerInventory(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleHarvestStart(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleHarvestCancel(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetNearbyCorpses(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleCorpseLootPickup(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleCorpseLootInspect(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetCharacterExperience(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleNPCInteract(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleDialogueChoice(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleDialogueClose(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    // Vendor / Repair / Trade
    void handleOpenVendorShop(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleOpenSkillShop(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleRequestLearnSkill(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleBuyItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSellItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleBuyItemBatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleSellItemBatch(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleOpenRepairShop(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleRepairItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleRepairAll(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTradeRequest(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTradeAccept(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTradeDecline(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTradeOfferUpdate(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTradeConfirm(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleTradeCancel(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    // Equipment
    void handleEquipItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleUnequipItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetEquipment(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    // Respawn
    void handleRespawnRequest(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    // Item drop by player and use
    void handleDropItemByPlayer(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleUseItem(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetBestiaryEntry(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleGetBestiaryOverview(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handleChatMessage(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void handlePlayerReady(const EventContext &context, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    EventQueue &eventQueue_;
    EventQueue &eventQueuePing_;
    ChunkServer *chunkServer_;

    std::vector<Event> eventsBatch_;
    constexpr static int BATCH_SIZE = 10;
    mutable std::mutex dispatchMutex_; ///< CRITICAL-3: serialises concurrent dispatch() calls from io_context threads

    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
};