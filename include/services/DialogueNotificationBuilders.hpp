#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

/**
 * @brief Pure client-notification builders for dialogue actions (Increment 9).
 *
 * Every clientNotifications.push_back in DialogueActionExecutor goes through
 * here — JSON shapes are pinned by unit tests, the orchestrator only decides
 * when to send.
 */
namespace DialogueNotificationBuilders
{

inline nlohmann::json questOffered(int questId,
    const std::string &clientQuestKey,
    const nlohmann::json &currentStep,
    bool hasStep,
    const nlohmann::json &rewards)
{
    nlohmann::json notification;
    notification["type"] = "quest_offered";
    notification["questId"] = questId;
    notification["clientQuestKey"] = clientQuestKey;
    if (hasStep)
        notification["currentStep"] = currentStep;
    notification["rewards"] = rewards;
    return notification;
}

inline nlohmann::json questFailed(int questId, const std::string &clientQuestKey)
{
    nlohmann::json notification;
    notification["type"] = "quest_failed";
    notification["questId"] = questId;
    notification["clientQuestKey"] = clientQuestKey;
    return notification;
}

inline nlohmann::json reputationChanged(const std::string &faction, int delta)
{
    nlohmann::json notification;
    notification["type"] = "reputationChanged";
    notification["faction"] = faction;
    notification["delta"] = delta;
    return notification;
}

inline nlohmann::json itemReceived(int itemId, const std::string &itemSlug, int quantity)
{
    nlohmann::json notification;
    notification["type"] = "item_received";
    notification["itemId"] = itemId;
    notification["item_slug"] = itemSlug;
    notification["quantity"] = quantity;
    return notification;
}

inline nlohmann::json expReceived(int64_t amount)
{
    nlohmann::json notification;
    notification["type"] = "exp_received";
    notification["amount"] = amount;
    return notification;
}

inline nlohmann::json goldReceived(int64_t amount)
{
    nlohmann::json notification;
    notification["type"] = "gold_received";
    notification["amount"] = amount;
    return notification;
}

inline nlohmann::json openVendorShop(const std::string &mode,
    int npcId,
    const std::string &npcSlug,
    const nlohmann::json &items)
{
    nlohmann::json notification;
    notification["type"] = "openVendorShop";
    notification["mode"] = mode;
    notification["npcId"] = npcId;
    notification["npcSlug"] = npcSlug;
    notification["items"] = items;
    return notification;
}

inline nlohmann::json openRepairShop(int npcId, int playerGold, const nlohmann::json &items)
{
    nlohmann::json notification;
    notification["type"] = "openRepairShop";
    notification["npcId"] = npcId;
    notification["playerGold"] = playerGold;
    notification["items"] = items;
    return notification;
}

inline nlohmann::json openSkillShop(int npcId,
    const std::string &npcSlug,
    int freeSkillPoints,
    int goldBalance,
    const nlohmann::json &skills)
{
    nlohmann::json notification;
    notification["type"] = "openSkillShop";
    notification["npcId"] = npcId;
    notification["npcSlug"] = npcSlug;
    notification["freeSkillPoints"] = freeSkillPoints;
    notification["goldBalance"] = goldBalance;
    notification["skills"] = skills;
    return notification;
}

inline nlohmann::json learnSkillFailed(const std::string &reason, const std::string &skillSlug)
{
    nlohmann::json notification;
    notification["type"] = "learn_skill_failed";
    notification["reason"] = reason;
    notification["skillSlug"] = skillSlug;
    return notification;
}

} // namespace DialogueNotificationBuilders
