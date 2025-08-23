#pragma once
#include "data/DataStructs.hpp"
#include "services/ItemManager.hpp"
#include "utils/Logger.hpp"
#include <chrono>
#include <map>
#include <random>
#include <shared_mutex>
#include <unordered_map>

// Forward declarations
class EventQueue;
class InventoryManager;

/**
 * @brief Manages the harvesting system for collecting loot from mob corpses
 *
 * This manager handles:
 * - Tracking harvestable corpses
 * - Managing harvest progress and timing
 * - Generating harvest-specific loot
 * - Validating harvest prerequisites (distance, corpse availability)
 * - Handling harvest interruptions
 */
class HarvestManager
{
  public:
    HarvestManager(ItemManager &itemManager, Logger &logger);

    /**
     * @brief Set event queue for sending harvest events to clients
     * @param eventQueue Event queue to send harvest-related events
     */
    void setEventQueue(EventQueue *eventQueue);

    /**
     * @brief Set inventory manager for adding harvested items to player inventories
     * @param inventoryManager InventoryManager to add harvested items
     */
    void setInventoryManager(InventoryManager *inventoryManager);

    /**
     * @brief Register a mob corpse as harvestable when it dies
     * @param mobUID Unique mob instance UID
     * @param mobId Template mob ID
     * @param position Position where mob died
     */
    void registerCorpse(int mobUID, int mobId, const PositionStruct &position);

    /**
     * @brief Start harvesting process for a player
     * @param characterId ID of character starting harvest
     * @param corpseUID UID of the corpse to harvest
     * @param playerPosition Current position of the player
     * @return true if harvest started successfully, false otherwise
     */
    bool startHarvest(int characterId, int corpseUID, const PositionStruct &playerPosition);

    /**
     * @brief Update harvest progress and complete if ready
     * This should be called periodically to check harvest completion
     */
    void updateHarvestProgress();

    /**
     * @brief Cancel harvest for a character (e.g., player moved too far)
     * @param characterId ID of character to cancel harvest for
     * @param reason Reason for cancellation (for logging/client notification)
     */
    void cancelHarvest(int characterId, const std::string &reason = "");

    /**
     * @brief Check if a character is currently harvesting
     * @param characterId ID of character to check
     * @return true if character is harvesting, false otherwise
     */
    bool isCharacterHarvesting(int characterId) const;

    /**
     * @brief Get harvest progress for a character
     * @param characterId ID of character
     * @return HarvestProgressStruct or empty struct if not harvesting
     */
    HarvestProgressStruct getHarvestProgress(int characterId) const;

    /**
     * @brief Complete harvest and generate loot for corpse (without adding to inventory)
     * @param characterId ID of character completing harvest
     * @return Vector of generated loot items (itemId, quantity)
     */
    std::vector<std::pair<int, int>> completeHarvestAndGenerateLoot(int characterId);

    /**
     * @brief Pickup specific items from corpse loot
     * @param characterId ID of character picking up items
     * @param corpseUID UID of the corpse to pickup from
     * @param requestedItems Vector of (itemId, quantity) pairs to pickup
     * @param playerPosition Current position of the player for validation
     * @return Pair of (success flag, vector of successfully picked up items)
     */
    std::pair<bool, std::vector<std::pair<int, int>>> pickupCorpseLoot(
        int characterId, int corpseUID, const std::vector<std::pair<int, int>> &requestedItems, const PositionStruct &playerPosition);

    /**
     * @brief Get available loot for a corpse
     * @param corpseUID UID of the corpse
     * @return Vector of available loot items (itemId, quantity) or empty if no loot
     */
    std::vector<std::pair<int, int>> getCorpseLoot(int corpseUID) const;

    /**
     * @brief Check if corpse has any remaining loot
     * @param corpseUID UID of the corpse
     * @return true if corpse has loot available
     */
    bool corpseHasLoot(int corpseUID) const;

    /**
     * @brief Get expected harvest loot for a corpse without consuming it
     * @param mobId Template mob ID
     * @return Vector of potential loot items (itemId, quantity)
     */
    std::vector<std::pair<int, int>> getExpectedHarvestLoot(int mobId) const;

    /**
     * @brief Get harvestable corpses near a position
     * @param position Center position
     * @param radius Search radius
     * @return Vector of nearby harvestable corpses
     */
    std::vector<HarvestableCorpseStruct> getHarvestableCorpsesNearPosition(
        const PositionStruct &position, float radius = 300.0f) const;

    /**
     * @brief Get corpse by UID
     * @param corpseUID Unique corpse UID
     * @return HarvestableCorpseStruct or empty struct if not found
     */
    HarvestableCorpseStruct getCorpseByUID(int corpseUID) const;

    /**
     * @brief Clean up old corpses that can no longer be harvested
     * @param maxAgeSeconds Maximum age in seconds before cleanup
     */
    void cleanupOldCorpses(int maxAgeSeconds = 600); // 10 minutes default

    /**
     * @brief Validate if a player can harvest a specific corpse
     * @param characterId ID of character attempting harvest
     * @param corpseUID UID of the corpse to harvest
     * @param playerPosition Current position of the player
     * @return Validation result with reason if failed
     */
    struct HarvestValidationResult
    {
        bool isValid = false;
        std::string failureReason = "";
    };
    HarvestValidationResult validateHarvest(int characterId, int corpseUID, const PositionStruct &playerPosition) const;

    /**
     * @brief Set references to managers needed for broadcasting
     * @param clientManager ClientManager for broadcasting to all clients
     * @param networkManager NetworkManager for sending messages
     */
    void setManagerReferences(class ClientManager *clientManager, class NetworkManager *networkManager);

    /**
     * @brief Broadcast harvest start notification to all clients
     * @param characterId ID of character starting harvest
     * @param corpseUID UID of the corpse being harvested
     * @param playerPosition Position of the harvesting player
     */
    void broadcastHarvestStart(int characterId, int corpseUID, const PositionStruct &playerPosition);

    /**
     * @brief Broadcast harvest completion notification to all clients
     * @param characterId ID of character completing harvest
     * @param corpseUID UID of the harvested corpse
     * @param playerPosition Position of the harvesting player
     */
    void broadcastHarvestComplete(int characterId, int corpseUID, const PositionStruct &playerPosition);

    /**
     * @brief Broadcast harvest cancellation notification to all clients
     * @param characterId ID of character canceling harvest
     * @param corpseUID UID of the corpse being harvested
     * @param reason Reason for cancellation
     */
    void broadcastHarvestCancel(int characterId, int corpseUID, const std::string &reason);

  private:
    ItemManager &itemManager_;
    Logger &logger_;

    // Event queue for sending harvest events to clients
    EventQueue *eventQueue_;

    // Inventory manager for adding harvested items to player inventories
    InventoryManager *inventoryManager_;

    // Manager references for broadcasting
    class ClientManager *clientManager_;
    class NetworkManager *networkManager_;

    // Store all harvestable corpses (corpseUID -> HarvestableCorpseStruct)
    std::unordered_map<int, HarvestableCorpseStruct> harvestableCorpses_;

    // Store active harvest sessions (characterId -> HarvestProgressStruct)
    std::unordered_map<int, HarvestProgressStruct> activeHarvests_;

    // Store generated loot for corpses (corpseUID -> CorpseLootStruct)
    std::unordered_map<int, CorpseLootStruct> corpseLoot_;

    // Thread safety
    mutable std::shared_mutex corpsesMutex_;
    mutable std::shared_mutex harvestsMutex_;
    mutable std::shared_mutex lootMutex_;

    // Random number generation for harvest loot
    std::random_device randomDevice_;
    mutable std::mt19937 randomGenerator_;

    /**
     * @brief Generate harvest loot for a specific mob
     * @param mobId Template mob ID
     * @return Vector of items to add to player inventory
     */
    std::vector<std::pair<int, int>> generateHarvestLoot(int mobId);

    /**
     * @brief Complete harvest and give loot to player
     * @param characterId ID of character completing harvest
     * @param corpseUID UID of the harvested corpse
     */
    void completeHarvest(int characterId, int corpseUID);

    /**
     * @brief Calculate distance between two positions
     */
    float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2) const;

    /**
     * @brief Send harvest event to client
     * @param eventType Type of harvest event
     * @param characterId Character ID to send to
     * @param data Event data
     */
    void sendHarvestEvent(const std::string &eventType, int characterId, const nlohmann::json &data);

    // Configuration
    static constexpr float DEFAULT_HARVEST_DURATION = 3.0f;     // seconds
    static constexpr float DEFAULT_INTERACTION_RADIUS = 150.0f; // units
    static constexpr float DEFAULT_MAX_MOVE_DISTANCE = 50.0f;   // units
};
