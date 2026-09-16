#include "services/QuestStore.hpp"
#include "network/GameServerWorker.hpp"
#include "utils/Logger.hpp"
#include <chrono>
#include <cmath>
#include <spdlog/logger.h>

QuestStore::QuestStore(Logger &logger, QuestStoreSeams seams)
    : logger_(logger), seams_(std::move(seams))
{
    log_ = logger.getSystem("quest");
}

void
QuestStore::setGameServerWorker(GameServerWorker *worker)
{
    gameServerWorker_ = worker;
}

// =============================================================================
// Static data loading
// =============================================================================

void
QuestStore::setQuests(const std::vector<QuestStruct> &quests)
{
    std::lock_guard<std::mutex> lock(mutex_);
    questsBySlug_.clear();
    questsById_.clear();

    for (const auto &q : quests)
    {
        questsBySlug_[q.slug] = q;
    }
    // Build id→pointer map after insertions (map won't reallocate)
    for (auto &[slug, q] : questsBySlug_)
    {
        questsById_[q.id] = &q;
    }

    loaded_ = true;
    logger_.log("[QuestManager] Loaded " + std::to_string(questsBySlug_.size()) + " quests.", GREEN);
}

const QuestStruct *
QuestStore::getQuestBySlug(const std::string &slug) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = questsBySlug_.find(slug);
    return (it != questsBySlug_.end()) ? &it->second : nullptr;
}

const QuestStruct *
QuestStore::getQuestById(int id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = questsById_.find(id);
    return (it != questsById_.end()) ? it->second : nullptr;
}

bool
QuestStore::isLoaded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_;
}

// =============================================================================
// Player quest progress lifecycle
// =============================================================================

void
QuestStore::loadPlayerQuests(int characterId,
    const std::vector<PlayerQuestProgressStruct> &quests)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &progress = playerProgress_[characterId];
        progress.clear();
        for (const auto &pq : quests)
            progress[pq.questId] = pq;
    }

    logger_.log("[QuestManager] Loaded " + std::to_string(quests.size()) +
                " quests for character " + std::to_string(characterId));

    // Send the current state of every loaded quest to the client so the
    // quest journal is populated immediately on login.
    // We do this AFTER releasing the mutex because both getQuestById() and
    // the send seam each acquire it internally.
    for (const auto &pq : quests)
    {
        const QuestStruct *quest = getQuestById(pq.questId);
        if (quest && seams_.sendQuestUpdate)
            seams_.sendQuestUpdate(characterId, pq, *quest);
    }
}

void
QuestStore::unloadPlayerQuests(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playerProgress_.erase(characterId);
}

void
QuestStore::fillQuestContext(int characterId, PlayerContextStruct &ctx) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = playerProgress_.find(characterId);
    if (it == playerProgress_.end())
        return;

    for (const auto &[questId, pq] : it->second)
    {
        // Prefer runtime slug; fall back to static quest data if empty
        // (guards against old data loaded before the slug fix was deployed)
        std::string slug = pq.questSlug;
        if (slug.empty())
        {
            auto qit = questsById_.find(questId);
            if (qit != questsById_.end())
                slug = qit->second->slug;
        }
        if (!slug.empty())
        {
            ctx.questStates[slug] = pq.state;
            ctx.questCurrentStep[slug] = pq.currentStep;
        }
        ctx.questProgress[questId] = pq.progress;
    }
}

std::string
QuestStore::getQuestStateBySlug(int characterId, const std::string &questSlug) const
{
    const QuestStruct *quest = getQuestBySlug(questSlug); // read-only after startup
    if (!quest)
        return "";

    std::lock_guard<std::mutex> lock(mutex_);
    auto charIt = playerProgress_.find(characterId);
    if (charIt == playerProgress_.end())
        return "";

    auto questIt = charIt->second.find(quest->id);
    if (questIt == charIt->second.end())
        return "";

    return questIt->second.state;
}

// =============================================================================
// Quest logic
// =============================================================================

bool
QuestStore::offerQuest(int characterId, const std::string &questSlug)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto qit = questsBySlug_.find(questSlug);
    if (qit == questsBySlug_.end())
    {
        log_->error("[QuestManager] offerQuest: unknown quest slug '" + questSlug + "'");
        return false;
    }
    const QuestStruct &quest = qit->second;

    // Character level check
    const int characterLevel = seams_.getCharacterLevel ? seams_.getCharacterLevel(characterId) : 0;
    if (characterLevel < quest.minLevel)
    {
        log_->info("[QuestManager] Character " + std::to_string(characterId) +
                   " level too low for quest '" + questSlug + "'");
        return false;
    }

    auto &progress = playerProgress_[characterId];

    // Check if quest is already active or offered
    auto pit = progress.find(quest.id);
    if (pit != progress.end())
    {
        const std::string &currentState = pit->second.state;
        if (currentState == "active" || currentState == "offered" || currentState == "completed")
        {
            log_->info("[QuestManager] Quest '" + questSlug + "' already in state " + currentState);
            return false;
        }
    }

    // Create / overwrite progress record
    PlayerQuestProgressStruct pq;
    pq.characterId = characterId;
    pq.questId = quest.id;
    pq.questSlug = questSlug;
    pq.state = "active";
    pq.currentStep = 0;
    pq.progress = nlohmann::json::object();
    pq.isDirty = true;
    pq.updatedAt = std::chrono::steady_clock::now();

    // Initialise progress JSON for step 0
    if (!quest.steps.empty())
    {
        const QuestStepStruct &step0 = quest.steps[0];
        if (step0.stepType == "kill")
            pq.progress["killed"] = 0;
        else if (step0.stepType == "collect")
        {
            int alreadyHave = 0;
            int requiredItemId = step0.params.value("item_id", -1);
            int required = step0.params.value("count", 1);
            if (requiredItemId > 0 && seams_.getPlayerInventory)
            {
                const auto inv = seams_.getPlayerInventory(characterId);
                for (const auto &slot : inv)
                {
                    if (slot.itemId == requiredItemId)
                    {
                        alreadyHave = slot.quantity;
                        break;
                    }
                }
            }
            pq.progress["have"] = std::min(alreadyHave, required);
        }
        else if (step0.stepType == "talk")
            pq.progress["done"] = false;
        else if (step0.stepType == "reach")
            pq.progress["done"] = false;
    }

    progress[quest.id] = std::move(pq);

    // Send update to client
    if (seams_.sendQuestUpdate)
        seams_.sendQuestUpdate(characterId, progress[quest.id], quest);

    // Step 0 might already be satisfied on offer (e.g. player already holds
    // the required collect items).  Drive the first auto-check right away.
    checkStepCompletion(characterId, progress[quest.id]);

    log_->info("[QuestManager] Quest '" + questSlug + "' offered to character " +
               std::to_string(characterId));
    return true;
}

bool
QuestStore::beginTurnIn(int characterId,
    const std::string &questSlug,
    PlayerQuestProgressStruct &pqOut,
    QuestStruct &questOut)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto qit = questsBySlug_.find(questSlug);
    if (qit == questsBySlug_.end())
    {
        log_->error("[QuestManager] turnInQuest: unknown quest '" + questSlug + "'");
        return false;
    }
    const QuestStruct &quest = qit->second;

    auto &progress = playerProgress_[characterId];
    auto pit = progress.find(quest.id);

    if (pit == progress.end() || pit->second.state != "completed")
    {
        log_->info("[QuestManager] Cannot turn in quest '" + questSlug +
                   "': not in completed state");
        return false;
    }

    pit->second.state = "turned_in";
    pit->second.isDirty = true;
    pit->second.updatedAt = std::chrono::steady_clock::now();

    // Take copies before releasing the lock
    pqOut = pit->second;
    questOut = quest;
    return true;
}

bool
QuestStore::failQuest(int characterId, const std::string &questSlug)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto qit = questsBySlug_.find(questSlug);
    if (qit == questsBySlug_.end())
    {
        log_->error("[QuestManager] failQuest: unknown quest '" + questSlug + "'");
        return false;
    }
    const QuestStruct &quest = qit->second;

    auto &progress = playerProgress_[characterId];
    auto pit = progress.find(quest.id);

    if (pit == progress.end())
    {
        log_->info("[QuestManager] failQuest: quest '" + questSlug + "' not in progress for character " +
                   std::to_string(characterId));
        return false;
    }

    const std::string &currentState = pit->second.state;
    if (currentState == "turned_in" || currentState == "failed")
    {
        log_->info("[QuestManager] failQuest: quest '" + questSlug + "' already in terminal state " + currentState);
        return false;
    }

    pit->second.state = "failed";
    pit->second.isDirty = true;
    pit->second.updatedAt = std::chrono::steady_clock::now();

    if (seams_.sendQuestUpdate)
        seams_.sendQuestUpdate(characterId, pit->second, quest);

    log_->info("[QuestManager] Quest '" + questSlug + "' failed for character " +
               std::to_string(characterId));

    // Auto reputation change for quest failure
    if (!quest.reputationFactionSlug.empty() && quest.reputationOnFail != 0 && seams_.changeReputation)
    {
        seams_.changeReputation(
            characterId, quest.reputationFactionSlug, quest.reputationOnFail);
        log_->info("[QuestManager] failQuest: rep change char=" + std::to_string(characterId) +
                   " faction=" + quest.reputationFactionSlug +
                   " delta=" + std::to_string(quest.reputationOnFail));
    }
    return true;
}

void
QuestStore::advanceQuestStepBySlug(int characterId, const std::string &questSlug)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto qit = questsBySlug_.find(questSlug);
    if (qit == questsBySlug_.end())
        return;

    auto &progress = playerProgress_[characterId];
    auto pit = progress.find(qit->second.id);
    if (pit == progress.end())
        return;

    if (pit->second.state != "active")
        return;

    advanceStep(characterId, pit->second);
}

// =============================================================================
// Trigger hooks
// =============================================================================

void
QuestStore::onMobKilled(int characterId, int mobId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto pit = playerProgress_.find(characterId);
    if (pit == playerProgress_.end())
        return;

    for (auto &[questId, pq] : pit->second)
    {
        if (pq.state != "active")
            continue;

        auto qit = questsById_.find(questId);
        if (qit == questsById_.end())
            continue;
        const QuestStruct &quest = *qit->second;

        if (static_cast<int>(quest.steps.size()) <= pq.currentStep)
            continue;
        const QuestStepStruct &step = quest.steps[pq.currentStep];

        if (step.stepType != "kill")
            continue;

        int requiredMobId = step.params.value("mob_id", -1);
        if (requiredMobId != mobId)
            continue;

        int required = step.params.value("count", 1);
        int current = pq.progress.value("killed", 0);
        if (current < required)
        {
            pq.progress["killed"] = current + 1;
            pq.isDirty = true;
            pq.updatedAt = std::chrono::steady_clock::now();

            if (seams_.sendQuestUpdate)
                seams_.sendQuestUpdate(characterId, pq, quest);
            checkStepCompletion(characterId, pq);
        }
    }
}

void
QuestStore::onItemObtained(int characterId, int itemId, int quantity)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto pit = playerProgress_.find(characterId);
    if (pit == playerProgress_.end())
        return;

    for (auto &[questId, pq] : pit->second)
    {
        if (pq.state != "active")
            continue;

        auto qit = questsById_.find(questId);
        if (qit == questsById_.end())
            continue;
        const QuestStruct &quest = *qit->second;

        if (static_cast<int>(quest.steps.size()) <= pq.currentStep)
            continue;
        const QuestStepStruct &step = quest.steps[pq.currentStep];

        if (step.stepType != "collect")
            continue;

        int requiredItemId = step.params.value("item_id", -1);
        if (requiredItemId != itemId)
            continue;

        int required = step.params.value("count", 1);
        int current = pq.progress.value("have", 0);
        int newValue = std::min(current + quantity, required);
        pq.progress["have"] = newValue;
        pq.isDirty = true;
        pq.updatedAt = std::chrono::steady_clock::now();

        if (seams_.sendQuestUpdate)
            seams_.sendQuestUpdate(characterId, pq, quest);
        checkStepCompletion(characterId, pq);
    }
}

void
QuestStore::onNPCTalked(int characterId, int npcId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto pit = playerProgress_.find(characterId);
    if (pit == playerProgress_.end())
        return;

    for (auto &[questId, pq] : pit->second)
    {
        if (pq.state != "active")
            continue;

        auto qit = questsById_.find(questId);
        if (qit == questsById_.end())
            continue;
        const QuestStruct &quest = *qit->second;

        if (static_cast<int>(quest.steps.size()) <= pq.currentStep)
            continue;
        const QuestStepStruct &step = quest.steps[pq.currentStep];

        if (step.stepType != "talk")
            continue;

        int requiredNpcId = step.params.value("npc_id", -1);
        if (requiredNpcId != npcId && requiredNpcId != -1)
            continue;

        pq.progress["done"] = true;
        pq.isDirty = true;
        pq.updatedAt = std::chrono::steady_clock::now();

        if (seams_.sendQuestUpdate)
            seams_.sendQuestUpdate(characterId, pq, quest);
        checkStepCompletion(characterId, pq);
    }
}

void
QuestStore::onPositionReached(int characterId, float x, float y)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto pit = playerProgress_.find(characterId);
    if (pit == playerProgress_.end())
        return;

    for (auto &[questId, pq] : pit->second)
    {
        if (pq.state != "active")
            continue;

        auto qit = questsById_.find(questId);
        if (qit == questsById_.end())
            continue;
        const QuestStruct &quest = *qit->second;

        if (static_cast<int>(quest.steps.size()) <= pq.currentStep)
            continue;
        const QuestStepStruct &step = quest.steps[pq.currentStep];

        if (step.stepType != "reach")
            continue;

        float tx = step.params.value("x", 0.0f);
        float ty = step.params.value("y", 0.0f);
        float radius = step.params.value("radius", 200.0f);

        float dx = x - tx;
        float dy = y - ty;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= radius)
        {
            pq.progress["done"] = true;
            pq.isDirty = true;
            pq.updatedAt = std::chrono::steady_clock::now();

            if (seams_.sendQuestUpdate)
                seams_.sendQuestUpdate(characterId, pq, quest);
            checkStepCompletion(characterId, pq);
        }
    }
}

void
QuestStore::onWorldObjectInteracted(int characterId, int objectId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto pit = playerProgress_.find(characterId);
    if (pit == playerProgress_.end())
        return;

    for (auto &[questId, pq] : pit->second)
    {
        if (pq.state != "active")
            continue;

        auto qit = questsById_.find(questId);
        if (qit == questsById_.end())
            continue;
        const QuestStruct &quest = *qit->second;

        if (static_cast<int>(quest.steps.size()) <= pq.currentStep)
            continue;
        const QuestStepStruct &step = quest.steps[pq.currentStep];

        if (step.stepType != "interact")
            continue;

        int requiredObjectId = step.params.value("object_id", -1);
        if (requiredObjectId != objectId)
            continue;

        int required = step.params.value("count", 1);
        int current = pq.progress.value("interacted", 0);
        if (current < required)
        {
            pq.progress["interacted"] = current + 1;
            pq.isDirty = true;
            pq.updatedAt = std::chrono::steady_clock::now();

            if (seams_.sendQuestUpdate)
                seams_.sendQuestUpdate(characterId, pq, quest);
            checkStepCompletion(characterId, pq);
        }
    }
}

// =============================================================================
// Internal helpers
// =============================================================================

void
QuestStore::checkStepCompletion(int characterId, PlayerQuestProgressStruct &pq)
{
    auto qit = questsById_.find(pq.questId);
    if (qit == questsById_.end())
        return;
    const QuestStruct &quest = *qit->second;

    if (static_cast<int>(quest.steps.size()) <= pq.currentStep)
        return;
    const QuestStepStruct &step = quest.steps[pq.currentStep];

    bool stepDone = false;

    if (step.stepType == "kill")
    {
        int required = step.params.value("count", 1);
        stepDone = (pq.progress.value("killed", 0) >= required);
    }
    else if (step.stepType == "collect")
    {
        int required = step.params.value("count", 1);
        stepDone = (pq.progress.value("have", 0) >= required);
    }
    else if (step.stepType == "talk" || step.stepType == "reach")
    {
        stepDone = pq.progress.value("done", false);
    }

    if (stepDone)
    {
        if (step.completionMode == "manual")
            return; // Do not auto-advance; a dialogue action (advance_quest_step) will trigger it
        advanceStep(characterId, pq);
    }
}

void
QuestStore::advanceStep(int characterId, PlayerQuestProgressStruct &pq)
{
    auto qit = questsById_.find(pq.questId);
    if (qit == questsById_.end())
        return;
    const QuestStruct &quest = *qit->second;

    int nextStep = pq.currentStep + 1;

    if (nextStep >= static_cast<int>(quest.steps.size()))
    {
        // All steps done → complete quest
        completeQuest(characterId, pq);
        return;
    }

    pq.currentStep = nextStep;
    pq.progress = nlohmann::json::object();
    pq.isDirty = true;
    pq.updatedAt = std::chrono::steady_clock::now();

    // Initialise progress for the new step
    const QuestStepStruct &newStep = quest.steps[nextStep];
    if (newStep.stepType == "kill")
        pq.progress["killed"] = 0;
    else if (newStep.stepType == "collect")
    {
        // Seed "have" with items the player already carries so they don't have
        // to re-collect things they legitimately picked up before this step.
        int alreadyHave = 0;
        int requiredItemId = newStep.params.value("item_id", -1);
        int required = newStep.params.value("count", 1);
        if (requiredItemId > 0 && seams_.getPlayerInventory)
        {
            const auto inv = seams_.getPlayerInventory(characterId);
            for (const auto &slot : inv)
            {
                if (slot.itemId == requiredItemId)
                {
                    alreadyHave = slot.quantity;
                    break;
                }
            }
        }
        pq.progress["have"] = std::min(alreadyHave, required);
    }
    else if (newStep.stepType == "talk" || newStep.stepType == "reach")
        pq.progress["done"] = false;

    if (seams_.sendQuestUpdate)
        seams_.sendQuestUpdate(characterId, pq, quest);

    // The new step might already be satisfied (e.g. the player already carries
    // the required items for a collect step, or has already talked to the NPC
    // via a flag set earlier).  Check immediately so the quest advances without
    // requiring a redundant trigger event.
    checkStepCompletion(characterId, pq);
}

void
QuestStore::completeQuest(int characterId, PlayerQuestProgressStruct &pq)
{
    pq.state = "completed";
    pq.isDirty = true;
    pq.updatedAt = std::chrono::steady_clock::now();

    auto qit = questsById_.find(pq.questId);
    if (qit != questsById_.end() && seams_.sendQuestUpdate)
        seams_.sendQuestUpdate(characterId, pq, *qit->second);

    logger_.log("[QuestManager] Quest ID " + std::to_string(pq.questId) +
                    " completed by character " + std::to_string(characterId),
        GREEN);
}

// =============================================================================
// Persistence
// =============================================================================

void
QuestStore::queueFlagUpdate(const UpdatePlayerFlagStruct &flagUpdate)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFlagUpdates_.push_back(flagUpdate);
}

void
QuestStore::flushDirtyProgress()
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto &[characterId, questMap] : playerProgress_)
    {
        for (auto &[questId, pq] : questMap)
        {
            if (!pq.isDirty)
                continue;

            // Build UPDATE_PLAYER_QUEST_PROGRESS JSON to send to game-server
            nlohmann::json packet;
            packet["header"]["eventType"] = "updatePlayerQuestProgress";
            packet["body"]["characterId"] = characterId;
            packet["body"]["questId"] = questId;
            packet["body"]["questSlug"] = pq.questSlug;
            packet["body"]["state"] = pq.state;
            packet["body"]["currentStep"] = pq.currentStep;
            packet["body"]["progress"] = pq.progress;

            if (gameServerWorker_)
                gameServerWorker_->sendDataToGameServer(packet.dump() + "\n");

            pq.isDirty = false;
        }
    }

    // Flush pending flag updates
    if (gameServerWorker_)
    {
        for (const auto &fu : pendingFlagUpdates_)
        {
            nlohmann::json packet;
            packet["header"]["eventType"] = "updatePlayerFlag";
            packet["body"]["characterId"] = fu.characterId;
            packet["body"]["flagKey"] = fu.flagKey;
            if (fu.boolValue.has_value())
                packet["body"]["boolValue"] = fu.boolValue.value();
            if (fu.intValue.has_value())
                packet["body"]["intValue"] = fu.intValue.value();

            gameServerWorker_->sendDataToGameServer(packet.dump() + "\n");
        }
    }
    pendingFlagUpdates_.clear();
}

void
QuestStore::flushAllProgress(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = playerProgress_.find(characterId);
    if (it == playerProgress_.end())
        return;

    for (auto &[questId, pq] : it->second)
    {
        nlohmann::json packet;
        packet["header"]["eventType"] = "updatePlayerQuestProgress";
        packet["body"]["characterId"] = characterId;
        packet["body"]["questId"] = questId;
        packet["body"]["questSlug"] = pq.questSlug;
        packet["body"]["state"] = pq.state;
        packet["body"]["currentStep"] = pq.currentStep;
        packet["body"]["progress"] = pq.progress;

        if (gameServerWorker_)
            gameServerWorker_->sendDataToGameServer(packet.dump() + "\n");
        pq.isDirty = false;
    }
}

void
QuestStore::flushPendingFlags()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (gameServerWorker_)
    {
        for (const auto &fu : pendingFlagUpdates_)
        {
            nlohmann::json packet;
            packet["header"]["eventType"] = "updatePlayerFlag";
            packet["body"]["characterId"] = fu.characterId;
            packet["body"]["flagKey"] = fu.flagKey;
            if (fu.boolValue.has_value())
                packet["body"]["boolValue"] = fu.boolValue.value();
            if (fu.intValue.has_value())
                packet["body"]["intValue"] = fu.intValue.value();

            gameServerWorker_->sendDataToGameServer(packet.dump() + "\n");
        }
    }
    pendingFlagUpdates_.clear();
}

void
QuestStore::markFlagsLoaded(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    flagsLoadedCharacters_.insert(characterId);
}

bool
QuestStore::areFlagsLoaded(int characterId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return flagsLoadedCharacters_.count(characterId) > 0;
}

void
QuestStore::clearFlagsLoaded(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    flagsLoadedCharacters_.erase(characterId);
}
