#pragma once
#include "data/DataStructs.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <shared_mutex>

class MobManager
{
  public:
    MobManager(Logger &logger);
    void setListOfMobs(std::vector<MobDataStruct> selectMobs);
    void setListOfMobsAttributes(std::vector<MobAttributeStruct> selectMobAttributes);

    std::map<int, MobDataStruct> getMobs() const;
    std::vector<MobDataStruct> getMobsAsVector() const;
    MobDataStruct getMobById(int mobId) const;

  private:
    Logger &logger_;

    // Store the mobs in memory as map with mobId as key
    std::map<int, MobDataStruct> mobs_;

    // Mutex for the mobs map
    mutable std::shared_mutex mutex_;
};