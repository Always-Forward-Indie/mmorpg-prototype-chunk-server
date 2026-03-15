#include "services/MobAIController.hpp"
#include "events/Event.hpp"
#include "events/EventData.hpp"
#include "events/EventQueue.hpp"
#include "services/CharacterManager.hpp"
#include "services/CombatSystem.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// checkAndTriggerFlee — call from any combat state when mob takes damage
// ---------------------------------------------------------------------------

bool
MobAIController::checkAndTriggerFlee(MobDataStruct &mob, MobMovementData &movementData, float currentTime, int attackerPlayerId)
{
    // Only flee if threshold is configured and mob has a valid max HP.
    if (mob.fleeHpThreshold <= 0.0f || mob.maxHealth <= 0)
        return false;

    // Already fleeing / returning / evading — don't re-trigger.
    if (movementData.combatState == MobCombatState::FLEEING ||
        movementData.combatState == MobCombatState::RETURNING ||
        movementData.combatState == MobCombatState::EVADING)
        return false;

    float hpRatio = static_cast<float>(mob.currentHealth) / static_cast<float>(mob.maxHealth);
    if (hpRatio >= mob.fleeHpThreshold)
        return false;

    // --- Compute flee destination ---
    // Run in the direction AWAY from the last attacker.
    float fleeVecX = 0.0f;
    float fleeVecY = 1.0f; // default: north
    if (attackerPlayerId > 0 && characterManager_)
    {
        auto attacker = characterManager_->getCharacterById(attackerPlayerId);
        if (attacker.characterId > 0)
        {
            float dx = mob.position.positionX - attacker.characterPosition.positionX;
            float dy = mob.position.positionY - attacker.characterPosition.positionY;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 0.0f)
            {
                fleeVecX = dx / dist;
                fleeVecY = dy / dist;
            }
        }
    }

    const float FLEE_DISTANCE = mob.aggroRange * mob.chaseMultiplier * 1.1f;
    movementData.fleeTargetPosition = mob.position;
    movementData.fleeTargetPosition.positionX = mob.position.positionX + fleeVecX * FLEE_DISTANCE;
    movementData.fleeTargetPosition.positionY = mob.position.positionY + fleeVecY * FLEE_DISTANCE;

    // Transition to FLEEING.
    movementData.combatState = MobCombatState::FLEEING;
    movementData.stateChangeTime = currentTime;
    movementData.fleeStartTime = currentTime;
    movementData.isFleeing = true;
    movementData.isBackpedaling = false;

    mobMovementManager_->updateMobMovementData(mob.uid, movementData);
    mobMovementManager_->forceMobStateUpdate(mob.uid);

    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " [" + mob.name +
                "] HP% " + std::to_string(static_cast<int>(hpRatio * 100)) +
                "% < threshold " + std::to_string(static_cast<int>(mob.fleeHpThreshold * 100)) +
                "% — entering FLEEING state");
    return true;
}

// ---------------------------------------------------------------------------
// Construction / dependency injection
// ---------------------------------------------------------------------------

MobAIController::MobAIController(Logger &logger)
    : logger_(logger),
      mobMovementManager_(nullptr),
      characterManager_(nullptr),
      eventQueue_(nullptr),
      combatSystem_(nullptr),
      mobInstanceManager_(nullptr),
      mobManager_(nullptr)
{
}

void
MobAIController::setMobMovementManager(MobMovementManager *mm)
{
    mobMovementManager_ = mm;
}
void
MobAIController::setCharacterManager(CharacterManager *cm)
{
    characterManager_ = cm;
}
void
MobAIController::setEventQueue(EventQueue *eq)
{
    eventQueue_ = eq;
}
void
MobAIController::setCombatSystem(CombatSystem *cs)
{
    combatSystem_ = cs;
}
void
MobAIController::setMobInstanceManager(MobInstanceManager *mi)
{
    mobInstanceManager_ = mi;
}
void
MobAIController::setMobManager(MobManager *mm)
{
    mobManager_ = mm;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

float
MobAIController::calculateDistance(const PositionStruct &a, const PositionStruct &b)
{
    float dx = a.positionX - b.positionX;
    float dy = a.positionY - b.positionY;
    return std::sqrt(dx * dx + dy * dy);
}

// ---------------------------------------------------------------------------
// selectAttackSkill — pick skill before entering PREPARING_ATTACK (plan §2.1)
// ---------------------------------------------------------------------------

std::optional<SkillStruct>
MobAIController::selectAttackSkill(const MobDataStruct &mob,
    const MobMovementData &movementData,
    float distanceToTarget)
{
    // Need template skills — skip if MobManager not wired up yet.
    if (!mobManager_)
        return std::nullopt;

    // getMobById returns by value; bind to a local copy so we own the data
    // and can safely return a copy of the chosen skill.
    MobDataStruct tmpl = mobManager_->getMobById(mob.id);
    if (tmpl.skills.empty())
        return std::nullopt;

    float currentTime = getCurrentGameTime();

    // Partition skills into "abilities" (cooldownMs > 0) and "basic attacks" (cooldownMs == 0).
    const SkillStruct *bestAbility = nullptr;
    const SkillStruct *bestBasic = nullptr;

    for (const auto &skill : tmpl.skills)
    {
        // Range filter: skip skills that can't reach the target.
        // skill.maxRange is stored in DB units (meters); positions are in position-units
        // that are 100x larger — consistent with SkillManager::isInRange and getBestSkillForMob.
        if (skill.maxRange > 0.0f && distanceToTarget > skill.maxRange * 100.0f)
            continue;

        if (skill.cooldownMs > 0)
        {
            // Per-skill cooldown check.
            auto it = movementData.skillLastUsedTime.find(skill.skillSlug);
            if (it != movementData.skillLastUsedTime.end())
            {
                float cdSec = static_cast<float>(skill.cooldownMs) / 1000.0f;
                if (currentTime - it->second < cdSec)
                    continue; // Still on cooldown.
            }
            if (!bestAbility)
                bestAbility = &skill; // First available ability wins.
        }
        else
        {
            // Basic attack — no cooldown tracking needed.
            if (!bestBasic)
                bestBasic = &skill;
        }
    }

    // Copy the chosen skill out of the local MobDataStruct before it
    // goes out of scope — returning a raw pointer would be a dangling pointer.
    const SkillStruct *best = bestAbility ? bestAbility : bestBasic;
    if (!best)
        return std::nullopt;
    return *best; // value copy, safe after tmpl is destroyed
}

bool
MobAIController::isTargetAlive(int targetPlayerId)
{
    if (!characterManager_)
        return false;

    auto player = characterManager_->getCharacterById(targetPlayerId);
    if (player.characterId == 0)
        return false;

    return player.characterCurrentHealth > 0;
}

bool
MobAIController::canAttackPlayer(const MobDataStruct &mob, int targetPlayerId, const MobMovementData &movementData)
{
    if (!characterManager_)
        return false;

    // NOTE: attack cooldown timing is already gated by the ATTACK_COOLDOWN combat state;
    // checking lastAttackTime here would conflict with postAttackCooldown and cause the
    // mob to spin between CHASING and PREPARING_ATTACK without ever landing a hit.

    // Check player existence + life
    auto targetPlayer = characterManager_->getCharacterById(targetPlayerId);
    if (targetPlayer.characterId == 0)
        return false;

    if (!isTargetAlive(targetPlayerId))
        return false;

    float distance = calculateDistance(mob.position, targetPlayer.characterPosition);
    return distance <= mob.attackRange;
}

void
MobAIController::executeMobAttack(const MobDataStruct &mob, int targetPlayerId, MobMovementData &movementData, float hitDelay)
{
    movementData.lastAttackTime = getCurrentGameTime();

    // Record skill cooldown before clearing pendingSkillSlug.
    if (!movementData.pendingSkillSlug.empty())
    {
        movementData.skillLastUsedTime[movementData.pendingSkillSlug] = movementData.lastAttackTime;
    }

    const std::string usedSkillSlug = movementData.pendingSkillSlug;
    movementData.pendingSkillSlug = "";

    mobMovementManager_->updateMobMovementData(mob.uid, movementData);

    if (combatSystem_)
    {
        combatSystem_->processAIAttack(mob.uid, targetPlayerId, usedSkillSlug, hitDelay);
        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " attacking player " +
                    std::to_string(targetPlayerId) +
                    (usedSkillSlug.empty() ? "" : " with skill [" + usedSkillSlug + "]"));
    }
    else
    {
        logger_.logError("CombatSystem not available - mob attack cannot be processed!");
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void
MobAIController::handleMobAttacked(int mobUID, int attackerPlayerId, int damage)
{
    if (!characterManager_)
    {
        logger_.logError("CharacterManager not set when handling mob attack");
        return;
    }

    if (!isTargetAlive(attackerPlayerId))
    {
        logger_.log("[INFO] Mob UID: " + std::to_string(mobUID) +
                    " attacked by dead player " + std::to_string(attackerPlayerId) + ", ignoring");
        return;
    }

    auto movementData = mobMovementManager_->getMobMovementData(mobUID);

    // Don't interrupt a leashing/evading mob — accumulate threat silently so
    // the threat table is ready when the mob finishes its return, but let it
    // complete HP regeneration undisturbed.
    if (movementData.combatState == MobCombatState::RETURNING ||
        movementData.combatState == MobCombatState::EVADING ||
        movementData.combatState == MobCombatState::FLEEING)
    {
        if (damage > 0)
        {
            movementData.threatTable[attackerPlayerId] += std::max(1, damage);
            movementData.attackerTimestamps[attackerPlayerId] = std::chrono::steady_clock::now();
            mobMovementManager_->updateMobMovementData(mobUID, movementData);
        }
        logger_.log("[INFO] Mob UID: " + std::to_string(mobUID) +
                    " is RETURNING/EVADING/FLEEING — ignoring interrupt from player " +
                    std::to_string(attackerPlayerId));
        return;
    }

    // Accumulate threat for the attacker
    movementData.threatTable[attackerPlayerId] += std::max(1, damage);
    movementData.attackerTimestamps[attackerPlayerId] = std::chrono::steady_clock::now();

    movementData.targetPlayerId = attackerPlayerId;
    movementData.isReturningToSpawn = false;
    movementData.nextMoveTime = getCurrentGameTime();

    mobMovementManager_->updateMobMovementData(mobUID, movementData);

    logger_.log("[INFO] Mob UID: " + std::to_string(mobUID) +
                " is now targeting player " + std::to_string(attackerPlayerId));

    // === Group aggro: notify nearby social mob-mates (only on real damage hits) ===
    // damage == 0 means this call itself is a social-alarm notification, so we
    // don't propagate further to prevent infinite recursion.
    if (damage > 0 && mobInstanceManager_)
    {
        auto mob = mobInstanceManager_->getMobInstance(mobUID);
        if (mob.uid > 0 && mob.isSocial)
        {
            // Use the mob's own aggroRange as social alert radius so that all
            // pack-mates the mob could "see" get alerted.  The old hardcoded
            // 20.0f was far too small compared to aggroRange (~400 units).
            const float kSocialRadius = mob.aggroRange;
            auto neighbors = mobInstanceManager_->getMobsInRange(
                mob.position.positionX, mob.position.positionY, kSocialRadius);

            for (const auto &neighbor : neighbors)
            {
                if (neighbor.uid == mobUID)
                    continue;
                if (neighbor.zoneId != mob.zoneId)
                    continue;
                if (neighbor.raceName != mob.raceName)
                    continue;
                if (neighbor.isDead)
                    continue;

                auto neighborMov = mobMovementManager_->getMobMovementData(neighbor.uid);
                if (neighborMov.combatState != MobCombatState::PATROLLING)
                    continue;

                // Alert neighbour with 0 damage — won't chain-propagate
                handleMobAttacked(neighbor.uid, attackerPlayerId, 0);
            }
        }
    }
}

void
MobAIController::handlePlayerAggro(MobDataStruct &mob, const SpawnZoneStruct &zone, MobMovementData &movementData)
{
    if (!characterManager_)
        return;

    // 1) Validate current target
    if (movementData.targetPlayerId > 0)
    {
        auto currentTarget = characterManager_->getCharacterById(movementData.targetPlayerId);
        if (currentTarget.characterId > 0 && isTargetAlive(movementData.targetPlayerId))
        {
            float distanceToTarget = calculateDistance(mob.position, currentTarget.characterPosition);
            float maxChaseDistance = mob.aggroRange * mob.chaseMultiplier;

            if (distanceToTarget > maxChaseDistance)
            {
                int lostTargetId = movementData.targetPlayerId;
                movementData.targetPlayerId = 0;
                movementData.isReturningToSpawn = true;
                movementData.threatTable.clear();
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                mobMovementManager_->sendMobTargetLost(mob, lostTargetId);

                logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                            " lost target (distance " + std::to_string(distanceToTarget) +
                            " > " + std::to_string(maxChaseDistance) + "), returning to spawn");
                return;
            }
        }
        else
        {
            int lostTargetId = movementData.targetPlayerId;
            movementData.targetPlayerId = 0;
            movementData.isReturningToSpawn = true;
            movementData.threatTable.clear();
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            mobMovementManager_->sendMobTargetLost(mob, lostTargetId);

            logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                        " target died or no longer exists, returning to spawn");
            return;
        }
    }

    // 2) Search for new target if none, not returning, and mob is aggressive
    // Passive (non-aggressive) mobs should not auto-aggro from proximity;
    // they still defend themselves when attacked via handleMobAttacked.
    if (mob.isAggressive && movementData.targetPlayerId == 0 && !movementData.isReturningToSpawn)
    {
        auto nearbyPlayers = characterManager_->getCharactersInZone(
            mob.position.positionX,
            mob.position.positionY,
            mob.aggroRange);

        if (!nearbyPlayers.empty() && mobMovementManager_->canSearchNewTargets(mob.position, zone))
        {
            // Pick highest-threat player in range; fall back to closest if no threat data
            float closestDistance = mob.aggroRange + 1.0f;
            int closestPlayerId = 0;
            int highestThreat = -1;
            int highestThreatId = 0;

            for (const auto &player : nearbyPlayers)
            {
                if (!isTargetAlive(player.characterId))
                    continue;

                float d = calculateDistance(mob.position, player.characterPosition);

                // Track closest as fallback
                if (d < closestDistance)
                {
                    closestDistance = d;
                    closestPlayerId = player.characterId;
                }

                // Track highest threat
                auto threatIt = movementData.threatTable.find(player.characterId);
                if (threatIt != movementData.threatTable.end())
                {
                    if (threatIt->second > highestThreat)
                    {
                        highestThreat = threatIt->second;
                        highestThreatId = player.characterId;
                    }
                }
            }

            int chosenPlayerId = (highestThreatId > 0) ? highestThreatId : closestPlayerId;

            if (chosenPlayerId > 0)
            {
                movementData.targetPlayerId = chosenPlayerId;
                movementData.isReturningToSpawn = false;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);

                logger_.log("[INFO] Mob UID: " + std::to_string(mob.uid) +
                            " found new target: " + std::to_string(chosenPlayerId) +
                            (highestThreatId > 0 ? " (by threat)" : " (by distance)"));
            }
        }
    }

    // 3) Caster archetype backpedal: if a caster mob is too close to its target, move away
    if (mob.aiArchetype == "caster" && movementData.targetPlayerId > 0 && characterManager_)
    {
        auto target = characterManager_->getCharacterById(movementData.targetPlayerId);
        if (target.characterId > 0 && isTargetAlive(movementData.targetPlayerId))
        {
            float dist = calculateDistance(mob.position, target.characterPosition);
            const float kCasterMinRange = mob.attackRange * 0.5f;
            const float kCasterPrefRange = mob.attackRange * 1.8f;

            if (dist < kCasterMinRange && !movementData.isBackpedaling)
            {
                float dx = mob.position.positionX - target.characterPosition.positionX;
                float dy = mob.position.positionY - target.characterPosition.positionY;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d > 0.0f)
                {
                    dx /= d;
                    dy /= d;
                }

                movementData.fleeTargetPosition.positionX = mob.position.positionX + dx * kCasterPrefRange;
                movementData.fleeTargetPosition.positionY = mob.position.positionY + dy * kCasterPrefRange;
                movementData.isBackpedaling = true;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                logger_.log("[COMBAT] Caster mob " + std::to_string(mob.uid) +
                            " backpedaling — too close (dist=" + std::to_string(dist) + ")");
            }
            else if (dist >= kCasterMinRange && movementData.isBackpedaling)
            {
                movementData.isBackpedaling = false;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                logger_.log("[COMBAT] Caster mob " + std::to_string(mob.uid) +
                            " backpedal done — safe distance reached");
            }
        }
    }
}

void
MobAIController::updateMobCombatState(MobDataStruct &mob, MobMovementData &movementData, float currentTime)
{
    if (!characterManager_)
        return;

    float timeSinceStateChange = currentTime - movementData.stateChangeTime;

    switch (movementData.combatState)
    {
    case MobCombatState::PATROLLING:
        if (movementData.targetPlayerId > 0)
        {
            movementData.combatState = MobCombatState::CHASING;
            movementData.stateChangeTime = currentTime;
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " entering CHASING state");
        }
        break;

    case MobCombatState::CHASING:
        if (movementData.targetPlayerId == 0)
        {
            movementData.combatState = movementData.isReturningToSpawn
                                           ? MobCombatState::RETURNING
                                           : MobCombatState::PATROLLING;
            movementData.stateChangeTime = currentTime;
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " lost target, returning to patrol");
        }
        else
        {
            auto targetPlayer = characterManager_->getCharacterById(movementData.targetPlayerId);
            if (targetPlayer.characterId == 0)
            {
                int lostTargetId = movementData.targetPlayerId;
                movementData.targetPlayerId = 0;
                movementData.combatState = MobCombatState::PATROLLING;
                movementData.stateChangeTime = currentTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                            " target no longer exists, returning to patrol");
            }
            else if (!isTargetAlive(movementData.targetPlayerId))
            {
                int lostTargetId = movementData.targetPlayerId;
                movementData.targetPlayerId = 0;
                movementData.combatState = MobCombatState::PATROLLING;
                movementData.stateChangeTime = currentTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                            " target is dead, returning to patrol");
            }
            else
            {
                // --- Flee threshold check (migration 016) -----------------------
                if (checkAndTriggerFlee(mob, movementData, currentTime, movementData.targetPlayerId))
                    break;

                // --- Threat decay (plan §4.1) -----------------------------------
                // Players outside aggroRange have their threat halved every second
                // (exponential decay: factor 0.95 per tick, continuous approx).
                if (movementData.lastThreatDecayTime == 0.0f)
                    movementData.lastThreatDecayTime = currentTime;

                float decayDelta = currentTime - movementData.lastThreatDecayTime;
                if (decayDelta >= 0.1f) // update at most every 100 ms
                {
                    movementData.lastThreatDecayTime = currentTime;
                    // decay factor: ~50% per second at 0.95^(decayDelta*10)
                    float decayFactor = std::pow(0.95f, decayDelta * 10.0f);

                    auto it = movementData.threatTable.begin();
                    while (it != movementData.threatTable.end())
                    {
                        auto player = characterManager_->getCharacterById(it->first);
                        if (player.characterId > 0)
                        {
                            float d = calculateDistance(mob.position, player.characterPosition);
                            if (d > mob.aggroRange)
                            {
                                it->second = static_cast<int>(it->second * decayFactor);
                                if (it->second <= 0)
                                {
                                    it = movementData.threatTable.erase(it);
                                    continue;
                                }
                            }
                        }
                        ++it;
                    }
                }
                // -----------------------------------------------------------------

                float timeSinceChasing = currentTime - movementData.stateChangeTime;
                const float maxChaseTime = mob.chaseDuration; // per-mob value (plan §4.2)
                if (timeSinceChasing > maxChaseTime)
                {
                    int lostTargetId = movementData.targetPlayerId;
                    movementData.targetPlayerId = 0;
                    movementData.combatState = MobCombatState::RETURNING;
                    movementData.stateChangeTime = currentTime;
                    movementData.isReturningToSpawn = true;
                    movementData.threatTable.clear();
                    movementData.attackerTimestamps.clear();
                    mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                    mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
                    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                " chase timeout, returning to spawn");
                }
                else
                {
                    float distance = calculateDistance(mob.position, targetPlayer.characterPosition);
                    if (distance <= mob.attackRange)
                    {
                        // Select skill BEFORE entering PREPARING_ATTACK so that
                        // cast time is derived from the chosen skill (plan §2.1).
                        auto chosenOpt = selectAttackSkill(mob, movementData, distance);
                        if (chosenOpt)
                        {
                            const SkillStruct &chosen = *chosenOpt;
                            movementData.pendingSkillSlug = chosen.skillSlug;
                            movementData.attackPrepareTime = static_cast<float>(chosen.castMs) / 1000.0f;
                            movementData.attackDuration = static_cast<float>(chosen.swingMs) / 1000.0f;
                            movementData.postAttackCooldown =
                                static_cast<float>(std::max(chosen.cooldownMs, chosen.gcdMs)) / 1000.0f;

                            // Ensure minimal timings so the state machine never
                            // gets stuck (e.g. castMs == 0 for instant attacks).
                            if (movementData.attackPrepareTime < 0.0f)
                                movementData.attackPrepareTime = 0.0f;
                            if (movementData.postAttackCooldown < 0.5f)
                                movementData.postAttackCooldown = 0.5f;

                            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                        " selected skill [" + chosen.skillSlug + "] — prepare=" +
                                        std::to_string(movementData.attackPrepareTime) + "s cd=" +
                                        std::to_string(movementData.postAttackCooldown) + "s");

                            // Send combatInitiation now so the client can play the cast/attack
                            // animation. combatResult is sent immediately after (below) with
                            // hitDelay = castTime + swingTime so the client schedules its own
                            // hit visuals without waiting for any server tick.
                            if (combatSystem_)
                                combatSystem_->broadcastMobSkillInitiation(mob.uid, movementData.targetPlayerId, chosen);
                        }
                        else
                        {
                            // No suitable skill found — use mob's base attack timing.
                            // Must be set explicitly; defaults in MobMovementData are
                            // attackDuration=3.0f which would cause a 3-second fake-idle.
                            movementData.pendingSkillSlug = "";
                            movementData.attackPrepareTime = 0.0f;
                            movementData.attackDuration = 0.3f;
                            movementData.postAttackCooldown = std::max(mob.attackCooldown, 0.5f);
                        }

                        // Send combatResult immediately after combatInitiation.
                        // hitDelay = castTime + swingTime — client shows the hit effect
                        // at the right moment using its own local timer, no server tick jitter.
                        executeMobAttack(mob, movementData.targetPlayerId, movementData, movementData.attackPrepareTime + movementData.attackDuration);

                        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                    " in attack range, preparing attack");
                        movementData.combatState = MobCombatState::PREPARING_ATTACK;
                        movementData.stateChangeTime = currentTime;
                        mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                        mobMovementManager_->forceMobStateUpdate(mob.uid);
                        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                    " initiated combat, preparing to attack player " +
                                    std::to_string(movementData.targetPlayerId) +
                                    " (distance: " + std::to_string(distance) + ")");
                    }
                    else if (distance > mob.aggroRange * mob.chaseMultiplier)
                    {
                        int lostTargetId = movementData.targetPlayerId;
                        movementData.targetPlayerId = 0;
                        movementData.combatState = MobCombatState::RETURNING;
                        movementData.stateChangeTime = currentTime;
                        movementData.isReturningToSpawn = true;
                        movementData.threatTable.clear();
                        movementData.attackerTimestamps.clear();
                        mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                        mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
                        logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                    " target too far away (distance: " + std::to_string(distance) +
                                    "), returning to spawn");
                    }
                }
            }
        }
        break;

    case MobCombatState::PREPARING_ATTACK:
        if (movementData.isReturningToSpawn)
        {
            movementData.combatState = MobCombatState::RETURNING;
            movementData.stateChangeTime = currentTime;
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            break;
        }

        // Flee threshold check (migration 016)
        if (checkAndTriggerFlee(mob, movementData, currentTime, movementData.targetPlayerId))
            break;

        if (timeSinceStateChange >= movementData.attackPrepareTime)
        {
            if (movementData.targetPlayerId > 0 && !isTargetAlive(movementData.targetPlayerId))
            {
                // Target died during cast — abort.
                int lostTargetId = movementData.targetPlayerId;
                movementData.targetPlayerId = 0;
                movementData.combatState = MobCombatState::PATROLLING;
                movementData.stateChangeTime = currentTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                            " target is dead during prepare attack, returning to patrol");
            }
            else if (movementData.targetPlayerId > 0)
            {
                // combatResult was already sent at CHASING→PREPARING_ATTACK.
                // Just advance to ATTACKING so the swing timer can gate ATTACK_COOLDOWN.
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) + " starting attack sequence");
                movementData.combatState = MobCombatState::ATTACKING;
                // Use the exact moment the cast ended (stateChangeTime + attackPrepareTime) rather
                // than currentTime so the swing-phase timer has no extra tick jitter baked in.
                movementData.stateChangeTime = movementData.stateChangeTime + movementData.attackPrepareTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                mobMovementManager_->forceMobStateUpdate(mob.uid);
            }
            else
            {
                // No target at all — return to spawn.
                movementData.combatState = MobCombatState::RETURNING;
                movementData.stateChangeTime = currentTime;
                movementData.isReturningToSpawn = true;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                            " lost target during prepare attack, returning to spawn");
            }
        }
        break;

    case MobCombatState::ATTACKING:
        // combatResult was already sent at CHASING→PREPARING_ATTACK entry (together with combatInitiation).
        // Just wait for the swing window to elapse, then enter cooldown.
        if (timeSinceStateChange >= movementData.attackDuration)
        {
            // Flee threshold check (migration 016)
            if (checkAndTriggerFlee(mob, movementData, currentTime, movementData.targetPlayerId))
                break;

            movementData.combatState = MobCombatState::ATTACK_COOLDOWN;
            movementData.stateChangeTime = currentTime;
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                        " swing finished, entering cooldown");
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            mobMovementManager_->forceMobStateUpdate(mob.uid);
        }
        break;

    case MobCombatState::ATTACK_COOLDOWN:
        if (timeSinceStateChange >= movementData.postAttackCooldown)
        {
            // Flee threshold check (migration 016)
            if (checkAndTriggerFlee(mob, movementData, currentTime, movementData.targetPlayerId))
                break;

            if (movementData.targetPlayerId > 0)
            {
                if (!isTargetAlive(movementData.targetPlayerId))
                {
                    int lostTargetId = movementData.targetPlayerId;
                    movementData.targetPlayerId = 0;
                    movementData.combatState = MobCombatState::PATROLLING;
                    movementData.stateChangeTime = currentTime;
                    mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                    mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
                    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                " target is dead after cooldown, returning to patrol");
                }
                else
                {
                    movementData.combatState = MobCombatState::CHASING;
                    movementData.stateChangeTime = currentTime;
                    mobMovementManager_->updateMobMovementData(mob.uid, movementData);
                    logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                                " cooldown finished, resuming chase");
                }
            }
            else if (movementData.isReturningToSpawn)
            {
                movementData.combatState = MobCombatState::RETURNING;
                movementData.stateChangeTime = currentTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            }
            else
            {
                movementData.combatState = MobCombatState::PATROLLING;
                movementData.stateChangeTime = currentTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            }
        }
        break;

    case MobCombatState::RETURNING:
        // ---- Leash HP regen: 10 % of maxHP/sec --------------------------------
        if (!mob.isDead && mob.currentHealth > 0 &&
            mob.currentHealth < mob.maxHealth && mob.maxHealth > 0)
        {
            if (movementData.lastRegenTime == 0.0f)
            {
                // First tick after entering RETURNING — record start time, heal next tick.
                movementData.lastRegenTime = currentTime;
                mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            }
            else
            {
                float regenDelta = currentTime - movementData.lastRegenTime;
                if (regenDelta >= 1.0f)
                {
                    int healAmount = static_cast<int>(mob.maxHealth * 0.10f * regenDelta);
                    if (healAmount > 0 && mobInstanceManager_)
                    {
                        auto result = mobInstanceManager_->applyHealToMob(mob.uid, healAmount);
                        if (result.success)
                        {
                            movementData.lastRegenTime = currentTime;
                            mobMovementManager_->updateMobMovementData(mob.uid, movementData);

                            // Broadcast HP change to all clients.
                            if (eventQueue_)
                            {
                                nlohmann::json healthData;
                                healthData["mobUID"] = mob.uid;
                                healthData["mobId"] = mob.id;
                                healthData["currentHealth"] = result.newHealth;
                                healthData["maxHealth"] = mob.maxHealth;
                                EventData eventData = healthData;
                                Event healthEvent(Event::MOB_HEALTH_UPDATE, 0, eventData);
                                eventQueue_->push(healthEvent);
                            }
                        }
                    }
                }
            }
        }
        else if (movementData.lastRegenTime != 0.0f &&
                 (mob.currentHealth >= mob.maxHealth || mob.isDead))
        {
            // Fully healed or dead — stop tracking.
            movementData.lastRegenTime = 0.0f;
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
        }
        // -----------------------------------------------------------------------

        if (!movementData.isReturningToSpawn)
        {
            // Reached spawn: enter brief EVADING window (2 s invulnerability).
            constexpr float EVADE_DURATION = 2.0f;
            movementData.combatState = MobCombatState::EVADING;
            movementData.stateChangeTime = currentTime;
            movementData.evadeEndTime = currentTime + EVADE_DURATION;
            movementData.lastRegenTime = 0.0f;
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            mobMovementManager_->forceMobStateUpdate(mob.uid);
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                        " reached spawn, entering EVADING for " +
                        std::to_string(EVADE_DURATION) + "s");
        }
        break;

    case MobCombatState::EVADING:
        // Brief invulnerability window after leashing — no movement, immune to damage.
        if (currentTime >= movementData.evadeEndTime)
        {
            movementData.combatState = MobCombatState::PATROLLING;
            movementData.stateChangeTime = currentTime;
            movementData.evadeEndTime = 0.0f;
            movementData.isReturningToSpawn = false;
            movementData.targetPlayerId = 0;
            movementData.hasPatrolTarget = false; // pick fresh waypoint from spawn (plan §5.1)
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                        " EVADING finished, back to patrol");
        }
        break;

    case MobCombatState::FLEEING:
    {
        // Run away until duration expires or aggressor is gone, then leash back.
        const float maxFleeDuration = mob.chaseDuration / 3.0f;
        float timeFleeing = currentTime - movementData.fleeStartTime;

        bool attackerGone = (movementData.targetPlayerId == 0 ||
                             !isTargetAlive(movementData.targetPlayerId));

        if (attackerGone || timeFleeing >= maxFleeDuration)
        {
            int lostTargetId = movementData.targetPlayerId;
            movementData.targetPlayerId = 0;
            movementData.combatState = MobCombatState::RETURNING;
            movementData.stateChangeTime = currentTime;
            movementData.isReturningToSpawn = true;
            movementData.isFleeing = false;
            movementData.threatTable.clear();
            movementData.attackerTimestamps.clear();
            mobMovementManager_->updateMobMovementData(mob.uid, movementData);
            if (lostTargetId > 0)
                mobMovementManager_->sendMobTargetLost(mob, lostTargetId);
            logger_.log("[COMBAT] Mob " + std::to_string(mob.uid) +
                        " FLEEING ended (" +
                        (attackerGone ? "attacker gone" : "duration expired") +
                        "), transitioning to RETURNING");
        }
        break;
    }
    }
}
