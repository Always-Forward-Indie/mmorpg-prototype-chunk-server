#include "services/CombatSystem.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/SkillSystem.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <chrono>

CombatSystem::CombatSystem(GameServices *gameServices)
    : gameServices_(gameServices)
{
    skillSystem_ = std::make_unique<SkillSystem>(gameServices);
    responseBuilder_ = std::make_unique<CombatResponseBuilder>(gameServices);
    broadcastCallback_ = nullptr;
}

void
CombatSystem::setBroadcastCallback(std::function<void(const nlohmann::json &)> callback)
{
    broadcastCallback_ = callback;
}

SkillInitiationResult
CombatSystem::initiateSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType)
{
    SkillInitiationResult result;
    result.casterId = casterId;
    result.targetId = targetId;
    result.targetType = targetType;
    result.skillName = skillSlug;

    gameServices_->getLogger().log("CombatSystem::initiateSkillUsage called with skill: " + skillSlug, GREEN);

    try
    {
        // Получаем скил для проверки времени каста
        const SkillStruct *skill = nullptr;

        // Определяем тип кастера и получаем скил
        try
        {
            auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            skill = skillSystem_->getCharacterSkill(casterId, skillSlug);
        }
        catch (...)
        {
            try
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                skill = skillSystem_->getMobSkill(casterId, skillSlug);
            }
            catch (...)
            {
                result.errorMessage = "Caster not found";
                return result;
            }
        }

        if (!skill)
        {
            result.errorMessage = "Skill not found: " + skillSlug;
            return result;
        }

        // Дополнительная проверка валидности skill указателя
        gameServices_->getLogger().log("Skill found: " + std::string(skill->skillName), GREEN);

        // Заполняем информацию о скиле
        result.skillName = skill->skillName;
        result.skillEffectType = skill->skillEffectType;
        result.skillSchool = skill->school;

        // Проверяем базовые требования (без потребления ресурсов)
        if (skillSystem_->isOnCooldown(casterId, skillSlug))
        {
            result.errorMessage = "Skill is on cooldown";
            return result;
        }

        // Создаем запись о начинающемся действии
        auto action = std::make_shared<CombatActionStruct>();
        action->actionId = 0; // TODO: генерировать уникальные ID

        // Безопасное присваивание строки
        if (skill && !skill->skillName.empty())
        {
            action->actionName = skill->skillName;
        }
        else
        {
            action->actionName = skillSlug; // Fallback к slug'у
        }

        action->actionType = CombatActionType::SKILL;
        action->targetType = targetType;
        action->casterId = casterId;
        action->targetId = targetId;
        action->castTime = static_cast<float>(skill->castMs) / 1000.0f;
        action->state = (skill->castMs > 0) ? CombatActionState::CASTING : CombatActionState::EXECUTING;
        action->startTime = std::chrono::steady_clock::now();
        action->endTime = action->startTime + std::chrono::milliseconds(skill->castMs);
        action->animationName = "skill_" + skillSlug; // TODO: получать из базы
        action->animationDuration = std::max(1.0f, action->castTime);

        // Сохраняем ongoing action
        ongoingActions_[casterId] = action;

        result.success = true;
        result.castTime = action->castTime;
        result.animationName = action->animationName;
        result.animationDuration = action->animationDuration;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error initiating combat action: " + std::string(e.what());
        gameServices_->getLogger().logError("CombatSystem::initiateCombatAction error: " + std::string(e.what()));
    }

    return result;
}

SkillExecutionResult
CombatSystem::executeSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType)
{
    SkillExecutionResult result;
    result.casterId = casterId;
    result.targetId = targetId;
    result.targetType = targetType;

    try
    {
        // Получаем информацию о скиле для заполнения метаданных
        const SkillStruct *skill = nullptr;
        try
        {
            auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            skill = skillSystem_->getCharacterSkill(casterId, skillSlug);
        }
        catch (...)
        {
            try
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                // Получаем скилы из шаблона моба
                auto mobTemplate = gameServices_->getMobManager().getMobById(mobData.id);
                for (const auto &mobSkill : mobTemplate.skills)
                {
                    if (mobSkill.skillSlug == skillSlug)
                    {
                        skill = &mobSkill;
                        break;
                    }
                }
            }
            catch (...)
            {
                result.errorMessage = "Caster not found";
                return result;
            }
        }

        if (skill)
        {
            result.skillName = skill->skillName;
            result.skillEffectType = skill->skillEffectType;
            result.skillSchool = skill->school;
        }
        else
        {
            result.errorMessage = "Skill not found: " + skillSlug;
            return result;
        }

        // Выполняем скил через SkillSystem
        auto skillResult = skillSystem_->useSkill(casterId, skillSlug, targetId, targetType);
        result.skillResult = skillResult;

        if (!skillResult.success)
        {
            result.errorMessage = skillResult.errorMessage;
            return result;
        }

        // Применяем эффекты
        applySkillEffects(skillResult, casterId, targetId, targetType);

        // Проверяем смерть цели
        if (skillResult.damageResult.totalDamage > 0)
        {
            try
            {
                if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                    int newHealth = std::max(0, targetData.characterCurrentHealth - skillResult.damageResult.totalDamage);
                    gameServices_->getCharacterManager().updateCharacterHealth(targetId, newHealth);

                    result.finalTargetHealth = newHealth;
                    result.finalTargetMana = targetData.characterCurrentMana;
                    result.targetDied = (newHealth <= 0);

                    if (result.targetDied)
                    {
                        handleTargetDeath(targetId, targetType);
                    }
                }
                else if (targetType == CombatTargetType::MOB)
                {
                    auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
                    int newHealth = std::max(0, mobData.currentHealth - skillResult.damageResult.totalDamage);

                    // Обновляем здоровье моба через MobInstanceManager
                    auto updateResult = gameServices_->getMobInstanceManager().updateMobHealth(targetId, newHealth);

                    result.finalTargetHealth = newHealth;
                    result.finalTargetMana = mobData.currentMana; // Устанавливаем текущую ману моба
                    result.targetDied = updateResult.mobDied;

                    if (updateResult.mobDied)
                    {
                        handleTargetDeath(targetId, targetType);
                    }
                    else
                    {
                        // Обработка аггро
                        handleMobAggro(casterId, targetId, skillResult.damageResult.totalDamage);
                    }
                }
            }
            catch (const std::exception &e)
            {
                gameServices_->getLogger().logError("Error applying damage: " + std::string(e.what()));
            }
        }

        // Применяем лечение
        if (skillResult.healAmount > 0)
        {
            try
            {
                if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                    int newHealth = std::min(targetData.characterMaxHealth,
                        targetData.characterCurrentHealth + skillResult.healAmount);
                    gameServices_->getCharacterManager().updateCharacterHealth(targetId, newHealth);
                    result.finalTargetHealth = newHealth;
                    result.finalTargetMana = targetData.characterCurrentMana;
                }
                else if (targetType == CombatTargetType::MOB)
                {
                    auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
                    int newHealth = std::min(mobData.maxHealth, mobData.currentHealth + skillResult.healAmount);
                    gameServices_->getMobInstanceManager().updateMobHealth(targetId, newHealth);
                    result.finalTargetHealth = newHealth;
                    result.finalTargetMana = mobData.currentMana;
                }
            }
            catch (const std::exception &e)
            {
                gameServices_->getLogger().logError("Error applying healing: " + std::string(e.what()));
            }
        }

        // Убеждаемся, что finalTargetMana установлена для мобов в любом случае
        if (targetType == CombatTargetType::MOB && result.finalTargetMana == 0 && skillResult.damageResult.totalDamage == 0 && skillResult.healAmount == 0)
        {
            try
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
                result.finalTargetMana = mobData.currentMana;
            }
            catch (const std::exception &e)
            {
                gameServices_->getLogger().logError("Error getting mob mana for result: " + std::string(e.what()));
            }
        }

        // Удаляем ongoing action
        ongoingActions_.erase(casterId);

        result.success = true;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error executing combat action: " + std::string(e.what());
        gameServices_->getLogger().logError("CombatSystem::executeCombatAction error: " + std::string(e.what()));
    }

    return result;
}

void
CombatSystem::interruptSkillUsage(int casterId, InterruptionReason reason)
{
    auto it = ongoingActions_.find(casterId);
    if (it != ongoingActions_.end())
    {
        it->second->state = CombatActionState::INTERRUPTED;
        it->second->interruptReason = reason;

        // TODO: возврат ресурсов при прерывании

        ongoingActions_.erase(it);

        gameServices_->getLogger().log("Skill usage interrupted for caster " + std::to_string(casterId) +
                                       ", reason: " + std::to_string(static_cast<int>(reason)));
    }
}

void
CombatSystem::updateOngoingActions()
{
    auto now = std::chrono::steady_clock::now();

    for (auto it = ongoingActions_.begin(); it != ongoingActions_.end();)
    {
        auto &action = it->second;

        if (action->state == CombatActionState::CASTING && now >= action->endTime)
        {
            // Завершаем каст, переходим к выполнению
            action->state = CombatActionState::EXECUTING;

            // Автоматически выполняем действие
            auto result = executeSkillUsage(action->casterId, action->actionName, action->targetId, action->targetType);

            // Отправляем результат всем клиентам через responseBuilder
            if (responseBuilder_ && broadcastCallback_)
            {
                auto broadcast = responseBuilder_->buildSkillExecutionBroadcast(result);
                broadcastCallback_(broadcast);
                gameServices_->getLogger().log("Skill execution broadcast sent for: " + action->actionName);
            }

            it = ongoingActions_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
CombatSystem::processAIAttack(int mobId)
{
    try
    {
        gameServices_->getLogger().log("CombatSystem::processAIAttack called for mob " + std::to_string(mobId));

        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(mobId);
        gameServices_->getLogger().log("Found mob instance for " + std::to_string(mobId) + ", name: " + mobData.name + ", type ID: " + std::to_string(mobData.id));

        // Получаем скилы из шаблона моба по его типу ID, а не из экземпляра
        auto mobTemplate = gameServices_->getMobManager().getMobById(mobData.id);
        if (mobTemplate.skills.empty())
        {
            gameServices_->getLogger().log("Mob type " + std::to_string(mobData.id) + " (UID " + std::to_string(mobId) + ") has no skills available");
            return; // Нет скилов для использования
        }

        // Используем скилы из шаблона, но данные позиции и состояния из экземпляра
        mobData.skills = mobTemplate.skills;

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " has " + std::to_string(mobData.skills.size()) + " skills");

        // Получаем список возможных целей (игроков в радиусе)
        auto players = gameServices_->getCharacterManager().getCharactersInZone(
            mobData.position.positionX, mobData.position.positionY, 20.0f); // 20 метров радиус

        if (players.empty())
        {
            gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " found no players in range");
            return; // Нет целей
        }

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " found " + std::to_string(players.size()) + " players in range");

        // Находим ближайшую цель
        CharacterDataStruct nearestTarget;
        float minDistance = std::numeric_limits<float>::max();

        for (const auto &player : players)
        {
            float dx = mobData.position.positionX - player.characterPosition.positionX;
            float dy = mobData.position.positionY - player.characterPosition.positionY;
            float distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance)
            {
                minDistance = distance;
                nearestTarget = player;
            }
        }

        if (nearestTarget.characterId == 0)
        {
            gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " found no suitable targets");
            return; // Нет подходящих целей
        }

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " targeting player " + std::to_string(nearestTarget.characterId) + " at distance " + std::to_string(minDistance));

        // Выбираем лучший скил
        const SkillStruct *bestSkill = skillSystem_->getBestSkillForMob(mobData, nearestTarget, minDistance);

        if (!bestSkill)
        {
            gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " found no suitable skills");
            return; // Нет подходящих скилов
        }

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " will use skill: " + bestSkill->skillName);

        // Используем новую систему скилов для выполнения атаки
        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " executing skill: " + bestSkill->skillSlug);
        auto result = executeSkillUsage(mobId, bestSkill->skillSlug, nearestTarget.characterId, CombatTargetType::PLAYER);

        if (result.success)
        {
            gameServices_->getLogger().log("Mob " + mobData.name + " used " + bestSkill->skillName +
                                           " on " + nearestTarget.characterName +
                                           " for " + std::to_string(result.skillResult.damageResult.totalDamage) + " damage");

            // Отправляем broadcast пакеты для AI атаки
            if (responseBuilder_ && broadcastCallback_)
            {
                gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " sending broadcast for skill execution");
                auto broadcast = responseBuilder_->buildSkillExecutionBroadcast(result);
                broadcastCallback_(broadcast);
                gameServices_->getLogger().log("AI skill execution broadcast sent for: " + bestSkill->skillName);
            }
            else
            {
                gameServices_->getLogger().logError("Mob " + std::to_string(mobId) + " broadcast failed - responseBuilder or callback missing");
            }
        }
        else
        {
            gameServices_->getLogger().logError("Mob " + std::to_string(mobId) + " skill execution failed: " + result.errorMessage);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error in AI attack: " + std::string(e.what()));
    }
}

std::vector<int>
CombatSystem::getAvailableTargets(int attackerId, const SkillStruct &skill)
{
    std::vector<int> targets;

    // TODO: Реализовать логику поиска доступных целей на основе типа скила
    // Учитывать дистанцию, препятствия, состояние целей и т.д.

    return targets;
}

void
CombatSystem::applySkillEffects(const SkillUsageResult &result, int casterId, int targetId, CombatTargetType targetType)
{
    // Базовые эффекты уже применены в executeCombatAction
    // Здесь можно добавить дополнительные эффекты: баффы, дебаффы, DoT и т.д.

    // TODO: Система эффектов/баффов
}

void
CombatSystem::handleTargetDeath(int targetId, CombatTargetType targetType)
{
    if (targetType == CombatTargetType::PLAYER)
    {
        // TODO: Логика смерти игрока
        gameServices_->getLogger().log("Player " + std::to_string(targetId) + " died");
    }
    else if (targetType == CombatTargetType::MOB)
    {
        // TODO: Логика смерти моба (лут, опыт и т.д.)
        gameServices_->getLogger().log("Mob " + std::to_string(targetId) + " died");
    }
}

void
CombatSystem::handleMobAggro(int attackerId, int targetId, int damage)
{
    if (damage > 0)
    {
        // Интегрируемся с MobMovementManager для обработки аггро
        try
        {
            auto &mobMovementManager = gameServices_->getMobMovementManager();
            mobMovementManager.handleMobAttacked(targetId, attackerId);

            gameServices_->getLogger().log("Mob " + std::to_string(targetId) +
                                           " gained aggro on " + std::to_string(attackerId) +
                                           " (damage: " + std::to_string(damage) + ")");
        }
        catch (const std::exception &e)
        {
            gameServices_->getLogger().logError("Error handling mob aggro: " + std::string(e.what()));
        }
    }
}

void
CombatSystem::processAIAttack(int mobId, int targetPlayerId)
{
    try
    {
        gameServices_->getLogger().log("CombatSystem::processAIAttack called for mob " + std::to_string(mobId) + " targeting player " + std::to_string(targetPlayerId));

        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(mobId);
        gameServices_->getLogger().log("Found mob instance for " + std::to_string(mobId) + ", name: " + mobData.name + ", type ID: " + std::to_string(mobData.id));

        // Получаем скилы из шаблона моба по его типу ID, а не из экземпляра
        auto mobTemplate = gameServices_->getMobManager().getMobById(mobData.id);
        if (mobTemplate.skills.empty())
        {
            gameServices_->getLogger().log("Mob type " + std::to_string(mobData.id) + " (UID " + std::to_string(mobId) + ") has no skills available");
            return; // Нет скилов для использования
        }

        // Используем скилы из шаблона, но данные позиции и состояния из экземпляра
        mobData.skills = mobTemplate.skills;

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " has " + std::to_string(mobData.skills.size()) + " skills");

        // Получаем данные цели
        auto targetPlayer = gameServices_->getCharacterManager().getCharacterById(targetPlayerId);
        if (targetPlayer.characterId == 0)
        {
            gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " target player " + std::to_string(targetPlayerId) + " not found");
            return; // Цель не найдена
        }

        // Проверяем дистанцию до цели
        float dx = mobData.position.positionX - targetPlayer.characterPosition.positionX;
        float dy = mobData.position.positionY - targetPlayer.characterPosition.positionY;
        float distance = std::sqrt(dx * dx + dy * dy);

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " targeting player " + std::to_string(targetPlayerId) + " at distance " + std::to_string(distance));

        // Выбираем лучший скил
        const SkillStruct *bestSkill = skillSystem_->getBestSkillForMob(mobData, targetPlayer, distance);

        if (!bestSkill)
        {
            gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " found no suitable skills for target");
            return; // Нет подходящих скилов
        }

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " will use skill: " + bestSkill->skillName + " on player " + std::to_string(targetPlayerId));

        // Сначала инициируем скил
        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " initiating skill: " + bestSkill->skillSlug + " on player " + std::to_string(targetPlayerId));

        // Создаем результат инициации вручную, так как у мобов скилы хранятся в шаблонах
        SkillInitiationResult initiationResult;
        initiationResult.success = true;
        initiationResult.casterId = mobId;
        initiationResult.targetId = targetPlayer.characterId;
        initiationResult.targetType = CombatTargetType::PLAYER;
        initiationResult.skillName = bestSkill->skillName;
        initiationResult.skillEffectType = bestSkill->skillEffectType;
        initiationResult.skillSchool = bestSkill->school;
        initiationResult.castTime = static_cast<float>(bestSkill->castMs) / 1000.0f;
        initiationResult.animationName = "skill_" + bestSkill->skillSlug;
        initiationResult.animationDuration = std::max(1.0f, initiationResult.castTime);

        // Отправляем broadcast пакет инициации
        if (responseBuilder_ && broadcastCallback_)
        {
            gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " sending initiation broadcast for skill: " + bestSkill->skillName);
            auto initiationBroadcast = responseBuilder_->buildSkillInitiationBroadcast(initiationResult);
            broadcastCallback_(initiationBroadcast);
            gameServices_->getLogger().log("AI skill initiation broadcast sent for: " + bestSkill->skillName);
        }

        // Затем выполняем скил напрямую через SkillSystem
        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " executing skill: " + bestSkill->skillSlug + " on player " + std::to_string(targetPlayerId));
        auto skillResult = skillSystem_->useSkill(mobId, bestSkill->skillSlug, targetPlayer.characterId, CombatTargetType::PLAYER);

        // Создаем результат выполнения
        SkillExecutionResult result;
        result.success = skillResult.success;
        result.casterId = mobId;
        result.targetId = targetPlayer.characterId;
        result.targetType = CombatTargetType::PLAYER;
        result.skillName = bestSkill->skillName;
        result.skillEffectType = bestSkill->skillEffectType;
        result.skillSchool = bestSkill->school;
        result.skillResult = skillResult;
        result.errorMessage = skillResult.errorMessage;

        if (result.success)
        {
            // Применяем урон к игроку
            if (skillResult.damageResult.totalDamage > 0)
            {
                try
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetPlayer.characterId);
                    int newHealth = std::max(0, targetData.characterCurrentHealth - skillResult.damageResult.totalDamage);
                    gameServices_->getCharacterManager().updateCharacterHealth(targetPlayer.characterId, newHealth);

                    result.finalTargetHealth = newHealth;
                    result.finalTargetMana = targetData.characterCurrentMana;
                    result.targetDied = (newHealth <= 0);

                    gameServices_->getLogger().log("Mob " + mobData.name + " dealt " + std::to_string(skillResult.damageResult.totalDamage) +
                                                   " damage to " + targetPlayer.characterName +
                                                   " (Health: " + std::to_string(newHealth) + "/" + std::to_string(targetData.characterMaxHealth) + ")");

                    if (result.targetDied)
                    {
                        handleTargetDeath(targetPlayer.characterId, CombatTargetType::PLAYER);
                        gameServices_->getLogger().log("Player " + targetPlayer.characterName + " died from mob attack");
                    }
                }
                catch (const std::exception &e)
                {
                    gameServices_->getLogger().logError("Error applying damage to player: " + std::string(e.what()));
                }
            }

            gameServices_->getLogger().log("Mob " + mobData.name + " used " + bestSkill->skillName +
                                           " on " + targetPlayer.characterName +
                                           " for " + std::to_string(result.skillResult.damageResult.totalDamage) + " damage");

            // Отправляем broadcast пакеты для AI атаки
            if (responseBuilder_ && broadcastCallback_)
            {
                gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " sending broadcast for skill execution");
                auto broadcast = responseBuilder_->buildSkillExecutionBroadcast(result);
                broadcastCallback_(broadcast);
                gameServices_->getLogger().log("AI skill execution broadcast sent for: " + bestSkill->skillName);
            }
            else
            {
                gameServices_->getLogger().logError("Mob " + std::to_string(mobId) + " broadcast failed - responseBuilder or callback missing");
            }
        }
        else
        {
            gameServices_->getLogger().logError("Mob " + std::to_string(mobId) + " skill execution failed: " + result.errorMessage);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error in AI attack with target: " + std::string(e.what()));
    }
}
