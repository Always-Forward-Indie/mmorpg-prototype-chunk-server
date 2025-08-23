#pragma once
#include "data/AttackSystem.hpp"
#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

/**
 * @brief Attack request data structure
 */
struct AttackRequestStruct
{
    int attackerId;
    int actionId;                     // ID of the action to use (0 = auto-select)
    int targetId;                     // ID of the target (0 = auto-select)
    PositionStruct targetPosition;    // For area-targeted attacks
    bool useAI;                       // Whether to use AI for target/action selection
    TargetSelectionStrategy strategy; // Preferred targeting strategy
};

/**
 * @brief Attack sequence request
 */
struct AttackSequenceStruct
{
    int attackerId;
    std::string sequenceName;
    std::vector<int> targetIds; // For multi-target sequences
    bool allowInterruption;
};

/**
 * @brief Target selection result
 */
struct TargetSelectionStruct
{
    int attackerId;
    std::vector<TargetCandidate> availableTargets;
    TargetCandidate selectedTarget;
    TargetSelectionStrategy usedStrategy;
    std::string selectionReason;
};

// Unified EventData for all servers
using EventData = std::variant<
    int,
    float,
    std::string,
    nlohmann::json,
    PositionStruct,
    MovementDataStruct,
    CharacterDataStruct,
    ClientDataStruct,
    SpawnZoneStruct,
    MobDataStruct,
    ChunkInfoStruct,
    CombatActionStruct,
    CombatResultStruct,
    CombatAnimationStruct,
    CombatActionPacket,
    CombatAnimationPacket,
    AttackRequestStruct,
    AttackSequenceStruct,
    TargetSelectionStruct,
    ItemDataStruct,
    MobLootInfoStruct,
    DroppedItemStruct,
    PlayerInventoryItemStruct,
    ItemPickupRequestStruct,
    HarvestRequestStruct,
    HarvestProgressStruct,
    HarvestCompleteStruct,
    HarvestableCorpseStruct,
    CorpseLootPickupRequestStruct,
    CorpseLootInspectRequestStruct,
    std::pair<int, int>, // For mob death events (mobUID, zoneId)
    std::vector<MobDataStruct>,
    std::vector<SpawnZoneStruct>,
    std::vector<MobAttributeStruct>,
    std::vector<CharacterDataStruct>,
    std::vector<CharacterAttributeStruct>,
    std::vector<ItemDataStruct>,
    std::vector<MobLootInfoStruct>,
    std::vector<DroppedItemStruct>,
    std::vector<HarvestableCorpseStruct>,
    std::vector<HarvestProgressStruct>,
    std::vector<ClientDataStruct>,
    std::vector<TargetCandidate>>;
