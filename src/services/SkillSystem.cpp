#include "services/SkillSystem.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatCalculator.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>

SkillSystem::SkillSystem(GameServices *gameServices)
    : gameServices_(gameServices)
{
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
        const SkillStruct *skill = nullptr;
        if (casterType == CasterType::PLAYER)
        {
            skill = getCharacterSkill(casterId, skillSlug);
        }
        else
        {
            skill = getMobSkill(casterId, skillSlug);
        }

        if (!skill)
        {
            result.errorMessage = "Skill not found: " + skillSlug;
            return result;
        }

        // Проверяем доступность скила
        if (!isSkillAvailable(casterId, skillSlug))
        {
            result.errorMessage = "Skill is on cooldown or not available";
            return result;
        }

        // Проверяем ресурсы
        if (!validateResources(casterId, *skill))
        {
            result.errorMessage = "Insufficient resources";
            return result;
        }

        // Проверяем цель
        if (!validateTarget(casterId, targetId, targetType))
        {
            result.errorMessage = "Invalid target";
            return result;
        }

        // Проверяем дистанцию
        if (!isInRange(*skill, casterId, targetId, targetType))
        {
            result.errorMessage = "Target is out of range";
            return result;
        }

        // Потребляем ресурсы
        consumeResources(casterId, *skill);

        // Выполняем расчеты
        if (casterType == CasterType::PLAYER)
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);

            if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
            {
                auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                result.damageResult = combatCalculator_->calculateSkillDamage(*skill, casterData, targetData);
                result.healAmount = combatCalculator_->calculateBaseDamage(*skill, casterData.attributes); // Временно для лечения
            }
            else if (targetType == CombatTargetType::MOB)
            {
                // Урон по мобу - пока используем упрощенную логику
                result.damageResult.totalDamage = combatCalculator_->calculateBaseDamage(*skill, casterData.attributes);
                result.damageResult.isCritical = combatCalculator_->rollCriticalHit(casterData.attributes);
            }
        }
        else
        { // MOB caster
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
            auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);

            result.damageResult = combatCalculator_->calculateMobSkillDamage(*skill, mobData, targetData);
        }

        // Устанавливаем кулдаун
        setCooldown(casterId, skillSlug, skill->cooldownMs);

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

const SkillStruct *
SkillSystem::getCharacterSkill(int characterId, const std::string &skillSlug)
{
    try
    {
        auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);

        // Создаем статическую копию найденного скила для безопасного возврата
        static SkillStruct tempSkill;

        for (const auto &skill : characterData.skills)
        {
            if (skill.skillSlug == skillSlug)
            {
                tempSkill = skill; // Копируем данные
                return &tempSkill;
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error getting character skill: " + std::string(e.what()));
    }

    return nullptr;
}

const SkillStruct *
SkillSystem::getMobSkill(int mobId, const std::string &skillSlug)
{
    try
    {
        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(mobId);

        // Создаем статическую копию найденного скила для безопасного возврата
        static SkillStruct tempSkill;

        for (const auto &skill : mobData.skills)
        {
            if (skill.skillSlug == skillSlug)
            {
                tempSkill = skill; // Копируем данные
                return &tempSkill;
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error getting mob skill: " + std::string(e.what()));
    }

    return nullptr;
}

void
SkillSystem::setCooldown(int casterId, const std::string &skillSlug, int cooldownMs)
{
    auto now = std::chrono::steady_clock::now();
    auto endTime = now + std::chrono::milliseconds(cooldownMs);
    cooldowns_[casterId][skillSlug] = endTime;
}

bool
SkillSystem::isOnCooldown(int casterId, const std::string &skillSlug)
{
    auto it = cooldowns_.find(casterId);
    if (it == cooldowns_.end())
    {
        return false;
    }

    auto skillIt = it->second.find(skillSlug);
    if (skillIt == it->second.end())
    {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    return now < skillIt->second;
}

const SkillStruct *
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

    return bestSkill;
}

void
SkillSystem::updateCooldowns()
{
    auto now = std::chrono::steady_clock::now();

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
SkillSystem::isInRange(const SkillStruct &skill, int casterId, int targetId, CombatTargetType targetType)
{
    try
    {
        PositionStruct casterPos, targetPos;

        // Получаем позицию кастера
        CasterType casterType = determineCasterType(casterId);
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
SkillSystem::validateTarget(int casterId, int targetId, CombatTargetType targetType)
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
SkillSystem::validateResources(int casterId, const SkillStruct &skill)
{
    if (skill.costMp <= 0)
    {
        return true; // Нет затрат маны
    }

    try
    {
        CasterType casterType = determineCasterType(casterId);

        if (casterType == CasterType::PLAYER)
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            return casterData.characterCurrentMana >= skill.costMp;
        }
        else if (casterType == CasterType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
            return mobData.currentMana >= skill.costMp;
        }
    }
    catch (...)
    {
        return false;
    }

    return false;
}

void
SkillSystem::consumeResources(int casterId, const SkillStruct &skill)
{
    if (skill.costMp <= 0)
    {
        return; // Нет затрат
    }

    try
    {
        CasterType casterType = determineCasterType(casterId);

        if (casterType == CasterType::PLAYER)
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            int newMana = std::max(0, casterData.characterCurrentMana - skill.costMp);
            gameServices_->getCharacterManager().updateCharacterMana(casterId, newMana);
        }
        else if (casterType == CasterType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
            int newMana = std::max(0, mobData.currentMana - skill.costMp);
            gameServices_->getMobInstanceManager().updateMobMana(casterId, newMana);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error consuming resources: " + std::string(e.what()));
    }
}
