#include "services/WorldObjectManager.hpp"

WorldObjectManager::WorldObjectManager(Logger &logger)
    : logger_(logger)
{
}

// ── Static data ───────────────────────────────────────────────────────────────

void
WorldObjectManager::setWorldObjects(const std::vector<WorldObjectDataStruct> &objects)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    objects_.clear();
    states_.clear();

    for (const auto &obj : objects)
    {
        objects_[obj.id] = obj;

        // Initialise runtime state only for global-scope objects
        if (obj.scope == "global")
        {
            WorldObjectInstanceStruct inst;
            inst.objectId = obj.id;
            inst.state = obj.isActiveByDefault ? obj.initialState : "disabled";
            inst.respawnSec = obj.respawnSec;
            states_[obj.id] = inst;
        }
    }

    logger_.log("[WIO] Loaded " + std::to_string(objects_.size()) + " world objects.");
}

bool
WorldObjectManager::isLoaded() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return !objects_.empty();
}

WorldObjectDataStruct
WorldObjectManager::getObjectById(int objectId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = objects_.find(objectId);
    if (it == objects_.end())
        return {};
    return it->second;
}

std::vector<WorldObjectDataStruct>
WorldObjectManager::getObjectsInZone(int zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<WorldObjectDataStruct> result;
    for (const auto &[id, obj] : objects_)
    {
        if (obj.zoneId == zoneId)
            result.push_back(obj);
    }
    return result;
}

std::vector<WorldObjectDataStruct>
WorldObjectManager::getAllObjects() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<WorldObjectDataStruct> result;
    result.reserve(objects_.size());
    for (const auto &[id, obj] : objects_)
        result.push_back(obj);
    return result;
}

// ── Global runtime state ──────────────────────────────────────────────────────

std::string
WorldObjectManager::getGlobalState(int objectId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(objectId);
    if (it == states_.end())
        return "active"; // per-player objects or unknown ids default to "active"
    return it->second.state;
}

void
WorldObjectManager::depleteGlobalObject(int objectId)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(objectId);
    if (it == states_.end())
        return;
    it->second.state = "depleted";
    it->second.depletedAt = std::chrono::steady_clock::now();
}

void
WorldObjectManager::setGlobalState(int objectId, const std::string &state)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(objectId);
    if (it == states_.end())
        return;
    it->second.state = state;
    if (state == "depleted")
        it->second.depletedAt = std::chrono::steady_clock::now();
}

std::vector<int>
WorldObjectManager::tickRespawns()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::vector<int> respawned;
    const auto now = std::chrono::steady_clock::now();

    for (auto &[id, inst] : states_)
    {
        if (inst.state != "depleted" || inst.respawnSec <= 0)
            continue;

        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - inst.depletedAt)
                                 .count();

        if (elapsed >= inst.respawnSec)
        {
            inst.state = "active";
            respawned.push_back(id);
        }
    }
    return respawned;
}

// ── Channel sessions ──────────────────────────────────────────────────────────

void
WorldObjectManager::startChannelSession(int characterId, int objectId, int channelTimeSec)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ChannelSession sess;
    sess.objectId = objectId;
    sess.startedAt = std::chrono::steady_clock::now();
    sess.channelTimeSec = channelTimeSec;
    channels_[characterId] = sess;
}

int
WorldObjectManager::getActiveChannelObjectId(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = channels_.find(characterId);
    if (it == channels_.end())
        return 0;
    return it->second.objectId;
}

float
WorldObjectManager::getChannelProgress(int characterId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = channels_.find(characterId);
    if (it == channels_.end())
        return -1.0f;

    const auto &sess = it->second;
    if (sess.channelTimeSec <= 0)
        return 1.0f;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sess.startedAt)
                             .count();

    const float progress = static_cast<float>(elapsed) /
                           (static_cast<float>(sess.channelTimeSec) * 1000.0f);
    return progress > 1.0f ? 1.0f : progress;
}

bool
WorldObjectManager::removeChannelSession(int characterId)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return channels_.erase(characterId) > 0;
}

std::vector<std::pair<int, int>>
WorldObjectManager::pollCompletedChannels()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<int, int>> completed;
    const auto now = std::chrono::steady_clock::now();

    for (auto it = channels_.begin(); it != channels_.end();)
    {
        const auto &sess = it->second;
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - sess.startedAt)
                                 .count();

        if (elapsed >= sess.channelTimeSec)
        {
            completed.emplace_back(it->first, sess.objectId);
            it = channels_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return completed;
}
