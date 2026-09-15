#include "services/InterestManager.hpp"
#include <cmath>
#include <spdlog/logger.h>

InterestManager::InterestManager(Logger &logger)
    : logger_(logger)
{
    log_ = logger.getSystem("interest");
}

void
InterestManager::configure(float cellSize, int viewRadius, float rehomeMarginFrac, bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cellSize > 0.0f)
        cellSize_ = cellSize;
    if (viewRadius >= 0)
        viewRadius_ = viewRadius;
    if (rehomeMarginFrac >= 0.0f && rehomeMarginFrac < 1.0f)
        rehomeMarginFrac_ = rehomeMarginFrac;
    enabled_ = enabled;
}

InterestManager::CellKey
InterestManager::cellFor(float x, float y, float cellSize)
{
    return CellKey{
        static_cast<int32_t>(std::floor(x / cellSize)),
        static_cast<int32_t>(std::floor(y / cellSize)),
    };
}

bool
InterestManager::isEnabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

float
InterestManager::cellSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return cellSize_;
}

InterestManager::Snapshot
InterestManager::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot s;
    if (!enabled_)
        return s;
    s.members = cellMembers_;
    s.tracked.reserve(subs_.size() * 2);
    for (const auto &[clientId, sub] : subs_)
        s.tracked.insert(clientId);
    return s;
}

std::unordered_set<InterestManager::CellKey, InterestManager::CellKeyHash>
InterestManager::neighbourhood(int32_t cx, int32_t cy) const
{
    std::unordered_set<CellKey, CellKeyHash> out;
    for (int dx = -viewRadius_; dx <= viewRadius_; ++dx)
        for (int dy = -viewRadius_; dy <= viewRadius_; ++dy)
            out.insert(CellKey{cx + dx, cy + dy});
    return out;
}

InterestManager::UpdateResult
InterestManager::onPlayerMoved(int clientId, int characterId, float x, float y)
{
    UpdateResult res;
    if (clientId <= 0)
        return res;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_)
        return res;

    const CellKey cell = cellFor(x, y, cellSize_);
    Sub &sub = subs_[clientId]; // creates on first move
    sub.characterId = characterId;

    if (!sub.hasAnchor)
    {
        // First sighting (or teleport from unknown): subscribe, full sync
        // duty is on the caller (snapshot entered cells, never deltas).
        sub.anchorCx = cell.cx;
        sub.anchorCy = cell.cy;
        sub.hasAnchor = true;
        sub.cells = neighbourhood(cell.cx, cell.cy);
        for (const auto &c : sub.cells)
        {
            cellMembers_[c].insert(clientId);
            res.entered.push_back(c);
        }
        ++resubscribes_;
        return res;
    }

    if (cell.cx == sub.anchorCx && cell.cy == sub.anchorCy)
        return res; // fast path: inside anchor cell, nothing changed

    // Hysteresis: require clearing the anchor bounds by the margin before
    // rehoming, so standing on the line never flaps subscriptions.
    const float margin = rehomeMarginFrac_ * cellSize_;
    const float minX = sub.anchorCx * cellSize_;
    const float maxX = (sub.anchorCx + 1) * cellSize_;
    const float minY = sub.anchorCy * cellSize_;
    const float maxY = (sub.anchorCy + 1) * cellSize_;
    const bool outside =
        x < minX - margin || x > maxX + margin ||
        y < minY - margin || y > maxY + margin;
    if (!outside)
        return res;

    auto fresh = neighbourhood(cell.cx, cell.cy);
    for (const auto &c : sub.cells)
    {
        if (fresh.find(c) == fresh.end())
        {
            res.left.push_back(c);
            auto mit = cellMembers_.find(c);
            if (mit != cellMembers_.end())
            {
                mit->second.erase(clientId);
                if (mit->second.empty())
                    cellMembers_.erase(mit);
            }
        }
    }
    for (const auto &c : fresh)
    {
        if (sub.cells.find(c) == sub.cells.end())
        {
            res.entered.push_back(c);
            cellMembers_[c].insert(clientId);
        }
    }
    sub.cells = std::move(fresh);
    sub.anchorCx = cell.cx;
    sub.anchorCy = cell.cy;
    ++resubscribes_;
    return res;
}

void
InterestManager::watch(int clientId, int mobUid)
{
    if (clientId <= 0 || mobUid <= 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_)
        return;
    watches_[clientId][mobUid] = std::chrono::steady_clock::now();
}

void
InterestManager::unwatch(int clientId, int mobUid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = watches_.find(clientId);
    if (it == watches_.end())
        return;
    it->second.erase(mobUid);
    if (it->second.empty())
        watches_.erase(it);
}

std::unordered_map<int, std::vector<int>>
InterestManager::watchIndex()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<int, std::vector<int>> out;
    if (!enabled_)
        return out;
    const auto now = std::chrono::steady_clock::now();
    for (auto &[clientId, mobs] : watches_)
    {
        for (auto it = mobs.begin(); it != mobs.end();)
        {
            if (now - it->second > std::chrono::seconds(WATCH_TTL_SEC))
                it = mobs.erase(it);
            else
            {
                out[it->first].push_back(clientId);
                ++it;
            }
        }
    }
    return out;
}

void
InterestManager::setSnapshots(bool enabled, int64_t cooldownMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_ = enabled;
    if (cooldownMs >= 0)
        snapshotCooldownMs_ = cooldownMs;
}

bool
InterestManager::snapshotAllowed(int clientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || !snapshots_)
        return false;
    const auto now = std::chrono::steady_clock::now();
    auto it = lastSnapshot_.find(clientId);
    if (it != lastSnapshot_.end() &&
        now - it->second < std::chrono::milliseconds(snapshotCooldownMs_))
    {
        ++snapshotsSkipped_;
        return false;
    }
    lastSnapshot_[clientId] = now;
    ++snapshotsSent_;
    return true;
}

void
InterestManager::removeClient(int clientId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    watches_.erase(clientId);
    lastSnapshot_.erase(clientId);
    auto it = subs_.find(clientId);
    if (it == subs_.end())
        return;
    for (const auto &c : it->second.cells)
    {
        auto mit = cellMembers_.find(c);
        if (mit != cellMembers_.end())
        {
            mit->second.erase(clientId);
            if (mit->second.empty())
                cellMembers_.erase(mit);
        }
    }
    subs_.erase(it);
}

std::vector<int>
InterestManager::getSubscribers(const CellKey &cell) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> out;
    if (!enabled_)
        return out;
    auto it = cellMembers_.find(cell);
    if (it == cellMembers_.end())
        return out;
    out.assign(it->second.begin(), it->second.end());
    return out;
}

std::vector<int>
InterestManager::recipientsFor(float x, float y,
    const std::vector<std::pair<int, bool>> &clients,
    int excludeClientId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> out;
    if (!enabled_)
        return out;
    const CellKey cell = cellFor(x, y, cellSize_);
    std::unordered_set<int> seen;
    auto emit = [&](int id)
    {
        if (id <= 0 || id == excludeClientId)
            return;
        if (seen.insert(id).second)
            out.push_back(id);
    };
    auto it = cellMembers_.find(cell);
    if (it != cellMembers_.end())
        for (int id : it->second)
            emit(id);
    for (const auto &[id, worldReady] : clients)
    {
        if (!worldReady || subs_.find(id) == subs_.end())
            emit(id);
    }
    return out;
}

std::vector<InterestManager::CellKey>
InterestManager::getClientCells(int clientId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<InterestManager::CellKey> out;
    auto it = subs_.find(clientId);
    if (it == subs_.end())
        return out;
    out.assign(it->second.cells.begin(), it->second.cells.end());
    return out;
}

void
InterestManager::recordFanout(uint64_t mobsBuilt, uint64_t eventsPushed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    tickMobs_ += mobsBuilt;
    tickEvents_ += eventsPushed;
}

InterestManager::Stats
InterestManager::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.trackedClients = subs_.size();
    s.liveCells = cellMembers_.size();
    for (const auto &[cell, members] : cellMembers_)
        s.totalSubs += members.size();
    s.resubscribes = resubscribes_;
    s.tickMobs = tickMobs_;
    s.tickEvents = tickEvents_;
    s.snapshotsSent = snapshotsSent_;
    s.snapshotsSkipped = snapshotsSkipped_;
    return s;
}
