#pragma once
#include "data/DataStructs.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <map>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class MobManager
{
  public:
    MobManager(Logger &logger);
    void setListOfMobs(std::vector<MobDataStruct> selectMobs);
    void setListOfMobsAttributes(std::vector<MobAttributeStruct> selectMobAttributes);
    void setListOfMobsSkills(std::vector<std::pair<int, std::vector<SkillStruct>>> mobSkillsMapping);

    std::map<int, MobDataStruct> getMobs() const;
    std::vector<MobDataStruct> getMobsAsVector() const;
    MobDataStruct getMobById(int mobId) const;
    MobDataStruct getMobByUid(int mobUid) const;

    // Update mob mana
    void updateMobMana(int mobUid, int newMana);

    // ── Bestiary static data (migration 040) ─────────────────────────────────

    /// Replace the full weaknesses table (called once on startup).
    /// @param data  mob template id → list of element slugs
    void setWeaknessesResistances(
        std::unordered_map<int, std::vector<std::string>> weaknesses,
        std::unordered_map<int, std::vector<std::string>> resistances);

    /// Returns weaknesses for a given mob template id (empty if unknown).
    std::vector<std::string> getWeaknessesForMob(int mobId) const;

    /// Returns resistances for a given mob template id (empty if unknown).
    std::vector<std::string> getResistancesForMob(int mobId) const;

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Store the mobs in memory as map with mobId as key
    std::map<int, MobDataStruct> mobs_;

    // Bestiary static lookup (mob template id → element slugs)
    std::unordered_map<int, std::vector<std::string>> weaknesses_;
    std::unordered_map<int, std::vector<std::string>> resistances_;

    // Mutex for the mobs map
    mutable std::shared_mutex mutex_;
};