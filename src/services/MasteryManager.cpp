#include "services/MasteryManager.hpp"
#include "data/DataStructs.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/IStatsNotifier.hpp"
#include "services/TitleManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

MasteryManager::MasteryManager(CharacterManager &characters,
    GameConfigService &gameConfig,
    TitleManager *titles,
    IStatsNotifier *statsNotify,
    Logger &logger)
    : characters_(characters),
      gameConfig_(gameConfig),
      titles_(titles),
      statsNotify_(statsNotify),
      logger_(logger)
{
    log_ = logger_.getSystem("mastery");
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
    // Flush everything first: periodic persist runs only every N hits, so
    // the last <N hits would otherwise be lost on every logout. Quiet
    // (no client notify — the session is going away).
    std::unordered_map<std::string, float> pending;
    {
        std::unique_lock lk(mutex_);
        auto it = data_.find(characterId);
        if (it != data_.end())
            pending = it->second;
        data_.erase(characterId);
        hitCounters_.erase(characterId);
    }
    for (const auto &[slug, value] : pending)
        sendSave(characterId, slug, value);
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
    // Snapshot mastery data under lock, then apply effects outside.
    std::unordered_map<std::string, float> snapshot;
    {
        std::shared_lock lk(mutex_);
        auto cit = data_.find(characterId);
        if (cit == data_.end())
            return;
        snapshot = cit->second;
    }

    const MasteryTiers tiers = this->tiers();

    for (const auto &[masterySlug, value] : snapshot)
    {
        if (value >= tiers.t1)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t1_damage";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = getTargetAttribute(masterySlug);
            eff.value = 1.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            characters_.addActiveEffect(characterId, eff);
        }
        if (value >= tiers.t2)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t2_damage";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = getTargetAttribute(masterySlug);
            eff.value = 4.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            characters_.addActiveEffect(characterId, eff);
        }
        if (value >= tiers.t3)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t3_crit";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = "crit_chance";
            eff.value = 3.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            characters_.addActiveEffect(characterId, eff);
        }
        if (value >= tiers.t4)
        {
            ActiveEffectStruct eff;
            eff.effectSlug = masterySlug + "_t4_parry";
            eff.effectTypeSlug = "buff";
            eff.attributeSlug = "parry_chance";
            eff.value = 2.0f;
            eff.sourceType = "mastery";
            eff.expiresAt = 0;
            eff.tickMs = 0;
            characters_.addActiveEffect(characterId, eff);
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

MasteryManager::MasteryTiers
MasteryManager::tiers() const
{
    MasteryTiers t;
    t.t1 = gameConfig_.getFloat("mastery.tier1_value", t.t1);
    t.t2 = gameConfig_.getFloat("mastery.tier2_value", t.t2);
    t.t3 = gameConfig_.getFloat("mastery.tier3_value", t.t3);
    t.t4 = gameConfig_.getFloat("mastery.tier4_value", t.t4);
    return t;
}

float
MasteryManager::calculateDelta(float currentValue, int charLevel, int targetLevel) const
{
    const float base = gameConfig_.getFloat("mastery.base_delta", 0.5f);

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

        const int flushEvery = gameConfig_.getInt("mastery.db_flush_every_hits", 10);

        // Check tier boundaries outside the integer modulo to handle rounding
        const MasteryTiers tiers = this->tiers();

        bool tierCrossed = (oldValue < tiers.t1 && newValue >= tiers.t1) || (oldValue < tiers.t2 && newValue >= tiers.t2) || (oldValue < tiers.t3 && newValue >= tiers.t3) || (oldValue < tiers.t4 && newValue >= tiers.t4);

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
    const MasteryTiers tiers = this->tiers();

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
            characters_.addActiveEffect(characterId, eff);
            if (statsNotify_ != nullptr)
                statsNotify_->sendStatsUpdate(characterId);
            if (statsNotify_ != nullptr)
                statsNotify_->sendWorldNotification(
                    characterId, "mastery_tier_up", nlohmann::json{{"masterySlug", masterySlug}, {"tier", effectSlug}});

            // Title auto-grant: check mastery conditions
            if (titles_ != nullptr)
            {
                nlohmann::json titleEvent;
                titleEvent["masterySlug"] = masterySlug;
                titleEvent["tierIndex"] = tierIndex;
                titles_->checkAndGrantTitles(characterId, "mastery", titleEvent);
            }
        }
        catch (const std::exception &e)
        {
            // Warn, not debug: the tier bonus may be partially applied
            // (effect without notify/title). Needs eyes, keeps running.
            log_->warn("[Mastery] milestone side-effects failed for char={} mastery={} tier={} ({}), bonus may be partial",
                characterId, masterySlug, effectSlug, e.what());
        }
        catch (...)
        {
            log_->warn("[Mastery] milestone side-effects failed for char={} mastery={} tier={} (unknown), bonus may be partial",
                characterId, masterySlug, effectSlug);
        }
    };

    if (oldValue < tiers.t1 && newValue >= tiers.t1)
        applyEffect(masterySlug + "_t1_damage", getTargetAttribute(masterySlug), 1.0f, 1);
    if (oldValue < tiers.t2 && newValue >= tiers.t2)
        applyEffect(masterySlug + "_t2_damage", getTargetAttribute(masterySlug), 4.0f, 2); // additive +4 (total +5)
    if (oldValue < tiers.t3 && newValue >= tiers.t3)
        applyEffect(masterySlug + "_t3_crit", "crit_chance", 3.0f, 3);
    if (oldValue < tiers.t4 && newValue >= tiers.t4)
        applyEffect(masterySlug + "_t4_parry", "parry_chance", 2.0f, 4);
}

// ── Persistence ────────────────────────────────────────────────────────────

void
MasteryManager::sendSave(int characterId,
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

void
MasteryManager::persist(int characterId,
    const std::string &masterySlug,
    float value)
{
    sendSave(characterId, masterySlug, value);

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
