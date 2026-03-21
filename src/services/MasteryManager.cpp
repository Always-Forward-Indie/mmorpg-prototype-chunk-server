#include "services/MasteryManager.hpp"
#include "data/DataStructs.hpp"
#include "services/GameServices.hpp"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

MasteryManager::MasteryManager(GameServices *gs)
    : gs_(gs),
      log_(spdlog::get("chunk-server"))
{
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void
MasteryManager::loadCharacterMasteries(int characterId,
    const std::unordered_map<std::string, float> &masteries)
{
    std::unique_lock lk(mutex_);
    data_[characterId] = masteries;
    hitCounters_[characterId]; // ensure map exists
    log_->info("[Mastery] Loaded {} entries for char={}", masteries.size(), characterId);
}

void
MasteryManager::unloadCharacterMasteries(int characterId)
{
    std::unique_lock lk(mutex_);
    data_.erase(characterId);
    hitCounters_.erase(characterId);
}

// ── Query ──────────────────────────────────────────────────────────────────

float
MasteryManager::getMasteryValue(int characterId,
    const std::string &masterySlug) const
{
    std::shared_lock lk(mutex_);
    auto cit = data_.find(characterId);
    if (cit == data_.end())
        return 0.0f;
    auto mit = cit->second.find(masterySlug);
    return mit != cit->second.end() ? mit->second : 0.0f;
}

void
MasteryManager::fillMasteryContext(int characterId,
    PlayerContextStruct &ctx) const
{
    std::shared_lock lk(mutex_);
    auto cit = data_.find(characterId);
    if (cit == data_.end())
        return;
    ctx.masteries = cit->second;
}

// ── Progression ────────────────────────────────────────────────────────────

float
MasteryManager::calculateDelta(float currentValue, int charLevel, int targetLevel) const
{
    const float base = (gs_)
                           ? gs_->getGameConfigService().getFloat("mastery.base_delta", 0.5f)
                           : 0.5f;

    int diff = targetLevel - charLevel;

    float levelFactor;
    if (diff >= 3)
        levelFactor = 2.0f;
    else if (diff >= 1)
        levelFactor = 1.5f;
    else if (diff == 0)
        levelFactor = 1.0f;
    else if (diff >= -5)
        levelFactor = 0.5f;
    else
        levelFactor = 0.1f;

    // Soft cap: last 20 points are very slow
    if (currentValue > 80.0f)
        levelFactor *= 0.3f;

    return base * levelFactor;
}

void
MasteryManager::onPlayerAttack(int characterId,
    const std::string &masterySlug,
    int charLevel,
    int targetLevel)
{
    if (masterySlug.empty())
        return;

    float oldValue = 0.0f;
    float newValue = 0.0f;
    bool shouldFlush = false;

    {
        std::unique_lock lk(mutex_);
        auto &val = data_[characterId][masterySlug];
        oldValue = val;
        float delta = calculateDelta(val, charLevel, targetLevel);
        val = std::min(val + delta, 100.0f);
        newValue = val;

        auto &counter = hitCounters_[characterId][masterySlug];
        ++counter;

        const int flushEvery = (gs_)
                                   ? gs_->getGameConfigService().getInt("mastery.db_flush_every_hits", 10)
                                   : 10;

        // Check tier boundaries outside the integer modulo to handle rounding
        const float t1 = (gs_) ? gs_->getGameConfigService().getFloat("mastery.tier1_value", 20.f) : 20.f;
        const float t2 = (gs_) ? gs_->getGameConfigService().getFloat("mastery.tier2_value", 50.f) : 50.f;
        const float t3 = (gs_) ? gs_->getGameConfigService().getFloat("mastery.tier3_value", 80.f) : 80.f;
        const float t4 = (gs_) ? gs_->getGameConfigService().getFloat("mastery.tier4_value", 100.f) : 100.f;

        bool tierCrossed = (oldValue < t1 && newValue >= t1) || (oldValue < t2 && newValue >= t2) || (oldValue < t3 && newValue >= t3) || (oldValue < t4 && newValue >= t4);

        shouldFlush = (counter % flushEvery == 0) || tierCrossed;
    }

    // Apply milestone effects (outside lock)
    checkAndApplyMilestone(characterId, masterySlug, oldValue, newValue);

    if (shouldFlush)
        persist(characterId, masterySlug, newValue);
}

void
MasteryManager::checkAndApplyMilestone(int characterId,
    const std::string &masterySlug,
    float oldValue,
    float newValue)
{
    if (!gs_)
        return;

    auto &cfg = gs_->getGameConfigService();
    const float t1 = cfg.getFloat("mastery.tier1_value", 20.f);
    const float t2 = cfg.getFloat("mastery.tier2_value", 50.f);
    const float t3 = cfg.getFloat("mastery.tier3_value", 80.f);
    const float t4 = cfg.getFloat("mastery.tier4_value", 100.f);

    auto applyEffect = [&](const std::string &effectSlug,
                           const std::string &attrSlug,
                           float value)
    {
        ActiveEffectStruct eff;
        eff.effectSlug = effectSlug;
        eff.attributeSlug = attrSlug;
        eff.value = value;
        eff.sourceType = "mastery";
        eff.expiresAt = 0; // permanent
        eff.tickMs = 0;    // stat modifier, not DoT
        try
        {
            gs_->getCharacterManager().addActiveEffect(characterId, eff);
            gs_->getStatsNotificationService().sendStatsUpdate(characterId);
            gs_->getStatsNotificationService().sendWorldNotification(
                characterId, "mastery_tier_up", nlohmann::json{{"masterySlug", masterySlug}, {"tier", effectSlug}});
        }
        catch (...)
        {
        }
    };

    if (oldValue < t1 && newValue >= t1)
        applyEffect(masterySlug + "_t1_damage", "physical_attack", 0.01f);
    if (oldValue < t2 && newValue >= t2)
        applyEffect(masterySlug + "_t2_damage", "physical_attack", 0.04f); // additive +4% (total 5%)
    if (oldValue < t3 && newValue >= t3)
        applyEffect(masterySlug + "_t3_crit", "crit_chance", 0.03f);
    if (oldValue < t4 && newValue >= t4)
        applyEffect(masterySlug + "_t4_parry", "parry_chance", 0.02f);
}

// ── Persistence ────────────────────────────────────────────────────────────

void
MasteryManager::persist(int characterId,
    const std::string &masterySlug,
    float value)
{
    if (!saveCallback_)
        return;
    try
    {
        nlohmann::json pkt;
        pkt["header"]["eventType"] = "saveMastery";
        pkt["body"]["characterId"] = characterId;
        pkt["body"]["masterySlug"] = masterySlug;
        pkt["body"]["value"] = value;
        saveCallback_(pkt.dump() + "\n");
    }
    catch (const std::exception &e)
    {
        log_->error("[Mastery] persist error: {}", e.what());
    }
}
