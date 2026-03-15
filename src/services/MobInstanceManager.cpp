#include "services/MobInstanceManager.hpp"
#include "events/Event.hpp"
#include "events/EventQueue.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <spdlog/logger.h>
#include <unordered_map>

MobInstanceManager::MobInstanceManager(Logger &logger)
    : logger_(logger), eventQueue_(nullptr)
{
    log_ = logger.getSystem("mob");
}

void
MobInstanceManager::applyBulkAttributes(const std::vector<MobAttributeStruct> &attrs)
{
    // Group incoming attributes by mob template id
    std::unordered_map<int, std::vector<MobAttributeStruct>> byType;
    for (const auto &a : attrs)
        byType[a.mob_id].push_back(a);

    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &[uid, instance] : mobInstances_)
    {
        auto it = byType.find(instance.id);
        if (it != byType.end())
        {
            instance.attributes = it->second;
        }
    }
}

void
MobInstanceManager::setEventQueue(EventQueue *eventQueue)
{
    eventQueue_ = eventQueue;
}

bool
MobInstanceManager::registerMobInstance(const MobDataStruct &mobInstance)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Check if UID already exists
    if (mobInstances_.find(mobInstance.uid) != mobInstances_.end())
    {
        log_->error("Mob instance with UID " + std::to_string(mobInstance.uid) + " already exists");
        return false;
    }

    // Register the mob instance
    mobInstances_[mobInstance.uid] = mobInstance;

    // Update zone index
    updateZoneIndex(mobInstance.uid, 0, mobInstance.zoneId);

    logger_.log("[INFO] Registered mob instance UID: " + std::to_string(mobInstance.uid) +
                " (Type: " + std::to_string(mobInstance.id) + ", Zone: " + std::to_string(mobInstance.zoneId) + ")");

    return true;
}

void
MobInstanceManager::unregisterMobInstance(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        int zoneId = it->second.zoneId;

        // Remove from zone index
        updateZoneIndex(mobUID, zoneId, 0);

        // Remove from main map
        mobInstances_.erase(it);

        log_->info("[INFO] Unregistered mob instance UID: " + std::to_string(mobUID));
    }
    else
    {
        log_->error("Attempted to unregister non-existent mob UID: " + std::to_string(mobUID));
    }
}

MobDataStruct
MobInstanceManager::getMobInstance(int mobUID) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        return it->second;
    }

    return MobDataStruct(); // Return empty struct if not found
}

std::vector<MobDataStruct>
MobInstanceManager::getMobInstancesInZone(int zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<MobDataStruct> result;

    auto zoneIt = mobsByZone_.find(zoneId);
    if (zoneIt != mobsByZone_.end())
    {
        for (int mobUID : zoneIt->second)
        {
            auto mobIt = mobInstances_.find(mobUID);
            if (mobIt != mobInstances_.end())
            {
                result.push_back(mobIt->second);
            }
        }
    }

    return result;
}

std::vector<std::pair<int, PositionStruct>>
MobInstanceManager::getMobPositionsInZone(int zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::pair<int, PositionStruct>> result;

    auto zoneIt = mobsByZone_.find(zoneId);
    if (zoneIt != mobsByZone_.end())
    {
        result.reserve(zoneIt->second.size());
        for (int mobUID : zoneIt->second)
        {
            auto mobIt = mobInstances_.find(mobUID);
            if (mobIt != mobInstances_.end())
            {
                result.emplace_back(mobUID, mobIt->second.position);
            }
        }
    }

    return result;
}

bool
MobInstanceManager::updateMobPosition(int mobUID, const PositionStruct &position)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        it->second.position = position;
        // Only log position updates very rarely to prevent spam.
        // positionLogThrottleMap_ is already protected by unique_lock above.
        float currentTime = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        float &lastLog = positionLogThrottleMap_[mobUID];
        if (lastLog == 0.0f || (currentTime - lastLog) > 30.0f) // Log every 30 seconds max
        {
            logger_.log("[DEBUG] Updated mob " + std::to_string(mobUID) + " position to (" +
                        std::to_string(position.positionX) + ", " +
                        std::to_string(position.positionY) + ", " +
                        std::to_string(position.positionZ) + ")");
            lastLog = currentTime;
        }
        return true;
    }

    log_->error("Failed to update position for mob UID: " + std::to_string(mobUID));
    return false;
}

MobHealthUpdateResult
MobInstanceManager::updateMobHealth(int mobUID, int health)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it == mobInstances_.end())
    {
        log_->error("Failed to update health for mob UID: " + std::to_string(mobUID));
        return {false, false, false};
    }

    bool wasAlreadyDead = it->second.isDead;

    // Don't process any health updates on already dead mobs
    if (wasAlreadyDead)
    {
        log_->info("[WARNING] Attempted to update health on already dead mob " + std::to_string(mobUID));
        return {false, false, true};
    }

    it->second.currentHealth = health;
    logger_.log("[DEBUG] Updated mob " + std::to_string(mobUID) + " health to " + std::to_string(health));

    bool mobDied = false;
    // Check if mob died
    if (health <= 0)
    {
        it->second.isDead = true;
        it->second.deathTimestamp = std::chrono::steady_clock::now();
        mobDied = true;
        log_->info("[INFO] Mob " + std::to_string(mobUID) + " has died");

        // Send event for loot generation
        if (eventQueue_)
        {
            logger_.log("[DEBUG] Sending loot generation event for mob ID " + std::to_string(it->second.id) +
                        " (UID " + std::to_string(mobUID) + ") at position (" +
                        std::to_string(it->second.position.positionX) + ", " +
                        std::to_string(it->second.position.positionY) + ")");

            // Create event data
            nlohmann::json mobLootData;
            mobLootData["mobId"] = it->second.id;
            mobLootData["mobUID"] = mobUID;
            mobLootData["positionX"] = it->second.position.positionX;
            mobLootData["positionY"] = it->second.position.positionY;
            mobLootData["positionZ"] = it->second.position.positionZ;
            mobLootData["zoneId"] = it->second.zoneId;

            Event lootEvent(Event::MOB_LOOT_GENERATION, 0, mobLootData);
            eventQueue_->push(std::move(lootEvent));
        }
    }

    return {true, mobDied, wasAlreadyDead};
}

bool
MobInstanceManager::updateMobMana(int mobUID, int mana)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        it->second.currentMana = mana;
        logger_.log("[DEBUG] Updated mob " + std::to_string(mobUID) + " mana to " + std::to_string(mana));
        return true;
    }

    log_->error("Failed to update mana for mob UID: " + std::to_string(mobUID));
    return false;
}

MobHealthUpdateResult
MobInstanceManager::applyDamageToMob(int mobUID, int damageAmount, int killerId)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it == mobInstances_.end())
    {
        log_->error("[applyDamageToMob] Mob UID not found: " + std::to_string(mobUID));
        return {false, false, false, 0, 0};
    }

    if (it->second.isDead)
    {
        return {false, false, true, 0, it->second.currentMana};
    }

    int newHealth = std::max(0, it->second.currentHealth - damageAmount);
    it->second.currentHealth = newHealth;

    bool mobDied = (newHealth <= 0);
    if (mobDied)
    {
        it->second.isDead = true;
        it->second.deathTimestamp = std::chrono::steady_clock::now();
        logger_.log("[INFO] Mob " + std::to_string(mobUID) + " has died from " + std::to_string(damageAmount) + " damage");

        if (eventQueue_)
        {
            nlohmann::json mobLootData;
            mobLootData["mobId"] = it->second.id;
            mobLootData["mobUID"] = mobUID;
            mobLootData["positionX"] = it->second.position.positionX;
            mobLootData["positionY"] = it->second.position.positionY;
            mobLootData["positionZ"] = it->second.position.positionZ;
            mobLootData["zoneId"] = it->second.zoneId;
            mobLootData["killerId"] = killerId;
            eventQueue_->push(Event(Event::MOB_LOOT_GENERATION, 0, mobLootData));
        }
    }

    return {true, mobDied, false, newHealth, it->second.currentMana};
}

MobHealthUpdateResult
MobInstanceManager::applyHealToMob(int mobUID, int healAmount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it == mobInstances_.end())
    {
        log_->error("[applyHealToMob] Mob UID not found: " + std::to_string(mobUID));
        return {false, false, false, 0, 0};
    }

    int newHealth = std::min(it->second.maxHealth,
        it->second.currentHealth + healAmount);
    it->second.currentHealth = newHealth;

    return {true, false, false, newHealth, it->second.currentMana};
}

int
MobInstanceManager::applyManaCostToMob(int mobUID, int costAmount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        int newMana = std::max(0, it->second.currentMana - costAmount);
        it->second.currentMana = newMana;
        return newMana;
    }

    log_->error("[applyManaCostToMob] Mob UID not found: " + std::to_string(mobUID));
    return 0;
}

bool
MobInstanceManager::trySpendMana(int mobUID, int amount)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        if (it->second.currentMana < amount)
            return false;
        it->second.currentMana -= amount;
        return true;
    }
    return false;
}

bool
MobInstanceManager::isMobAlive(int mobUID) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        return !it->second.isDead && it->second.currentHealth > 0;
    }

    return false;
}

bool
MobInstanceManager::markMobAsDead(int mobUID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it == mobInstances_.end())
    {
        log_->error("Failed to mark mob as dead - UID not found: " + std::to_string(mobUID));
        return false;
    }

    bool wasAlive = !it->second.isDead;
    it->second.isDead = true;
    it->second.currentHealth = 0;
    it->second.deathTimestamp = std::chrono::steady_clock::now();
    log_->info("[INFO] Marked mob " + std::to_string(mobUID) + " as dead");

    return true;
}

std::unordered_map<int, MobDataStruct>
MobInstanceManager::getAllMobInstances() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return mobInstances_;
}

std::vector<MobDataStruct>
MobInstanceManager::getAllLivingInstances() const
{
    std::vector<MobDataStruct> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    result.reserve(mobInstances_.size());
    for (const auto &[uid, mob] : mobInstances_)
    {
        if (!mob.isDead)
            result.push_back(mob);
    }
    return result;
}

bool
MobInstanceManager::updateMobInstance(const MobDataStruct &updated)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = mobInstances_.find(updated.uid);
    if (it == mobInstances_.end())
        return false;
    it->second = updated;
    return true;
}

std::vector<MobDataStruct>
MobInstanceManager::getMobsInRange(float centerX, float centerY, float radius) const
{
    std::vector<MobDataStruct> result;
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &[uid, mob] : mobInstances_)
    {
        if (mob.isDead)
            continue;
        float dx = mob.position.positionX - centerX;
        float dy = mob.position.positionY - centerY;
        if (std::sqrt(dx * dx + dy * dy) <= radius)
            result.push_back(mob);
    }
    return result;
}

int
MobInstanceManager::getAliveMobCountInZone(int zoneId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    int count = 0;

    auto zoneIt = mobsByZone_.find(zoneId);
    if (zoneIt != mobsByZone_.end())
    {
        for (int mobUID : zoneIt->second)
        {
            auto mobIt = mobInstances_.find(mobUID);
            if (mobIt != mobInstances_.end() && !mobIt->second.isDead && mobIt->second.currentHealth > 0)
            {
                count++;
            }
        }
    }

    return count;
}

void
MobInstanceManager::updateZoneIndex(int mobUID, int oldZoneId, int newZoneId)
{
    // Remove from old zone
    if (oldZoneId != 0)
    {
        auto oldZoneIt = mobsByZone_.find(oldZoneId);
        if (oldZoneIt != mobsByZone_.end())
        {
            auto &mobList = oldZoneIt->second;
            mobList.erase(std::remove(mobList.begin(), mobList.end(), mobUID), mobList.end());

            // Remove zone entry if empty
            if (mobList.empty())
            {
                mobsByZone_.erase(oldZoneIt);
            }
        }
    }

    // Add to new zone
    if (newZoneId != 0)
    {
        mobsByZone_[newZoneId].push_back(mobUID);
    }
}
