#include "services/TrainerManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include <spdlog/logger.h>

TrainerManager::TrainerManager(ItemManager &itemManager, Logger &logger)
    : itemManager_(itemManager), logger_(logger)
{
    log_ = logger.getSystem("trainer");
}

void
TrainerManager::setTrainerData(const std::vector<TrainerNPCDataStruct> &trainers)
{
    std::unique_lock lock(mutex_);
    trainers_.clear();
    for (const auto &t : trainers)
        trainers_[t.npcId] = t;
    log_->info("[TrainerManager] Loaded trainer data for {} NPCs", trainers_.size());
}

const TrainerNPCDataStruct *
TrainerManager::getTrainerByNpcId(int npcId) const
{
    std::shared_lock lock(mutex_);
    auto it = trainers_.find(npcId);
    if (it == trainers_.end())
        return nullptr;
    return &it->second;
}

const ClassSkillTreeEntryStruct *
TrainerManager::getSkillEntry(int npcId, const std::string &skillSlug) const
{
    std::shared_lock lock(mutex_);
    auto it = trainers_.find(npcId);
    if (it == trainers_.end())
        return nullptr;
    for (const auto &e : it->second.skills)
        if (e.skillSlug == skillSlug)
            return &e;
    return nullptr;
}

nlohmann::json
TrainerManager::buildSkillShopJson(
    int npcId,
    const PlayerContextStruct &ctx,
    const InventoryManager &inventoryMgr) const
{
    std::shared_lock lock(mutex_);
    auto it = trainers_.find(npcId);
    if (it == trainers_.end())
        return nlohmann::json(); // null

    const auto &trainer = it->second;
    const int charLevel = ctx.characterLevel;
    const int freeSp = ctx.freeSkillPoints;

    // Resolve gold_coin item id once
    const ItemDataStruct *goldItem = itemManager_.getItemBySlug("gold_coin");
    int goldBalance = 0;
    if (goldItem)
        goldBalance = inventoryMgr.getGoldAmount(ctx.characterId);

    nlohmann::json skills = nlohmann::json::array();
    for (const auto &e : trainer.skills)
    {
        bool isLearned = ctx.learnedSkillSlugs.count(e.skillSlug) > 0;

        // Prerequisite check
        bool prereqMet = e.prerequisiteSkillSlug.empty() ||
                         ctx.learnedSkillSlugs.count(e.prerequisiteSkillSlug) > 0;

        // Level check
        bool levelMet = charLevel >= e.requiredLevel;

        // SP check
        bool spMet = freeSp >= e.spCost;

        // Gold check
        bool goldMet = (e.goldCost <= 0) || (goldBalance >= e.goldCost);

        // Skill book check: player must own the required book
        bool bookMet = true;
        std::string bookSlug;
        if (e.requiresBook && e.bookItemId > 0)
        {
            const auto &inv = inventoryMgr.getPlayerInventory(ctx.characterId);
            bookMet = false;
            for (const auto &slot : inv)
            {
                if (slot.itemId == e.bookItemId && slot.quantity > 0)
                {
                    bookMet = true;
                    break;
                }
            }
            const auto &bookData = itemManager_.getItemById(e.bookItemId);
            if (bookData.id > 0)
                bookSlug = bookData.slug;
        }

        bool canLearn = !isLearned && prereqMet && levelMet && spMet && goldMet && bookMet;

        nlohmann::json entry;
        entry["skillId"] = e.skillId;
        entry["skillSlug"] = e.skillSlug;
        entry["skillName"] = e.skillName;
        entry["description"] = e.description;
        entry["isPassive"] = e.isPassive;
        entry["requiredLevel"] = e.requiredLevel;
        entry["spCost"] = e.spCost;
        entry["goldCost"] = e.goldCost;
        entry["requiresBook"] = e.requiresBook;
        entry["bookItemId"] = e.bookItemId;
        entry["bookSlug"] = bookSlug;
        entry["prerequisiteSkillSlug"] = e.prerequisiteSkillSlug;
        entry["isLearned"] = isLearned;
        entry["prereqMet"] = prereqMet;
        entry["levelMet"] = levelMet;
        entry["spMet"] = spMet;
        entry["goldMet"] = goldMet;
        entry["bookMet"] = bookMet;
        entry["canLearn"] = canLearn;
        skills.push_back(std::move(entry));
    }

    return skills;
}
