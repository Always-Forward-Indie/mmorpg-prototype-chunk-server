#include "services/QuestManager.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <cmath>
#include <spdlog/logger.h>

QuestManager::QuestManager(GameServices *services, Logger &logger)
    : services_(services), logger_(logger)
{
    log_ = logger.getSystem("quest");
}

void
QuestManager::setGameServerWorker(GameServerWorker *worker)
{
    gameServerWorker_ = worker;
}

void
QuestManager::setNetworkManager(NetworkManager *nm)
{
    networkManager_ = nm;
}

// =============================================================================
// Static data loading
// =============================================================================

void
QuestManager::setQuests(const std::vector<QuestStruct> &quests)
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
QuestManager::getQuestBySlug(const std::string &slug) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = questsBySlug_.find(slug);
    return (it != questsBySlug_.end()) ? &it->second : nullptr;
}

const QuestStruct *
QuestManager::getQuestById(int id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = questsById_.find(id);
    return (it != questsById_.end()) ? it->second : nullptr;
}

bool
QuestManager::isLoaded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_;
}

// =============================================================================
// Player quest progress lifecycle
// =============================================================================

void
QuestManager::loadPlayerQuests(int characterId,
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
    // sendQuestUpdate() each acquire it internally.
    for (const auto &pq : quests)
    {
        const QuestStruct *quest = getQuestById(pq.questId);
        if (quest)
            sendQuestUpdate(characterId, pq, *quest);
    }
}

void
QuestManager::unloadPlayerQuests(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playerProgress_.erase(characterId);
}

void
QuestManager::fillQuestContext(int characterId, PlayerContextStruct &ctx) const
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

// =============================================================================
// Quest logic
// =============================================================================

bool
QuestManager::offerQuest(int characterId, const std::string &questSlug)
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
    auto charData = services_->getCharacterManager().getCharacterData(characterId);
    if (charData.characterLevel < quest.minLevel)
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
            if (requiredItemId > 0)
            {
                const auto &inv = services_->getInventoryManager().getPlayerInventory(characterId);
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
    sendQuestUpdate(characterId, progress[quest.id], quest);

    // Step 0 might already be satisfied on offer (e.g. player already holds
    // the required collect items).  Drive the first auto-check right away.
    checkStepCompletion(characterId, progress[quest.id]);

    log_->info("[QuestManager] Quest '" + questSlug + "' offered to character " +
               std::to_string(characterId));
    return true;
}

std::vector<nlohmann::json>
QuestManager::turnInQuest(int characterId, const std::string &questSlug, int clientId)
{
    std::vector<nlohmann::json> notifications;

    // Snapshot everything we need from shared state while holding the lock,
    // then release it BEFORE calling any external service.
    // This prevents a deadlock cycle:
    //   turnInQuest (holds mutex_) → addItemToInventory → onItemObtained → mutex_ (already held)
    PlayerQuestProgressStruct pqSnapshot;
    QuestStruct questSnapshot;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto qit = questsBySlug_.find(questSlug);
        if (qit == questsBySlug_.end())
        {
            log_->error("[QuestManager] turnInQuest: unknown quest '" + questSlug + "'");
            return notifications;
        }
        const QuestStruct &quest = qit->second;

        auto &progress = playerProgress_[characterId];
        auto pit = progress.find(quest.id);

        if (pit == progress.end() || pit->second.state != "completed")
        {
            log_->info("[QuestManager] Cannot turn in quest '" + questSlug +
                       "': not in completed state");
            return notifications;
        }

        pit->second.state = "turned_in";
        pit->second.isDirty = true;
        pit->second.updatedAt = std::chrono::steady_clock::now();

        // Take copies before releasing the lock
        pqSnapshot = pit->second;
        questSnapshot = quest;
    } // mutex_ released here

    // Build notifications
    nlohmann::json turnInNotif;
    turnInNotif["type"] = "quest_turned_in";
    turnInNotif["questId"] = questSnapshot.id;
    turnInNotif["clientQuestKey"] = questSnapshot.clientQuestKey;
    notifications.push_back(std::move(turnInNotif));

    for (const auto &reward : questSnapshot.rewards)
    {
        if (reward.rewardType == "exp")
        {
            nlohmann::json n;
            n["type"] = "exp_received";
            n["amount"] = reward.amount;
            notifications.push_back(std::move(n));
        }
        else if (reward.rewardType == "item")
        {
            nlohmann::json n;
            n["type"] = "item_received";
            n["itemId"] = reward.itemId;
            n["quantity"] = reward.quantity;
            notifications.push_back(std::move(n));
        }
        else if (reward.rewardType == "gold")
        {
            nlohmann::json n;
            n["type"] = "gold_received";
            n["amount"] = reward.amount;
            notifications.push_back(std::move(n));
        }
    }

    // Send QUEST_UPDATE (no mutex held — sendQuestUpdate acquires nothing in QuestManager)
    sendQuestUpdate(characterId, pqSnapshot, questSnapshot);

    log_->info("[QuestManager] Quest '" + questSlug + "' turned in by character " +
               std::to_string(characterId));

    // Remove collected quest items from inventory before granting rewards.
    // Iterate over every "collect" step and consume the required items.
    for (const auto &step : questSnapshot.steps)
    {
        if (step.stepType == "collect")
        {
            int requiredItemId = step.params.value("item_id", -1);
            int required = step.params.value("count", 1);
            if (requiredItemId > 0 && required > 0)
            {
                services_->getInventoryManager().removeItemFromInventory(
                    characterId, requiredItemId, required);
            }
        }
    }

    // Grant rewards — mutex_ is NOT held here, so calls back into QuestManager
    // (e.g. onItemObtained) will not deadlock.
    for (const auto &reward : questSnapshot.rewards)
    {
        if (reward.rewardType == "exp" && reward.amount > 0)
        {
            services_->getExperienceManager().grantExperience(
                characterId, static_cast<int>(reward.amount), "quest_reward", questSnapshot.id);
        }
        else if (reward.rewardType == "item" && reward.itemId > 0)
        {
            services_->getInventoryManager().addItemToInventory(
                characterId, reward.itemId, reward.quantity);
        }
        else if (reward.rewardType == "gold" && reward.amount > 0)
        {
            const ItemDataStruct *goldItem =
                services_->getItemManager().getItemBySlug("gold_coin");
            if (goldItem)
            {
                services_->getInventoryManager().addItemToInventory(
                    characterId, goldItem->id, static_cast<int>(reward.amount));
            }
            else
            {
                log_->error("[QuestManager] turnInQuest: 'gold_coin' item not found — gold reward skipped");
            }
        }
    }

    return notifications;
}

bool
QuestManager::failQuest(int characterId, const std::string &questSlug)
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

    sendQuestUpdate(characterId, pit->second, quest);

    log_->info("[QuestManager] Quest '" + questSlug + "' failed for character " +
               std::to_string(characterId));
    return true;
}

void
QuestManager::advanceQuestStepBySlug(int characterId, const std::string &questSlug)
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
QuestManager::onMobKilled(int characterId, int mobId)
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

            sendQuestUpdate(characterId, pq, quest);
            checkStepCompletion(characterId, pq);
        }
    }
}

void
QuestManager::onItemObtained(int characterId, int itemId, int quantity)
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

        sendQuestUpdate(characterId, pq, quest);
        checkStepCompletion(characterId, pq);
    }
}

void
QuestManager::onNPCTalked(int characterId, int npcId)
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

        sendQuestUpdate(characterId, pq, quest);
        checkStepCompletion(characterId, pq);
    }
}

void
QuestManager::onPositionReached(int characterId, float x, float y)
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

            sendQuestUpdate(characterId, pq, quest);
            checkStepCompletion(characterId, pq);
        }
    }
}

// =============================================================================
// Internal helpers
// =============================================================================

void
QuestManager::checkStepCompletion(int characterId, PlayerQuestProgressStruct &pq)
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
QuestManager::advanceStep(int characterId, PlayerQuestProgressStruct &pq)
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
        if (requiredItemId > 0 && services_)
        {
            const auto &inv = services_->getInventoryManager().getPlayerInventory(characterId);
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

    sendQuestUpdate(characterId, pq, quest);

    // The new step might already be satisfied (e.g. the player already carries
    // the required items for a collect step, or has already talked to the NPC
    // via a flag set earlier).  Check immediately so the quest advances without
    // requiring a redundant trigger event.
    checkStepCompletion(characterId, pq);
}

void
QuestManager::completeQuest(int characterId, PlayerQuestProgressStruct &pq)
{
    pq.state = "completed";
    pq.isDirty = true;
    pq.updatedAt = std::chrono::steady_clock::now();

    auto qit = questsById_.find(pq.questId);
    if (qit != questsById_.end())
        sendQuestUpdate(characterId, pq, *qit->second);

    logger_.log("[QuestManager] Quest ID " + std::to_string(pq.questId) +
                    " completed by character " + std::to_string(characterId),
        GREEN);
}

void
QuestManager::sendQuestUpdate(int characterId,
    const PlayerQuestProgressStruct &pq,
    const QuestStruct &quest)
{
    // Find the client socket for this character
    int clientId = 0;
    try
    {
        auto charData = services_->getCharacterManager().getCharacterData(characterId);
        clientId = charData.clientId;
    }
    catch (...)
    {
        return;
    }

    if (clientId <= 0)
        return;

    auto clientSocket = services_->getClientManager().getClientSocket(clientId);
    if (!clientSocket)
        return;

    // Build QUEST_UPDATE packet
    nlohmann::json body;
    body["questId"] = quest.id;
    body["questSlug"] = quest.slug;
    body["clientQuestKey"] = quest.clientQuestKey;
    body["state"] = pq.state;
    body["currentStep"] = pq.currentStep;
    body["progress"] = pq.progress;

    body["totalSteps"] = static_cast<int>(quest.steps.size());
    if (pq.currentStep < static_cast<int>(quest.steps.size()))
    {
        const QuestStepStruct &step = quest.steps[pq.currentStep];
        body["clientStepKey"] = step.clientStepKey;
        body["stepType"] = step.stepType;
        body["completionMode"] = step.completionMode;
        body["required"] = step.params;
    }

    nlohmann::json packet = ResponseBuilder()
                                .setHeader("eventType", "QUEST_UPDATE")
                                .build();
    packet["body"] = body;

    if (!networkManager_)
    {
        log_->error("[QuestManager] sendQuestUpdate: networkManager_ not set");
        return;
    }

    networkManager_->sendResponse(
        clientSocket,
        networkManager_->generateResponseMessage("success", packet));
}

// =============================================================================
// Persistence
// =============================================================================

bool
QuestManager::getFlagBool(int characterId, const std::string &key) const
{
    const auto charData = services_->getCharacterManager().getCharacterData(characterId);
    for (const auto &f : charData.flags)
    {
        if (f.flagKey == key && f.boolValue.has_value())
            return f.boolValue.value();
    }
    return false;
}

void
QuestManager::setFlagBool(int characterId, const std::string &key, bool value)
{
    // Queue persistence to game-server
    UpdatePlayerFlagStruct fu;
    fu.characterId = characterId;
    fu.flagKey = key;
    fu.boolValue = value;
    queueFlagUpdate(fu);

    // Update in-memory cache so same-session reads see the new value immediately
    auto charData = services_->getCharacterManager().getCharacterData(characterId);
    bool found = false;
    for (auto &f : charData.flags)
    {
        if (f.flagKey == key)
        {
            f.boolValue = value;
            found = true;
            break;
        }
    }
    if (!found)
    {
        PlayerFlagStruct nf;
        nf.flagKey = key;
        nf.boolValue = value;
        charData.flags.push_back(std::move(nf));
    }
    services_->getCharacterManager().setCharacterFlags(characterId, std::move(charData.flags));
}

void
QuestManager::queueFlagUpdate(const UpdatePlayerFlagStruct &flagUpdate)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFlagUpdates_.push_back(flagUpdate);
}

void
QuestManager::flushDirtyProgress()
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
QuestManager::flushAllProgress(int characterId)
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
QuestManager::flushPendingFlags()
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
QuestManager::markFlagsLoaded(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    flagsLoadedCharacters_.insert(characterId);
}

bool
QuestManager::areFlagsLoaded(int characterId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return flagsLoadedCharacters_.count(characterId) > 0;
}

void
QuestManager::clearFlagsLoaded(int characterId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    flagsLoadedCharacters_.erase(characterId);
}

std::string
QuestManager::getQuestStateBySlug(int characterId, const std::string &questSlug) const
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
