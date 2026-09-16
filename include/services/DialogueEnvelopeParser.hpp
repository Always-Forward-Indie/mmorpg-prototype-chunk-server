#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * @brief Pure action_group envelope parser (Increment 9).
 *
 * Splits the three accepted shapes — flat array, {"actions":[...]}, single
 * action object — into (type, action) pairs. 1-1 with the pre-extraction
 * DialogueActionExecutor::execute branching: null/empty → none; array items
 * without "type" are skipped; the single-object form dispatches even when
 * "type" is missing (empty type → "unknown action" log downstream).
 */
struct ParsedDialogueAction
{
    std::string type;
    nlohmann::json action;
};

inline std::vector<ParsedDialogueAction> parseDialogueActionGroup(const nlohmann::json &actionGroup)
{
    std::vector<ParsedDialogueAction> out;

    if (actionGroup.is_null() || actionGroup.empty())
        return out;

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
        out.push_back({actionGroup.value("type", ""), actionGroup});
        return out;
    }

    for (const auto &action : *actionsArray)
    {
        if (!action.contains("type"))
            continue;
        out.push_back({action["type"].get<std::string>(), action});
    }

    return out;
}
