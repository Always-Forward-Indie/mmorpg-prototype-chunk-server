#include "services/CombatCalculator.hpp"
#include <algorithm>
#include <cmath>

CombatCalculator::CombatCalculator()
    : gen_(rd_()), dis_(0.0f, 1.0f)
{
}

DamageCalculationStruct
CombatCalculator::calculateSkillDamage(
    const SkillStruct &skill,
    const CharacterDataStruct &attacker,
    const CharacterDataStruct &target)
{
    DamageCalculationStruct result;
    result.damageType = (skill.school == "physical") ? "physical" : "magical";

    // Проверка промаха
    result.isMissed = rollMiss(attacker.attributes, target.attributes);
    if (result.isMissed)
    {
        return result;
    }

    // Расчет базового урона
    result.baseDamage = calculateBaseDamage(skill, attacker.attributes);

    // Проверка критического удара
    result.isCritical = rollCriticalHit(attacker.attributes);
    if (result.isCritical)
    {
        float critMultiplier = getAttributeValue(attacker.attributes, "crit_multiplier") / 100.0f;
        if (critMultiplier == 0)
            critMultiplier = 2.0f; // Дефолтный множитель
        result.scaledDamage = static_cast<int>(result.baseDamage * critMultiplier);
    }
    else
    {
        result.scaledDamage = result.baseDamage;
    }

    // Проверка блокирования
    result.isBlocked = rollBlock(target.attributes);
    if (result.isBlocked)
    {
        int blockValue = getAttributeValue(target.attributes, "block_value");
        result.scaledDamage = std::max(0, result.scaledDamage - blockValue);
    }

    // Применение защиты
    int defenseValue;
    if (result.damageType == "physical")
    {
        defenseValue = getAttributeValue(target.attributes, "physical_defense");
    }
    else
    {
        defenseValue = getAttributeValue(target.attributes, "magical_defense");
    }

    result.totalDamage = applyDefense(result.scaledDamage, defenseValue, result.damageType);

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

    // Для моба используем упрощенную систему промахов
    if (dis_(gen_) < 0.05f)
    { // 5% шанс промаха
        result.isMissed = true;
        return result;
    }

    // Расчет базового урона
    result.baseDamage = calculateBaseDamage(skill, attacker.attributes);

    // Для мобов критический удар рассчитывается проще
    if (dis_(gen_) < 0.15f)
    { // 15% шанс крита для мобов
        result.isCritical = true;
        result.scaledDamage = static_cast<int>(result.baseDamage * 2.0f);
    }
    else
    {
        result.scaledDamage = result.baseDamage;
    }

    // Проверка блокирования
    result.isBlocked = rollBlock(target.attributes);
    if (result.isBlocked)
    {
        int blockValue = getAttributeValue(target.attributes, "block_value");
        result.scaledDamage = std::max(0, result.scaledDamage - blockValue);
    }

    // Применение защиты
    int defenseValue;
    if (result.damageType == "physical")
    {
        defenseValue = getAttributeValue(target.attributes, "physical_defense");
    }
    else
    {
        defenseValue = getAttributeValue(target.attributes, "magical_defense");
    }

    result.totalDamage = applyDefense(result.scaledDamage, defenseValue, result.damageType);

    return result;
}

int
CombatCalculator::calculateBaseDamage(
    const SkillStruct &skill,
    const std::vector<CharacterAttributeStruct> &attackerAttributes)
{
    int scaleStatValue = getAttributeValue(attackerAttributes, skill.scaleStat);
    int baseDamage = static_cast<int>(skill.flatAdd + (scaleStatValue * skill.coeff));
    return std::max(1, baseDamage); // Минимум 1 урона
}

int
CombatCalculator::calculateBaseDamage(
    const SkillStruct &skill,
    const std::vector<MobAttributeStruct> &attackerAttributes)
{
    int scaleStatValue = getAttributeValue(attackerAttributes, skill.scaleStat);
    int baseDamage = static_cast<int>(skill.flatAdd + (scaleStatValue * skill.coeff));
    return std::max(1, baseDamage); // Минимум 1 урона
}

bool
CombatCalculator::rollCriticalHit(const std::vector<CharacterAttributeStruct> &attackerAttributes)
{
    int critChance = getAttributeValue(attackerAttributes, "crit_chance");
    return dis_(gen_) < (critChance / 100.0f);
}

bool
CombatCalculator::rollBlock(const std::vector<CharacterAttributeStruct> &targetAttributes)
{
    int blockChance = getAttributeValue(targetAttributes, "block_chance");
    return dis_(gen_) < (blockChance / 100.0f);
}

bool
CombatCalculator::rollMiss(
    const std::vector<CharacterAttributeStruct> &attackerAttributes,
    const std::vector<CharacterAttributeStruct> &targetAttributes)
{
    int accuracy = getAttributeValue(attackerAttributes, "accuracy");
    int evasion = getAttributeValue(targetAttributes, "evasion");

    // Базовый шанс попадания 95%, модифицируется точностью и уклонением
    float hitChance = 0.95f + (accuracy - evasion) * 0.01f;
    hitChance = std::clamp(hitChance, 0.05f, 0.95f); // Ограничиваем от 5% до 95%

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
CombatCalculator::applyDefense(int damage, int defenseValue, const std::string &damageType)
{
    // Простая формула защиты: каждая единица защиты уменьшает урон на 1%
    float damageReduction = defenseValue * 0.01f;
    damageReduction = std::clamp(damageReduction, 0.0f, 0.75f); // Максимум 75% защиты

    int finalDamage = static_cast<int>(damage * (1.0f - damageReduction));
    return std::max(1, finalDamage); // Минимум 1 урона
}
