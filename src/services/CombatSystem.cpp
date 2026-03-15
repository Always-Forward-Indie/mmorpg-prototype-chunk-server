#include "services/CombatSystem.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/SkillSystem.hpp"
#include "utils/Logger.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/logger.h>
#include <tuple>
#include <vector>

// Static counter for unique action IDs
static std::atomic<uint64_t> nextActionId{1};

CombatSystem::CombatSystem(GameServices *gameServices)
    : gameServices_(gameServices)
{
    log_ = gameServices_->getLogger().getSystem("combat");
    skillSystem_ = std::make_unique<SkillSystem>(gameServices);
    responseBuilder_ = std::make_unique<CombatResponseBuilder>(gameServices);
    broadcastCallback_ = nullptr;
}

void
CombatSystem::setBroadcastCallback(std::function<void(const nlohmann::json &)> callback)
{
    broadcastCallback_ = callback;
    // Также настраиваем callback для ExperienceManager
    setupExperienceCallbacks();
}

void
CombatSystem::setupExperienceCallbacks()
{
    if (broadcastCallback_)
    {
        auto &experienceManager = gameServices_->getExperienceManager();
        experienceManager.setExperiencePacketCallback(broadcastCallback_);
        experienceManager.setStatsUpdatePacketCallback(broadcastCallback_);

        auto &statsService = gameServices_->getStatsNotificationService();
        statsService.setStatsUpdateCallback(broadcastCallback_);
    }
}

SkillInitiationResult
CombatSystem::initiateSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType)
{
    SkillInitiationResult result;
    result.casterId = casterId;
    result.targetId = targetId;
    result.targetType = targetType;
    result.skillSlug = skillSlug; // Сохраняем оригинальный slug

    log_->info("CombatSystem::initiateSkillUsage called with skill: " + skillSlug);

    try
    {
        // Получаем скил для проверки времени каста
        std::optional<SkillStruct> skillOpt;

        // HIGH-8: no exceptions needed — both getCharacterData/getMobInstance return a
        // default-constructed struct (id=0) when not found, never throw.
        {
            auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (characterData.characterId != 0) // player
            {
                skillOpt = skillSystem_->getCharacterSkill(casterId, skillSlug);
            }
            else
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                if (mobData.uid != 0) // mob
                {
                    skillOpt = skillSystem_->getMobSkill(casterId, skillSlug);
                }
                else
                {
                    result.errorMessage = "Caster not found";
                    return result;
                }
            }
        }

        if (!skillOpt.has_value())
        {
            result.errorMessage = "Skill not found: " + skillSlug;
            return result;
        }

        const SkillStruct &skill = skillOpt.value();
        // Дополнительная проверка валидности skill указателя
        log_->info("Skill found: " + std::string(skill.skillName));

        // Заполняем информацию о скиле
        result.skillName = skill.skillName;
        result.skillEffectType = skill.skillEffectType;
        result.skillSchool = skill.school;

        // Проверяем базовые требования (без потребления ресурсов)
        if (skillSystem_->isOnCooldown(casterId, skillSlug))
        {
            log_->warn("[initiateSkillUsage] Skill '" + skillSlug + "' is on cooldown for caster " +
                       std::to_string(casterId));
            result.errorMessage = "Skill is on cooldown";
            return result;
        }

        // Проверяем, что у кастера нет активного каста (защита от спама во время cast time).
        // Только для скилов с castMs > 0 — мгновенные можно спамить свободно.
        if (skill.castMs > 0)
        {
            std::lock_guard<std::mutex> lock(actionsMutex_);
            auto it = ongoingActions_.find(casterId);
            if (it != ongoingActions_.end() && it->second->state == CombatActionState::CASTING)
            {
                log_->warn("[initiateSkillUsage] Caster " + std::to_string(casterId) +
                           " is already casting '" + it->second->skillSlug + "' — rejecting new cast '" + skillSlug + "'");
                result.errorMessage = "Already casting";
                return result;
            }
        }

        // HIGH-8: mana check without exceptions
        {
            auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (characterData.characterId != 0) // player
            {
                if (characterData.characterCurrentMana < skill.costMp)
                {
                    result.errorMessage = "Not enough mana";
                    return result;
                }
            }
            else
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                if (mobData.uid != 0 && mobData.currentMana < skill.costMp)
                {
                    result.errorMessage = "Not enough mana";
                    return result;
                }
            }
        }

        // Создаем запись о начинающемся действии
        auto action = std::make_shared<CombatActionStruct>();
        action->actionId = nextActionId.fetch_add(1); // Генерация уникального ID действия

        // Сохраняем оригинальный skillSlug для выполнения
        action->skillSlug = skillSlug;

        // Безопасное присваивание строки
        if (!skill.skillName.empty())
        {
            action->actionName = skill.skillName;
        }
        else
        {
            action->actionName = skillSlug; // Fallback к slug'у
        }

        action->actionType = CombatActionType::SKILL;
        action->targetType = targetType;
        action->casterId = casterId;
        action->targetId = targetId;
        action->skillSlug = skillSlug; // Сохраняем оригинальный slug для выполнения
        action->castTime = static_cast<float>(skill.castMs) / 1000.0f;
        action->state = (skill.castMs > 0) ? CombatActionState::CASTING : CombatActionState::EXECUTING;
        action->startTime = std::chrono::steady_clock::now();
        action->endTime = action->startTime + std::chrono::milliseconds(skill.castMs);
        action->animationName = skill.animationName.empty() ? "skill_" + skillSlug : skill.animationName;
        {
            // animationDuration = cast + swing, but capped to just below
            // the full attack cycle so the next attack's animation never overlaps.
            constexpr float kMargin = 0.05f;
            float kSwing = static_cast<float>(skill.swingMs) / 1000.0f;
            float cdSec = static_cast<float>(std::max(skill.cooldownMs, skill.gcdMs)) / 1000.0f;
            if (cdSec < 0.5f)
                cdSec = 0.5f; // mirrors the minimum in MobAIController
            float cycleTime = action->castTime + kSwing + cdSec;
            action->animationDuration = std::min(action->castTime + kSwing, cycleTime - kMargin);
            if (action->animationDuration < kSwing)
                action->animationDuration = kSwing;
        }

        // Сохраняем ongoing action
        {
            std::lock_guard<std::mutex> lock(actionsMutex_);
            ongoingActions_[casterId] = action;
        }

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
    result.skillSlug = skillSlug; // Сохраняем оригинальный slug

    try
    {
        // Получаем информацию о скиле для заполнения метаданных
        // HIGH-8: explicit id check, no exceptions
        std::optional<SkillStruct> skillOpt;
        {
            auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (characterData.characterId != 0) // player
            {
                skillOpt = skillSystem_->getCharacterSkill(casterId, skillSlug);
            }
            else
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                if (mobData.uid != 0) // mob
                {
                    skillOpt = skillSystem_->getMobSkill(casterId, skillSlug);
                }
                else
                {
                    result.errorMessage = "Caster not found";
                    return result;
                }
            }
        }

        if (skillOpt.has_value())
        {
            const SkillStruct &skill = skillOpt.value();
            result.skillName = skill.skillName;
            result.skillEffectType = skill.skillEffectType;
            result.skillSchool = skill.school;
        }
        else
        {
            result.errorMessage = "Skill not found: " + skillSlug;
            return result;
        }

        // Route AoE skills to dedicated handler (resource management handled inside)
        if (targetType == CombatTargetType::AREA)
        {
            executeAoESkillUsage(casterId, skillSlug);
            result.success = true;
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
        applySkillEffects(skillResult, casterId, skillSlug, targetId, targetType);

        // Проверяем смерть цели
        if (skillResult.damageResult.totalDamage > 0)
        {
            try
            {
                if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
                {
                    auto hpResult = gameServices_->getCharacterManager().applyDamageToCharacter(
                        targetId, skillResult.damageResult.totalDamage);

                    // Suppress regen after taking a hit
                    gameServices_->getCharacterManager().markCharacterInCombat(targetId);

                    result.finalTargetHealth = hpResult.newHealth;
                    result.finalTargetMana = hpResult.currentMana;
                    result.targetDied = hpResult.died;
                    result.healthPopulated = true;

                    if (result.targetDied)
                    {
                        handleTargetDeath(targetId, targetType);
                    }
                }
                else if (targetType == CombatTargetType::MOB)
                {
                    // Skip damage while mob is leashing (RETURNING) or in post-leash
                    // invulnerability window (EVADING).
                    auto mobMoveData = gameServices_->getMobMovementManager().getMobMovementData(targetId);
                    bool isEvading = (mobMoveData.combatState == MobCombatState::RETURNING ||
                                      mobMoveData.combatState == MobCombatState::EVADING);
                    if (isEvading)
                    {
                        gameServices_->getLogger().log("[COMBAT] Mob " + std::to_string(targetId) +
                                                       " is leashing — damage blocked (EVADING)");
                        // Leave result.healthPopulated = false so no HP update is sent.
                    }
                    else
                    {
                        auto updateResult = gameServices_->getMobInstanceManager().applyDamageToMob(
                            targetId, skillResult.damageResult.totalDamage, casterId);

                        result.finalTargetHealth = updateResult.newHealth;
                        result.finalTargetMana = updateResult.currentMana;
                        result.targetDied = updateResult.mobDied;
                        result.healthPopulated = true;

                        // Durability: player's weapon loses durability on hit
                        try
                        {
                            auto weapon = gameServices_->getInventoryManager().getEquippedWeapon(casterId);
                            if (weapon.has_value())
                            {
                                const auto &wItem = gameServices_->getItemManager().getItemById(weapon->itemId);
                                if (wItem.isDurable && wItem.durabilityMax > 0)
                                {
                                    int loss = static_cast<int>(gameServices_->getGameConfigService().getFloat("durability.weapon_loss_per_hit", 1.0f));
                                    int cur = (weapon->durabilityCurrent > 0) ? weapon->durabilityCurrent : wItem.durabilityMax;
                                    int newDur = std::max(0, cur - loss);
                                    gameServices_->getInventoryManager().updateDurability(casterId, weapon->id, newDur);
                                    saveDurabilityChange(casterId, weapon->id, newDur);
                                    checkAndTriggerDurabilityWarning(casterId, cur, newDur, wItem.durabilityMax);
                                }
                            }
                        }
                        catch (const std::exception &e)
                        {
                            log_->warn("[COMBAT] Weapon durability update error: " + std::string(e.what()));
                        }

                        // --- Mastery: player attack on mob → mastery experience ---
                        try
                        {
                            auto weapon = gameServices_->getInventoryManager().getEquippedWeapon(casterId);
                            if (weapon.has_value())
                            {
                                const auto &wItem = gameServices_->getItemManager().getItemById(weapon->itemId);
                                if (!wItem.masterySlug.empty())
                                {
                                    const auto charData = gameServices_->getCharacterManager().getCharacterData(casterId);
                                    auto mobInst = gameServices_->getMobInstanceManager().getMobInstance(targetId);
                                    gameServices_->getMasteryManager().onPlayerAttack(
                                        casterId, wItem.masterySlug, charData.characterLevel, mobInst.level);
                                }
                            }
                        }
                        catch (const std::exception &e)
                        {
                            log_->warn("[Mastery] Error on player attack: " + std::string(e.what()));
                        }

                        if (updateResult.mobDied)
                        {
                            handleMobDeath(targetId, casterId);
                        }
                        else
                        {
                            handleMobAggro(casterId, targetId, skillResult.damageResult.totalDamage);
                        }
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
                    auto hpResult = gameServices_->getCharacterManager().applyHealToCharacter(
                        targetId, skillResult.healAmount);
                    result.finalTargetHealth = hpResult.newHealth;
                    result.finalTargetMana = hpResult.currentMana;
                    result.healthPopulated = true;

                    // Heal threat: healer draws half the heal amount as mob aggro
                    // (only when healer is different from the healed target).
                    if (casterId != targetId && skillResult.healAmount > 0)
                    {
                        const int healThreat = skillResult.healAmount / 2;
                        if (healThreat > 0)
                        {
                            try
                            {
                                auto &mmgr = gameServices_->getMobMovementManager();
                                auto allMobs = gameServices_->getMobInstanceManager().getAllMobInstances();
                                for (auto &[uid, mobInst] : allMobs)
                                {
                                    if (mobInst.isDead)
                                        continue;
                                    auto mobMov = mmgr.getMobMovementData(uid);
                                    if (mobMov.targetPlayerId == targetId)
                                    {
                                        mobMov.threatTable[casterId] += healThreat;
                                        mmgr.updateMobMovementData(uid, mobMov);
                                    }
                                }
                            }
                            catch (const std::exception &e)
                            {
                                gameServices_->getLogger().logError(
                                    "Error applying heal threat: " + std::string(e.what()));
                            }
                        }
                    }
                }
                else if (targetType == CombatTargetType::MOB)
                {
                    auto updateResult = gameServices_->getMobInstanceManager().applyHealToMob(
                        targetId, skillResult.healAmount);
                    result.finalTargetHealth = updateResult.newHealth;
                    result.finalTargetMana = updateResult.currentMana;
                    result.healthPopulated = true;
                }
            }
            catch (const std::exception &e)
            {
                gameServices_->getLogger().logError("Error applying healing: " + std::string(e.what()));
            }
        }

        // Убеждаемся, что finalTargetHealth и finalTargetMana установлены в любом случае
        // Это важно для случаев когда атака промахивается (isMissed = true)
        if (!result.healthPopulated)
        {
            try
            {
                if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                    result.finalTargetHealth = targetData.characterCurrentHealth;
                    result.finalTargetMana = targetData.characterCurrentMana;
                }
                else if (targetType == CombatTargetType::MOB)
                {
                    auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
                    result.finalTargetHealth = mobData.currentHealth;
                    result.finalTargetMana = mobData.currentMana;
                }
            }
            catch (const std::exception &e)
            {
                gameServices_->getLogger().logError("Error getting target health/mana for result: " + std::string(e.what()));
            }
        }

        // НЕ удаляем ongoing action здесь - это делается в updateOngoingActions()
        // ongoingActions_.erase(casterId);

        result.success = true;

        // Отправляем обновление статов для кастера (потратил ману)
        try
        {
            auto &statsService = gameServices_->getStatsNotificationService();
            statsService.sendStatsUpdate(casterId);

            // Если цель - игрок и получил урон/лечение, тоже отправляем обновление
            if ((targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF) &&
                targetId != casterId &&
                (skillResult.damageResult.totalDamage > 0 || skillResult.healAmount > 0))
            {
                statsService.sendStatsUpdate(targetId);
            }
        }
        catch (const std::exception &e)
        {
            gameServices_->getLogger().logError("Error sending stats update after skill usage: " + std::string(e.what()));
        }
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
    std::lock_guard<std::mutex> lock(actionsMutex_);
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

    // Snapshot actions that are ready to execute, erase them under lock,
    // then execute outside the lock to avoid holding it during heavy work.
    std::vector<std::tuple<int, std::string, int, CombatTargetType, std::string>> toExecute;
    {
        std::lock_guard<std::mutex> lock(actionsMutex_);
        for (auto it = ongoingActions_.begin(); it != ongoingActions_.end();)
        {
            auto &action = it->second;
            if (action->state == CombatActionState::CASTING && now >= action->endTime)
            {
                action->state = CombatActionState::EXECUTING;
                toExecute.emplace_back(action->casterId, action->skillSlug, action->targetId, action->targetType, action->actionName);
                it = ongoingActions_.erase(it);
            }
            else if (action->state == CombatActionState::EXECUTING)
            {
                // Instant skills (castMs=0) are executed synchronously in dispatchSkillAction
                // and leave a stale EXECUTING entry. Clean it up here.
                it = ongoingActions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    for (auto &[casterId, skillSlug, targetId, targetType, actionName] : toExecute)
    {
        auto result = executeSkillUsage(casterId, skillSlug, targetId, targetType);

        if (responseBuilder_ && broadcastCallback_)
        {
            auto broadcast = responseBuilder_->buildSkillExecutionBroadcast(result);
            broadcastCallback_(broadcast);
            log_->info("Skill execution broadcast sent for: " + actionName);
        }
    }
}

void
CombatSystem::tickEffects()
{
    try
    {
        auto &charMgr = gameServices_->getCharacterManager();
        auto [ticks, expiredCharacters] = charMgr.processEffectTicks(); // applies HP changes under CharacterManager lock

        // Notify clients whose stat-modifier effects expired
        for (int cid : expiredCharacters)
        {
            gameServices_->getStatsNotificationService().sendStatsUpdate(cid);
        }

        if (ticks.empty())
            return;

        for (const auto &tick : ticks)
        {
            gameServices_->getLogger().log(
                "[CombatSystem::tickEffects] " + tick.effectTypeSlug +
                " '" + tick.effectSlug + "' on char " + std::to_string(tick.characterId) +
                " value=" + std::to_string(static_cast<int>(tick.value)) +
                " hp=" + std::to_string(tick.newHealth));

            // Broadcast to zone
            if (responseBuilder_ && broadcastCallback_)
            {
                broadcastCallback_(responseBuilder_->buildEffectTickBroadcast(tick));
            }

            // Handle death from DoT
            if (tick.targetDied)
            {
                log_->info(
                    "[CombatSystem::tickEffects] Character " + std::to_string(tick.characterId) +
                    " died from DoT effect '" + tick.effectSlug + "'");
                handleTargetDeath(tick.characterId, CombatTargetType::PLAYER);
            }
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_->getLogger().logError("[CombatSystem::tickEffects] " + std::string(ex.what()));
    }
}

void
CombatSystem::executeAoESkillUsage(int casterId, const std::string &skillSlug)
{
    try
    {
        // Let SkillSystem handle mana consumption, cooldown, and validation.
        // AREA targetType: validateTarget returns true, no damage calc inside useSkill.
        auto skillResult = skillSystem_->useSkill(casterId, skillSlug, 0, CombatTargetType::AREA);
        if (!skillResult.success)
        {
            log_->error(
                "[AoE] useSkill failed for caster=" + std::to_string(casterId) +
                " skill=" + skillSlug + ": " + skillResult.errorMessage);
            return;
        }

        auto skillOpt = skillSystem_->getCharacterSkill(casterId, skillSlug);
        if (!skillOpt.has_value())
            return;
        const SkillStruct &skill = skillOpt.value();

        auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
        const float cx = casterData.characterPosition.positionX;
        const float cy = casterData.characterPosition.positionY;
        const float radius = (skill.areaRadius > 0.0f) ? skill.areaRadius : 5.0f;
        const int maxHits = static_cast<int>(
            gameServices_->getGameConfigService().getFloat("combat.aoe_target_cap", 10.0f));
        int hitCount = 0;

        auto *calc = skillSystem_->getCombatCalculator();
        if (!calc)
            return;

        // ---- Mob targets ----
        auto mobs = gameServices_->getMobInstanceManager().getMobsInRange(cx, cy, radius);
        for (const auto &mob : mobs)
        {
            if (hitCount >= maxHits)
                break;

            // Simplified player→mob path (mirror of SkillSystem::useSkill MOB branch)
            int dmg = calc->calculateBaseDamage(skill, casterData.attributes);
            if (calc->rollCriticalHit(casterData.attributes))
                dmg = static_cast<int>(dmg * 2.0f);
            dmg = std::max(0, dmg);

            // Skip if mob is leashing back to spawn (RETURNING) or in post-leash
            // invulnerability window (EVADING).
            {
                auto mobMoveData = gameServices_->getMobMovementManager().getMobMovementData(mob.uid);
                if (mobMoveData.combatState == MobCombatState::RETURNING ||
                    mobMoveData.combatState == MobCombatState::EVADING)
                {
                    gameServices_->getLogger().log("[COMBAT] AOE: Mob " + std::to_string(mob.uid) +
                                                   " is leashing — damage blocked (EVADING)");
                    continue;
                }
            }

            auto upResult = gameServices_->getMobInstanceManager().applyDamageToMob(mob.uid, dmg, casterId);

            SkillExecutionResult execResult;
            execResult.success = true;
            execResult.casterId = casterId;
            execResult.targetId = mob.uid;
            execResult.targetType = CombatTargetType::MOB;
            execResult.skillSlug = skillSlug;
            execResult.skillName = skill.skillName;
            execResult.skillEffectType = skill.skillEffectType;
            execResult.skillSchool = skill.school;
            execResult.skillResult.damageResult.totalDamage = dmg;
            execResult.finalTargetHealth = upResult.newHealth;
            execResult.finalTargetMana = upResult.currentMana;
            execResult.targetDied = upResult.mobDied;

            if (responseBuilder_ && broadcastCallback_)
                broadcastCallback_(responseBuilder_->buildSkillExecutionBroadcast(execResult));

            if (upResult.mobDied)
                handleMobDeath(mob.uid, casterId);
            else
                handleMobAggro(casterId, mob.uid, dmg);

            ++hitCount;
        }

        // ---- Player targets (skip self) ----
        auto players = gameServices_->getCharacterManager().getCharactersInZone(cx, cy, radius);
        for (const auto &target : players)
        {
            if (hitCount >= maxHits)
                break;
            if (target.characterId == casterId)
                continue;

            auto dmgResult = calc->calculateSkillDamage(skill, casterData, target);
            if (dmgResult.isMissed)
                continue;

            auto hpAoE = gameServices_->getCharacterManager().applyDamageToCharacter(
                target.characterId, dmgResult.totalDamage);

            // Suppress regen for AoE targets
            gameServices_->getCharacterManager().markCharacterInCombat(target.characterId);

            SkillExecutionResult execResult;
            execResult.success = true;
            execResult.casterId = casterId;
            execResult.targetId = target.characterId;
            execResult.targetType = CombatTargetType::PLAYER;
            execResult.skillSlug = skillSlug;
            execResult.skillName = skill.skillName;
            execResult.skillEffectType = skill.skillEffectType;
            execResult.skillSchool = skill.school;
            execResult.skillResult.damageResult = dmgResult;
            execResult.finalTargetHealth = hpAoE.newHealth;
            execResult.finalTargetMana = hpAoE.currentMana;
            execResult.targetDied = hpAoE.died;

            if (responseBuilder_ && broadcastCallback_)
                broadcastCallback_(responseBuilder_->buildSkillExecutionBroadcast(execResult));

            if (execResult.targetDied)
                handleTargetDeath(target.characterId, CombatTargetType::PLAYER);

            ++hitCount;
        }

        gameServices_->getLogger().log(
            "[AoE] '" + skillSlug + "' by char " + std::to_string(casterId) +
            " hit " + std::to_string(hitCount) + " target(s) in r=" + std::to_string(radius));
    }
    catch (const std::exception &ex)
    {
        gameServices_->getLogger().logError("[CombatSystem::executeAoESkillUsage] " + std::string(ex.what()));
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
CombatSystem::applySkillEffects(const SkillUsageResult &result, int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType)
{
    // Retrieve the skill definition to access its effect list.
    std::optional<SkillStruct> skillOpt = skillSystem_->getCharacterSkill(casterId, skillSlug);
    if (!skillOpt.has_value())
        return;
    const SkillStruct &skill = skillOpt.value();
    if (skill.effects.empty())
        return;

    // Determine which entity receives the effect.
    // buff / hot / stat → typically the caster (self-buff) or the target (enemy debuff/dot).
    // Convention: effectTypeSlug "dot" / "debuff" → target; "buff" / "hot" → caster for SELF,
    // or targetId for PLAYER target (support heals). Unknown → skip.

    const int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                               .count();

    for (const auto &ed : skill.effects)
    {
        if (ed.effectSlug.empty() || ed.effectTypeSlug.empty())
            continue;

        // Determine recipient
        int recipientId = -1;
        if (ed.effectTypeSlug == "dot" || ed.effectTypeSlug == "debuff")
        {
            // Dots / debuffs land on the target (only players for now)
            if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
                recipientId = targetId;
            // TODO: mobs don't have activeEffects yet — skip mob targets
        }
        else // buff / hot
        {
            // Self-buffs land on the caster; support heals land on the target player
            if (targetType == CombatTargetType::SELF)
                recipientId = casterId;
            else if (targetType == CombatTargetType::PLAYER && (ed.effectTypeSlug == "hot" || ed.effectTypeSlug == "buff"))
                recipientId = targetId;
            else
                recipientId = casterId; // default: buff stays on caster
        }

        if (recipientId <= 0)
            continue;

        // Verify recipient is a player character
        {
            auto charData = gameServices_->getCharacterManager().getCharacterData(recipientId);
            if (charData.characterId == 0)
                continue; // not a player
        }

        ActiveEffectStruct eff;
        eff.effectSlug = ed.effectSlug;
        eff.effectTypeSlug = ed.effectTypeSlug;
        eff.attributeSlug = ed.attributeSlug;
        eff.value = ed.value;
        eff.sourceType = "skill";
        eff.expiresAt = (ed.durationSeconds > 0) ? (nowSec + ed.durationSeconds) : 0;
        eff.tickMs = ed.tickMs;

        if (ed.tickMs > 0)
        {
            eff.nextTickAt = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(ed.tickMs);
        }

        gameServices_->getCharacterManager().addActiveEffect(recipientId, eff);

        // Notify client: buff bar and effective stats need refreshing
        gameServices_->getStatsNotificationService().sendStatsUpdate(recipientId);

        log_->info("[applySkillEffects] Applied '" + ed.effectSlug +
                   "' (" + ed.effectTypeSlug + ") on char " + std::to_string(recipientId) +
                   " from skill '" + skill.skillSlug + "'");
    }
}

void
CombatSystem::handleTargetDeath(int targetId, CombatTargetType targetType)
{
    if (targetType == CombatTargetType::PLAYER)
    {
        // Логика смерти игрока - отнимаем опыт
        try
        {
            auto &experienceManager = gameServices_->getExperienceManager();
            auto characterData = gameServices_->getCharacterManager().getCharacterData(targetId);

            // Guard: character may have been healed between the death event and this handler
            // (e.g. HoT tick fired concurrently). Also prevents double-death penalty.
            if (characterData.characterCurrentHealth > 0)
            {
                gameServices_->getLogger().log("Player " + std::to_string(targetId) +
                                               " handleTargetDeath called but HP=" + std::to_string(characterData.characterCurrentHealth) + " — skipping penalty");
                return;
            }

            int penaltyAmount = experienceManager.calculateDeathPenalty(characterData.characterLevel, characterData.characterExperiencePoints);

            if (penaltyAmount > 0)
            {
                // Set experience debt instead of removing XP immediately.
                // The debt will be paid off gradually as the player earns XP (50% per gain).
                characterData.experienceDebt += penaltyAmount;
                gameServices_->getCharacterManager().loadCharacterData(characterData);
                gameServices_->getLogger().log("Player " + std::to_string(targetId) + " died — experience debt set to " +
                                               std::to_string(characterData.experienceDebt));
            }
            else
            {
                log_->info("Player " + std::to_string(targetId) + " died but no experience debt was added");
            }
        }
        catch (const std::exception &e)
        {
            gameServices_->getLogger().logError("Error handling player death experience penalty: " + std::string(e.what()));
        }

        // Durability: death penalty — reduce all equipped durable items
        try
        {
            float penaltyPct = gameServices_->getGameConfigService().getFloat("durability.death_penalty_pct", 0.05f);
            auto equipped = gameServices_->getInventoryManager().getEquippedItems(targetId);
            for (const auto &invSlot : equipped)
            {
                const auto &iData = gameServices_->getItemManager().getItemById(invSlot.itemId);
                if (!iData.isDurable || iData.durabilityMax <= 0)
                    continue;
                int penalty = static_cast<int>(std::ceil(iData.durabilityMax * penaltyPct));
                int cur = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : iData.durabilityMax;
                int newDur = std::max(0, cur - penalty);
                gameServices_->getInventoryManager().updateDurability(targetId, invSlot.id, newDur);
                saveDurabilityChange(targetId, invSlot.id, newDur);
                checkAndTriggerDurabilityWarning(targetId, cur, newDur, iData.durabilityMax);
            }
        }
        catch (const std::exception &e)
        {
            log_->warn("[COMBAT] Death durability penalty error: " + std::string(e.what()));
        }

        // Send final stats snapshot after all death penalties (XP, durability) are applied.
        // This covers DoT/AoE/mob-attack death paths that have no sendStatsUpdate after
        // handleTargetDeath. The direct-skill path sends an additional one shortly after,
        // which is harmless (client just replaces state with the latest values).
        try
        {
            gameServices_->getStatsNotificationService().sendStatsUpdate(targetId);
        }
        catch (const std::exception &e)
        {
            log_->warn("[COMBAT] Death stats update error: " + std::string(e.what()));
        }
    }
    else if (targetType == CombatTargetType::MOB)
    {
        // Логика смерти моба - начисляем опыт убийце
        log_->info("Mob " + std::to_string(targetId) + " died");

        // Найти кто убил моба (это должно передаваться отдельно, но пока используем последнего атаковавшего)
        // В будущем можно добавить систему threat/aggro для определения убийцы
        // Пока что эта логика будет вызываться из executeSkillUsage с передачей ID атакующего
    }
}

void
CombatSystem::handleMobDeath(int mobId, int killerId)
{
    try
    {
        // Получаем данные моба для расчета опыта
        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(mobId);
        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " (level " + std::to_string(mobData.level) + ") was killed by " + std::to_string(killerId));

        // Начисляем опыт убийце, если он является персонажем игрока
        try
        {
            auto killerData = gameServices_->getCharacterManager().getCharacterData(killerId);
            auto &experienceManager = gameServices_->getExperienceManager();

            // Вычисляем количество опыта за убийство моба
            // rankMult масштабирует XP: элитный моб (2.20x) даёт в 2.2 раза больше опыта
            const int scaledBaseXp = static_cast<int>(mobData.baseExperience * mobData.rankMult);
            int expAmount = experienceManager.calculateMobExperience(mobData.level, killerData.characterLevel, scaledBaseXp);

            if (expAmount > 0)
            {
                auto result = experienceManager.grantExperience(killerId, expAmount, "mob_kill", mobId);

                if (result.success)
                {
                    gameServices_->getLogger().log("Character " + std::to_string(killerId) +
                                                   " gained " + std::to_string(expAmount) +
                                                   " experience for killing mob " + std::to_string(mobId));

                    if (result.levelUp)
                    {
                        gameServices_->getLogger().log("Character " + std::to_string(killerId) +
                                                           " leveled up to level " + std::to_string(result.experienceEvent.newLevel),
                            CYAN);
                    }
                }
                else
                {
                    log_->error("Failed to grant experience: " + result.errorMessage);
                }
            }
        }
        catch (const std::exception &e)
        {
            // Убийца не является персонажем игрока (возможно, моб убил моба)
            log_->info("Killer " + std::to_string(killerId) + " is not a player character, no experience granted");
        }

        // --- Fellowship Bonus ---
        // If another player attacked this mob within the config window, both that
        // player and the killer receive a small bonus XP for fighting together.
        try
        {
            auto &cfg = gameServices_->getGameConfigService();
            const float bonusPct = cfg.getFloat("fellowship.bonus_pct", 0.07f);
            const int windowSec = cfg.getInt("fellowship.attack_window_sec", 15);

            auto mobMovData = gameServices_->getMobMovementManager().getMobMovementData(mobId);
            const auto now = std::chrono::steady_clock::now();

            std::vector<int> fellows;
            for (const auto &[charId, lastAttack] : mobMovData.attackerTimestamps)
            {
                if (charId == killerId)
                    continue;
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastAttack).count();
                if (elapsed <= windowSec)
                    fellows.push_back(charId);
            }

            // Anti-alt: resolve killer's accountId and exclude co-attackers from the same account
            const int killerAccountId = gameServices_->getClientManager().getClientDataByCharacterId(killerId).accountId;
            if (killerAccountId > 0)
            {
                fellows.erase(
                    std::remove_if(fellows.begin(), fellows.end(), [&](int fellowId)
                        {
                            int fellowAccountId = gameServices_->getClientManager().getClientDataByCharacterId(fellowId).accountId;
                            return (fellowAccountId > 0 && fellowAccountId == killerAccountId); }),
                    fellows.end());
            }

            if (!fellows.empty())
            {
                auto &expMgr = gameServices_->getExperienceManager();
                const int scaledBaseXp = static_cast<int>(mobData.baseExperience * mobData.rankMult);

                // Bonus to killer
                {
                    auto killerData = gameServices_->getCharacterManager().getCharacterData(killerId);
                    if (killerData.characterId != 0)
                    {
                        int bonus = static_cast<int>(
                            expMgr.calculateMobExperience(mobData.level, killerData.characterLevel, scaledBaseXp) * bonusPct);
                        if (bonus > 0)
                        {
                            expMgr.grantExperience(killerId, bonus, "fellowship_bonus", mobId);
                            gameServices_->getStatsNotificationService().sendWorldNotification(
                                killerId, "fellowship_bonus", "+" + std::to_string(bonus) + " Fellowship XP");
                        }
                    }
                }

                // Bonus to each fellow
                for (int fellowId : fellows)
                {
                    auto fellowData = gameServices_->getCharacterManager().getCharacterData(fellowId);
                    if (fellowData.characterId == 0)
                        continue;
                    int bonus = static_cast<int>(
                        expMgr.calculateMobExperience(mobData.level, fellowData.characterLevel, scaledBaseXp) * bonusPct);
                    if (bonus > 0)
                    {
                        expMgr.grantExperience(fellowId, bonus, "fellowship_bonus", mobId);
                        gameServices_->getStatsNotificationService().sendWorldNotification(
                            fellowId, "fellowship_bonus", "+" + std::to_string(bonus) + " Fellowship XP");
                        log_->info("[Fellowship] +" + std::to_string(bonus) + " XP \u2192 char " + std::to_string(fellowId));
                    }
                }

                log_->info("[Fellowship] Mob " + std::to_string(mobId) + " had " +
                           std::to_string(fellows.size()) + " fellow attacker(s)");
            }
        }
        catch (const std::exception &e)
        {
            log_->warn("[Fellowship] Error: " + std::string(e.what()));
        }

        // --- Item Soul: increment kill_count on killer's equipped weapon ---
        try
        {
            auto weapon = gameServices_->getInventoryManager().getEquippedWeapon(killerId);
            if (weapon.has_value())
            {
                const auto &wItem = gameServices_->getItemManager().getItemById(weapon->itemId);
                if (wItem.isEquippable)
                {
                    int newKillCount = weapon->killCount + 1;
                    gameServices_->getInventoryManager().updateItemKillCount(killerId, weapon->id, newKillCount);

                    // Debounce: only flush to DB at tier boundaries or every N kills to
                    // avoid hammering the game-server on every mob death.
                    const auto &cfg = gameServices_->getGameConfigService();
                    const int flushEvery = cfg.getInt("item_soul.db_flush_every_kills", 5);
                    const int t1 = cfg.getInt("item_soul.tier1_kills", 50);
                    const int t2 = cfg.getInt("item_soul.tier2_kills", 200);
                    const int t3 = cfg.getInt("item_soul.tier3_kills", 500);
                    const bool tierCrossed = (newKillCount == t1 || newKillCount == t2 || newKillCount == t3);
                    if (newKillCount % flushEvery == 0 || tierCrossed)
                        saveItemKillCountChange(killerId, weapon->id, newKillCount);

                    log_->info("[ItemSoul] Weapon invId=" + std::to_string(weapon->id) +
                               " killCount=" + std::to_string(newKillCount));
                }
            }
        }
        catch (const std::exception &e)
        {
            log_->warn("[ItemSoul] Error updating kill_count: " + std::string(e.what()));
        }

        // Quest trigger: notify QuestManager that the mob was killed
        try
        {
            gameServices_->getQuestManager().onMobKilled(killerId, mobData.id);
        }
        catch (...)
        {
        }

        // --- Bestiary: record kill for progression ---
        try
        {
            if (mobData.id > 0)
                gameServices_->getBestiaryManager().recordKill(killerId, mobData.id);
        }
        catch (const std::exception &e)
        {
            log_->warn("[Bestiary] Error recording kill: " + std::string(e.what()));
        }

        // --- Threshold Champion kill counter ---
        try
        {
            if (!mobData.isChampion && mobData.id > 0)
            {
                auto gameZone = gameServices_->getGameZoneManager().getZoneForPosition(mobData.position);
                if (gameZone.has_value())
                    gameServices_->getChampionManager().recordMobKill(gameZone->id, mobData.id);
            }
        }
        catch (const std::exception &e)
        {
            log_->warn("[Champion] Error recording mob kill: " + std::string(e.what()));
        }

        // --- Champion death notification ---
        if (mobData.isChampion)
        {
            try
            {
                gameServices_->getChampionManager().onChampionKilled(mobId, killerId, mobData.slug);
            }
            catch (const std::exception &e)
            {
                log_->warn("[Champion] Error handling champion death: " + std::string(e.what()));
            }
        }

        // --- Reputation: mob kill → faction rep change ---
        try
        {
            if (!mobData.factionSlug.empty() && mobData.repDeltaPerKill != 0)
            {
                gameServices_->getReputationManager().changeReputation(
                    killerId, mobData.factionSlug, mobData.repDeltaPerKill);
            }
        }
        catch (const std::exception &e)
        {
            log_->warn("[Reputation] Error on mob kill: " + std::string(e.what()));
        }

        // Вызываем общую логику смерти цели (для совместимости)
        handleTargetDeath(mobId, CombatTargetType::MOB);
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error handling mob death: " + std::string(e.what()));
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
            mobMovementManager.handleMobAttacked(targetId, attackerId, damage);

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
CombatSystem::broadcastMobSkillInitiation(int mobId, int targetPlayerId, const SkillStruct &skill)
{
    if (!responseBuilder_ || !broadcastCallback_)
        return;

    SkillInitiationResult r;
    r.success = true;
    r.casterId = mobId;
    r.targetId = targetPlayerId;
    r.targetType = CombatTargetType::PLAYER;
    r.skillName = skill.skillName;
    r.skillSlug = skill.skillSlug;
    r.skillEffectType = skill.skillEffectType;
    r.skillSchool = skill.school;
    r.castTime = static_cast<float>(skill.castMs) / 1000.0f;
    r.animationName = skill.animationName.empty() ? "skill_" + skill.skillSlug : skill.animationName;
    {
        constexpr float kMargin = 0.05f;
        float kSwing = static_cast<float>(skill.swingMs) / 1000.0f;
        float cdSec = static_cast<float>(std::max(skill.cooldownMs, skill.gcdMs)) / 1000.0f;
        if (cdSec < 0.5f)
            cdSec = 0.5f;
        float cycleTime = r.castTime + kSwing + cdSec;
        r.animationDuration = std::min(r.castTime + kSwing, cycleTime - kMargin);
        if (r.animationDuration < kSwing)
            r.animationDuration = kSwing;
    }
    broadcastCallback_(responseBuilder_->buildSkillInitiationBroadcast(r));
    log_->info("[AI] combatInitiation sent for mob " + std::to_string(mobId) + " skill: " + skill.skillName);
}

void
CombatSystem::processAIAttack(int mobId, int targetPlayerId, const std::string &forcedSkillSlug, float hitDelay)
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
        // Берём актуальную позицию моба из MobMovementManager, а не из устаревшего
        // снимка MobInstanceManager (который не обновляется пока моб стоит).
        auto mobMoveData = gameServices_->getMobMovementManager().getMobMovementData(mobId);
        const PositionStruct &mobPos = (mobMoveData.combatState != MobCombatState::PATROLLING ||
                                           mobMoveData.targetPlayerId != 0)
                                           ? mobMoveData.lastSentPosition
                                           : mobData.position;
        float dx = mobPos.positionX - targetPlayer.characterPosition.positionX;
        float dy = mobPos.positionY - targetPlayer.characterPosition.positionY;
        float distance = std::sqrt(dx * dx + dy * dy);

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " targeting player " + std::to_string(targetPlayerId) + " at distance " + std::to_string(distance));

        // Выбираем скил: если MobAIController уже выбрал скил (forcedSkillSlug),
        // используем его напрямую. Иначе — CombatSystem выбирает лучший скил сам.
        std::optional<std::reference_wrapper<const SkillStruct>> forcedOpt;
        if (!forcedSkillSlug.empty())
        {
            for (const auto &skill : mobData.skills)
            {
                if (skill.skillSlug == forcedSkillSlug)
                {
                    forcedOpt = std::cref(skill);
                    break;
                }
            }
            if (!forcedOpt)
            {
                log_->info("[WARN] Mob " + std::to_string(mobId) +
                           " forced skill [" + forcedSkillSlug + "] not found — falling back to auto-select");
            }
        }

        auto bestSkillOpt = forcedOpt ? forcedOpt : skillSystem_->getBestSkillForMob(mobData, targetPlayer, distance);

        if (!bestSkillOpt)
        {
            log_->info("Mob " + std::to_string(mobId) + " found no suitable skills for target");
            return; // Нет подходящих скилов
        }
        const SkillStruct &bestSkill = bestSkillOpt->get();

        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " will use skill: " + bestSkill.skillName + " on player " + std::to_string(targetPlayerId));

        // combatInitiation was already sent by broadcastMobSkillInitiation() when
        // MobAIController entered PREPARING_ATTACK. Here we only execute the attack.
        gameServices_->getLogger().log("Mob " + std::to_string(mobId) + " executing skill: " + bestSkill.skillSlug + " on player " + std::to_string(targetPlayerId));
        auto skillResult = skillSystem_->useSkill(mobId, bestSkill.skillSlug, targetPlayer.characterId, CombatTargetType::PLAYER);

        // Создаем результат выполнения
        SkillExecutionResult result;
        result.success = skillResult.success;
        result.casterId = mobId;
        result.targetId = targetPlayer.characterId;
        result.targetType = CombatTargetType::PLAYER;
        result.skillName = bestSkill.skillName;
        result.skillSlug = bestSkill.skillSlug;
        result.skillEffectType = bestSkill.skillEffectType;
        result.skillSchool = bestSkill.school;
        result.skillResult = skillResult;
        result.errorMessage = skillResult.errorMessage;
        result.hitDelay = hitDelay;

        if (result.success)
        {
            // Применяем урон к игроку
            if (skillResult.damageResult.totalDamage > 0)
            {
                try
                {
                    auto hpResult = gameServices_->getCharacterManager().applyDamageToCharacter(
                        targetPlayer.characterId, skillResult.damageResult.totalDamage);

                    // Suppress regen: mob hit the player
                    gameServices_->getCharacterManager().markCharacterInCombat(targetPlayer.characterId);

                    result.finalTargetHealth = hpResult.newHealth;
                    result.finalTargetMana = hpResult.currentMana;
                    result.targetDied = hpResult.died;
                    result.healthPopulated = true;

                    gameServices_->getLogger().log("Mob " + mobData.name + " dealt " + std::to_string(skillResult.damageResult.totalDamage) +
                                                   " damage to " + targetPlayer.characterName +
                                                   " (Health: " + std::to_string(hpResult.newHealth) + "/" + std::to_string(targetPlayer.characterMaxHealth) + ")");

                    // Durability: equipped armor loses durability on received hit
                    try
                    {
                        int armorLoss = static_cast<int>(gameServices_->getGameConfigService().getFloat("durability.armor_loss_per_hit", 1.0f));
                        auto equipped = gameServices_->getInventoryManager().getEquippedItems(targetPlayer.characterId);
                        for (const auto &invSlot : equipped)
                        {
                            const auto &iData = gameServices_->getItemManager().getItemById(invSlot.itemId);
                            if (!iData.isDurable || iData.equipSlotSlug == "weapon" || iData.durabilityMax <= 0)
                                continue;
                            int cur = (invSlot.durabilityCurrent > 0) ? invSlot.durabilityCurrent : iData.durabilityMax;
                            int newDur = std::max(0, cur - armorLoss);
                            gameServices_->getInventoryManager().updateDurability(targetPlayer.characterId, invSlot.id, newDur);
                            saveDurabilityChange(targetPlayer.characterId, invSlot.id, newDur);
                            checkAndTriggerDurabilityWarning(targetPlayer.characterId, cur, newDur, iData.durabilityMax);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        log_->warn("[COMBAT] Armor durability update error: " + std::string(e.what()));
                    }

                    if (result.targetDied)
                    {
                        handleTargetDeath(targetPlayer.characterId, CombatTargetType::PLAYER);
                        log_->info("Player " + targetPlayer.characterName + " died from mob attack");
                    }
                }
                catch (const std::exception &e)
                {
                    gameServices_->getLogger().logError("Error applying damage to player: " + std::string(e.what()));
                }
            }

            // Убеждаемся, что finalTargetHealth и finalTargetMana установлены в любом случае
            // Это важно для случаев когда атака промахивается (isMissed = true)
            if (!result.healthPopulated)
            {
                try
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetPlayer.characterId);
                    result.finalTargetHealth = targetData.characterCurrentHealth;
                    result.finalTargetMana = targetData.characterCurrentMana;
                }
                catch (const std::exception &e)
                {
                    gameServices_->getLogger().logError("Error getting target health/mana for AI attack result: " + std::string(e.what()));
                }
            }

            log_->info("Mob " + mobData.name + " used " + bestSkill.skillName +
                       " on " + targetPlayer.characterName +
                       " for " + std::to_string(result.skillResult.damageResult.totalDamage) + " damage");

            // Отправляем broadcast пакеты для AI атаки
            if (responseBuilder_ && broadcastCallback_)
            {
                log_->info("Mob " + std::to_string(mobId) + " sending broadcast for skill execution");
                auto broadcast = responseBuilder_->buildSkillExecutionBroadcast(result);
                broadcastCallback_(broadcast);
                log_->info("AI skill execution broadcast sent for: " + bestSkill.skillName);
            }
            else
            {
                log_->error("Mob " + std::to_string(mobId) + " broadcast failed - responseBuilder or callback missing");
            }
        }
        else
        {
            log_->error("Mob " + std::to_string(mobId) + " skill execution failed: " + result.errorMessage);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error in AI attack with target: " + std::string(e.what()));
    }
}

void
CombatSystem::setSaveDurabilityCallback(std::function<void(const std::string &)> callback)
{
    saveDurabilityCallback_ = std::move(callback);
}

void
CombatSystem::setRefreshAttributesCallback(std::function<void(int)> callback)
{
    refreshAttributesCallback_ = std::move(callback);
}

void
CombatSystem::checkAndTriggerDurabilityWarning(int characterId, int oldDur, int newDur, int maxDur)
{
    if (!refreshAttributesCallback_ || maxDur <= 0)
        return;
    float threshold = gameServices_->getGameConfigService().getFloat("durability.warning_threshold_pct", 0.30f);
    bool wasAbove = (static_cast<float>(oldDur) / maxDur) >= threshold;
    bool nowBelow = (static_cast<float>(newDur) / maxDur) < threshold;
    // Also trigger when item becomes fully broken (or repaired back to full in future)
    if ((wasAbove && nowBelow) || (oldDur > 0 && newDur == 0))
        refreshAttributesCallback_(characterId);
}

void
CombatSystem::setSaveItemKillCountCallback(std::function<void(const std::string &)> callback)
{
    saveItemKillCountCallback_ = std::move(callback);
}

void
CombatSystem::saveItemKillCountChange(int characterId, int inventoryItemId, int killCount)
{
    if (!saveItemKillCountCallback_)
        return;
    nlohmann::json packet;
    packet["header"]["eventType"] = "saveItemKillCount";
    packet["header"]["clientId"] = 0;
    packet["header"]["hash"] = "";
    packet["body"]["characterId"] = characterId;
    packet["body"]["inventoryItemId"] = inventoryItemId;
    packet["body"]["killCount"] = killCount;
    saveItemKillCountCallback_(packet.dump() + "\n");
}

void
CombatSystem::saveDurabilityChange(int characterId, int inventoryItemId, int durabilityCurrent)
{
    if (!saveDurabilityCallback_)
        return;
    nlohmann::json packet;
    packet["header"]["eventType"] = "saveDurabilityChange";
    packet["header"]["clientId"] = 0;
    packet["header"]["hash"] = "";
    packet["body"]["characterId"] = characterId;
    packet["body"]["inventoryItemId"] = inventoryItemId;
    packet["body"]["durabilityCurrent"] = durabilityCurrent;
    saveDurabilityCallback_(packet.dump() + "\n");
}
