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

void
MasteryManager::loadMasteryDefinitions(const std::vector<MasteryDefinitionStruct> &defs)
{
    std::unique_lock lk(mutex_);
    for (const auto &d : defs)
        definitions_[d.slug] = d;
    log_->info("[Mastery] Loaded {} static definitions", defs.size());
}

std::string
MasteryManager::getTargetAttribute(const std::string &masterySlug) const
{
    // definitions_ is populated once on startup and never modified; shared_lock is safe.
    std::shared_lock lk(mutex_);
    auto it = definitions_.find(masterySlug);
    return it != definitions_.end() ? it->second.targetAttributeSlug : "physical_attack";
}

void
MasteryManager::reapplyMilestoneEffects(int characterId)
{
    if (!gs_)
        return;

    // Snapshot mastery data under lock, then apply effects outside.
    std::unordered_map<std::string, float> snapshot;
    {
        std::shared_lock lk(mutex_);
        auto cit = data_.find(characterId);
        if (cit == data_.end())
            return;
        snapshot = cit->second;
    }

    auto &cfg = gs_->getGameConfigService();
    const float t1 = cfg.getFloat("mastery.tier1_value", 20.f);
    const float t2 = cfg.getFloat("mastery.tier2_value", 50.f);
    const float t3 = cfg.getFloat("mastery.tier3_value", 80.f);
    const float t4 = cfg.getFloat("mastery.tier4_value", 100.f);

    for (const auto &[masterySlug, value] : snapshot)
    {
        if (value >= t1)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t1_damage";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = getTargetAttribute(masterySlug);
            eff.value = 1.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            gs_->getCharacterManager().addActiveEffect(characterId, eff);
        }
        if (value >= t2)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t2_damage";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = getTargetAttribute(masterySlug);
            eff.value = 4.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            gs_->getCharacterManager().addActiveEffect(characterId, eff);
        }
        if (value >= t3)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t3_crit";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = "crit_chance";
            eff.value = 3.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            gs_->getCharacterManager().addActiveEffect(characterId, eff);
        }
        if (value >= t4)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t4_parry";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = "parry_chance";
            eff.value = 2.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            gs_->getCharacterManager().addActiveEffect(characterId, eff);
        }
    }

    log_->info("[Mastery] Reapplied milestone effects for char={}, {} masteries checked", characterId, snapshot.size());
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

std::unordered_map<std::string, float>
MasteryManager::getAllMasteries(int characterId) const
{
    std::shared_lock lk(mutex_);
    auto cit = data_.find(characterId);
    if (cit == data_.end())
        return {};
    return cit->second;
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
                           float value,
                           int tierIndex)
    {
        ActiveEffectStruct eff;
        eff.effectSlug = effectSlug;
        eff.effectTypeSlug = "buff";
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

            // Title auto-grant: check mastery conditions
            nlohmann::json titleEvent;
            titleEvent["masterySlug"] = masterySlug;
            titleEvent["tierIndex"] = tierIndex;
            gs_->getTitleManager().checkAndGrantTitles(characterId, "mastery", titleEvent);
        }
        catch (...)
        {
        }
    };

    if (oldValue < t1 && newValue >= t1)
        applyEffect(masterySlug + "_t1_damage", getTargetAttribute(masterySlug), 1.0f, 1);
    if (oldValue < t2 && newValue >= t2)
        applyEffect(masterySlug + "_t2_damage", getTargetAttribute(masterySlug), 4.0f, 2); // additive +4 (total +5)
    if (oldValue < t3 && newValue >= t3)
        applyEffect(masterySlug + "_t3_crit", "crit_chance", 3.0f, 3);
    if (oldValue < t4 && newValue >= t4)
        applyEffect(masterySlug + "_t4_parry", "parry_chance", 2.0f, 4);
}

// ── Persistence ────────────────────────────────────────────────────────────

void
MasteryManager::persist(int characterId,
    const std::string &masterySlug,
    float value)
{
    if (saveCallback_)
    {
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

    // Notify client with incremental progress update
    if (clientNotifyCallback_)
    {
        try
        {
            clientNotifyCallback_(characterId, masterySlug, value, "");
        }
        catch (const std::exception &e)
        {
            log_->error("[Mastery] clientNotifyCallback error: {}", e.what());
        }
    }
}
