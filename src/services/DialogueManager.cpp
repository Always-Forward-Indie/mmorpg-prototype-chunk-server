#include "services/DialogueManager.hpp"
#include "services/DialogueConditionEvaluator.hpp"
#include <algorithm>

DialogueManager::DialogueManager(Logger &logger)
    : logger_(logger)
{
}

void
DialogueManager::setDialogues(const std::vector<DialogueGraphStruct> &dialogues)
{
    std::lock_guard<std::mutex> lock(mutex_);
    dialoguesById_.clear();
    dialoguesBySlug_.clear();

    for (const auto &dlg : dialogues)
    {
        dialoguesById_[dlg.id] = dlg;
        if (!dlg.slug.empty())
            dialoguesBySlug_[dlg.slug] = dlg.id;
    }

    loaded_ = true;
    logger_.log("[DialogueManager] Loaded " + std::to_string(dialoguesById_.size()) + " dialogues.", GREEN);
}

void
DialogueManager::setNPCDialogueMappings(const std::vector<NPCDialogueMappingStruct> &mappings)
{
    std::lock_guard<std::mutex> lock(mutex_);
    npcMappings_.clear();

    for (const auto &m : mappings)
    {
        npcMappings_[m.npcId].push_back(m);
    }

    // Sort each NPC's mappings by priority DESC
    for (auto &[npcId, list] : npcMappings_)
    {
        std::sort(list.begin(), list.end(), [](const NPCDialogueMappingStruct &a, const NPCDialogueMappingStruct &b)
            { return a.priority > b.priority; });
    }

    logger_.log("[DialogueManager] Loaded NPC dialogue mappings for " +
                    std::to_string(npcMappings_.size()) + " NPCs.",
        GREEN);
}

int
DialogueManager::selectDialogueForNPC(int npcId, const PlayerContextStruct &ctx) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = npcMappings_.find(npcId);
    if (it == npcMappings_.end())
        return -1;

    for (const auto &mapping : it->second)
    {
        // Evaluate the mapping-level condition
        if (DialogueConditionEvaluator::evaluate(mapping.conditionGroup, ctx))
        {
            // Verify the dialogue actually exists
            if (dialoguesById_.count(mapping.dialogueId))
                return mapping.dialogueId;
        }
    }

    return -1;
}

const DialogueGraphStruct *
DialogueManager::getDialogueById(int id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = dialoguesById_.find(id);
    return (it != dialoguesById_.end()) ? &it->second : nullptr;
}

const DialogueGraphStruct *
DialogueManager::getDialogueBySlug(const std::string &slug) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto sit = dialoguesBySlug_.find(slug);
    if (sit == dialoguesBySlug_.end())
        return nullptr;
    auto dit = dialoguesById_.find(sit->second);
    return (dit != dialoguesById_.end()) ? &dit->second : nullptr;
}

bool
DialogueManager::isLoaded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_;
}
