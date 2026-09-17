#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utils/Logger.hpp>
#include <vector>

// Interest management v2 (spatial subscriptions).
//
// The world is divided into a uniform grid. Each client subscribes to the
// 3x3 neighbourhood (view_radius=1) around its anchor cell; senders fan out
// only to subscribers of the entity's cell (phase 2+). This manager tracks
// subscriptions only — it never sends anything (phase 1: metrics only).
//
// Anti-jitter rules:
// - Rehome margin: the anchor cell changes only after the client moves past
//   the cell edge by marginFrac * cellSize (no flapping on the boundary).
// - Teleports (spawn/respawn/lag jump across a whole cell) resubscribe the
//   same way — the diff naturally covers it, and callers must send a full
//   snapshot for entered cells (never deltas for unseen entities).
// - Fail-open: senders must treat "no subscription" as "send everything"
//   (loading clients, unknown positions, interest disabled).
class InterestManager
{
  public:
    struct CellKey
    {
        int32_t cx{0};
        int32_t cy{0};
        bool operator==(const CellKey &o) const { return cx == o.cx && cy == o.cy; }
    };

    struct CellKeyHash
    {
        size_t operator()(const CellKey &k) const noexcept
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(k.cx)) << 32) |
                   static_cast<uint32_t>(k.cy);
        }
    };

    struct UpdateResult
    {
        std::vector<CellKey> entered;
        std::vector<CellKey> left;
    };

    struct Stats
    {
        size_t trackedClients{0};
        size_t liveCells{0};
        size_t totalSubs{0};
        uint64_t resubscribes{0};
        // Fan-out tick counters (phase 2+): mob updates built vs client
        // events pushed. Culled% = 1 - pushed/(built*clients).
        uint64_t tickMobs{0};
        uint64_t tickEvents{0};
        // Enter-snapshots sent/skipped (cooldown) — storm visibility.
        uint64_t snapshotsSent{0};
        uint64_t snapshotsSkipped{0};
    };

    // Enter-snapshot gate: at most one snapshot per client per cooldown.
    // Returns true when the caller may send (and records it).
    bool snapshotAllowed(int clientId);

    // Called once per fan-out tick (single call, already aggregated).
    void recordFanout(uint64_t mobsBuilt, uint64_t eventsPushed);

    explicit InterestManager(Logger &logger);

    // Single source of truth for design defaults (Wave 2.1/2.2). The member
    // initializers below AND the ChunkServer configure() call both use these,
    // so a tuning change touches exactly one place. Live values still come
    // from game_config (interest.cell_size et al.) with these as fallbacks.
    static constexpr float kDefaultCellSize = 1500.0f;
    static constexpr int kDefaultViewRadius = 1; // 1 => 3x3 neighbourhood
    static constexpr float kDefaultRehomeMarginFrac = 0.15f;
    static constexpr bool kDefaultEnabled = true;
    static constexpr bool kDefaultSnapshots = true;
    static constexpr int64_t kDefaultSnapshotCooldownMs = 2000;

    // Live-tunable geometry. Defaults = design values, override via
    // GameConfigService (interest.cell_size / interest.view_radius /
    // interest.rehome_margin_frac / interest.enabled / interest.snapshots /
    // interest.snapshot_cooldown_ms).
    void configure(float cellSize, int viewRadius, float rehomeMarginFrac, bool enabled);
    void setSnapshots(bool enabled, int64_t cooldownMs);

    // Track a client position update. Cheap: early-outs while inside the
    // anchor cell (+ margin). Thread-safe.
    UpdateResult onPlayerMoved(int clientId, int characterId, float x, float y);

    // Drop all subscriptions (disconnect/evict). Idempotent.
    void removeClient(int clientId);

    // Subscribers of one cell (phase 2 fan-out). Empty when disabled.
    std::vector<int> getSubscribers(const CellKey &cell) const;

    // Watchlist: mobs a client actively interacts with (attack target).
    // Watched mobs bypass cell culling for that client — otherwise hunters
    // chase frozen positions (spawn snapshot goes stale outside the
    // subscription). Entries expire after WATCH_TTL_SEC.
    static constexpr int64_t WATCH_TTL_SEC = 30;
    void watch(int clientId, int mobUid);
    void unwatch(int clientId, int mobUid);
    // mobUid -> watching clientIds (one lock, call once per fan-out tick).
    // Also purges expired entries.
    std::unordered_map<int, std::vector<int>> watchIndex();

    // All cells a client is subscribed to (phase 2 snapshot-on-enter).
    std::vector<CellKey> getClientCells(int clientId) const;

    // Recipient list for a positional broadcast from (x, y): subscribers of
    // the source cell + fail-open clients (untracked or !isWorldReady).
    // Empty when disabled (caller falls back to legacy broadcast-all).
    // excludeClientId < 0 disables exclusion.
    std::vector<int> recipientsFor(float x, float y,
        const std::vector<std::pair<int, bool>> &clients,
        int excludeClientId = -1) const;

    static CellKey cellFor(float x, float y, float cellSize);
    bool isEnabled() const;
    float cellSize() const;

    // One-lock snapshot of the whole subscription table for fan-out ticks:
    // cell -> member clientIds, plus the set of tracked clientIds (anyone
    // NOT tracked is fail-open = receives everything).
    struct Snapshot
    {
        std::unordered_map<CellKey, std::unordered_set<int>, CellKeyHash> members;
        std::unordered_set<int> tracked;
    };
    Snapshot snapshot() const;

    Stats stats() const;

  private:
    std::unordered_set<CellKey, CellKeyHash> neighbourhood(int32_t cx, int32_t cy) const;

    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::mutex mutex_;
    float cellSize_{kDefaultCellSize};
    int viewRadius_{kDefaultViewRadius};
    float rehomeMarginFrac_{kDefaultRehomeMarginFrac};
    bool enabled_{kDefaultEnabled};

    struct Sub
    {
        int characterId{0};
        int32_t anchorCx{0};
        int32_t anchorCy{0};
        bool hasAnchor{false};
        std::unordered_set<CellKey, CellKeyHash> cells;
    };
    std::unordered_map<int, Sub> subs_; // clientId -> subscription
    std::unordered_map<CellKey, std::unordered_set<int>, CellKeyHash> cellMembers_;
    uint64_t resubscribes_{0};
    uint64_t tickMobs_{0};
    uint64_t tickEvents_{0};
    bool snapshots_{kDefaultSnapshots};
    int64_t snapshotCooldownMs_{kDefaultSnapshotCooldownMs};
    std::unordered_map<int, std::chrono::steady_clock::time_point> lastSnapshot_;
    uint64_t snapshotsSent_{0};
    uint64_t snapshotsSkipped_{0};
    // clientId -> {mobUid -> watch start (steady clock)}.
    std::unordered_map<int, std::unordered_map<int, std::chrono::steady_clock::time_point>> watches_;
};
