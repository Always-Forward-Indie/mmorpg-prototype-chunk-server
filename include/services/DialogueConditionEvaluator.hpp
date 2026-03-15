#pragma once
#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>

/**
 * @brief Stateless utility that evaluates condition_group JSON against a PlayerContextStruct.
 *
 * Supported condition formats (see TZ section 3.4):
 *
 *   // Atomic rules
 *   {"type":"flag",       "key":"mila_greeted",    "eq":true}
 *   {"type":"flag",       "key":"visit_count",      "gte":3}
 *   {"type":"quest",      "slug":"wolf_hunt",        "state":"active"}
 *   {"type":"quest",      "slug":"wolf_hunt",        "state":"not_started"}
 *   {"type":"quest_step", "slug":"wolf_hunt",        "step":1}      // exact step index
 *   {"type":"quest_step", "slug":"wolf_hunt",        "gte":1}       // step >= 1
 *   {"type":"level",      "gte":5}
 *   {"type":"level",      "lte":20}
 *   {"type":"item",       "item_id":7,              "gte":5}    // inventory check (delegated to flags for now)
 *
 *   // Logical groups
 *   {"all":[...]}   – AND
 *   {"any":[...]}   – OR
 *   {"not":{...}}   – NOT
 *
 * All inventory ("item") conditions use QuestManager progress in PlayerContextStruct.
 */
class DialogueConditionEvaluator
{
  public:
    /**
     * @brief Evaluate a condition_group.
     * @param conditionGroup  JSON condition. null / empty → always true.
     * @param ctx             Current player snapshot.
     * @return true if condition is satisfied.
     */
    static bool evaluate(const nlohmann::json &conditionGroup,
        const PlayerContextStruct &ctx);

  private:
    static bool evaluateRule(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    static bool evaluateFlag(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    static bool evaluateQuest(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    static bool evaluateLevel(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    static bool evaluateQuestStep(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    static bool evaluateInventory(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    // Stage 4 — Reputation and Mastery conditions
    // {"type":"reputation", "faction":"bandits", "gte":200}
    // {"type":"mastery",    "slug":"sword",       "gte":50}
    static bool evaluateReputation(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);

    static bool evaluateMastery(const nlohmann::json &rule,
        const PlayerContextStruct &ctx);
};
