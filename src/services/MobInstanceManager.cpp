#include "services/MobInstanceManager.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_map>

MobInstanceManager::MobInstanceManager(Logger &logger)
    : logger_(logger)
{
}

bool
MobInstanceManager::registerMobInstance(const MobDataStruct &mobInstance)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Check if UID already exists
    if (mobInstances_.find(mobInstance.uid) != mobInstances_.end())
    {
        logger_.logError("Mob instance with UID " + std::to_string(mobInstance.uid) + " already exists");
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

        logger_.log("[INFO] Unregistered mob instance UID: " + std::to_string(mobUID));
    }
    else
    {
        logger_.logError("Attempted to unregister non-existent mob UID: " + std::to_string(mobUID));
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

bool
MobInstanceManager::updateMobPosition(int mobUID, const PositionStruct &position)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        it->second.position = position;
        // Only log position updates very rarely to prevent spam
        static std::unordered_map<int, float> lastLogTime;
        float currentTime = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (lastLogTime[mobUID] == 0 || (currentTime - lastLogTime[mobUID]) > 30.0f) // Log every 30 seconds max
        {
            logger_.log("[DEBUG] Updated mob " + std::to_string(mobUID) + " position to (" +
                        std::to_string(position.positionX) + ", " +
                        std::to_string(position.positionY) + ", " +
                        std::to_string(position.positionZ) + ")");
            lastLogTime[mobUID] = currentTime;
        }
        return true;
    }

    logger_.logError("Failed to update position for mob UID: " + std::to_string(mobUID));
    return false;
}

bool
MobInstanceManager::updateMobHealth(int mobUID, int health)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = mobInstances_.find(mobUID);
    if (it != mobInstances_.end())
    {
        it->second.currentHealth = health;
        logger_.log("[DEBUG] Updated mob " + std::to_string(mobUID) + " health to " + std::to_string(health));

        // Check if mob died
        if (health <= 0)
        {
            it->second.isDead = true;
            logger_.log("[INFO] Mob " + std::to_string(mobUID) + " has died");
        }

        return true;
    }

    logger_.logError("Failed to update health for mob UID: " + std::to_string(mobUID));
    return false;
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

    logger_.logError("Failed to update mana for mob UID: " + std::to_string(mobUID));
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
    if (it != mobInstances_.end())
    {
        it->second.isDead = true;
        it->second.currentHealth = 0;
        logger_.log("[INFO] Marked mob " + std::to_string(mobUID) + " as dead");
        return true;
    }

    logger_.logError("Failed to mark mob as dead - UID not found: " + std::to_string(mobUID));
    return false;
}

std::unordered_map<int, MobDataStruct>
MobInstanceManager::getAllMobInstances() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return mobInstances_;
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
