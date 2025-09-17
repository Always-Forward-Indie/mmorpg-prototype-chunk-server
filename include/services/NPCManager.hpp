#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <mutex>
#include <unordered_map>
#include <vector>

/**
 * @brief Thread-safe manager for NPC data received from game server
 *
 * This class manages NPC data and attributes received from the game server.
 * It provides thread-safe access to NPC information for spawning and
 * sending NPC data to players when they enter zones.
 *
 * Following SOLID principles:
 * - Single Responsibility: Manages only NPC data
 * - Open/Closed: Extensible for new NPC operations
 * - Liskov Substitution: Can be used wherever a manager is expected
 * - Interface Segregation: Focused interface for NPC operations
 * - Dependency Inversion: Depends on abstractions (Logger)
 */
class NPCManager
{
  public:
    /**
     * @brief Construct a new NPCManager
     * @param logger Reference to logger for error reporting
     */
    explicit NPCManager(Logger &logger);

    /**
     * @brief Destructor
     */
    ~NPCManager() = default;

    // Delete copy constructor and assignment operator to prevent copying
    NPCManager(const NPCManager &) = delete;
    NPCManager &operator=(const NPCManager &) = delete;

    /**
     * @brief Set NPCs list received from game server
     * @param npcs Vector of NPC data structures
     */
    void setNPCsList(const std::vector<NPCDataStruct> &npcs);

    /**
     * @brief Set NPC attributes received from game server
     * @param attributes Vector of NPC attribute structures
     */
    void setNPCsAttributes(const std::vector<NPCAttributeStruct> &attributes);

    /**
     * @brief Get all NPCs
     * @return Vector of all NPC data structures
     */
    std::vector<NPCDataStruct> getAllNPCs() const;

    /**
     * @brief Get NPC by ID
     * @param npcId The ID of the NPC to retrieve
     * @return NPCDataStruct if found, default constructed struct if not found
     */
    NPCDataStruct getNPCById(int npcId) const;

    /**
     * @brief Get NPCs within a specific area
     * @param centerX Center X coordinate
     * @param centerY Center Y coordinate
     * @param radius Radius to search within
     * @return Vector of NPCs within the specified area
     */
    std::vector<NPCDataStruct> getNPCsInArea(float centerX, float centerY, float radius) const;

    /**
     * @brief Check if NPCs data has been loaded
     * @return True if NPCs have been loaded from game server
     */
    bool isNPCsLoaded() const;

    /**
     * @brief Get total count of loaded NPCs
     * @return Number of NPCs currently loaded
     */
    size_t getNPCCount() const;

    /**
     * @brief Clear all NPC data
     */
    void clearNPCData();

  private:
    mutable std::mutex npcsMutex_;                                           ///< Mutex for thread-safe access to NPCs
    std::unordered_map<int, NPCDataStruct> npcsMap_;                         ///< Map of NPC ID to NPC data
    std::unordered_map<int, std::vector<NPCAttributeStruct>> attributesMap_; ///< Map of NPC ID to attributes
    bool npcsLoaded_;                                                        ///< Flag indicating if NPCs have been loaded
    Logger &logger_;                                                         ///< Reference to logger for error reporting

    /**
     * @brief Apply attributes to NPC data
     * @param npc Reference to NPC data structure to modify
     */
    void applyAttributesToNPC(NPCDataStruct &npc) const;
};