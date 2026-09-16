#pragma once

#include "data/DataStructs.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * @brief Injected catalog lookups for quest JSON enrichment (Increment 11).
 *
 * resolveStepForClient/resolveRewardsForClient translate static IDs to
 * client slugs 1-1 with the pre-split QuestManager; the orchestrator binds
 * these to Mob/Item/NPC managers, unit tests bind fakes.
 */
struct QuestResolverLookups
{
    std::function<MobDataStruct(int mobId)> getMobById;
    std::function<ItemDataStruct(int itemId)> getItemById;
    std::function<NPCDataStruct(int npcId)> getNPCById;
};

class QuestResolvers
{
  public:
    explicit QuestResolvers(QuestResolverLookups lookups)
        : lookups_(std::move(lookups))
    {
    }

    /**
     * @brief Resolve a quest step to a client-ready JSON object (IDs → slugs).
     */
    nlohmann::json resolveStepForClient(const QuestStepStruct &step) const
    {
        nlohmann::json j;
        j["clientStepKey"] = step.clientStepKey;
        j["stepType"] = step.stepType;
        j["count"] = step.params.value("count", 1);

        if (step.stepType == "kill")
        {
            int mobId = step.params.value("mob_id", -1);
            MobDataStruct mob = (mobId > 0 && lookups_.getMobById) ? lookups_.getMobById(mobId) : MobDataStruct{};
            j["target_slug"] = mob.slug;
        }
        else if (step.stepType == "collect")
        {
            int itemId = step.params.value("item_id", -1);
            ItemDataStruct item = (itemId > 0 && lookups_.getItemById) ? lookups_.getItemById(itemId) : ItemDataStruct{};
            j["target_slug"] = item.slug;
        }
        else if (step.stepType == "talk")
        {
            int npcId = step.params.value("npc_id", -1);
            NPCDataStruct npc = (npcId > 0 && lookups_.getNPCById) ? lookups_.getNPCById(npcId) : NPCDataStruct{};
            j["target_slug"] = npc.slug;
        }
        else if (step.stepType == "reach")
        {
            j["zone_slug"] = step.params.value("zone_slug", "");
            j["x"] = step.params.value("x", 0.0f);
            j["y"] = step.params.value("y", 0.0f);
        }
        else
        {
            // "custom" or unknown — pass params as-is, client handles
            j["params"] = step.params;
        }
        return j;
    }

    /**
     * @brief Resolve quest rewards to a client-ready JSON array respecting isHidden.
     * @param revealHidden  If true (used in quest_turned_in), all rewards are fully disclosed.
     */
    nlohmann::json resolveRewardsForClient(const std::vector<QuestRewardStruct> &rewards,
        bool revealHidden = false,
        int classId = 0) const
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &reward : rewards)
        {
            // Skip rewards restricted to other classes (classId==0 means unknown/no filter)
            if (classId != 0 && !reward.allowedClassIds.empty())
            {
                bool found = false;
                for (int cid : reward.allowedClassIds)
                    if (cid == classId)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    continue;
            }

            nlohmann::json r;
            r["rewardType"] = reward.rewardType;

            if (reward.isHidden && !revealHidden)
            {
                r["isHidden"] = true;
                // Only send type indicator so client can render the correct "???" icon
            }
            else
            {
                r["isHidden"] = false;
                if (reward.rewardType == "item")
                {
                    ItemDataStruct item = lookups_.getItemById ? lookups_.getItemById(reward.itemId)
                                                               : ItemDataStruct{};
                    r["item_slug"] = item.slug;
                    r["quantity"] = reward.quantity;
                }
                else if (reward.rewardType == "exp" || reward.rewardType == "gold")
                {
                    r["amount"] = reward.amount;
                }
            }
            arr.push_back(std::move(r));
        }
        return arr;
    }

  private:
    QuestResolverLookups lookups_;
};
