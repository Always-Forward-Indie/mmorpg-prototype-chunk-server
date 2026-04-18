#include "services/CombatCalculator.hpp"
#include <algorithm>
#include <cmath>

CombatCalculator::CombatCalculator(GameConfigService *configService)
    : gen_(rd_()), dis_(0.0f, 1.0f), gameConfig_(configService)
{
}

void
CombatCalculator::setGameConfigService(GameConfigService *configService)
{
    gameConfig_ = configService;
}

float
CombatCalculator::cfg(const std::string &key, float defaultValue) const
{
    if (!gameConfig_)
        return defaultValue;
    return gameConfig_->getFloat(key, defaultValue);
}

std::vector<CharacterAttributeStruct>
CombatCalculator::mergeEffects(const CharacterDataStruct &character) const
{
    auto attrs = character.attributes; // copy base attributes

    if (character.activeEffects.empty())
        return attrs;

    const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                               .count();

    for (const auto &eff : character.activeEffects)
    {
        if (eff.attributeSlug.empty())
            continue; // non-stat effect
        if (eff.expiresAt != 0 && eff.expiresAt <= nowSec)
            continue; // expired
        // DoT/HoT are not stat modifiers — handled by tickEffects()
        if (eff.effectTypeSlug == "dot" || eff.effectTypeSlug == "hot")
            continue;

        auto it = std::find_if(attrs.begin(), attrs.end(), [&](const CharacterAttributeStruct &a)
            { return a.slug == eff.attributeSlug; });
        if (it != attrs.end())
        {
            it->value += static_cast<int>(eff.value);
        }
        else
        {
            CharacterAttributeStruct newAttr;
            newAttr.slug = eff.attributeSlug;
            newAttr.value = static_cast<int>(eff.value);
            attrs.push_back(std::move(newAttr));
        }
    }
    return attrs;
}

DamageCalculationStruct
CombatCalculator::calculateSkillDamage(
    const SkillStruct &skill,
    const CharacterDataStruct &attacker,
    const CharacterDataStruct &target)
{
    DamageCalculationStruct result;
    result.damageType = (skill.school == "physical") ? "physical" : "magical";

    // Apply non-expired active effects to base attributes
    const auto effAtk = mergeEffects(attacker);
    const auto effTgt = mergeEffects(target);

    // Level difference modifier
    const int rawDiff = attacker.characterLevel - target.characterLevel;
    const int cap = static_cast<int>(cfg("combat.level_diff_cap", 10.0f));
    const int levelDiff = std::clamp(rawDiff, -cap, cap);
    const float hitMod = levelDiff * cfg("combat.level_diff_hit_per_level", 0.02f);
    const float dmgMod = 1.0f + levelDiff * cfg("combat.level_diff_damage_per_level", 0.04f);

    // Проверка промаха
    result.isMissed = rollMiss(effAtk, effTgt, hitMod);
    if (result.isMissed)
    {
        return result;
    }

    // Расчет базового урона (с разбросом ±variance из конфига)
    result.baseDamage = calculateBaseDamage(skill, effAtk);

    // Проверка критического удара
    result.isCritical = rollCriticalHit(effAtk);
    if (result.isCritical)
    {
        float critMultiplierPct = static_cast<float>(getAttributeValue(effAtk, "crit_multiplier"));
        if (critMultiplierPct <= 0.0f)
            critMultiplierPct = cfg("combat.default_crit_multiplier", 200.0f);
        result.scaledDamage = static_cast<int>(result.baseDamage * (critMultiplierPct / 100.0f));
    }
    else
    {
        result.scaledDamage = result.baseDamage;
    }

    // Проверка блокирования
    result.isBlocked = rollBlock(effTgt);
    if (result.isBlocked)
    {
        int blockValue = getAttributeValue(effTgt, "block_value");
        result.scaledDamage = std::max(0, result.scaledDamage - blockValue);
    }

    // Бонус/штраф за разницу уровней (после блока, до защиты)
    result.scaledDamage = std::max(0, static_cast<int>(result.scaledDamage * dmgMod));

    // Применение защиты
    int defenseValue;
    if (result.damageType == "physical")
    {
        defenseValue = getAttributeValue(effTgt, "physical_defense");
    }
    else
    {
        defenseValue = getAttributeValue(effTgt, "magical_defense");
    }

    result.totalDamage = applyDefense(result.scaledDamage, defenseValue, result.damageType, target.characterLevel);

    // Сопротивление школы: {school}_resistance — дополнительный % от пост-защитного урона
    {
        int resistRaw = getAttributeValue(effTgt, skill.school + "_resistance");
        const float maxResCap = cfg("combat.max_resistance_cap", 75.0f);
        const float resistPct = std::min(static_cast<float>(resistRaw), maxResCap) / 100.0f;
        result.totalDamage = std::max(0, static_cast<int>(result.totalDamage * (1.0f - resistPct)));
    }

    return result;
}

DamageCalculationStruct
CombatCalculator::calculateMobSkillDamage(
    const SkillStruct &skill,
    const MobDataStruct &attacker,
    const CharacterDataStruct &target)
{
    DamageCalculationStruct result;
    result.damageType = (skill.school == "physical") ? "physical" : "magical";

    // Apply non-expired active effects to target's base attributes
    const auto effTgt = mergeEffects(target);

    // Level difference modifier (mob level - character level)
    const int rawDiff = attacker.level - target.characterLevel;
    const int cap = static_cast<int>(cfg("combat.level_diff_cap", 10.0f));
    const int levelDiff = std::clamp(rawDiff, -cap, cap);
    const float hitMod = levelDiff * cfg("combat.level_diff_hit_per_level", 0.02f);
    const float dmgMod = 1.0f + levelDiff * cfg("combat.level_diff_damage_per_level", 0.04f);

    // Промах: accuracy моба vs evasion игрока — симметрично calculateSkillDamage
    result.isMissed = rollMiss(attacker.attributes, effTgt, hitMod);
    if (result.isMissed)
    {
        return result;
    }

    // Базовый урон
    result.baseDamage = calculateBaseDamage(skill, attacker.attributes);

    // Критический удар из атрибутов моба (crit_chance, crit_multiplier)
    result.isCritical = rollCriticalHit(attacker.attributes);
    if (result.isCritical)
    {
        float critMultiplierPct = static_cast<float>(getAttributeValue(attacker.attributes, "crit_multiplier"));
        if (critMultiplierPct <= 0.0f)
            critMultiplierPct = cfg("combat.default_crit_multiplier", 200.0f);
        result.scaledDamage = static_cast<int>(result.baseDamage * (critMultiplierPct / 100.0f));
    }
    else
    {
        result.scaledDamage = result.baseDamage;
    }

    // Блок только у персонажа (мобы не блокируют)
    result.isBlocked = rollBlock(effTgt);
    if (result.isBlocked)
    {
        int blockValue = getAttributeValue(effTgt, "block_value");
        result.scaledDamage = std::max(0, result.scaledDamage - blockValue);
    }

    // Бонус/штраф за разницу уровней
    result.scaledDamage = std::max(0, static_cast<int>(result.scaledDamage * dmgMod));

    // Применение защиты
    int defenseValue;
    if (result.damageType == "physical")
    {
        defenseValue = getAttributeValue(effTgt, "physical_defense");
    }
    else
    {
        defenseValue = getAttributeValue(effTgt, "magical_defense");
    }

    result.totalDamage = applyDefense(result.scaledDamage, defenseValue, result.damageType, target.characterLevel);

    // Сопротивление школы: {school}_resistance — дополнительный % от пост-защитного урона
    {
        int resistRaw = getAttributeValue(effTgt, skill.school + "_resistance");
        const float maxResCap = cfg("combat.max_resistance_cap", 75.0f);
        const float resistPct = std::min(static_cast<float>(resistRaw), maxResCap) / 100.0f;
        result.totalDamage = std::max(0, static_cast<int>(result.totalDamage * (1.0f - resistPct)));
    }

    return result;
}

int
CombatCalculator::calculateHealAmount(
    const SkillStruct &skill,
    const std::vector<CharacterAttributeStruct> &casterAttributes)
{
    // Healing scales off skill.scaleStat (e.g. "healing_power", "spirit", "intellect")
    // but is NOT reduced by the target's armour or resistances.
    // A separate heal-variance config key (default 0.10 = ±10%) ensures heals are
    // slightly randomised but more predictable than damage.
    int scaleStat = getAttributeValue(casterAttributes, skill.scaleStat);
    int rawHeal = static_cast<int>(skill.flatAdd + (scaleStat * skill.coeff));
    rawHeal = std::max(1, rawHeal);

    const float variance = cfg("combat.heal_variance", 0.10f);
    float factor = 1.0f + (dis_(gen_) * 2.0f - 1.0f) * variance;
    return std::max(1, static_cast<int>(rawHeal * factor));
}

int
CombatCalculator::calculateBaseDamage(
    const SkillStruct &skill,
    const std::vector<CharacterAttributeStruct> &attackerAttributes)
{
    int scaleStatValue = getAttributeValue(attackerAttributes, skill.scaleStat);
    int rawDamage = static_cast<int>(skill.flatAdd + (scaleStatValue * skill.coeff));
    rawDamage = std::max(1, rawDamage);

    // Разброс урона ±N%: каждый удар не должен быть детерминированным числом
    const float variance = cfg("combat.damage_variance", 0.12f);
    float factor = 1.0f + (dis_(gen_) * 2.0f - 1.0f) * variance; // [1-v, 1+v]
    return std::max(1, static_cast<int>(rawDamage * factor));
}

int
CombatCalculator::calculateBaseDamage(
    const SkillStruct &skill,
    const std::vector<MobAttributeStruct> &attackerAttributes)
{
    int scaleStatValue = getAttributeValue(attackerAttributes, skill.scaleStat);
    int rawDamage = static_cast<int>(skill.flatAdd + (scaleStatValue * skill.coeff));
    rawDamage = std::max(1, rawDamage);

    const float variance = cfg("combat.damage_variance", 0.12f);
    float factor = 1.0f + (dis_(gen_) * 2.0f - 1.0f) * variance;
    return std::max(1, static_cast<int>(rawDamage * factor));
}

bool
CombatCalculator::rollCriticalHit(const std::vector<CharacterAttributeStruct> &attackerAttributes)
{
    int critChance = getAttributeValue(attackerAttributes, "crit_chance");
    const float cap = cfg("combat.crit_chance_cap", 75.0f);
    float effective = std::min(static_cast<float>(critChance), cap);
    return dis_(gen_) < (effective / 100.0f);
}

bool
CombatCalculator::rollCriticalHit(const std::vector<MobAttributeStruct> &attackerAttributes)
{
    int critChance = getAttributeValue(attackerAttributes, "crit_chance");
    const float cap = cfg("combat.crit_chance_cap", 75.0f);
    float effective = std::min(static_cast<float>(critChance), cap);
    return dis_(gen_) < (effective / 100.0f);
}

bool
CombatCalculator::rollBlock(const std::vector<CharacterAttributeStruct> &targetAttributes)
{
    int blockChance = getAttributeValue(targetAttributes, "block_chance");
    const float cap = cfg("combat.block_chance_cap", 75.0f);
    float effective = std::min(static_cast<float>(blockChance), cap);
    return dis_(gen_) < (effective / 100.0f);
}

bool
CombatCalculator::rollMiss(
    const std::vector<CharacterAttributeStruct> &attackerAttributes,
    const std::vector<CharacterAttributeStruct> &targetAttributes,
    float hitModifier)
{
    int accuracy = getAttributeValue(attackerAttributes, "accuracy");
    int evasion = getAttributeValue(targetAttributes, "evasion");

    const float baseHit = cfg("combat.base_hit_chance", 0.95f);
    const float minHit = cfg("combat.hit_chance_min", 0.05f);
    const float maxHit = cfg("combat.hit_chance_max", 0.95f);

    float hitChance = baseHit + (accuracy - evasion) * 0.01f + hitModifier;
    hitChance = std::clamp(hitChance, minHit, maxHit);

    return dis_(gen_) > hitChance;
}

bool
CombatCalculator::rollMiss(
    const std::vector<MobAttributeStruct> &attackerAttributes,
    const std::vector<CharacterAttributeStruct> &targetAttributes,
    float hitModifier)
{
    int accuracy = getAttributeValue(attackerAttributes, "accuracy");
    int evasion = getAttributeValue(targetAttributes, "evasion");

    const float baseHit = cfg("combat.base_hit_chance", 0.95f);
    const float minHit = cfg("combat.hit_chance_min", 0.05f);
    const float maxHit = cfg("combat.hit_chance_max", 0.95f);

    float hitChance = baseHit + (accuracy - evasion) * 0.01f + hitModifier;
    hitChance = std::clamp(hitChance, minHit, maxHit);

    return dis_(gen_) > hitChance;
}

int
CombatCalculator::getAttributeValue(const std::vector<CharacterAttributeStruct> &attributes, const std::string &slug)
{
    for (const auto &attr : attributes)
    {
        if (attr.slug == slug)
        {
            return attr.value;
        }
    }
    return 0;
}

int
CombatCalculator::getAttributeValue(const std::vector<MobAttributeStruct> &attributes, const std::string &slug)
{
    for (const auto &attr : attributes)
    {
        if (attr.slug == slug)
        {
            return attr.value;
        }
    }
    return 0;
}

int
CombatCalculator::applyDefense(int damage, int defenseValue, const std::string &damageType, int targetLevel)
{
    // Формула убывающей доходности: reduction = armor / (armor + K * targetLevel)
    // K и cap читаются из GameConfigService (если недоступен — используются хардкодные значения).
    const float K = cfg("combat.defense_formula_k", 7.5f);
    const float cap = cfg("combat.defense_cap", 0.85f);

    const int effectiveLevel = std::max(1, targetLevel);
    float damageReduction = static_cast<float>(defenseValue) /
                            (static_cast<float>(defenseValue) + K * static_cast<float>(effectiveLevel));
    damageReduction = std::clamp(damageReduction, 0.0f, cap);

    int finalDamage = static_cast<int>(damage * (1.0f - damageReduction));
    return std::max(0, finalDamage);
}
