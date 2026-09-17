#include "services/CharacterStatsNotificationService.hpp"
#include "services/CharacterManager.hpp"
#include "services/EquipmentManager.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/StatsPacketBuilder.hpp"
#include "utils/Logger.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

CharacterStatsNotificationService::CharacterStatsNotificationService(CharacterManager &characters,
    ExperienceManager &experience,
    InventoryManager &inventory,
    EquipmentManager &equipment,
    ItemManager &items,
    GameConfigService &gameConfig,
    GameZoneManager &gameZones,
    Logger &logger)
    : characters_(characters),
      experience_(experience),
      inventory_(inventory),
      equipment_(equipment),
      items_(items),
      gameConfig_(gameConfig),
      gameZones_(gameZones),
      logger_(logger)
{
    log_ = logger_.getSystem("character");
}

void
CharacterStatsNotificationService::sendStatsUpdate(int characterId)
{
    if (statsUpdateCallback_)
    {
        try
        {
            auto packet = buildStatsUpdatePacket(characterId);
            statsUpdateCallback_(packet);
        }
        catch (const std::exception &e)
        {
            logger_.logError("Failed to send stats update for character " +
                             std::to_string(characterId) + ": " + e.what());
        }
    }
}

void
CharacterStatsNotificationService::sendStatsUpdate(int characterId, const std::string &source)
{
    if (statsUpdateCallback_)
    {
        try
        {
            auto packet = buildStatsUpdatePacket(characterId);
            if (!source.empty())
                packet["body"]["source"] = source;
            statsUpdateCallback_(packet);
        }
        catch (const std::exception &e)
        {
            logger_.logError("Failed to send stats update for character " +
                             std::to_string(characterId) + ": " + e.what());
        }
    }
}

void
CharacterStatsNotificationService::sendWorldNotification(int characterId,
    const std::string &notificationType,
    const nlohmann::json &data,
    const std::string &priority,
    const std::string &channel)
{
    if (!directSendCallback_ && !statsUpdateCallback_)
        return;
    try
    {
        nlohmann::json packet;
        packet["header"]["eventType"] = "world_notification";
        packet["header"]["status"] = "success";
        packet["body"]["characterId"] = characterId;
        packet["body"]["notificationId"] = std::to_string(++notifSeq_);
        packet["body"]["notificationType"] = notificationType;
        packet["body"]["priority"] = priority;
        packet["body"]["channel"] = channel;
        packet["body"]["text"] = "";
        packet["body"]["data"] = data;
        if (directSendCallback_)
            directSendCallback_(characterId, packet);
        else
            statsUpdateCallback_(packet);
    }
    catch (const std::exception &e)
    {
        logger_.logError("sendWorldNotification error: " + std::string(e.what()));
    }
}

void
CharacterStatsNotificationService::setStatsUpdateCallback(std::function<void(const nlohmann::json &)> callback)
{
    statsUpdateCallback_ = callback;
}

void
CharacterStatsNotificationService::setDirectSendCallback(
    std::function<void(int, const nlohmann::json &)> callback)
{
    directSendCallback_ = std::move(callback);
}

void
CharacterStatsNotificationService::sendWorldNotificationToGameZone(int gameZoneId,
    const std::string &notificationType,
    const nlohmann::json &data,
    const std::string &priority,
    const std::string &channel)
{
    if (!statsUpdateCallback_)
        return;
    try
    {
        // Retrieve all character IDs currently connected
        auto allChars = characters_.getCharactersList();
        for (const auto &charData : allChars)
        {
            if (charData.characterId == 0)
                continue;

            auto zone = gameZones_.getZoneForPosition(charData.characterPosition);
            if (!zone.has_value() || zone->id != gameZoneId)
                continue;

            sendWorldNotification(charData.characterId, notificationType, data, priority, channel);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("sendWorldNotificationToGameZone error: " + std::string(e.what()));
    }
}

nlohmann::json
CharacterStatsNotificationService::buildStatsUpdatePacket(int characterId)
{
    const auto characterData = characters_.getCharacterData(characterId);

    StatsPacketBuilderInput in;
    in.character = characterData;

    // ── Experience level thresholds ───────────────────────────────────────────
    in.levelStart = experience_.getExperienceForLevelFromGameServer(
        characterData.characterLevel);

    // ── Weight ────────────────────────────────────────────────────────────────
    in.currentWeight = inventory_.getTotalWeight(characterId);
    in.weightLimit = equipment_.getCarryWeightLimit(characterId);

    // ── Resolve equipped-item bonuses (apply_on == "equip") ──────────────────
    const auto equipState = equipment_.getEquipmentState(characterId);
    for (const auto &[slotSlug, slot] : equipState.slots)
    {
        if (slot.inventoryItemId == 0)
            continue;
        const auto item = items_.getItemById(slot.itemId);
        if (item.id == 0)
            continue;
        for (const auto &attr : item.attributes)
        {
            if (attr.apply_on != "equip")
                continue;
            in.equipBonuses[attr.slug] += attr.value;
            if (in.equipNames.find(attr.slug) == in.equipNames.end())
                in.equipNames[attr.slug] = attr.name;
        }
    }

    in.nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                    .count();

    // Item Soul: resolve kill-count tier bonus to the equipped weapon's primary attribute
    try
    {
        auto weaponOpt = inventory_.getEquippedWeapon(characterId);
        if (weaponOpt.has_value())
        {
            const auto &cfg = gameConfig_;
            const int kc = weaponOpt->killCount;
            const int t1 = cfg.getInt("item_soul.tier1_kills", 50);
            const int t2 = cfg.getInt("item_soul.tier2_kills", 200);
            const int t3 = cfg.getInt("item_soul.tier3_kills", 500);
            const int soulBonus = (kc >= t3)   ? cfg.getInt("item_soul.tier3_bonus_flat", 3)
                                  : (kc >= t2) ? cfg.getInt("item_soul.tier2_bonus_flat", 2)
                                  : (kc >= t1) ? cfg.getInt("item_soul.tier1_bonus_flat", 1)
                                               : 0;
            if (soulBonus > 0)
            {
                const auto &wItem = items_.getItemById(weaponOpt->itemId);
                for (const auto &attr : wItem.attributes)
                {
                    if (attr.apply_on == "equip" && !attr.slug.empty())
                    {
                        in.soulAttrSlug = attr.slug;
                        in.soulAttrName = attr.name;
                        in.soulBonusFlat = soulBonus;
                        break; // one bonus per weapon
                    }
                }
            }
        }
    }
    catch (...)
    {
    }

    return StatsPacketBuilder::build(in);
}
