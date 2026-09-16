#include "services/CombatSystem.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatResponseBuilder.hpp"
#include "services/GameServices.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/SkillInitiationValidator.hpp"
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
    : gameServices_(gameServices),
      durability_(gameServices_->getInventoryManager(),
          gameServices_->getItemManager(),
          gameServices_->getGameConfigService(),
          gameServices_->getLogger()),
      rewardPipeline_(gameServices_->getCharacterManager(),
          gameServices_->getMobInstanceManager(),
          gameServices_->getMobMovementManager(),
          gameServices_->getGameZoneManager(),
          gameServices_->getExperienceManager(),
          gameServices_->getInventoryManager(),
          gameServices_->getItemManager(),
          gameServices_->getGameConfigService(),
          gameServices_->getClientManager(),
          gameServices_->getBestiaryManager(),
          gameServices_->getChampionManager(),
          gameServices_->getReputationManager(),
          gameServices_->getLogger()),
      deathPipeline_(gameServices_->getCharacterManager(),
          gameServices_->getExperienceManager(),
          durability_,
          gameServices_->getGameZoneManager(),
          gameServices_->getLogger())
{
    log_ = gameServices_->getLogger().getSystem("combat");
    skillSystem_ = std::make_unique<SkillSystem>(gameServices_->getCharacterManager(),
        gameServices_->getMobInstanceManager(),
        gameServices_->getMobManager(),
        gameServices_->getMobMovementManager(),
        gameServices_->getCooldownService(),
        gameServices_->getLogger());
    responseBuilder_ = std::make_unique<CombatResponseBuilder>(
        gameServices_->getCharacterManager(),
        gameServices_->getMobInstanceManager(),
        gameServices_->getLogger());
    durability_.setNotifyCallback(
        [this](int characterId, const std::string &type, const nlohmann::json &data,
            const std::string &priority, const std::string &channel)
        {
            gameServices_->getStatsNotificationService().sendWorldNotification(
                characterId, type, data, priority, channel);
        });
    rewardPipeline_.setQuestHook(
        [this](int killerId, int mobTemplateId)
        {
            gameServices_->getQuestManager().onMobKilled(killerId, mobTemplateId);
        });
    rewardPipeline_.setNotifyCallback(
        [this](int characterId, const std::string &type, const nlohmann::json &data,
            const std::string &priority, const std::string &channel)
        {
            gameServices_->getStatsNotificationService().sendWorldNotification(
                characterId, type, data, priority, channel);
        });
    rewardPipeline_.setStatsUpdateCallback(
        [this](int characterId)
        {
            gameServices_->getStatsNotificationService().sendStatsUpdate(characterId);
        });
    rewardPipeline_.setAnalyticsCallback(
        [this](const std::string &packet)
        {
            gameServices_->sendAnalytics(packet);
        });
    rewardPipeline_.setSaveKillCountCallback(
        [this](int characterId, int inventoryItemId, int killCount)
        {
            saveItemKillCountChange(characterId, inventoryItemId, killCount);
        });
    deathPipeline_.setStatsUpdateCallback(
        [this](int characterId)
        {
            gameServices_->getStatsNotificationService().sendStatsUpdate(characterId);
        });
    deathPipeline_.setAnalyticsCallback(
        [this](const std::string &packet)
        {
            gameServices_->sendAnalytics(packet);
        });
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

        // Проверяем базовые требования без сайд-эффектов (читать состояние)
        // перед мутирующими операциями (трата маны, установка кулдауна).
        // Порядок важен: early-exit без изменений состояния → кулдаун только при
        // полной валидации. Предикат — чистый SkillInitiationValidator
        // (already-casting → mana → range → alive → PvP); здесь только сбор данных.
        InitiationCheckInput check;
        check.skillCostMp = skill.costMp;
        check.skillMaxRange = skill.maxRange;
        check.skillEffect = skill.skillEffectType;
        check.targetType = targetType;
        check.isSelfTarget = (targetId == casterId);
        std::string ongoingSkillSlug;

        // Проверяем, что у кастера нет активного каста — во время каста нельзя использовать
        // ни другой каст, ни мгновенный скил.
        {
            std::lock_guard<std::mutex> lock(actionsMutex_);
            auto it = ongoingActions_.find(casterId);
            if (it != ongoingActions_.end() && it->second->state == CombatActionState::CASTING)
            {
                check.alreadyCasting = true;
                ongoingSkillSlug = it->second->skillSlug;
            }
        }

        // HIGH-8: mana check without exceptions — must happen before cooldown is set
        {
            auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (characterData.characterId != 0) // player
            {
                check.casterMana = characterData.characterCurrentMana;
            }
            else
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                if (mobData.uid != 0)
                    check.casterMana = mobData.currentMana;
                else
                    check.casterMana = skill.costMp; // unreachable (caster checked above) — pass through
            }
        }

        // Range check: reject before creating the ongoing action so that
        // combatInitiation is never sent as "success" when the target is out
        // of reach.  Mirrors the identical logic in SkillSystem::isInRange.
        if (targetType != CombatTargetType::AREA && targetType != CombatTargetType::NONE)
        {
            PositionStruct casterPos{}, targetPos{};
            bool posValid = true;

            // Caster position
            {
                auto characterData = gameServices_->getCharacterManager().getCharacterData(casterId);
                if (characterData.characterId != 0)
                    casterPos = characterData.characterPosition;
                else
                {
                    auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                    if (mobData.uid != 0)
                        casterPos = mobData.position;
                    else
                        posValid = false;
                }
            }

            // Target position
            if (posValid)
            {
                if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                    if (targetData.characterId != 0)
                        targetPos = targetData.characterPosition;
                    else
                        posValid = false;
                }
                else if (targetType == CombatTargetType::MOB)
                {
                    auto mobMoveData = gameServices_->getMobMovementManager().getMobMovementData(targetId);
                    const PositionStruct &lastSent = mobMoveData.lastSentPosition;
                    if (lastSent.positionX != 0.0f || lastSent.positionY != 0.0f)
                        targetPos = lastSent;
                    else
                    {
                        auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
                        if (mobData.uid != 0)
                            targetPos = mobData.position;
                        else
                            posValid = false;
                    }
                }
            }

            if (posValid)
            {
                float dx = casterPos.positionX - targetPos.positionX;
                float dy = casterPos.positionY - targetPos.positionY;
                check.checkRange = true;
                check.distance = std::sqrt(dx * dx + dy * dy);
            }
        }

        // Target alive check: reject initiation if target is dead.
        // Must happen before cooldown is claimed.
        if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
            check.targetKnown = (mobData.uid != 0);
            check.targetAlive = !mobData.isDead;
        }
        else if (targetType == CombatTargetType::PLAYER && targetId != casterId)
        {
            auto charData = gameServices_->getCharacterManager().getCharacterData(targetId);
            check.targetKnown = (charData.characterId != 0);
            check.targetAlive = !charData.isDead;
        }

        switch (validateSkillInitiation(check))
        {
            case InitiationReject::AlreadyCasting:
                log_->warn("[initiateSkillUsage] Caster " + std::to_string(casterId) +
                           " is already casting '" + ongoingSkillSlug + "' — rejecting skill '" + skillSlug + "'");
                result.errorMessage = "Already casting";
                return result;
            case InitiationReject::InsufficientMana:
                result.errorMessage = "Not enough mana";
                return result;
            case InitiationReject::OutOfRange:
                log_->warn("[initiateSkillUsage] Target " + std::to_string(targetId) +
                           " out of range for caster " + std::to_string(casterId) +
                           " distance=" + std::to_string(check.distance) +
                           " maxRange=" + std::to_string(skill.maxRange * 100.0f));
                result.errorMessage = "Target is out of range";
                return result;
            case InitiationReject::TargetDead:
                if (targetType == CombatTargetType::MOB)
                {
                    log_->warn("[COMBAT] Target mob {} is dead — rejecting initiation by caster {}",
                        targetId, casterId);
                }
                else
                {
                    log_->warn("[COMBAT] Target player {} is dead — rejecting initiation by caster {}",
                        targetId, casterId);
                }
                result.errorMessage = "Target is dead";
                return result;
            case InitiationReject::PvpBlocked:
                log_->warn("[COMBAT] PvP blocked during initiation: caster {} -> player {} ({})",
                    casterId, targetId, skillSlug);
                result.errorMessage = "PvP is not available";
                return result;
            case InitiationReject::None:
                break;
        }

        // Атомарная проверка + установка кулдауна — только после валидации
        // всех не-мутирующих требований выше (каст, мана, дистанция, alive, PvP).
        // executeSkillUsage will pass cooldownAlreadySet=true to useSkill so that
        // the cooldown is not re-checked (it would look "on cooldown" and fail).
        {
            bool onGCD = false;
            if (!skillSystem_->trySetCooldown(casterId, skillSlug, skill.cooldownMs, skill.gcdMs, &onGCD))
            {
                if (onGCD)
                {
                    log_->warn("[initiateSkillUsage] GCD active for caster " + std::to_string(casterId) +
                               " skill='" + skillSlug + "'");
                    result.errorMessage = "Global cooldown active";
                }
                else
                {
                    log_->warn("[initiateSkillUsage] Skill '" + skillSlug + "' is on cooldown for caster " +
                               std::to_string(casterId));
                    result.errorMessage = "Skill is on cooldown";
                }
                return result;
            }

            // Persist cooldown for player characters so it survives reconnects.
            if (skill.cooldownMs > 0)
            {
                auto charCheck = gameServices_->getCharacterManager().getCharacterData(casterId);
                log_->info("[CooldownPersist] char={} skill={} cooldownMs={} charCheck.characterId={}",
                    casterId,
                    skillSlug,
                    skill.cooldownMs,
                    charCheck.characterId);
                if (charCheck.characterId != 0)
                {
                    int64_t cooldownEndsAtMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count() +
                        skill.cooldownMs;
                    log_->info("[CooldownPersist] sending persist for char={} skill={} endsAtMs={}",
                        casterId,
                        skillSlug,
                        cooldownEndsAtMs);
                    gameServices_->sendSkillCooldownPersist(casterId, skillSlug, cooldownEndsAtMs);
                }
            }
            else
            {
                log_->info("[CooldownPersist] skipped — cooldownMs=0 for char={} skill={}", casterId, skillSlug);
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

        // ── Apply attack_speed / cast_speed modifier ─────────────────────────
        // Formula: effectiveMs = baseMs / (1 + speed_stat / divisor)
        // For castMs > 0 (spells): use cast_speed
        // For castMs == 0 (melee/instant): use attack_speed on swingMs
        float speedFactor = 1.0f;
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (casterData.characterId != 0)
            {
                auto &gameCfg = gameServices_->getGameConfigService();
                if (skill.castMs > 0)
                {
                    int castSpd = 0;
                    for (const auto &a : casterData.attributes)
                        if (a.slug == "cast_speed")
                        {
                            castSpd = a.value;
                            break;
                        }
                    float divisor = gameCfg.getFloat("combat.cast_speed_base_divisor", 100.0f);
                    speedFactor = 1.0f / (1.0f + static_cast<float>(castSpd) / divisor);
                }
                else
                {
                    int atkSpd = 0;
                    for (const auto &a : casterData.attributes)
                        if (a.slug == "attack_speed")
                        {
                            atkSpd = a.value;
                            break;
                        }
                    float divisor = gameCfg.getFloat("combat.attack_speed_base_divisor", 100.0f);
                    speedFactor = 1.0f / (1.0f + static_cast<float>(atkSpd) / divisor);
                }
            }
        }

        action->castTime = static_cast<float>(skill.castMs) / 1000.0f * speedFactor;
        // Skills with castMs > 0 are deferred (CASTING); castMs == 0 are instant.
        action->state = (skill.castMs > 0) ? CombatActionState::CASTING : CombatActionState::EXECUTING;
        action->startTime = std::chrono::steady_clock::now();
        // Cooldown was already claimed by trySetCooldown above; mark this so
        // executeSkillUsage (called later by updateOngoingActions) skips the
        // duplicate cooldown check inside useSkill.
        action->cooldownPreset = true;
        float kSwing = static_cast<float>(skill.swingMs) / 1000.0f * speedFactor;

        action->animationName = skill.animationName.empty() ? "skill_" + skillSlug : skill.animationName;
        {
            // animationDuration = castTime + swingTime (full animation coverage; no capping).

            action->animationDuration = action->castTime + kSwing;
        }

        // Fire the result at the exact end of effective cast time.
        int effectiveCastMs = static_cast<int>(std::lround(skill.castMs * speedFactor));
        int effectiveSwingMs = static_cast<int>(std::lround(skill.swingMs * speedFactor));
        action->endTime = action->startTime + std::chrono::milliseconds(effectiveCastMs - effectiveSwingMs);

        // Сохраняем ongoing action
        {
            std::lock_guard<std::mutex> lock(actionsMutex_);
            ongoingActions_[casterId] = action;
        }

        result.success = true;
        result.castTime = action->castTime;
        result.animationName = action->animationName;
        result.animationDuration = action->animationDuration;
        result.cooldownMs = skill.cooldownMs;
        result.gcdMs = skill.gcdMs;
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                         .count();
        result.serverTimestamp = nowMs;
        result.castStartedAt = nowMs;
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error initiating combat action: " + std::string(e.what());
        gameServices_->getLogger().logError("CombatSystem::initiateCombatAction error: " + std::string(e.what()));
    }

    return result;
}

SkillExecutionResult
CombatSystem::executeSkillUsage(int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType, bool cooldownAlreadySet)
{
    SkillExecutionResult result;
    result.casterId = casterId;
    result.targetId = targetId;
    result.targetType = targetType;
    result.skillSlug = skillSlug;
    // Set serverTimestamp immediately so it is present even on early-return failure paths
    result.serverTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                 .count();

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
            result.success = executeAoESkillUsage(casterId, skillSlug, cooldownAlreadySet);
            return result;
        }

        // Teleport skills are self-only — reject any attempt to cast on another entity
        if (skillOpt.has_value() && skillOpt->skillEffectType == "teleport_respawn")
        {
            if (targetType != CombatTargetType::SELF || targetId != casterId)
            {
                result.errorMessage = "Teleport skills can only target yourself";
                return result;
            }
        }

        // Re-validate target is still alive before spending resources (HIGH-8 style)
        if (targetType == CombatTargetType::MOB)
        {
            auto mobData = gameServices_->getMobInstanceManager().getMobInstance(targetId);
            if (mobData.uid == 0 || mobData.isDead)
            {
                result.errorMessage = "Target is dead";
                return result;
            }
        }
        else if (targetType == CombatTargetType::PLAYER)
        {
            // Block self-damage via PLAYER target type
            if (targetId == casterId)
            {
                result.errorMessage = "Cannot target yourself with this skill";
                return result;
            }

            auto charData = gameServices_->getCharacterManager().getCharacterData(targetId);
            if (charData.characterId == 0 || charData.isDead)
            {
                result.errorMessage = "Target is dead";
                return result;
            }
        }

        // Выполняем скил через SkillSystem
        // Pass cooldownAlreadySet so that skills initiated via initiateSkillUsage
        // do not attempt to re-set the cooldown (which would fail because it was
        // already claimed at cast-start time).
        auto skillResult = skillSystem_->useSkill(casterId, skillSlug, targetId, targetType, cooldownAlreadySet);
        result.skillResult = skillResult;

        if (!skillResult.success)
        {
            result.errorMessage = skillResult.errorMessage;
            return result;
        }

        // Применяем эффекты
        applySkillEffects(skillResult, casterId, skillSlug, targetId, targetType, &result);

        // Проверяем смерть цели
        if (skillResult.damageResult.totalDamage > 0)
        {
            // PvP guard: damage-dealing skills cannot target other players until a
            // PvP consent system (zone flags, duel, etc.) is implemented.
            if (targetType == CombatTargetType::PLAYER && targetId != casterId)
            {
                log_->warn("[COMBAT] PvP damage blocked: caster {} -> player {}", casterId, targetId);
                result.errorMessage = "PvP is not available";
                result.success = false;
                try
                {
                    auto targetData = gameServices_->getCharacterManager().getCharacterData(targetId);
                    result.finalTargetHealth = targetData.characterCurrentHealth;
                    result.finalTargetMana = targetData.characterCurrentMana;
                    result.healthPopulated = true;
                }
                catch (...)
                {
                }
                return result;
            }

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

                        // Durability + Mastery: single weapon fetch for both (HIGH-8 style)
                        try
                        {
                            durability_.applyWeaponHitWear(casterId);

                            auto weapon = gameServices_->getInventoryManager().getEquippedWeapon(casterId);
                            if (weapon.has_value())
                            {
                                const auto &wItem = gameServices_->getItemManager().getItemById(weapon->itemId);

                                // Mastery: player gains weapon mastery XP on hit
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
                            log_->warn("[COMBAT] Weapon durability/mastery update error: " + std::string(e.what()));
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

        // Mark caster as in-combat so their regen is suppressed while fighting
        {
            auto casterChar = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (casterChar.characterId != 0)
                gameServices_->getCharacterManager().markCharacterInCombat(casterId);
        }

        // Capture caster's remaining mana after skill consumed it
        {
            auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
            if (casterData.characterId != 0)
                result.finalCasterMana = casterData.characterCurrentMana;
            else
            {
                auto mobData = gameServices_->getMobInstanceManager().getMobInstance(casterId);
                if (mobData.uid != 0)
                    result.finalCasterMana = mobData.currentMana;
            }
        }

        result.success = true;
        // serverTimestamp already set at the top of this function.

        // Handle teleport_respawn skill: resolve nearest respawn zone and update in-memory position.
        // The actual network packets are sent by CombatEventHandler after execution returns.
        if (skillOpt.has_value() && skillOpt->skillEffectType == "teleport_respawn")
        {
            try
            {
                PositionStruct casterPos =
                    gameServices_->getCharacterManager().getCharacterPosition(casterId);
                RespawnZoneStruct zone =
                    gameServices_->getRespawnZoneManager().findNearest(casterPos);

                if (zone.id > 0)
                {
                    PositionStruct dest = gameServices_->getRespawnZoneManager().getRandomPointInZone(zone);
                    dest.rotationZ = casterPos.rotationZ;

                    // Update in-memory position so the server state is immediately consistent.
                    gameServices_->getCharacterManager().setCharacterPosition(casterId, dest);

                    result.hasTeleport = true;
                    result.teleportPosition = dest;
                }
                else
                {
                    log_->warn("[COMBAT] teleport_respawn: no respawn zone found for caster {}", casterId);
                }
            }
            catch (const std::exception &te)
            {
                log_->warn("[COMBAT] teleport_respawn error for caster {}: {}", casterId, te.what());
            }
        }

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
CombatSystem::clearOngoingAction(int casterId)
{
    std::lock_guard<std::mutex> lock(actionsMutex_);
    ongoingActions_.erase(casterId);
}

std::vector<SkillExecutionResult>
CombatSystem::updateOngoingActions()
{
    auto now = std::chrono::steady_clock::now();

    // Snapshot actions that are ready to execute, erase them under lock,
    // then execute outside the lock to avoid holding it during heavy work.
    std::vector<std::tuple<int, std::string, int, CombatTargetType, std::string, bool>> toExecute;
    {
        std::lock_guard<std::mutex> lock(actionsMutex_);
        for (auto it = ongoingActions_.begin(); it != ongoingActions_.end();)
        {
            auto &action = it->second;
            if (action->state == CombatActionState::CASTING && now >= action->endTime)
            {
                action->state = CombatActionState::EXECUTING;
                toExecute.emplace_back(action->casterId, action->skillSlug, action->targetId, action->targetType, action->actionName, action->cooldownPreset);
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

    std::vector<SkillExecutionResult> results;
    for (auto &[casterId, skillSlug, targetId, targetType, actionName, cooldownPreset] : toExecute)
    {
        auto result = executeSkillUsage(casterId, skillSlug, targetId, targetType, cooldownPreset);

        if (responseBuilder_ && broadcastCallback_)
        {
            auto broadcast = responseBuilder_->buildSkillExecutionBroadcast(result);
            broadcastCallback_(broadcast);
            log_->info("Skill execution broadcast sent for: " + actionName);
        }
        results.push_back(std::move(result));
    }
    return results;
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

bool
CombatSystem::executeAoESkillUsage(int casterId, const std::string &skillSlug, bool cooldownAlreadySet)
{
    try
    {
        // Let SkillSystem handle mana consumption, cooldown, and validation.
        // AREA targetType: validateTarget returns true, no damage calc inside useSkill.
        auto skillResult = skillSystem_->useSkill(casterId, skillSlug, 0, CombatTargetType::AREA, cooldownAlreadySet);
        if (!skillResult.success)
        {
            log_->error(
                "[AoE] useSkill failed for caster=" + std::to_string(casterId) +
                " skill=" + skillSlug + ": " + skillResult.errorMessage);
            return false;
        }

        auto skillOpt = skillSystem_->getCharacterSkill(casterId, skillSlug);
        if (!skillOpt.has_value())
            return false;
        const SkillStruct &skill = skillOpt.value();

        // Fetch caster data AFTER mana was consumed by useSkill above
        auto casterData = gameServices_->getCharacterManager().getCharacterData(casterId);
        const float cx = casterData.characterPosition.positionX;
        const float cy = casterData.characterPosition.positionY;
        const float radius = (skill.areaRadius > 0.0f) ? skill.areaRadius : 5.0f;
        const int maxHits = static_cast<int>(std::lround(
            gameServices_->getGameConfigService().getFloat("combat.aoe_target_cap", 10.0f)));
        int hitCount = 0;

        auto *calc = skillSystem_->getCombatCalculator();
        if (!calc)
            return false;

        // Mark caster as in-combat (suppress regen)
        if (casterData.characterId != 0)
            gameServices_->getCharacterManager().markCharacterInCombat(casterId);

        // Prepare batched result (one broadcast for all targets)
        AoESkillExecutionResult batchResult;
        batchResult.casterId = casterId;
        batchResult.skillSlug = skillSlug;
        batchResult.skillName = skill.skillName;
        batchResult.skillEffectType = skill.skillEffectType;
        batchResult.skillSchool = skill.school;
        batchResult.serverTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                                          .count();
        batchResult.finalCasterMana = (casterData.characterId != 0) ? casterData.characterCurrentMana : 0;

        // ---- Mob targets ----
        auto mobs = gameServices_->getMobInstanceManager().getMobsInRange(cx, cy, radius);
        for (const auto &mob : mobs)
        {
            if (hitCount >= maxHits)
                break;

            // Simplified player→mob path (mirror of SkillSystem::useSkill MOB branch)
            int dmg = calc->calculateBaseDamage(skill, casterData.attributes);
            bool isCrit = calc->rollCriticalHit(casterData.attributes);
            if (isCrit)
                dmg = static_cast<int>(std::lround(dmg * 2.0f));
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

            AoETargetResultEntry entry;
            entry.targetId = mob.uid;
            entry.targetType = CombatTargetType::MOB;
            entry.damage = dmg;
            entry.isCritical = isCrit;
            entry.isBlocked = false;
            entry.isMissed = false;
            entry.targetDied = upResult.mobDied;
            entry.finalTargetHealth = upResult.newHealth;
            batchResult.targets.push_back(entry);

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

            // PvP guard: AoE damage cannot hit other players (consistent with direct-target guard)
            log_->warn("[COMBAT] PvP AoE damage blocked: caster {} -> player {}", casterId, target.characterId);
            continue;

            auto dmgResult = calc->calculateSkillDamage(skill, casterData, target);
            if (dmgResult.isMissed)
                continue;

            auto hpAoE = gameServices_->getCharacterManager().applyDamageToCharacter(
                target.characterId, dmgResult.totalDamage);

            // Suppress regen for AoE targets
            gameServices_->getCharacterManager().markCharacterInCombat(target.characterId);

            AoETargetResultEntry entry;
            entry.targetId = target.characterId;
            entry.targetType = CombatTargetType::PLAYER;
            entry.damage = dmgResult.totalDamage;
            entry.isCritical = dmgResult.isCritical;
            entry.isBlocked = dmgResult.isBlocked;
            entry.isMissed = false; // already filtered above
            entry.targetDied = hpAoE.died;
            entry.finalTargetHealth = hpAoE.newHealth;
            batchResult.targets.push_back(entry);

            if (hpAoE.died)
            {
                handleTargetDeath(target.characterId, CombatTargetType::PLAYER);
            }
            else
            {
                // Apply DoT/debuff effects on surviving player targets (AoE — no persistence handle needed)
                applySkillEffects(skillResult, casterId, skillSlug, target.characterId, CombatTargetType::PLAYER, nullptr);
            }

            ++hitCount;
        }

        // Single batched broadcast for all AoE targets
        if (responseBuilder_ && broadcastCallback_)
            broadcastCallback_(responseBuilder_->buildAoESkillExecutionBroadcast(batchResult));

        // Caster stats update (mana consumed)
        gameServices_->getStatsNotificationService().sendStatsUpdate(casterId);

        gameServices_->getLogger().log(
            "[AoE] '" + skillSlug + "' by char " + std::to_string(casterId) +
            " hit " + std::to_string(hitCount) + " target(s) in r=" + std::to_string(radius));

        return true;
    }
    catch (const std::exception &ex)
    {
        gameServices_->getLogger().logError("[CombatSystem::executeAoESkillUsage] " + std::string(ex.what()));
        return false;
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
CombatSystem::applySkillEffects(const SkillUsageResult &result, int casterId, const std::string &skillSlug, int targetId, CombatTargetType targetType, SkillExecutionResult *execResult)
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

        // Record for persistence: CombatEventHandler will send saveActiveEffect to game server
        if (execResult)
            execResult->appliedEffects.push_back(eff);

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
        deathPipeline_.execute(targetId);
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
        rewardPipeline_.execute(mobId, killerId);

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
        float kSwing = static_cast<float>(skill.swingMs) / 1000.0f;
        r.animationDuration = r.castTime + kSwing;
    }
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                     .count();
    r.serverTimestamp = nowMs;
    r.castStartedAt = nowMs;
    broadcastCallback_(responseBuilder_->buildSkillInitiationBroadcast(r));
    log_->info("[AI] combatInitiation sent for mob " + std::to_string(mobId) + " skill: " + skill.skillName);
}

void
CombatSystem::processAIAttack(int mobId, int targetPlayerId, const std::string &forcedSkillSlug)
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
        result.serverTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
                                     .count();

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
                        durability_.applyArmorHitWear(targetPlayer.characterId);
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
    durability_.setSaveCallback(std::move(callback));
}

void
CombatSystem::setRefreshAttributesCallback(std::function<void(int)> callback)
{
    durability_.setRefreshAttributesCallback(std::move(callback));
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
CombatSystem::restoreSkillCooldown(int characterId, const std::string &skillSlug, int64_t remainingMs)
{
    skillSystem_->restoreCooldown(characterId, skillSlug, remainingMs);
}
