#include "services/QuestManager.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <cmath>

QuestManager::QuestManager(GameServices *services, Logger &logger)
    : services_(services), logger_(logger)
{
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
    std::lock_guard<std::mutex> lock(mutex_);
    auto &progress = playerProgress_[characterId];
    progress.clear();
    for (const auto &pq : quests)
    {
        progress[pq.questId] = pq;
    }
    logger_.log("[QuestManager] Loaded " + std::to_string(quests.size()) +
                " quests for character " + std::to_string(characterId));
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
        if (!pq.questSlug.empty())
            ctx.questStates[pq.questSlug] = pq.state;
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
        logger_.logError("[QuestManager] offerQuest: unknown quest slug '" + questSlug + "'");
        return false;
    }
    const QuestStruct &quest = qit->second;

    // Character level check
    auto charData = services_->getCharacterManager().getCharacterData(characterId);
    if (charData.characterLevel < quest.minLevel)
    {
        logger_.log("[QuestManager] Character " + std::to_string(characterId) +
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
            logger_.log("[QuestManager] Quest '" + questSlug + "' already in state " + currentState);
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
            pq.progress["have"] = 0;
        else if (step0.stepType == "talk")
            pq.progress["done"] = false;
        else if (step0.stepType == "reach")
            pq.progress["done"] = false;
    }

    progress[quest.id] = std::move(pq);

    // Send update to client
    sendQuestUpdate(characterId, progress[quest.id], quest);

    logger_.log("[QuestManager] Quest '" + questSlug + "' offered to character " +
                std::to_string(characterId));
    return true;
}

std::vector<nlohmann::json>
QuestManager::turnInQuest(int characterId, const std::string &questSlug, int clientId)
{
    std::vector<nlohmann::json> notifications;
    std::lock_guard<std::mutex> lock(mutex_);

    auto qit = questsBySlug_.find(questSlug);
    if (qit == questsBySlug_.end())
    {
        logger_.logError("[QuestManager] turnInQuest: unknown quest '" + questSlug + "'");
        return notifications;
    }
    const QuestStruct &quest = qit->second;

    auto &progress = playerProgress_[characterId];
    auto pit = progress.find(quest.id);

    if (pit == progress.end() || pit->second.state != "completed")
    {
        logger_.log("[QuestManager] Cannot turn in quest '" + questSlug +
                    "': not in completed state");
        return notifications;
    }

    pit->second.state = "turned_in";
    pit->second.isDirty = true;
    pit->second.updatedAt = std::chrono::steady_clock::now();

    // Grant rewards - we need to unlock mutex temporarily to call services
    // We'll build the notification list here and grant outside the lock
    // (rewards are granted below after collecting info)

    // Build quest_turned_in notification
    nlohmann::json turnInNotif;
    turnInNotif["type"] = "quest_turned_in";
    turnInNotif["questId"] = quest.id;
    turnInNotif["clientQuestKey"] = quest.clientQuestKey;
    notifications.push_back(std::move(turnInNotif));

    // Collect reward descriptions
    for (const auto &reward : quest.rewards)
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

    // Send QUEST_UPDATE to the client (state = turned_in)
    sendQuestUpdate(characterId, pit->second, quest);

    logger_.log("[QuestManager] Quest '" + questSlug + "' turned in by character " +
                std::to_string(characterId));

    // Grant rewards (unlocked - services accessed)
    // Note: we hold the QuestManager mutex here; services' managers have their own locks
    for (const auto &reward : quest.rewards)
    {
        if (reward.rewardType == "exp" && reward.amount > 0)
        {
            services_->getExperienceManager().grantExperience(
                characterId, static_cast<int>(reward.amount), "quest_reward", quest.id);
        }
        else if (reward.rewardType == "item" && reward.itemId > 0)
        {
            services_->getInventoryManager().addItemToInventory(
                characterId, reward.itemId, reward.quantity);
        }
    }

    return notifications;
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
        advanceStep(characterId, pq);
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
        pq.progress["have"] = 0;
    else if (newStep.stepType == "talk" || newStep.stepType == "reach")
        pq.progress["done"] = false;

    sendQuestUpdate(characterId, pq, quest);
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
    body["clientQuestKey"] = quest.clientQuestKey;
    body["state"] = pq.state;
    body["currentStep"] = pq.currentStep;
    body["progress"] = pq.progress;

    if (pq.currentStep < static_cast<int>(quest.steps.size()))
    {
        const QuestStepStruct &step = quest.steps[pq.currentStep];
        body["clientStepKey"] = step.clientStepKey;
        body["stepType"] = step.stepType;
        body["required"] = step.params;
    }

    nlohmann::json packet = ResponseBuilder()
                                .setHeader("eventType", "QUEST_UPDATE")
                                .build();
    packet["body"] = body;

    if (!networkManager_)
    {
        logger_.logError("[QuestManager] sendQuestUpdate: networkManager_ not set");
        return;
    }

    networkManager_->sendResponse(
        clientSocket,
        networkManager_->generateResponseMessage("success", packet));
}

// =============================================================================
// Persistence
// =============================================================================

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
