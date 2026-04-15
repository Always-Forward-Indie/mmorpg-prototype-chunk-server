#include "services/AmbientSpeechManager.hpp"
#include "services/DialogueConditionEvaluator.hpp"
#include <algorithm>
#include <map>

void
AmbientSpeechManager::setAmbientSpeechData(const std::vector<NPCAmbientSpeechConfigStruct> &configs)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    configs_.clear();
    for (const auto &cfg : configs)
        configs_[cfg.npcId] = cfg;
}

bool
AmbientSpeechManager::isLoaded() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return !configs_.empty();
}

nlohmann::json
AmbientSpeechManager::buildFilteredPoolForPlayer(int npcId, const PlayerContextStruct &ctx) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = configs_.find(npcId);
    if (it == configs_.end())
        return nlohmann::json{};

    const NPCAmbientSpeechConfigStruct &cfg = it->second;

    // Filter lines by condition, then group by priority (descending).
    // Use std::map with reversed comparator so highest priority comes first.
    std::map<int, std::vector<const NPCAmbientLineStruct *>, std::greater<int>> byPriority;

    for (const auto &line : cfg.lines)
    {
        bool conditionMet = true;
        if (!line.conditionGroup.is_null() && !line.conditionGroup.empty())
            conditionMet = DialogueConditionEvaluator::evaluate(line.conditionGroup, ctx);

        if (conditionMet)
            byPriority[line.priority].push_back(&line);
    }

    if (byPriority.empty())
        return nlohmann::json{}; // no valid lines for this player

    // Build pools JSON array
    nlohmann::json poolsArr = nlohmann::json::array();
    for (const auto &[priority, lines] : byPriority)
    {
        nlohmann::json linesArr = nlohmann::json::array();
        for (const auto *line : lines)
        {
            linesArr.push_back({
                {"id", line->id},
                {"lineKey", line->lineKey},
                {"triggerType", line->triggerType},
                {"triggerRadius", line->triggerRadius},
                {"weight", line->weight},
                {"cooldownSec", line->cooldownSec},
            });
        }
        poolsArr.push_back({{"priority", priority}, {"lines", std::move(linesArr)}});
    }

    nlohmann::json result;
    result["npcId"] = cfg.npcId;
    result["minIntervalSec"] = cfg.minIntervalSec;
    result["maxIntervalSec"] = cfg.maxIntervalSec;
    result["pools"] = std::move(poolsArr);
    return result;
}

nlohmann::json
AmbientSpeechManager::buildFilteredPoolsForPlayer(const std::vector<int> &npcIds,
    const PlayerContextStruct &ctx) const
{
    nlohmann::json arr = nlohmann::json::array();
    for (int npcId : npcIds)
    {
        nlohmann::json pool = buildFilteredPoolForPlayer(npcId, ctx);
        if (!pool.is_null() && pool.contains("npcId"))
            arr.push_back(std::move(pool));
    }
    return arr;
}
