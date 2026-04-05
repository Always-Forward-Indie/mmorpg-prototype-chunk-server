#pragma once

#include "data/DataStructs.hpp"
#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handles equipment system packets.
 *
 * Client → Chunk Server:
 *   equipItem   → EQUIP_ITEM
 *   unequipItem → UNEQUIP_ITEM
 *   getEquipment → GET_EQUIPMENT
 */
class EquipmentEventHandler : public BaseEventHandler
{
  public:
    EquipmentEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    void handleEquipItemEvent(const Event &event);
    void handleUnequipItemEvent(const Event &event);
    void handleGetEquipmentEvent(const Event &event);

    /** Send EQUIPMENT_STATE packet to the client. */
    void sendEquipmentState(int clientId, int characterId, const TimestampStruct &ts);

    /** Calculate carry weight and push WEIGHT_STATUS packet to the client. */
    void sendWeightStatus(int clientId, int characterId);

    /**
     * Broadcast PLAYER_EQUIPMENT_UPDATE to all clients so they can render
     * the correct gear visuals on other characters.
     * @param excludeClientId  Client that already received EQUIPMENT_STATE (owner).
     *                         Pass -1 to broadcast to everyone.
     */
    void broadcastEquipmentUpdate(int characterId, int excludeClientId = -1);

  private:
    /** Persist equip/unequip to game server (character_equipment table). */
    void saveEquipmentChange(
        int characterId,
        const std::string &action,
        int inventoryItemId,
        const std::string &slotSlug);

    /** Ask game server to re-query character attributes (re-applies equip bonuses). */
    void triggerAttributesRefresh(int characterId, int clientId);
};
