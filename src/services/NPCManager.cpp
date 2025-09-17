#include "services/NPCManager.hpp"
#include "utils/TerminalColors.hpp"
#include <algorithm>
#include <cmath>

NPCManager::NPCManager(Logger &logger)
    : npcsLoaded_(false), logger_(logger)
{
    logger_.log("NPCManager initialized", GREEN);
}

void
NPCManager::setNPCsList(const std::vector<NPCDataStruct> &npcs)
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    npcsMap_.clear();

    for (const auto &npc : npcs)
    {
        npcsMap_[npc.id] = npc;

        // Apply attributes if they exist
        NPCDataStruct &storedNPC = npcsMap_[npc.id];
        applyAttributesToNPC(storedNPC);
    }

    npcsLoaded_ = true;
    logger_.log("Loaded " + std::to_string(npcs.size()) + " NPCs into NPCManager", GREEN);
}

void
NPCManager::setNPCsAttributes(const std::vector<NPCAttributeStruct> &attributes)
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    // Clear existing attributes
    attributesMap_.clear();

    // Group attributes by NPC ID
    for (const auto &attribute : attributes)
    {
        attributesMap_[attribute.npc_id].push_back(attribute);
    }

    // Apply attributes to existing NPCs
    for (auto &pair : npcsMap_)
    {
        applyAttributesToNPC(pair.second);
    }

    logger_.log("Loaded attributes for " + std::to_string(attributesMap_.size()) + " NPCs", GREEN);
}

std::vector<NPCDataStruct>
NPCManager::getAllNPCs() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    std::vector<NPCDataStruct> npcs;
    npcs.reserve(npcsMap_.size());

    for (const auto &pair : npcsMap_)
    {
        npcs.push_back(pair.second);
    }

    return npcs;
}

NPCDataStruct
NPCManager::getNPCById(int npcId) const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    auto it = npcsMap_.find(npcId);
    if (it != npcsMap_.end())
    {
        return it->second;
    }

    // Return default constructed NPC if not found
    return NPCDataStruct{};
}

std::vector<NPCDataStruct>
NPCManager::getNPCsInArea(float centerX, float centerY, float radius) const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    std::vector<NPCDataStruct> npcsInArea;

    for (const auto &pair : npcsMap_)
    {
        const auto &npc = pair.second;
        float dx = npc.position.positionX - centerX;
        float dy = npc.position.positionY - centerY;
        float distance = std::sqrt(dx * dx + dy * dy);

        // debug npc
        // std::cout << "NPC ID: " << npc.id << " Position: (" << npc.position.positionX << ", " << npc.position.positionY << ") Distance: " << distance << std::endl;

        // debug radius
        // std::cout << "Search Radius: " << radius << std::endl;

        if (distance <= radius)
        {
            npcsInArea.push_back(npc);
        }
    }

    return npcsInArea;
}

bool
NPCManager::isNPCsLoaded() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);
    return npcsLoaded_;
}

size_t
NPCManager::getNPCCount() const
{
    std::lock_guard<std::mutex> lock(npcsMutex_);
    return npcsMap_.size();
}

void
NPCManager::clearNPCData()
{
    std::lock_guard<std::mutex> lock(npcsMutex_);

    npcsMap_.clear();
    attributesMap_.clear();
    npcsLoaded_ = false;

    logger_.log("Cleared all NPC data", YELLOW);
}

void
NPCManager::applyAttributesToNPC(NPCDataStruct &npc) const
{
    auto attributesIt = attributesMap_.find(npc.id);
    if (attributesIt != attributesMap_.end())
    {
        npc.attributes = attributesIt->second;
    }
    else
    {
        npc.attributes.clear();
    }
}