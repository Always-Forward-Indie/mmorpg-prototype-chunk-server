#include "services/DialogueConditionEvaluator.hpp"

bool
DialogueConditionEvaluator::evaluate(const nlohmann::json &conditionGroup,
    const PlayerContextStruct &ctx)
{
    // null or empty JSON → always satisfied
    if (conditionGroup.is_null() || conditionGroup.empty())
        return true;

    // --- Logical groups ---
    if (conditionGroup.contains("all") && conditionGroup["all"].is_array())
    {
        for (const auto &sub : conditionGroup["all"])
        {
            if (!evaluate(sub, ctx))
                return false;
        }
        return true;
    }

    if (conditionGroup.contains("any") && conditionGroup["any"].is_array())
    {
        for (const auto &sub : conditionGroup["any"])
        {
            if (evaluate(sub, ctx))
                return true;
        }
        return false;
    }

    if (conditionGroup.contains("not") && conditionGroup["not"].is_object())
    {
        return !evaluate(conditionGroup["not"], ctx);
    }

    // --- Atomic rule ---
    return evaluateRule(conditionGroup, ctx);
}

bool
DialogueConditionEvaluator::evaluateRule(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("type"))
        return true; // Unknown rule → permissive

    const std::string type = rule["type"].get<std::string>();

    if (type == "flag")
        return evaluateFlag(rule, ctx);
    if (type == "quest")
        return evaluateQuest(rule, ctx);
    if (type == "level")
        return evaluateLevel(rule, ctx);
    if (type == "item")
        return evaluateInventory(rule, ctx);

    // Unknown rule type → permissive
    return true;
}

bool
DialogueConditionEvaluator::evaluateFlag(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("key"))
        return true;

    const std::string key = rule["key"].get<std::string>();

    // Boolean flag
    if (rule.contains("eq") && rule["eq"].is_boolean())
    {
        bool expected = rule["eq"].get<bool>();
        auto it = ctx.flagsBool.find(key);
        bool actual = (it != ctx.flagsBool.end()) ? it->second : false;
        return actual == expected;
    }

    // Integer flag comparisons
    auto it = ctx.flagsInt.find(key);
    int actual = (it != ctx.flagsInt.end()) ? it->second : 0;

    if (rule.contains("eq"))
        return actual == rule["eq"].get<int>();
    if (rule.contains("gte"))
        return actual >= rule["gte"].get<int>();
    if (rule.contains("lte"))
        return actual <= rule["lte"].get<int>();
    if (rule.contains("gt"))
        return actual > rule["gt"].get<int>();
    if (rule.contains("lt"))
        return actual < rule["lt"].get<int>();

    return true;
}

bool
DialogueConditionEvaluator::evaluateQuest(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("slug") || !rule.contains("state"))
        return true;

    const std::string slug = rule["slug"].get<std::string>();
    const std::string expectedState = rule["state"].get<std::string>();

    auto it = ctx.questStates.find(slug);

    if (expectedState == "not_started")
    {
        // Quest has never been taken OR is in a "finalized" state (turned_in/failed after cooldown would be reset)
        return (it == ctx.questStates.end()) ||
               (it->second == "turned_in") ||
               (it->second == "failed");
    }

    if (it == ctx.questStates.end())
        return false;

    return it->second == expectedState;
}

bool
DialogueConditionEvaluator::evaluateLevel(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    int level = ctx.characterLevel;

    if (rule.contains("gte"))
        return level >= rule["gte"].get<int>();
    if (rule.contains("lte"))
        return level <= rule["lte"].get<int>();
    if (rule.contains("eq"))
        return level == rule["eq"].get<int>();
    if (rule.contains("gt"))
        return level > rule["gt"].get<int>();
    if (rule.contains("lt"))
        return level < rule["lt"].get<int>();

    return true;
}

bool
DialogueConditionEvaluator::evaluateInventory(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    // Inventory check uses quest progress JSON {"have": N} stored in questProgress
    // For a simpler approach we check all quest progress entries for item counts.
    // Alternatively this is delegated to a future InventoryManager integration.
    // For now: iterate all active quest progress maps looking for "have" counts
    // that match item_id. This is a best-effort approach until InventoryManager
    // exposes item counts through PlayerContextStruct.

    if (!rule.contains("item_id"))
        return true;

    // Currently not directly integrated; return true to be permissive.
    // Full integration: QuestManager populates flagsInt with "item_{id}" = quantity
    int itemId = rule["item_id"].get<int>();
    std::string itemKey = "item_" + std::to_string(itemId);

    auto it = ctx.flagsInt.find(itemKey);
    int have = (it != ctx.flagsInt.end()) ? it->second : 0;

    if (rule.contains("gte"))
        return have >= rule["gte"].get<int>();
    if (rule.contains("lte"))
        return have <= rule["lte"].get<int>();
    if (rule.contains("eq"))
        return have == rule["eq"].get<int>();

    return true;
}
