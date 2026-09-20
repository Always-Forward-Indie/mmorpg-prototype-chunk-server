#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// Chunk→game fact outbox (see Tools/Tests/OUTBOX_PLAN.md): at-least-once
/// delivery with idempotent receivers (game `fact_keys_applied` table).
///
/// Threading: strand-confined on GameServerWorker (every method below must
/// run on the worker strand; the periodic flush timer is strand-bound too).
/// Single-threaded in unit tests. Counters are atomic so the 10s status
/// task (foreign thread) can snapshot them lock-free.
struct OutboxFact
{
    std::string key;
    std::string payload;
    int64_t firstMs = 0;
    int64_t nextMs = 0;
    int attempts = 0;
};

struct OutboxSnapshot
{
    size_t pending = 0;
    uint64_t sent = 0;
    uint64_t acked = 0;
    uint64_t expired = 0;
};

class FactOutbox
{
  public:
    static constexpr int64_t kFlushMs = 5000;
    static constexpr int64_t kBackoffCapMs = 60000;
    static constexpr size_t kCap = 20000;

    FactOutbox()
    {
        // Random boot id: sequence counters restart with the process, so
        // keys must differ across boots — otherwise a post-restart fact
        // collides with a pre-restart key in fact_keys_applied and gets
        // falsely deduped (LOSS, not duplication).
        std::random_device rd;
        std::mt19937_64 gen(rd());
        bootId_ = std::to_string(gen());
    }

    /// Fact eventTypes tracked with keys + retries. ONLY types whose game
    /// handler claims keys and sends factAck may be listed: untracked facts
    /// would retry forever (no ack ever comes) into DLQ noise. Widen this
    /// list strictly together with wiring the game-side handler (same
    /// 6-line claim/ack pattern). Link-level traffic
    /// (chunkServerConnection, markCharactersOnline, heartbeats) and
    /// typed-vector handlers without key access (positions/HP/progress,
    /// quest/flag/playtime/markOnline delegates) stay on the legacy path.
    static bool isFactType(const std::string &eventType)
    {
        static const std::unordered_set<std::string> kFacts = {
            "saveLearnedSkill",
            "saveReputation",
            "saveInventoryChange",
            "saveDurabilityChange",
            "saveCurrencyTransaction",
            "saveEquipmentChange",
            "saveExperienceDebt",
            "saveActiveEffect",
            "saveItemKillCount",
            "savePityCounter",
            "saveBestiaryKill",
            "timedChampionKilled",
            "saveMastery",
            "saveSkillBarSlot",
            "savePlayerTitle",
            "saveSkillCooldown",
            "analyticsEvent",
        };
        return kFacts.count(eventType) > 0;
    }

    /// Store a fact, return its key ("{characterId}:{factType}:{seq}").
    /// Mint a fresh key ("{characterId}:{factType}:{bootId}:{seq}").
    /// Caller stamps it into the payload, then store()s the stamped copy
    /// (retries must carry the same key the game dedups on).
    std::string assignKey(int characterId, const std::string &factType)
    {
        return std::to_string(characterId) + ":" + factType + ":" +
               bootId_ + ":" + std::to_string(++seq_);
    }

    /// Store a stamped payload under its key. Same key twice = coalesce to
    /// latest (absolute SET semantics; deltas always carry fresh keys).
    void store(std::string key, std::string payload, int64_t nowMs)
    {
        auto it = pending_.find(key);
        if (it != pending_.end())
        {
            it->second.payload = std::move(payload);
            return;
        }
        if (pending_.size() >= kCap)
        {
            auto oldest = std::min_element(pending_.begin(), pending_.end(),
                [](const auto &a, const auto &b) { return a.second.firstMs < b.second.firstMs; });
            pending_.erase(oldest);
            ++expired_;
        }
        pending_.emplace(key, OutboxFact{key, std::move(payload), nowMs, nowMs, 0});
        pendingCount_.store(pending_.size(), std::memory_order_relaxed);
    }

    /// Legacy helper: store under a fresh key, return it.
    std::string enqueue(int characterId,
        const std::string &factType,
        std::string payload,
        int64_t nowMs)
    {
        std::string key = assignKey(characterId, factType);
        store(key, std::move(payload), nowMs);
        return key;
    }

    /// Facts due for (re)send; bumps attempts/nextMs with backoff.
    std::vector<OutboxFact> dueFlush(int64_t nowMs)
    {
        std::vector<OutboxFact> due;
        for (auto &[key, f] : pending_)
        {
            if (f.nextMs > nowMs)
                continue;
            ++f.attempts;
            f.nextMs = nowMs + backoffFor(f.attempts);
            due.push_back(f);
            ++sent_;
        }
        return due;
    }

    /// Drop an acked fact. Returns true when something was pending
    /// (false = unknown/duplicate ack; caller may log at debug).
    bool ack(const std::string &key)
    {
        auto it = pending_.find(key);
        if (it == pending_.end())
            return false;
        pending_.erase(it);
        pendingCount_.store(pending_.size(), std::memory_order_relaxed);
        ++acked_;
        return true;
    }

    /// Drop facts older than maxAgeMs (DLQ: caller logs them first).
    std::vector<OutboxFact> sweepExpired(int64_t nowMs, int64_t maxAgeMs)
    {
        std::vector<OutboxFact> out;
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            if (nowMs - it->second.firstMs > maxAgeMs)
            {
                out.push_back(it->second);
                it = pending_.erase(it);
                ++expired_;
            }
            else
            {
                ++it;
            }
        }
        pendingCount_.store(pending_.size(), std::memory_order_relaxed);
        return out;
    }

    OutboxSnapshot snapshot() const
    {
        return OutboxSnapshot{
            pendingCount_.load(std::memory_order_relaxed),
            sent_.load(std::memory_order_relaxed),
            acked_.load(std::memory_order_relaxed),
            expired_.load(std::memory_order_relaxed),
        };
    }

    /// Count an immediate (non-retry) send so sent>=acked always holds.
    void noteSent() { ++sent_; }

    size_t size() const { return pendingCount_.load(std::memory_order_relaxed); }

  private:
    static int64_t backoffFor(int attempts)
    {
        int64_t wait = kFlushMs;
        for (int i = 1; i < attempts && wait < kBackoffCapMs; ++i)
            wait *= 2;
        return wait > kBackoffCapMs ? kBackoffCapMs : wait;
    }

    uint64_t seq_ = 0;
    std::string bootId_;
    std::unordered_map<std::string, OutboxFact> pending_;
    std::atomic<size_t> pendingCount_{0};
    std::atomic<uint64_t> sent_{0};
    std::atomic<uint64_t> acked_{0};
    std::atomic<uint64_t> expired_{0};
};
