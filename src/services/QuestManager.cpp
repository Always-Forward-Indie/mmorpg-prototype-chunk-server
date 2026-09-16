#include "services/QuestManager.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include <spdlog/logger.h>

QuestManager::QuestManager(GameServices *services, Logger &logger)
    : services_(services),
      logger_(logger),
      store_(logger,
          QuestStoreSeams{
              [this](int characterId, const PlayerQuestProgressStruct &pq, const QuestStruct &quest)
              {
                  sendQuestUpdate(characterId, pq, quest);
              },
              [this](int characterId)
              {
                  return services_->getCharacterManager().getCharacterData(characterId).characterLevel;
              },
              [this](int characterId)
              {
                  return services_->getInventoryManager().getPlayerInventory(characterId);
              },
              [this](int characterId, const std::string &faction, int delta)
              {
                  services_->getReputationManager().changeReputation(characterId, faction, delta);
              },
          }),
      resolvers_(QuestResolverLookups{
          [this](int mobId)
          {
              return services_->getMobManager().getMobById(mobId);
          },
          [this](int itemId)
          {
              return services_->getItemManager().getItemById(itemId);
          },
          [this](int npcId)
          {
              return services_->getNPCManager().getNPCById(npcId);
          },
      })
{
    log_ = logger.getSystem("quest");
}

void
QuestManager::setGameServerWorker(GameServerWorker *worker)
{
    store_.setSendToGameServerCallback(
        [worker](const std::string &packet)
        {
            if (worker)
                worker->sendDataToGameServer(packet);
        });
}

void
QuestManager::setNetworkManager(NetworkManager *nm)
{
    networkManager_ = nm;
}

// =============================================================================
// Delegates to QuestStore
// =============================================================================

void
QuestManager::setQuests(const std::vector<QuestStruct> &quests)
{
    store_.setQuests(quests);
}

const QuestStruct *
QuestManager::getQuestBySlug(const std::string &slug) const
{
    return store_.getQuestBySlug(slug);
}

const QuestStruct *
QuestManager::getQuestById(int id) const
{
    return store_.getQuestById(id);
}

bool
QuestManager::isLoaded() const
{
    return store_.isLoaded();
}

void
QuestManager::loadPlayerQuests(int characterId,
    const std::vector<PlayerQuestProgressStruct> &quests)
{
    store_.loadPlayerQuests(characterId, quests);
}

void
QuestManager::unloadPlayerQuests(int characterId)
{
    store_.unloadPlayerQuests(characterId);
}

void
QuestManager::fillQuestContext(int characterId, PlayerContextStruct &ctx) const
{
    store_.fillQuestContext(characterId, ctx);
}

std::string
QuestManager::getQuestStateBySlug(int characterId, const std::string &questSlug) const
{
    return store_.getQuestStateBySlug(characterId, questSlug);
}

bool
QuestManager::offerQuest(int characterId, const std::string &questSlug)
{
    return store_.offerQuest(characterId, questSlug);
}

bool
QuestManager::failQuest(int characterId, const std::string &questSlug)
{
    return store_.failQuest(characterId, questSlug);
}

void
QuestManager::advanceQuestStepBySlug(int characterId, const std::string &questSlug)
{
    store_.advanceQuestStepBySlug(characterId, questSlug);
}

void
QuestManager::onMobKilled(int characterId, int mobId)
{
    store_.onMobKilled(characterId, mobId);
}

void
QuestManager::onItemObtained(int characterId, int itemId, int quantity)
{
    store_.onItemObtained(characterId, itemId, quantity);
}

void
QuestManager::onNPCTalked(int characterId, int npcId)
{
    store_.onNPCTalked(characterId, npcId);
}

void
QuestManager::onPositionReached(int characterId, float x, float y)
{
    store_.onPositionReached(characterId, x, y);
}

void
QuestManager::onWorldObjectInteracted(int characterId, int objectId)
{
    store_.onWorldObjectInteracted(characterId, objectId);
}

void
QuestManager::queueFlagUpdate(const UpdatePlayerFlagStruct &flagUpdate)
{
    store_.queueFlagUpdate(flagUpdate);
}

void
QuestManager::flushDirtyProgress()
{
    store_.flushDirtyProgress();
}

void
QuestManager::flushAllProgress(int characterId)
{
    store_.flushAllProgress(characterId);
}

void
QuestManager::flushPendingFlags()
{
    store_.flushPendingFlags();
}

void
QuestManager::markFlagsLoaded(int characterId)
{
    store_.markFlagsLoaded(characterId);
}

bool
QuestManager::areFlagsLoaded(int characterId) const
{
    return store_.areFlagsLoaded(characterId);
}

void
QuestManager::clearFlagsLoaded(int characterId)
{
    store_.clearFlagsLoaded(characterId);
}

nlohmann::json
QuestManager::resolveStepForClient(const QuestStepStruct &step) const
{
    return resolvers_.resolveStepForClient(step);
}

nlohmann::json
QuestManager::resolveRewardsForClient(const std::vector<QuestRewardStruct> &rewards,
    bool revealHidden,
    int classId) const
{
    return resolvers_.resolveRewardsForClient(rewards, revealHidden, classId);
}

// =============================================================================
// turnInQuest: snapshot in store, side effects here (no store lock held)
// =============================================================================

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

    if (!store_.beginTurnIn(characterId, questSlug, pqSnapshot, questSnapshot))
        return notifications;

    // Fetch character class for class-specific reward filtering
    const int playerClassId = services_->getCharacterManager().getCharacterData(characterId).classId;

    // Helper: returns true if this reward applies to this character's class
    auto rewardAppliesToClass = [&](const QuestRewardStruct &r) -> bool
    {
        if (r.allowedClassIds.empty())
            return true;
        for (int cid : r.allowedClassIds)
            if (cid == playerClassId)
                return true;
        return false;
    };

    // Build notifications
    nlohmann::json turnInNotif;
    turnInNotif["type"] = "quest_turned_in";
    turnInNotif["questId"] = questSnapshot.id;
    turnInNotif["clientQuestKey"] = questSnapshot.clientQuestKey;
    // All rewards fully revealed at turn-in (no hidden secrets remain)
    // Only include rewards that apply to this character's class
    {
        nlohmann::json filteredRewards = nlohmann::json::array();
        for (const auto &reward : questSnapshot.rewards)
        {
            if (!rewardAppliesToClass(reward))
                continue;
            auto resolved = resolveRewardsForClient({reward}, /*revealHidden=*/true);
            if (!resolved.empty())
                filteredRewards.push_back(resolved[0]);
        }
        turnInNotif["rewardsReceived"] = std::move(filteredRewards);
    }
    notifications.push_back(std::move(turnInNotif));

    // Title auto-grant: check quest conditions
    if (services_)
    {
        nlohmann::json titleEvent;
        titleEvent["questSlug"] = questSnapshot.slug;
        services_->getTitleManager().checkAndGrantTitles(characterId, "quest", titleEvent);
    }

    for (const auto &reward : questSnapshot.rewards)
    {
        if (!rewardAppliesToClass(reward))
            continue;

        if (reward.rewardType == "exp")
        {
            nlohmann::json n;
            n["type"] = "exp_received";
            n["amount"] = reward.amount;
            notifications.push_back(std::move(n));
        }
        else if (reward.rewardType == "item")
        {
            ItemDataStruct itemData = services_->getItemManager().getItemById(reward.itemId);
            nlohmann::json n;
            n["type"] = "item_received";
            n["itemId"] = reward.itemId;
            n["item_slug"] = itemData.slug;
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

    // Send QUEST_UPDATE (no store lock held — sendQuestUpdate takes none)
    sendQuestUpdate(characterId, pqSnapshot, questSnapshot);

    log_->info("[QuestManager] Quest '" + questSlug + "' turned in by character " +
               std::to_string(characterId));

    // Auto reputation change for quest completion
    if (!questSnapshot.reputationFactionSlug.empty() && questSnapshot.reputationOnComplete != 0)
    {
        services_->getReputationManager().changeReputation(
            characterId, questSnapshot.reputationFactionSlug, questSnapshot.reputationOnComplete);
        log_->info("[QuestManager] turnInQuest: rep change char=" + std::to_string(characterId) +
                   " faction=" + questSnapshot.reputationFactionSlug +
                   " delta=" + std::to_string(questSnapshot.reputationOnComplete));
    }

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

    // Grant rewards — the store lock is NOT held here, so calls back into the
    // store (e.g. onItemObtained) will not deadlock.
    for (const auto &reward : questSnapshot.rewards)
    {
        if (!rewardAppliesToClass(reward))
            continue;

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

// =============================================================================
// Flag helpers (CharacterManager via services_, persistence queue via store)
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
QuestManager::setFlagInt(int characterId, const std::string &key, int value)
{
    // Queue persistence to game-server
    UpdatePlayerFlagStruct fu;
    fu.characterId = characterId;
    fu.flagKey = key;
    fu.intValue = value;
    queueFlagUpdate(fu);

    // Update in-memory cache so same-session reads see the new value immediately
    auto charData = services_->getCharacterManager().getCharacterData(characterId);
    bool found = false;
    for (auto &f : charData.flags)
    {
        if (f.flagKey == key)
        {
            f.intValue = value;
            found = true;
            break;
        }
    }
    if (!found)
    {
        PlayerFlagStruct nf;
        nf.flagKey = key;
        nf.intValue = value;
        charData.flags.push_back(std::move(nf));
    }
    services_->getCharacterManager().setCharacterFlags(characterId, std::move(charData.flags));
}

// =============================================================================
// QUEST_UPDATE sender (takes no store lock — safe under store scopes)
// =============================================================================

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

        // Enriched step with resolved slugs + current progress count
        nlohmann::json enrichedStep = resolveStepForClient(step);
        int current = 0;
        if (step.stepType == "kill")
            current = pq.progress.value("killed", 0);
        else if (step.stepType == "collect")
            current = pq.progress.value("have", 0);
        else if (step.stepType == "talk" || step.stepType == "reach")
            current = pq.progress.value("done", false) ? 1 : 0;
        enrichedStep["current"] = current;
        body["currentStepEnriched"] = std::move(enrichedStep);
    }
    body["rewards"] = resolveRewardsForClient(
        quest.rewards, /*revealHidden=*/false, services_->getCharacterManager().getCharacterData(characterId).classId);

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
