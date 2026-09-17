#pragma once
// Thread-safe ongoing-action registry for combat (A4 extract).
//
// CombatSystem kept an unordered_map<CombatActionStruct> + mutex inline and
// touched it from three places: the already-casting read in
// initiateSkillUsage, the insert after validation, clearOngoingAction, and
// the due-sweep in updateOngoingActions. All four now delegate here; the
// sweep semantics are 1-1 (due CASTING → EXECUTING + collected for
// out-of-lock execution; stale EXECUTING entries reaped).
#include "data/CombatStructs.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class OngoingActionStore
{
  public:
    /// One action whose cast finished and must be executed out-of-lock.
    struct DueItem
    {
        int casterId = 0;
        std::string skillSlug;
        int targetId = 0;
        CombatTargetType targetType = CombatTargetType::NONE;
        std::string actionName;
        bool cooldownPreset = false;
    };

    /// Slug of the in-progress CASTING action, if any (initiation guard).
    std::optional<std::string> castingSlug(int casterId) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = actions_.find(casterId);
        if (it != actions_.end() && it->second->state == CombatActionState::CASTING)
            return it->second->skillSlug;
        return std::nullopt;
    }

    void put(int casterId, std::shared_ptr<CombatActionStruct> action)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        actions_[casterId] = std::move(action);
    }

    void erase(int casterId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        actions_.erase(casterId);
    }

    /// Collect due CASTING actions (flipped to EXECUTING) and reap stale
    /// EXECUTING entries left by synchronous instant-skill dispatch.
    std::vector<DueItem> takeDueAndSweep()
    {
        const auto now = std::chrono::steady_clock::now();
        std::vector<DueItem> due;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = actions_.begin(); it != actions_.end();)
        {
            const auto &action = it->second;
            if (action->state == CombatActionState::CASTING && now >= action->endTime)
            {
                action->state = CombatActionState::EXECUTING;
                DueItem item;
                item.casterId = action->casterId;
                item.skillSlug = action->skillSlug;
                item.targetId = action->targetId;
                item.targetType = action->targetType;
                item.actionName = action->actionName;
                item.cooldownPreset = action->cooldownPreset;
                due.push_back(std::move(item));
                it = actions_.erase(it);
            }
            else if (action->state == CombatActionState::EXECUTING)
            {
                // Instant skills (castMs=0) are executed synchronously in
                // dispatchSkillAction and leave a stale EXECUTING entry.
                it = actions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return due;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return actions_.size();
    }

  private:
    std::unordered_map<int, std::shared_ptr<CombatActionStruct>> actions_;
    mutable std::mutex mutex_;
};
