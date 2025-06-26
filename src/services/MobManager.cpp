#include "services/MobManager.hpp"

MobManager::MobManager(Logger& logger)
    : logger_(logger)
{
}

// Function to load mobs from the database and store them in memory
void MobManager::loadListOfAllMobs(
    //array for the mobs
    std::vector<MobDataStruct> selectMobs
)
{
    try
    {
        if (selectMobs.empty())
        {
            // log that the data is empty
            logger_.logError("No mobs found in the GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto& row : selectMobs)
        {
            MobDataStruct mobData;
            mobData.id = row.id;
            mobData.name = row.name;
            mobData.level = row.level;
            mobData.raceName = row.raceName;
            mobData.currentHealth = row.currentHealth;
            mobData.currentMana = row.currentMana;
            mobData.maxHealth = row.maxHealth;
            mobData.maxMana = row.maxMana;
            mobData.isAggressive = row.isAggressive;
            mobData.isDead = row.isDead;
            mobData.position = row.position;

            mobs_[mobData.id] = mobData;
        }
    }
    catch (const std::exception& e)
    {
        logger_.logError("Error loading mobs: " + std::string(e.what()));
    }
}

// load mob attributes
void MobManager::loadListOfMobsAttributes(
    std::vector<MobAttributeStruct> selectMobAttributes
)
{
    try
    {
        if (selectMobAttributes.empty())
        {
            logger_.logError("No mob attributes found in the GS");
            return;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto& row : selectMobAttributes)
        {
            if (mobs_.find(row.mob_id) == mobs_.end())
            {
                logger_.logError("Mob ID " + std::to_string(row.mob_id) + " not found for attribute " + row.name);
                continue; // Пропускаем, если моба нет
            }

            MobAttributeStruct mobAttribute;
            mobAttribute.id = row.id;
            mobAttribute.mob_id = row.mob_id;
            mobAttribute.name = row.name;
            mobAttribute.slug = row.slug;
            mobAttribute.value = row.value;

            mobs_[mobAttribute.mob_id].attributes.push_back(mobAttribute);
        }
    }
    catch (const std::exception& e)
    {
        logger_.logError("Error loading mob attributes: " + std::string(e.what()));
    }
}

// Function to get all mobs from memory as map
std::map<int, MobDataStruct> MobManager::getMobs() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return mobs_;
}

// Function to get all mobs from memory as vector
std::vector<MobDataStruct> MobManager::getMobsAsVector() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<MobDataStruct> mobs;
    for (const auto& mob : mobs_)
    {
        mobs.push_back(mob.second);
    }
    return mobs;
}

// Function to get a mob by ID
MobDataStruct MobManager::getMobById(int mobId) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto mob = mobs_.find(mobId);
    if (mob != mobs_.end())
    {
        return mob->second;
    }
    else
    {
        return MobDataStruct();
    }
}