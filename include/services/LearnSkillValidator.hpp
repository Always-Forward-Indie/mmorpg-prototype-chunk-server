#pragma once

#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

/**
 * @brief Pure learn_skill precondition checker (Increment 9).
 *
 * The executor resolves live data (learned slugs, free SP, gold total, book
 * presence, gold-item existence) and this decides accept/reject 1-1 with the
 * pre-extraction guard chain. Mutation (consume book/gold/SP, persist) stays
 * in the orchestrator.
 */
enum class LearnSkillError
{
    None,            ///< all preconditions hold — proceed to consume
    MissingSlug,     ///< skill_slug empty — silent return (log only upstream)
    AlreadyLearned,  ///< learn_skill_failed/already_learned
    InsufficientSP,  ///< learn_skill_failed/insufficient_sp
    GoldItemUnknown, ///< gold_coin template missing — silent return (log only upstream)
    InsufficientGold, ///< learn_skill_failed/insufficient_gold
    MissingBook,     ///< learn_skill_failed/missing_skill_book
};

struct LearnSkillRequest
{
    std::string skillSlug;
    int spCost = 1;
    int goldCost = 0;
    bool requiresBook = false;
    int bookItemId = 0;
};

struct LearnSkillState
{
    std::unordered_set<std::string> learnedSlugs;
    int freeSkillPoints = 0;
    bool goldItemKnown = false;
    int goldItemId = 0; ///< resolved gold_coin template id (valid when goldItemKnown)
    int totalGold = 0;
    bool hasBook = false;
};

/// Parse costs from the action JSON. Returns false when skill_slug is empty
/// (executor logs and returns silently — no notification, 1-1).
inline bool parseLearnSkillRequest(const nlohmann::json &action, LearnSkillRequest &out)
{
    out.skillSlug = action.value("skill_slug", "");
    if (out.skillSlug.empty())
        return false;
    out.spCost = action.value("sp_cost", 1);
    out.goldCost = action.value("gold_cost", 0);
    out.requiresBook = action.value("requires_book", false);
    out.bookItemId = action.value("book_item_id", 0);
    return true;
}

inline LearnSkillError validateLearnSkill(const LearnSkillRequest &req, const LearnSkillState &st)
{
    if (st.learnedSlugs.count(req.skillSlug) > 0)
        return LearnSkillError::AlreadyLearned;
    if (st.freeSkillPoints < req.spCost)
        return LearnSkillError::InsufficientSP;
    if (req.goldCost > 0)
    {
        if (!st.goldItemKnown)
            return LearnSkillError::GoldItemUnknown;
        if (st.totalGold < req.goldCost)
            return LearnSkillError::InsufficientGold;
    }
    if (req.requiresBook && req.bookItemId > 0 && !st.hasBook)
        return LearnSkillError::MissingBook;
    return LearnSkillError::None;
}

/// Machine-readable failure reason for learn_skill_failed notifications.
inline std::string learnSkillErrorReason(LearnSkillError err)
{
    switch (err)
    {
        case LearnSkillError::AlreadyLearned:
            return "already_learned";
        case LearnSkillError::InsufficientSP:
            return "insufficient_sp";
        case LearnSkillError::InsufficientGold:
            return "insufficient_gold";
        case LearnSkillError::MissingBook:
            return "missing_skill_book";
        default:
            return "";
    }
}
