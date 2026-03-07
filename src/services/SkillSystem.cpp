#include "services/SkillSystem.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatCalculator.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <spdlog/logger.h>

SkillSystem::SkillSystem(GameServices *gameServices)
    : gameServices_(gameServices)
{
    log_ = gameServices_->getLogger().getSystem("skill");
    combatCalculator_ = std::make_unique<CombatCalculator>();
}

SkillUsageResult
SkillSystem::useSkill(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType)
{
    SkillUsageResult result;
    result.success = false;

    try
    {
        // Определяем тип кастера
        CasterType casterType = determineCasterType(casterId);

        if (casterType == CasterType::UNKNOWN)
        {
            result.errorMessage = "Unknown caster type";
            return result;
        }

        // Получаем скил
        std::optional<SkillStruct> skillOpt;
        if (casterType == CasterType::PLAYER)
        {
            log_->info("Getting character skill: " + skillSlug + " for player " + std::to_string(casterId));
            skillOpt = getCharacterSkill(casterId, skillSlug);
        }
        else
        {
            log_->info("Getting mob skill: " + skillSlug + " for mob " + std::to_string(casterId));
            skillOpt = getMobSkill(casterId, skillSlug);
        }

        if (!skillOpt.has_value())
        {
            log_->error("Skill not found in useSkill: " + skillSlug + " for caster " + std::to_string(casterId));
            result.errorMessage = "Skill not found: " + skillSlug;
            return result;
        }

        const SkillStruct &skill = skillOpt.value();
        gameServices_->getLogger().log("Skill found in useSkill: " + std::string(skill.skillName) + " (" + skill.skillSlug + ")", GREEN);

        // HIGH-1: atomic check-AND-set under a single unique_lock — prevents two
        // concurrent calls from both passing the cooldown check simultaneously.
        if (!trySetCooldown(casterId, skillSlug, skill.cooldownMs))
        {
            result.errorMessage = "Skill is on cooldown or not available";
            return result;
        }

        // Проверяем ресурсы и сразу списываем ману атомарно
        if (!tryConsumeResources(casterId, skill, casterType)) // MEDIUM-2: pass casterType
        {
            result.errorMessage = "Insufficient resources";
            return result;
        }

        // Проверяем цель
        if (!validateTarget(casterId, targetId, targetType, casterType)) // MEDIUM-2: pass casterType
        {
            result.errorMessage = "Invalid target";
            return result;
        }

        // Проверяем дистанцию
        if (!isInRange(skill, casterId, targetId, targetType, casterType)) // MEDIUM-2: pass casterType
        {
            result.errorMessage = "Target is out of range";
            return result;
        }

        // Mana already consumed by tryConsumeResources above.

        // Выполняем расчеты
        if (casterType == CasterType::PLAYER)
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);

            if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
            {
                auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                result.damageResult = combatCalculator_->calculateSkillDamage(skill, casterData, targetData);
                result.healAmount = combatCalculator_->calculateHealAmount(skill, casterData.attributes); // Heal uses dedicated formula (no armour reduction)
            }
            else if (targetType == CombatTargetType::MOB)
            {
                // Урон по мобу - пока используем упрощенную логику
                result.damageResult.totalDamage = combatCalculator_->calculateBaseDamage(skill, casterData.attributes);
                result.damageResult.isCritical = combatCalculator_->rollCriticalHit(casterData.attributes);
            }
        }
        else
        { // MOB caster
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
            auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);

            result.damageResult = combatCalculator_->calculateMobSkillDamage(skill, mobData, targetData);
        }

        result.success = true;
        // result.skillUsed = *skill; // Убираем это поле пока
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error using skill: " + std::string(e.what());
        gameServices_->getLogger().logError("SkillSystem::useSkill error: " + std::string(e.what()));
    }

    return result;
}

bool
SkillSystem::isSkillAvailable(int casterId, const std::string &skillSlug)
{
    // Проверяем кулдаун
    if (isOnCooldown(casterId, skillSlug))
    {
        return false;
    }

    // Дополнительные проверки можно добавить здесь
    return true;
}

std::optional<SkillStruct>
SkillSystem::getCharacterSkill(int characterId, const std::string &skillSlug)
{
    try
    {
        log_->info("Getting character skill " + skillSlug + " for character " + std::to_string(characterId));
        auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);
        gameServices_->getLogger().log("Character " + std::to_string(characterId) + " has " + std::to_string(characterData.skills.size()) + " skills", GREEN);

        for (const auto &skill : characterData.skills)
        {
            log_->info("Checking skill: " + skill.skillSlug + " against " + skillSlug);
            if (skill.skillSlug == skillSlug)
            {
                log_->info("Skill found: " + skill.skillName + " (" + skill.skillSlug + ")");
                // Возвращаем копию скила
                return skill;
            }
        }

        log_->error("Skill " + skillSlug + " not found for character " + std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error getting character skill: " + std::string(e.what()));
    }

    return std::nullopt;
}

std::optional<SkillStruct>
SkillSystem::getMobSkill(int mobId, const std::string &skillSlug)
{
    try
    {
        log_->info("Getting mob skill " + skillSlug + " for mob " + std::to_string(mobId));
        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(mobId);
        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " has " + std::to_string(mobData.skills.size()) + " skills", GREEN);

        for (const auto &skill : mobData.skills)
        {
            log_->info("Checking mob skill: " + skill.skillSlug + " against " + skillSlug);
            if (skill.skillSlug == skillSlug)
            {
                log_->info("Mob skill found: " + skill.skillName + " (" + skill.skillSlug + ")");
                // Возвращаем копию скила
                return skill;
            }
        }

        log_->error("Skill " + skillSlug + " not found for mob " + std::to_string(mobId));
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error getting mob skill: " + std::string(e.what()));
    }

    return std::nullopt;
}

void
SkillSystem::setCooldown(int casterId, const std::string &skillSlug, int cooldownMs)
{
    auto now = std::chrono::steady_clock::now();
    auto endTime = now + std::chrono::milliseconds(cooldownMs);
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);
    cooldowns_[casterId][skillSlug] = endTime;
}

bool
SkillSystem::trySetCooldown(int casterId, const std::string &skillSlug, int cooldownMs)
{
    // HIGH-1: single unique_lock covers both the check and the set.
    // No other thread can sneak in between, eliminating the TOCTOU race.
    auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);
    auto &entry = cooldowns_[casterId][skillSlug];
    if (now < entry)
        return false; // still on cooldown
    entry = now + std::chrono::milliseconds(cooldownMs);
    return true;
}

bool
SkillSystem::isOnCooldown(int casterId, const std::string &skillSlug)
{
    std::shared_lock<std::shared_mutex> lock(cooldownsMutex_);
    auto it = cooldowns_.find(casterId);
    if (it == cooldowns_.end())
        return false;
    auto skillIt = it->second.find(skillSlug);
    if (skillIt == it->second.end())
        return false;
    return std::chrono::steady_clock::now() < skillIt->second;
}

std::optional<std::reference_wrapper<const SkillStruct>>
SkillSystem::getBestSkillForMob(const MobDataStruct &mobData,
    const CharacterDataStruct &targetData,
    float distance)
{
    const SkillStruct *bestSkill = nullptr;
    float bestScore = -1.0f;

    for (const auto &skill : mobData.skills)
    {
        // Проверяем доступность
        if (!isSkillAvailable(mobData.uid, skill.skillSlug))
        {
            continue;
        }

        // Проверяем дистанцию
        if (distance > skill.maxRange * 100)
        {
            continue;
        }

        // Простая оценка скила
        float score = 0.0f;

        // Предпочитаем скилы с большим уроном
        if (skill.skillEffectType == "damage")
        {
            score += skill.coeff * 10.0f;
        }

        // Предпочитаем скилы с меньшим кулдауном
        score += (10000.0f - skill.cooldownMs) / 1000.0f;

        // Предпочитаем скилы с большей дистанцией если цель далеко
        if (distance > 500.0f)
        {
            score += skill.maxRange * 100 * 0.1f;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestSkill = &skill;
        }
    }

    if (!bestSkill)
        return std::nullopt;
    return std::cref(*bestSkill);
}

void
SkillSystem::updateCooldowns()
{
    auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::shared_mutex> lock(cooldownsMutex_);
    for (auto &casterCooldowns : cooldowns_)
    {
        auto it = casterCooldowns.second.begin();
        while (it != casterCooldowns.second.end())
        {
            if (now >= it->second)
            {
                it = casterCooldowns.second.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

SkillSystem::CasterType
SkillSystem::determineCasterType(int casterId)
{
    // Проверяем, является ли это игроком
    try
    {
        auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
        if (characterData.characterId != 0)
        {
            return CasterType::PLAYER;
        }
    }
    catch (...)
    {
        // Не игрок, проверяем мобов
    }

    // Проверяем, является ли это мобом
    try
    {
        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
        if (mobData.uid != 0)
        {
            return CasterType::MOB;
        }
    }
    catch (...)
    {
        // Не моб
    }

    return CasterType::UNKNOWN;
}

bool
SkillSystem::isInRange(const SkillStruct &skill, int casterId, int targetId, CombatTargetType targetType, CasterType casterType)
{
    try
    {
        PositionStruct casterPos, targetPos;

        // Получаем позицию кастера
        // MEDIUM-2: reuse pre-computed casterType, no extra determineCasterType() call
        if (casterType == CasterType::PLAYER)
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            casterPos = casterData.characterPosition;
        }
        else if (casterType == CasterType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
            casterPos = mobData.position;
        }
        else
        {
            return false;
        }

        // Получаем позицию цели
        if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
        {
            auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
            targetPos = targetData.characterPosition;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
            targetPos = mobData.position;
        }
        else
        {
            return true; // Для AREA и NONE не проверяем дистанцию
        }

        // Вычисляем дистанцию
        float dx = casterPos.positionX - targetPos.positionX;
        float dy = casterPos.positionY - targetPos.positionY;
        float distance = std::sqrt(dx * dx + dy * dy);

        return distance <= skill.maxRange * 100;
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error checking range: " + std::string(e.what()));
        return false;
    }
}

bool
SkillSystem::validateTarget(int casterId, int targetId, CombatTargetType targetType, CasterType /*casterType*/)
{
    // Базовые проверки
    if (targetType == CombatTargetType::SELF)
    {
        return casterId == targetId;
    }

    if (targetType == CombatTargetType::AREA || targetType == CombatTargetType::NONE)
    {
        return true; // Эти типы не требуют конкретной цели
    }

    // Проверяем существование цели
    try
    {
        if (targetType == CombatTargetType::PLAYER)
        {
            auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
            return targetData.characterId != 0 && targetData.characterCurrentHealth > 0;
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
            return mobData.uid != 0 && mobData.currentHealth > 0;
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

bool
SkillSystem::tryConsumeResources(int casterId, const SkillStruct &skill, CasterType casterType)
{
    if (skill.costMp <= 0)
        return true; // Нет затрат — всегда успех

    // MEDIUM-2: casterType passed in, skip determineCasterType() call
    if (casterType == CasterType::PLAYER)
        return gameServices_->getCharacterManager().trySpendMana(casterId, skill.costMp);
    else if (casterType == CasterType::MOB)
        return gameServices_->getMobInstanceManager().trySpendMana(casterId, skill.costMp);

    return false;
}
