#pragma once
#include "data/DataStructs.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <shared_mutex>
#include <map>

class MobManager
{
public:
    MobManager(Logger& logger);
    void loadListOfAllMobs(std::vector<MobDataStruct> selectMobs);
    void loadListOfMobsAttributes(std::vector<MobAttributeStruct> selectMobAttributes);

    std::map<int, MobDataStruct> getMobs() const;
    std::vector<MobDataStruct> getMobsAsVector() const;
    MobDataStruct getMobById(int mobId) const;

private:
    Logger& logger_;

    // Store the mobs in memory as map with mobId as key
    std::map<int, MobDataStruct> mobs_;

    // Mutex for the mobs map
    mutable std::shared_mutex mutex_;
};