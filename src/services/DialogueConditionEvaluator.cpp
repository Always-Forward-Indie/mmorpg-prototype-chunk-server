#include "services/DialogueConditionEvaluator.hpp"

bool
DialogueConditionEvaluator::evaluate(const nlohmann::json &conditionGroup,
    const PlayerContextStruct &ctx)
{
    // null or empty JSON → always satisfied
    if (conditionGroup.is_null() || conditionGroup.empty())
        return true;

    // --- Flat array: [{rule1}, {rule2}] → implicit AND (same as "all") ---
    if (conditionGroup.is_array())
    {
        for (const auto &sub : conditionGroup)
        {
            if (!evaluate(sub, ctx))
                return false;
        }
        return true;
    }

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
    if (type == "quest_step")
        return evaluateQuestStep(rule, ctx);
    if (type == "level")
        return evaluateLevel(rule, ctx);
    if (type == "item")
        return evaluateInventory(rule, ctx);
    if (type == "reputation")
        return evaluateReputation(rule, ctx);
    if (type == "mastery")
        return evaluateMastery(rule, ctx);
    if (type == "has_skill_points")
        return evaluateHasSkillPoints(rule, ctx);
    if (type == "skill_learned")
        return evaluateSkillLearned(rule, ctx);
    if (type == "skill_not_learned")
        return evaluateSkillNotLearned(rule, ctx);
    if (type == "class")
        return evaluateClass(rule, ctx);
    if (type == "object_state")
    {
        // {"type":"object_state","object_id":N,"state":"depleted"}
        // Checks the runtime state of a WIO global-scope object against the expected value.
        // Per-player state is not accessible here; use flag["wio_interacted_<id>"] instead.
        int objId = rule.value("object_id", 0);
        std::string expectedState = rule.value("state", "active");
        if (objId <= 0)
            return true;
        // GameServices pointer not available from a static method — we use the flag approach:
        // The EventHandler sets flag "wio_state_<id>" if a broadcast was received.
        // Fallback: always pass if no runtime data is available.
        const std::string flagKey = "wio_state_" + std::to_string(objId);
        auto it = ctx.flagsInt.find(flagKey);
        if (it == ctx.flagsInt.end())
            return true; // no data → permissive
        // Encode: 0=active, 1=depleted, 2=disabled
        static const std::unordered_map<std::string, int> stateCode = {
            {"active", 0}, {"depleted", 1}, {"disabled", 2}};
        auto ec = stateCode.find(expectedState);
        int expected = (ec != stateCode.end()) ? ec->second : 0;
        return it->second == expected;
    }

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
        // Quest is "not started" only if there is no progress record at all.
        // turned_in and failed are their own terminal states and must not
        // match not_started so that separate dialogue branches for those
        // states are shown instead of the initial offer branch.
        return it == ctx.questStates.end();
    }

    if (it == ctx.questStates.end())
        return false;

    return it->second == expectedState;
}

bool
DialogueConditionEvaluator::evaluateQuestStep(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("slug"))
        return true;

    const std::string slug = rule["slug"].get<std::string>();

    auto it = ctx.questCurrentStep.find(slug);
    // If the quest has no progress record yet, treat current step as -1
    // so that "step":0 conditions only fire once the quest is underway.
    int currentStep = (it != ctx.questCurrentStep.end()) ? it->second : -1;

    // Exact match: {"type":"quest_step", "slug":"...", "step":N}
    if (rule.contains("step"))
        return currentStep == rule["step"].get<int>();

    // Comparison operators (same pattern as level/flag)
    if (rule.contains("eq"))
        return currentStep == rule["eq"].get<int>();
    if (rule.contains("gte"))
        return currentStep >= rule["gte"].get<int>();
    if (rule.contains("lte"))
        return currentStep <= rule["lte"].get<int>();
    if (rule.contains("gt"))
        return currentStep > rule["gt"].get<int>();
    if (rule.contains("lt"))
        return currentStep < rule["lt"].get<int>();

    return true;
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

// ── Reputation ─────────────────────────────────────────────────────────────
// Rule: {"type":"reputation", "faction":"bandits", "gte":200}
bool
DialogueConditionEvaluator::evaluateReputation(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("faction"))
        return true;
    const std::string faction = rule["faction"].get<std::string>();
    auto it = ctx.reputations.find(faction);
    int value = (it != ctx.reputations.end()) ? it->second : 0;

    if (rule.contains("gte"))
        return value >= rule["gte"].get<int>();
    if (rule.contains("lte"))
        return value <= rule["lte"].get<int>();
    if (rule.contains("eq"))
        return value == rule["eq"].get<int>();
    if (rule.contains("gt"))
        return value > rule["gt"].get<int>();
    if (rule.contains("lt"))
        return value < rule["lt"].get<int>();
    // tier check: {"faction":"bandits","tier":"friendly"}
    if (rule.contains("tier"))
    {
        // Simple tier ordering: enemy < stranger < neutral < friendly < ally
        static const std::unordered_map<std::string, int> tierRank{
            {"enemy", 0}, {"stranger", 1}, {"neutral", 2}, {"friendly", 3}, {"ally", 4}};
        auto tierIt = rule["tier"].get<std::string>();
        int needed = tierRank.count(tierIt) ? tierRank.at(tierIt) : -1;

        // Determine current tier
        int current = 2; // neutral
        if (value < -500)
            current = 0;
        else if (value < 0)
            current = 1;
        else if (value < 200)
            current = 2;
        else if (value < 500)
            current = 3;
        else
            current = 4;
        return current >= needed;
    }
    return true;
}

// ── Mastery ────────────────────────────────────────────────────────────────
// Rule: {"type":"mastery", "slug":"sword", "gte":50}
bool
DialogueConditionEvaluator::evaluateMastery(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("slug"))
        return true;
    const std::string slug = rule["slug"].get<std::string>();
    auto it = ctx.masteries.find(slug);
    float value = (it != ctx.masteries.end()) ? it->second : 0.0f;

    if (rule.contains("gte"))
        return value >= rule["gte"].get<float>();
    if (rule.contains("lte"))
        return value <= rule["lte"].get<float>();
    if (rule.contains("eq"))
        return std::abs(value - rule["eq"].get<float>()) < 0.001f;
    if (rule.contains("gt"))
        return value > rule["gt"].get<float>();
    if (rule.contains("lt"))
        return value < rule["lt"].get<float>();

    return true;
}

// ── Skill system conditions ────────────────────────────────────────────────
// Rule: {"type":"has_skill_points","gte":1}
bool
DialogueConditionEvaluator::evaluateHasSkillPoints(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    int sp = ctx.freeSkillPoints;
    if (rule.contains("gte"))
        return sp >= rule["gte"].get<int>();
    if (rule.contains("gt"))
        return sp > rule["gt"].get<int>();
    if (rule.contains("eq"))
        return sp == rule["eq"].get<int>();
    return sp > 0;
}

// Rule: {"type":"skill_learned","slug":"shield_bash"}
bool
DialogueConditionEvaluator::evaluateSkillLearned(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("slug"))
        return true;
    const std::string slug = rule["slug"].get<std::string>();
    return ctx.learnedSkillSlugs.count(slug) > 0;
}

// Rule: {"type":"skill_not_learned","slug":"whirlwind"}
bool
DialogueConditionEvaluator::evaluateSkillNotLearned(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    if (!rule.contains("slug"))
        return true;
    const std::string slug = rule["slug"].get<std::string>();
    return ctx.learnedSkillSlugs.count(slug) == 0;
}

bool
DialogueConditionEvaluator::evaluateClass(const nlohmann::json &rule,
    const PlayerContextStruct &ctx)
{
    // Single id: {"type":"class","class_id":1}
    if (rule.contains("class_id"))
        return ctx.classId == rule["class_id"].get<int>();

    // Multiple ids: {"type":"class","class_ids":[1,3]}
    if (rule.contains("class_ids") && rule["class_ids"].is_array())
    {
        for (const auto &elem : rule["class_ids"])
        {
            if (elem.is_number_integer() && elem.get<int>() == ctx.classId)
                return true;
        }
        return false;
    }

    return true; // no id specified → permissive
}
