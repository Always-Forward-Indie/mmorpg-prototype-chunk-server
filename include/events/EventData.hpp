#pragma once
#include "data/AttackSystem.hpp"
#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
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

/**
 * @brief Player skill initialization data
 */
struct PlayerSkillInitStruct
{
    int characterId;
    std::vector<SkillStruct> skills;
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
    NPCDataStruct,
    ChunkInfoStruct,
    CombatActionStruct,
    CombatResultStruct,
    CombatActionPacket,
    AttackRequestStruct,
    AttackSequenceStruct,
    TargetSelectionStruct,
    ItemDataStruct,
    MobLootInfoStruct,
    DroppedItemStruct,
    PlayerInventoryItemStruct,
    ItemPickupRequestStruct,
    ItemDropByPlayerRequestStruct,
    ItemUseRequestStruct,
    HarvestRequestStruct,
    HarvestProgressStruct,
    HarvestCompleteStruct,
    HarvestableCorpseStruct,
    CorpseLootPickupRequestStruct,
    CorpseLootInspectRequestStruct,
    ExperienceEventStruct,
    ExperienceGrantResult,
    ExperienceLevelEntry,
    PlayerSkillInitStruct,
    std::pair<int, int>, // For mob death events (mobUID, zoneId)
    std::vector<MobDataStruct>,
    std::vector<NPCDataStruct>,
    std::vector<SpawnZoneStruct>,
    std::vector<MobAttributeStruct>,
    std::vector<NPCAttributeStruct>,
    std::vector<CharacterDataStruct>,
    std::vector<CharacterAttributeStruct>,
    std::vector<ItemDataStruct>,
    std::vector<MobLootInfoStruct>,
    std::vector<DroppedItemStruct>,
    std::vector<int>, // e.g. removed dropped item UIDs for ITEM_REMOVE broadcast
    std::vector<HarvestableCorpseStruct>,
    std::vector<HarvestProgressStruct>,
    std::vector<ClientDataStruct>,
    std::vector<TargetCandidate>,
    std::vector<ExperienceLevelEntry>,
    std::vector<std::pair<int, std::vector<SkillStruct>>>, // For mob skills mapping (mobId -> skills)
    // Dialogue / Quest event payloads
    DialogueGraphStruct,
    NPCDialogueMappingStruct,
    QuestStruct,
    PlayerQuestProgressStruct,
    PlayerFlagStruct,
    NPCInteractRequestStruct,
    DialogueChoiceRequestStruct,
    DialogueCloseRequestStruct,
    UpdatePlayerQuestProgressStruct,
    UpdatePlayerFlagStruct,
    std::vector<DialogueGraphStruct>,
    std::vector<NPCDialogueMappingStruct>,
    std::vector<QuestStruct>,
    std::vector<PlayerQuestProgressStruct>,
    std::vector<PlayerFlagStruct>,
    std::vector<ActiveEffectStruct>,                       // Active buffs/debuffs for a character (on join)
    std::vector<PlayerInventoryItemStruct>,                // Inventory items loaded from DB (on join)
    std::pair<int, std::vector<CharacterAttributeStruct>>, // Attribute refresh: {characterId, attrs}
    std::vector<MobMoveUpdateStruct>,                      // Lightweight mob movement updates
    // Vendor / Trade / Repair / Durability payloads
    VendorNPCDataStruct,
    VendorStockUpdateStruct,
    OpenVendorShopRequestStruct,
    BuyItemRequestStruct,
    SellItemRequestStruct,
    BuyBatchRequestStruct,
    SellBatchRequestStruct,
    OpenRepairShopRequestStruct,
    RepairItemRequestStruct,
    RepairAllRequestStruct,
    TradeRequestStruct,
    TradeRespondStruct,
    TradeOfferUpdateStruct,
    TradeConfirmCancelStruct,
    DurabilityUpdateStruct,
    SaveCurrencyTransactionStruct,
    SaveDurabilityChangeStruct,
    // Equipment system payloads
    EquipItemRequestStruct,
    UnequipItemRequestStruct,
    GetEquipmentRequestStruct,
    SaveEquipmentChangeStruct,
    std::vector<VendorNPCDataStruct>,
    std::vector<VendorStockUpdateStruct>,
    // Skill trainer system payloads
    TrainerNPCDataStruct,
    OpenSkillShopRequestStruct,
    RequestLearnSkillRequestStruct,
    std::vector<TrainerNPCDataStruct>,
    // Respawn system payloads
    RespawnZoneStruct,
    RespawnRequestStruct,
    std::vector<RespawnZoneStruct>,
    // Status effect template payloads
    std::vector<StatusEffectTemplate>,
    // Game zone payloads
    std::vector<GameZoneStruct>,
    // Timed champion payloads (Stage 3)
    TimedChampionTemplate,
    std::vector<TimedChampionTemplate>,
    TimedChampionKilledStruct, // Chat system
    ChatMessageStruct,         // Mob weaknesses and resistances: {mobTemplateId -> [elementSlugs], ...} x2
    std::pair<std::unordered_map<int, std::vector<std::string>>,
        std::unordered_map<int, std::vector<std::string>>>,
    // Title system payloads
    EquipTitleRequestStruct>;
