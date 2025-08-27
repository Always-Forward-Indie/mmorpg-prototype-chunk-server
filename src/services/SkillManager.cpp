#include "services/SkillManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "services/MobManager.hpp"
#include <algorithm>
#include <climits>
#include <cmath>

SkillManager::SkillManager()
    : combatCalculator_(std::make_unique<CombatCalculator>()),
      gameServices_(nullptr)
{
}

SkillManager::SkillManager(GameServices *gameServices)
    : combatCalculator_(std::make_unique<CombatCalculator>()),
      gameServices_(gameServices)
{
}

void
SkillManager::setGameServices(GameServices *gameServices)
{
    gameServices_ = gameServices;
}

SkillUsageResult
SkillManager::useCharacterSkill(int casterId, const std::string &skillSlug, int targetId)
{
    SkillUsageResult result;
    result.success = false;

    if (!gameServices_)
    {
        result.errorMessage = "GameServices not initialized";
        return result;
    }

    try
    {
        // 1. Получаем данные кастера
        auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
        if (casterData.characterId == 0)
        {
            result.errorMessage = "Caster not found";
            return result;
        }

        // 2. Найти скил в списке скилов персонажа
        const SkillStruct *skill = getCharacterSkill(casterData, skillSlug);
        if (!skill)
        {
            result.errorMessage = "Skill not found";
            return result;
        }

        // 3. Проверить доступность скила
        if (!isSkillAvailable(casterId, *skill, casterData))
        {
            if (isOnCooldown(casterId, skillSlug))
            {
                result.errorMessage = "Skill is on cooldown";
            }
            else if (skill->costMp > casterData.characterCurrentMana)
            {
                result.errorMessage = "Not enough mana";
            }
            else
            {
                result.errorMessage = "Skill not available";
            }
            return result;
        }

        // 4. Получаем данные цели
        auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
        if (targetData.characterId == 0)
        {
            result.errorMessage = "Target not found";
            return result;
        }

        // 5. Проверяем дистанцию
        float distance = std::sqrt(
            std::pow(casterData.characterPosition.positionX - targetData.characterPosition.positionX, 2) +
            std::pow(casterData.characterPosition.positionY - targetData.characterPosition.positionY, 2));

        if (!isInRange(*skill, distance))
        {
            // debug distance and max range using logger
            gameServices_->getLogger().log("Distance: " + std::to_string(distance) + ", Max Range: " + std::to_string(skill->maxRange * 100.0f), YELLOW);

            result.errorMessage = "Target is out of range";
            return result;
        }

        // 6. Рассчитываем урон через CombatCalculator
        result.damageResult = combatCalculator_->calculateSkillDamage(*skill, casterData, targetData);

        // 7. Применяем эффекты к цели
        if (result.damageResult.totalDamage > 0)
        {
            int newHealth = std::max<int>(0, targetData.characterCurrentHealth - result.damageResult.totalDamage);
            gameServices_->getCharacterManager().updateCharacterHealth(targetId, newHealth);
        }

        // 8. Тратим ману кастера
        int newMana = std::max<int>(0, casterData.characterCurrentMana - skill->costMp);
        gameServices_->getCharacterManager().updateCharacterMana(casterId, newMana);

        // 9. Устанавливаем кулдаун
        setCooldown(casterId, skillSlug, skill->cooldownMs);

        result.success = true;
        return result;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error using skill: " + std::string(e.what());
        return result;
    }
}

SkillUsageResult
SkillManager::useCharacterSkillWithTargetType(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType)
{
    SkillUsageResult result;
    result.success = false;

    if (!gameServices_)
    {
        result.errorMessage = "GameServices not initialized";
        return result;
    }

    try
    {
        // 1. Получаем данные кастера
        auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
        if (casterData.characterId == 0)
        {
            result.errorMessage = "Caster not found";
            return result;
        }

        // 2. Найти скил в списке скилов персонажа
        const SkillStruct *skill = getCharacterSkill(casterData, skillSlug);
        if (!skill)
        {
            result.errorMessage = "Skill not found";
            return result;
        }

        // 3. Проверить доступность скила
        if (!isSkillAvailable(casterId, *skill, casterData))
        {
            if (isOnCooldown(casterId, skillSlug))
            {
                result.errorMessage = "Skill is on cooldown";
            }
            else if (skill->costMp > casterData.characterCurrentMana)
            {
                result.errorMessage = "Not enough mana";
            }
            else
            {
                result.errorMessage = "Skill not available";
            }
            return result;
        }

        // 4. Получаем данные цели в зависимости от типа
        float distance = 0.0f;
        if (targetType == CombatTargetType::PLAYER)
        {
            auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
            if (targetData.characterId == 0)
            {
                result.errorMessage = "Target player not found";
                return result;
            }

            // Проверяем дистанцию до игрока
            distance = std::sqrt(
                std::pow(casterData.characterPosition.positionX - targetData.characterPosition.positionX, 2) +
                std::pow(casterData.characterPosition.positionY - targetData.characterPosition.positionY, 2));

            if (!isInRange(*skill, distance))
            {
                result.errorMessage = "Target player is out of range";
                return result;
            }

            // Рассчитываем урон против игрока
            result.damageResult = combatCalculator_->calculateSkillDamage(*skill, casterData, targetData);

            // Применяем урон к игроку
            if (result.damageResult.totalDamage > 0)
            {
                int newHealth = std::max<int>(0, targetData.characterCurrentHealth - result.damageResult.totalDamage);
                gameServices_->getCharacterManager().updateCharacterHealth(targetId, newHealth);
            }
        }
        else if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
            if (mobData.uid == 0)
            {
                result.errorMessage = "Target mob not found";
                return result;
            }

            // Проверяем дистанцию до моба
            distance = std::sqrt(
                std::pow(casterData.characterPosition.positionX - mobData.position.positionX, 2) +
                std::pow(casterData.characterPosition.positionY - mobData.position.positionY, 2));

            if (!isInRange(*skill, distance))
            {
                result.errorMessage = "Target mob is out of range";
                return result;
            }

            // Рассчитываем урон против моба (создаем временную CharacterDataStruct из MobDataStruct)
            CharacterDataStruct tempTargetData;
            tempTargetData.characterId = mobData.uid;
            tempTargetData.characterMaxHealth = mobData.maxHealth;
            tempTargetData.characterCurrentHealth = mobData.currentHealth;
            tempTargetData.characterPosition = mobData.position;
            // Копируем атрибуты моба
            for (const auto &attr : mobData.attributes)
            {
                CharacterAttributeStruct charAttr;
                charAttr.name = attr.name;
                charAttr.slug = attr.slug;
                charAttr.value = attr.value;
                tempTargetData.attributes.push_back(charAttr);
            }

            result.damageResult = combatCalculator_->calculateSkillDamage(*skill, casterData, tempTargetData);

            // Применяем урон к мобу
            if (result.damageResult.totalDamage > 0)
            {
                int newHealth = std::max<int>(0, mobData.currentHealth - result.damageResult.totalDamage);
                auto updateResult = gameServices_->getMobInstanceManager().updateMobHealth(targetId, newHealth);

                if (!updateResult.success)
                {
                    result.errorMessage = "Failed to update mob health";
                    return result;
                }

                if (updateResult.wasAlreadyDead)
                {
                    result.errorMessage = "Target mob is already dead";
                    return result;
                }
            }
        }
        else if (targetType == CombatTargetType::SELF)
        {
            // Применяем скил к себе (обычно лечение или бафы)
            result.damageResult = combatCalculator_->calculateSkillDamage(*skill, casterData, casterData);

            // Если это лечение
            if (skill->skillEffectType == "heal" || skill->skillEffectType == "HEAL")
            {
                result.healAmount = result.damageResult.totalDamage; // Используем "урон" как лечение
                result.damageResult.totalDamage = 0;                 // Убираем урон для лечения

                int newHealth = std::min<int>(casterData.characterMaxHealth,
                    casterData.characterCurrentHealth + result.healAmount);
                gameServices_->getCharacterManager().updateCharacterHealth(casterId, newHealth);
            }
        }
        else
        {
            result.errorMessage = "Unsupported target type";
            return result;
        }

        // 5. Тратим ману кастера
        int newMana = std::max<int>(0, casterData.characterCurrentMana - skill->costMp);
        gameServices_->getCharacterManager().updateCharacterMana(casterId, newMana);

        // 6. Устанавливаем кулдаун
        setCooldown(casterId, skillSlug, skill->cooldownMs);

        result.success = true;
        return result;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error using skill with target type: " + std::string(e.what());
        return result;
    }
}

SkillUsageResult
SkillManager::useMobSkill(int mobId, const std::string &skillSlug, int targetId)
{
    SkillUsageResult result;
    result.success = false;

    if (!gameServices_)
    {
        result.errorMessage = "GameServices not initialized";
        return result;
    }

    try
    {
        // 1. Получаем данные моба по UID (int ID)
        auto mobData = gameServices_->getMobManager().getMobByUid(mobId);
        if (mobData.id == 0)
        {
            result.errorMessage = "Mob not found";
            return result;
        }

        // 2. Найти скил в списке скилов моба
        const SkillStruct *skill = getMobSkill(mobData, skillSlug);
        if (!skill)
        {
            result.errorMessage = "Skill not found";
            return result;
        }

        // 3. Проверить доступность скила
        if (!isSkillAvailable(mobId, *skill, mobData))
        {
            if (isOnCooldown(mobId, skillSlug))
            {
                result.errorMessage = "Skill is on cooldown";
            }
            else if (skill->costMp > mobData.currentMana)
            {
                result.errorMessage = "Not enough mana";
            }
            else
            {
                result.errorMessage = "Skill not available";
            }
            return result;
        }

        // 4. Получаем данные цели (персонажа)
        auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
        if (targetData.characterId == 0)
        {
            result.errorMessage = "Target not found";
            return result;
        }

        // 5. Проверяем дистанцию
        float distance = std::sqrt(
            std::pow(mobData.position.positionX - targetData.characterPosition.positionX, 2) +
            std::pow(mobData.position.positionY - targetData.characterPosition.positionY, 2));

        if (!isInRange(*skill, distance))
        {
            result.errorMessage = "Target is out of range";
            return result;
        }

        // 6. Рассчитываем урон через CombatCalculator (для мобов)
        result.damageResult = combatCalculator_->calculateMobSkillDamage(*skill, mobData, targetData);

        // 7. Применяем эффекты к цели
        if (result.damageResult.totalDamage > 0)
        {
            int newHealth = std::max<int>(0, targetData.characterCurrentHealth - result.damageResult.totalDamage);
            gameServices_->getCharacterManager().updateCharacterHealth(targetId, newHealth);
        }

        // 8. Тратим ману моба
        int newMana = std::max<int>(0, mobData.currentMana - skill->costMp);
        gameServices_->getMobManager().updateMobMana(mobData.uid, newMana);

        // 9. Устанавливаем кулдаун
        setCooldown(mobId, skillSlug, skill->cooldownMs);

        result.success = true;
        return result;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error using mob skill: " + std::string(e.what());
        return result;
    }
}

bool
SkillManager::isSkillAvailable(int casterId, const SkillStruct &skill, const CharacterDataStruct &casterData)
{
    // Проверка кулдауна
    if (isOnCooldown(casterId, skill.skillSlug))
    {
        return false;
    }

    // Проверка маны
    if (skill.costMp > casterData.characterCurrentMana)
    {
        return false;
    }

    return true;
}

bool
SkillManager::isSkillAvailable(int mobId, const SkillStruct &skill, const MobDataStruct &mobData)
{
    // Проверка кулдауна
    if (isOnCooldown(mobId, skill.skillSlug))
    {
        return false;
    }

    // Проверка маны
    if (skill.costMp > mobData.currentMana)
    {
        return false;
    }

    return true;
}

const SkillStruct *
SkillManager::getCharacterSkill(const CharacterDataStruct &characterData, const std::string &skillSlug)
{
    for (const auto &skill : characterData.skills)
    {
        if (skill.skillSlug == skillSlug)
        {
            return &skill;
        }
    }
    return nullptr;
}

const SkillStruct *
SkillManager::getMobSkill(const MobDataStruct &mobData, const std::string &skillSlug)
{
    for (const auto &skill : mobData.skills)
    {
        if (skill.skillSlug == skillSlug)
        {
            return &skill;
        }
    }
    return nullptr;
}

void
SkillManager::setCooldown(int casterId, const std::string &skillSlug, int cooldownMs)
{
    auto now = std::chrono::steady_clock::now();
    auto cooldownEnd = now + std::chrono::milliseconds(cooldownMs);
    cooldowns_[casterId][skillSlug] = cooldownEnd;
}

bool
SkillManager::isOnCooldown(int casterId, const std::string &skillSlug)
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

void
SkillManager::updateCooldowns()
{
    auto now = std::chrono::steady_clock::now();

    for (auto &[casterId, skills] : cooldowns_)
    {
        auto it = skills.begin();
        while (it != skills.end())
        {
            if (now >= it->second)
            {
                it = skills.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

const SkillStruct *
SkillManager::getBestSkillForMob(
    const MobDataStruct &mobData,
    const CharacterDataStruct &targetData,
    float distance)
{
    const SkillStruct *bestSkill = nullptr;
    float bestScore = 0.0f;

    for (const auto &skill : mobData.skills)
    {
        // Проверка доступности
        if (!isSkillAvailable(mobData.uid, skill, mobData))
        {
            continue;
        }

        // Проверка дистанции
        if (!isInRange(skill, distance))
        {
            continue;
        }

        // Простой алгоритм выбора лучшего скила
        float score = 0.0f;

        // Предпочитаем скилы с большим уроном
        score += skill.coeff * 10.0f + skill.flatAdd;

        // Предпочитаем скилы с меньшим кулдауном
        if (skill.cooldownMs > 0)
        {
            score += 100.0f / skill.cooldownMs;
        }

        // Предпочитаем скилы с меньшей стоимостью маны
        if (skill.costMp > 0)
        {
            score += 50.0f / skill.costMp;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestSkill = &skill;
        }
    }

    return bestSkill;
}

bool
SkillManager::isInRange(const SkillStruct &skill, float distance)
{
    return distance <= skill.maxRange * 100.0f;
}
