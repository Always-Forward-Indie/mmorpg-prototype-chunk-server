#pragma once
// Thread-safe consumable-use cooldown registry (B3 extract).
//
// ItemEventHandler::handleUseItemEvent throttles potion/scroll/food reuse
// per (character, item) with second-granularity expiries. The map+mutex
// lived inline in the handler; the check-and-set now delegates here 1-1.
// Same store family as OngoingActionStore / CooldownService (which stays
// skill-keyed and millisecond-based — deliberately not unified).
#include <chrono>
#include <cstddef>
#include <mutex>
#include <unordered_map>

class ItemCooldownStore
{
  public:
    /// Atomically check the cooldown and claim it when free.
    /// @return true when the item may be used (slot claimed or no cooldown
    ///         defined), false while a previous claim is still active.
    bool tryAcquire(int characterId, int itemId, int cooldownSeconds)
    {
        if (cooldownSeconds <= 0)
            return true; // no cooldown defined — always allow
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        auto &entry = expiries_[characterId][itemId];
        if (now < entry)
            return false; // still on cooldown
        entry = now + std::chrono::seconds(cooldownSeconds);
        return true;
    }

    /// Forget all claims (test room-clearing; production never calls it —
    /// expiries lapse on their own).
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        expiries_.clear();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t n = 0;
        for (const auto &kv : expiries_)
            n += kv.second.size();
        return n;
    }

  private:
    // characterId -> (itemId -> ready-at time_point)
    std::unordered_map<int, std::unordered_map<int, std::chrono::steady_clock::time_point>> expiries_;
    mutable std::mutex mutex_;
};
