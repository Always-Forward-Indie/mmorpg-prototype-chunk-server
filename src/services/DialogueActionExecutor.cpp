#include "services/DialogueActionExecutor.hpp"
#include "services/GameServices.hpp"
#include "services/QuestManager.hpp"
#include <spdlog/logger.h>

DialogueActionExecutor::DialogueActionExecutor(GameServices &services, Logger &logger)
    : services_(services), logger_(logger)
{
    log_ = logger.getSystem("dialogue");
}

DialogueActionExecutor::ActionResult
DialogueActionExecutor::execute(const nlohmann::json &actionGroup,
    int characterId,
    int clientId,
    PlayerContextStruct &ctx)
{
    ActionResult result;

    if (actionGroup.is_null() || actionGroup.empty())
        return result;

    // Support both {"actions":[...]} envelope and a flat array
    const nlohmann::json *actionsArray = nullptr;
    if (actionGroup.is_array())
    {
        actionsArray = &actionGroup;
    }
    else if (actionGroup.contains("actions") && actionGroup["actions"].is_array())
    {
        actionsArray = &actionGroup["actions"];
    }
    else
    {
        // Single action object
        std::string type = actionGroup.value("type", "");
        executeDispatch(actionGroup, type, characterId, clientId, ctx, result);
        return result;
    }

    for (const auto &action : *actionsArray)
    {
        if (!action.contains("type"))
            continue;
        const std::string type = action["type"].get<std::string>();
        executeDispatch(action, type, characterId, clientId, ctx, result);
    }

    return result;
}

// Helper to dispatch a single action by type
void
DialogueActionExecutor::executeDispatch(const nlohmann::json &action, const std::string &type, int characterId, int clientId, PlayerContextStruct &ctx, ActionResult &result)
{
    if (type == "set_flag")
        executeSetFlag(action, characterId, ctx, result);
    else if (type == "offer_quest")
        executeOfferQuest(action, characterId, clientId, ctx, result);
    else if (type == "turn_in_quest")
        executeTurnInQuest(action, characterId, clientId, result);
    else if (type == "advance_quest_step")
        executeAdvanceQuestStep(action, characterId, result);
    else if (type == "give_item")
        executeGiveItem(action, characterId, clientId, result);
    else if (type == "give_exp")
        executeGiveExp(action, characterId, clientId, result);
    else
        log_->info("[DialogueAction] Unknown action type: " + type);
}

void
DialogueActionExecutor::executeSetFlag(const nlohmann::json &action,
    int characterId,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    if (!action.contains("key"))
        return;

    const std::string key = action["key"].get<std::string>();

    if (action.contains("bool_value"))
    {
        bool val = action["bool_value"].get<bool>();
        ctx.flagsBool[key] = val;

        // Persist to game-server
        UpdatePlayerFlagStruct flagUpdate;
        flagUpdate.characterId = characterId;
        flagUpdate.flagKey = key;
        flagUpdate.boolValue = val;
        services_.getQuestManager().queueFlagUpdate(flagUpdate);
    }
    else if (action.contains("int_value"))
    {
        int val = action["int_value"].get<int>();
        ctx.flagsInt[key] = val;

        UpdatePlayerFlagStruct flagUpdate;
        flagUpdate.characterId = characterId;
        flagUpdate.flagKey = key;
        flagUpdate.intValue = val;
        services_.getQuestManager().queueFlagUpdate(flagUpdate);
    }
    else if (action.contains("inc"))
    {
        int delta = action["inc"].get<int>();
        ctx.flagsInt[key] = ctx.flagsInt[key] + delta;

        UpdatePlayerFlagStruct flagUpdate;
        flagUpdate.characterId = characterId;
        flagUpdate.flagKey = key;
        flagUpdate.intValue = ctx.flagsInt[key];
        services_.getQuestManager().queueFlagUpdate(flagUpdate);
    }
}

void
DialogueActionExecutor::executeOfferQuest(const nlohmann::json &action,
    int characterId,
    int clientId,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    auto &questManager = services_.getQuestManager();

    if (questManager.offerQuest(characterId, slug))
    {
        ctx.questStates[slug] = "active";

        // Build client notification
        const QuestStruct *quest = questManager.getQuestBySlug(slug);
        if (quest)
        {
            nlohmann::json notification;
            notification["type"] = "quest_offered";
            notification["questId"] = quest->id;
            notification["clientQuestKey"] = quest->clientQuestKey;
            result.clientNotifications.push_back(std::move(notification));
        }

        log_->info("[DialogueAction] Offered quest '" + slug + "' to character " +
                    std::to_string(characterId));
    }
}

void
DialogueActionExecutor::executeTurnInQuest(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    auto &questManager = services_.getQuestManager();

    auto notifications = questManager.turnInQuest(characterId, slug, clientId);
    for (auto &n : notifications)
        result.clientNotifications.push_back(std::move(n));
}

void
DialogueActionExecutor::executeAdvanceQuestStep(const nlohmann::json &action,
    int characterId,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    services_.getQuestManager().advanceQuestStepBySlug(characterId, slug);
}

void
DialogueActionExecutor::executeGiveItem(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("item_id"))
        return;

    int itemId = action["item_id"].get<int>();
    int quantity = action.value("quantity", 1);

    bool ok = services_.getInventoryManager().addItemToInventory(characterId, itemId, quantity);
    if (ok)
    {
        nlohmann::json notification;
        notification["type"] = "item_received";
        notification["itemId"] = itemId;
        notification["quantity"] = quantity;
        result.clientNotifications.push_back(std::move(notification));
    }
}

void
DialogueActionExecutor::executeGiveExp(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("amount"))
        return;

    int64_t amount = action["amount"].get<int64_t>();
    auto expResult = services_.getExperienceManager().grantExperience(
        characterId, static_cast<int>(amount), "quest_reward", 0);

    if (expResult.success)
    {
        nlohmann::json notification;
        notification["type"] = "exp_received";
        notification["amount"] = amount;
        result.clientNotifications.push_back(std::move(notification));
    }
}
