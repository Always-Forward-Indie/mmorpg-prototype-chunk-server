#include "services/CharacterStatsNotificationService.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "services/GameZoneManager.hpp"
#include "utils/Logger.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <unordered_map>

CharacterStatsNotificationService::CharacterStatsNotificationService(GameServices *gameServices)
    : gameServices_(gameServices)
{
    log_ = gameServices_->getLogger().getSystem("character");
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
            gameServices_->getLogger().logError("Failed to send stats update for character " +
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
            gameServices_->getLogger().logError("Failed to send stats update for character " +
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
        gameServices_->getLogger().logError("sendWorldNotification error: " + std::string(e.what()));
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
        auto allChars = gameServices_->getCharacterManager().getCharactersList();
        for (const auto &charData : allChars)
        {
            if (charData.characterId == 0)
                continue;

            auto zone = gameServices_->getGameZoneManager().getZoneForPosition(charData.characterPosition);
            if (!zone.has_value() || zone->id != gameZoneId)
                continue;

            sendWorldNotification(charData.characterId, notificationType, data, priority, channel);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("sendWorldNotificationToGameZone error: " + std::string(e.what()));
    }
}

nlohmann::json
CharacterStatsNotificationService::buildStatsUpdatePacket(int characterId)
{
    const auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);
    std::string requestId = "stats_update_" + std::to_string(characterId);

    // ── Experience level thresholds ───────────────────────────────────────────
    int levelStart = gameServices_->getExperienceManager().getExperienceForLevelFromGameServer(
        characterData.characterLevel);

    // ── Weight ────────────────────────────────────────────────────────────────
    float currentWeight = gameServices_->getInventoryManager().getTotalWeight(characterId);
    float weightLimit = gameServices_->getEquipmentManager().getCarryWeightLimit(characterId);

    // ── Build effective attributes: base + equipment bonuses + active effects ─
    // Start with base character attributes
    std::unordered_map<std::string, int> baseValues;
    std::unordered_map<std::string, int> effectiveValues;
    std::unordered_map<std::string, std::string> attrNames;

    for (const auto &a : characterData.attributes)
    {
        baseValues[a.slug] = a.value;
        effectiveValues[a.slug] = a.value;
        attrNames[a.slug] = a.name;
    }

    // Add item attribute bonuses from equipped gear (apply_on == "equip")
    const auto equipState = gameServices_->getEquipmentManager().getEquipmentState(characterId);
    for (const auto &[slotSlug, slot] : equipState.slots)
    {
        if (slot.inventoryItemId == 0)
            continue;
        const auto item = gameServices_->getItemManager().getItemById(slot.itemId);
        if (item.id == 0)
            continue;
        for (const auto &attr : item.attributes)
        {
            if (attr.apply_on != "equip")
                continue;
            effectiveValues[attr.slug] += attr.value;
            if (attrNames.find(attr.slug) == attrNames.end())
                attrNames[attr.slug] = attr.name;
        }
    }

    // Add non-expired stat-modifier active effects (skip dot/hot – they are damage ticks)
    const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                               .count();

    for (const auto &eff : characterData.activeEffects)
    {
        if (eff.attributeSlug.empty())
            continue;
        if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
            continue;
        if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
            continue;
        effectiveValues[eff.attributeSlug] += static_cast<int>(eff.value);
        if (attrNames.find(eff.attributeSlug) == attrNames.end())
            attrNames[eff.attributeSlug] = eff.attributeSlug;
    }

    // Item Soul: apply kill-count tier bonus to the equipped weapon's primary attribute
    try
    {
        auto weaponOpt = gameServices_->getInventoryManager().getEquippedWeapon(characterId);
        if (weaponOpt.has_value())
        {
            const auto &cfg = gameServices_->getGameConfigService();
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
                const auto &wItem = gameServices_->getItemManager().getItemById(weaponOpt->itemId);
                for (const auto &attr : wItem.attributes)
                {
                    if (attr.apply_on == "equip" && !attr.slug.empty())
                    {
                        effectiveValues[attr.slug] += soulBonus;
                        if (attrNames.find(attr.slug) == attrNames.end())
                            attrNames[attr.slug] = attr.name;
                        break; // one bonus per weapon
                    }
                }
            }
        }
    }
    catch (...)
    {
    }

    // Build attributes JSON array (base attrs + any extras added only by equipment/effects)
    nlohmann::json attributesJson = nlohmann::json::array();
    for (const auto &[slug, baseVal] : baseValues)
    {
        attributesJson.push_back({{"slug", slug},
            {"name", attrNames.count(slug) ? attrNames.at(slug) : slug},
            {"base", baseVal},
            {"effective", effectiveValues.at(slug)}});
    }
    for (const auto &[slug, effVal] : effectiveValues)
    {
        if (baseValues.count(slug))
            continue; // already included above
        attributesJson.push_back({{"slug", slug},
            {"name", attrNames.count(slug) ? attrNames.at(slug) : slug},
            {"base", 0},
            {"effective", effVal}});
    }

    // ── Active effects display list (all non-expired effects) ─────────────────
    nlohmann::json activeEffectsJson = nlohmann::json::array();
    for (const auto &eff : characterData.activeEffects)
    {
        if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
            continue;
        activeEffectsJson.push_back({{"slug", eff.effectSlug},
            {"effectTypeSlug", eff.effectTypeSlug},
            {"attributeSlug", eff.attributeSlug},
            {"value", eff.value},
            {"expiresAt", eff.expiresAt}});
    }

    // ── Build packet ──────────────────────────────────────────────────────────
    // Use effective (base + active-effect bonuses) max values in the health/mana objects
    // so the client bar is drawn against the real cap, not the stripped base value.
    // This matches what is reported in the attributes array and prevents false "current > max"
    // warnings when passive skills (e.g. mana_shield) raise the effective maximum.
    const int effectiveMaxHealth = effectiveValues.count("max_health")
                                       ? effectiveValues.at("max_health")
                                       : characterData.characterMaxHealth;
    const int effectiveMaxMana = effectiveValues.count("max_mana")
                                     ? effectiveValues.at("max_mana")
                                     : characterData.characterMaxMana;

    TimestampStruct timestamps = TimestampUtils::createReceiveTimestamp(0, requestId);
    ResponseBuilder builder;

    builder.setHeader("eventType", "stats_update")
        .setHeader("status", "success")
        .setHeader("requestId", requestId)
        .setTimestamps(timestamps);

    builder.setBody("characterId", characterId)
        .setBody("level", characterData.characterLevel)
        .setBody("freeSkillPoints", characterData.freeSkillPoints)
        .setBody("experience", nlohmann::json{{"current", characterData.characterExperiencePoints}, {"levelStart", levelStart}, {"nextLevel", characterData.expForNextLevel}, {"debt", characterData.experienceDebt}})
        .setBody("health", nlohmann::json{{"current", characterData.characterCurrentHealth}, {"max", effectiveMaxHealth}})
        .setBody("mana", nlohmann::json{{"current", characterData.characterCurrentMana}, {"max", effectiveMaxMana}})
        .setBody("weight", nlohmann::json{{"current", currentWeight}, {"max", weightLimit}})
        .setBody("attributes", attributesJson)
        .setBody("activeEffects", activeEffectsJson);

    return builder.build();
}
