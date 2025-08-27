#pragma once
#include "SkillStructs.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct PositionStruct
{
    float positionX = 0;
    float positionY = 0;
    float positionZ = 0;
    float rotationZ = 0;
};

struct MovementDataStruct
{
    int clientId = 0;
    int characterId = 0;
    PositionStruct position;
};

struct MessageStruct
{
    std::string status = "";
    nlohmann::json message;
};

struct ChunkInfoStruct
{
    int id = 0;
    std::string ip = "";
    int port = 0;
    float posX = 0;
    float posY = 0;
    float posZ = 0;
    float sizeX = 0;
    float sizeY = 0;
    float sizeZ = 0;
};

struct CharacterAttributeStruct
{
    int id = 0;
    int character_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct MobAttributeStruct
{
    int mob_id = 0;
    int id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct ItemAttributeStruct
{
    int id = 0;
    int item_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

struct ItemDataStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    std::string description = "";
    bool isQuestItem = false;
    int itemType = 0;
    std::string itemTypeName = "";
    std::string itemTypeSlug = "";
    bool isContainer = false;
    bool isDurable = false;
    bool isTradable = true;
    bool isEquippable = false;
    bool isHarvest = false; // Flag to indicate this item can only be obtained through harvesting
    float weight = 0.0f;
    int rarityId = 1;
    std::string rarityName = "";
    std::string raritySlug = "";
    int stackMax = 64;
    int durabilityMax = 100;
    int vendorPriceBuy = 1;
    int vendorPriceSell = 1;
    int equipSlot = 0;
    std::string equipSlotName = "";
    std::string equipSlotSlug = "";
    int levelRequirement = 0;
    std::vector<ItemAttributeStruct> attributes;
};

struct MobLootInfoStruct
{
    int id = 0;
    int mobId = 0;
    int itemId = 0;
    float dropChance = 0.0f;
    bool isHarvestOnly = false; // Flag to indicate this loot only drops from harvesting
};

// Structure for tracking harvestable corpses
struct HarvestableCorpseStruct
{
    int mobUID = 0;                                  // Unique mob instance UID that died
    int mobId = 0;                                   // Template mob ID
    PositionStruct position;                         // Position of the corpse
    std::chrono::steady_clock::time_point deathTime; // When the mob died
    bool hasBeenHarvested = false;                   // Track if corpse has been harvested
    int harvestedByCharacterId = 0;                  // Track who harvested it (0 = no one)
    int currentHarvesterCharacterId = 0;             // Track who is currently harvesting (0 = no one)
    float interactionRadius = 150.0f;                // How close player needs to be
};

// Structure for storing loot generated for a corpse
struct CorpseLootStruct
{
    int corpseUID = 0;                                   // UID of the corpse this loot belongs to
    std::vector<std::pair<int, int>> availableLoot;      // Vector of (itemId, quantity) pairs
    std::chrono::steady_clock::time_point generatedTime; // When loot was generated
    bool hasRemainingLoot() const
    {
        return !availableLoot.empty();
    }
};

// Structure for client request to pickup specific items from corpse loot
struct CorpseLootPickupRequestStruct
{
    int characterId = 0;                             // Server-side character ID from session
    int playerId = 0;                                // Client-side player ID for verification
    int corpseUID = 0;                               // UID of the corpse to pickup from
    std::vector<std::pair<int, int>> requestedItems; // Vector of (itemId, quantity) pairs to pickup
};

// Structure for client request to get list of available loot in a corpse
struct CorpseLootInspectRequestStruct
{
    int characterId = 0; // Server-side character ID from session
    int playerId = 0;    // Client-side player ID for verification
    int corpseUID = 0;   // UID of the corpse to inspect
};

// Structure for harvest request from client
struct HarvestRequestStruct
{
    int characterId = 0; // Server-side character ID from session
    int playerId = 0;    // Client-side player ID for verification
    int corpseUID = 0;   // UID of the corpse to harvest
};

// Structure for harvest progress tracking
struct HarvestProgressStruct
{
    int characterId = 0;
    int corpseUID = 0;
    std::chrono::steady_clock::time_point startTime;
    float harvestDuration = 3.0f; // Harvest time in seconds
    bool isActive = false;
    PositionStruct startPosition;  // Position where harvest started
    float maxMoveDistance = 50.0f; // Max distance player can move while harvesting
};

// Structure for harvest completion event
struct HarvestCompleteStruct
{
    int playerId = 0; // Player who completed the harvest
    int corpseId = 0; // Corpse that was harvested
};

struct DroppedItemStruct
{
    int uid = 0; // Unique instance ID for the dropped item
    int itemId = 0;
    int quantity = 1;
    PositionStruct position;
    std::chrono::steady_clock::time_point dropTime;
    int droppedByMobUID = 0; // UID of the mob that dropped it
    bool canBePickedUp = true;
};

struct PlayerInventoryItemStruct
{
    int id = 0;
    int characterId = 0;
    int itemId = 0;
    int quantity = 1;
};

// Item pickup request structure
struct ItemPickupRequestStruct
{
    int characterId = 0; // Server-side character ID from session
    int playerId = 0;    // Client-side player ID for verification
    int droppedItemUID = 0;
    PositionStruct playerPosition;
};

struct CharacterDataStruct
{
    int clientId = 0;
    int characterId = 0;
    int characterLevel = 0;
    int characterExperiencePoints = 0;
    int characterCurrentHealth = 0;
    int characterCurrentMana = 0;
    int characterMaxHealth = 0;
    int characterMaxMana = 0;
    int expForNextLevel = 0;
    std::string characterName = "";
    std::string characterClass = "";
    std::string characterRace = "";
    PositionStruct characterPosition;
    std::vector<CharacterAttributeStruct> attributes;
    std::vector<SkillStruct> skills;
};

struct ClientDataStruct
{
    int clientId = 0;
    std::string hash = "";
    int characterId = 0;
};

struct MobDataStruct
{
    int id = 0;
    int uid = 0; // Уникальный идентификатор экземпляра моба
    int zoneId = 0;
    std::string name = "";
    std::string slug = "";
    std::string raceName = "";
    int level = 0;
    int currentHealth = 0;
    int currentMana = 0;
    int maxHealth = 0;
    int maxMana = 0;
    std::vector<MobAttributeStruct> attributes;
    std::vector<SkillStruct> skills;
    PositionStruct position;
    bool isAggressive = false;
    bool isDead = false;

    float speedMultiplier = 1.0f;
    float nextMoveTime = 0.0f;

    // New movement attributes
    float movementDirectionX = 0.0f;
    float movementDirectionY = 0.0f;

    float stepMultiplier = 0.0f;

    // Define the equality operator
    bool operator==(const MobDataStruct &other) const
    {
        return uid == other.uid;
    }
};

struct SpawnZoneStruct
{
    int zoneId = 0;
    std::string zoneName = "";
    float posX = 0;
    float sizeX = 0;
    float posY = 0;
    float sizeY = 0;
    float posZ = 0;
    float sizeZ = 0;
    int spawnMobId = 0;
    int spawnCount = 0;
    int spawnedMobsCount = 0;
    bool spawnEnabled = true; // Indicates if the spawn zone is enabled for spawning mobs
    std::vector<int> spawnedMobsUIDList;
    std::vector<MobDataStruct> spawnedMobsList;
    std::chrono::seconds respawnTime; // Represents respawn time in seconds
};

struct EventContext
{
    std::string eventType;
    ClientDataStruct clientData;
    CharacterDataStruct characterData;
    PositionStruct positionData;
    MessageStruct messageStruct;
    std::string fullMessage; // For storing the complete JSON message
};

struct EventDataStruct
{
    std::string eventType;
    ClientDataStruct clientData;
    CharacterDataStruct characterData;
    PositionStruct positionData;
    MessageStruct messageStruct;
};

/**
 * @brief Parameters for mob movement behavior
 */
struct MobMovementParams
{
    float minMoveDistance = 120.0f;
    float minSeparationDistance = 140.0f;
    float baseSpeedMin = 80.0f;
    float baseSpeedMax = 140.0f;
    float moveTimeMin = 10.0f;
    float moveTimeMax = 40.0f;
    float speedTimeMin = 12.0f;
    float speedTimeMax = 28.0f;
    float cooldownMin = 5.0f;
    float cooldownMax = 15.0f;
    float borderAngleMin = 30.0f;
    float borderAngleMax = 100.0f;
    float stepMultiplierMin = 1.2f;
    float stepMultiplierMax = 3.0f;
    float initialDelayMax = 5.0f;
    float rotationJitterMin = -5.0f;
    float rotationJitterMax = 5.0f;
    float directionAdjustMin = 0.2f;
    float directionAdjustMax = 0.6f;
    float borderThresholdPercent = 0.25f;
    float maxStepSizePercent = 0.08f;
    float maxStepSizeAbsolute = 450.0f;
    int maxRetries = 4;
};

/**
 * @brief AI behavior configuration for mobs
 */
struct MobAIConfig
{
    // Aggro and chase distances
    float aggroRange = 400.0f;       // Дистанция агро (когда моб начинает преследовать)
    float maxChaseDistance = 800.0f; // Максимальная дистанция преследования от цели

    // Zone-based distance calculations
    float returnToSpawnZoneDistance = 1000.0f; // Дистанция от границы зоны для возврата (сейчас не работает потому-что мы жестко ограничиваем передвижение без цели в рамках зоны)
    float newTargetZoneDistance = 150.0f;      // Дистанция от границы зоны для поиска новых целей
    float maxChaseFromZoneEdge = 1500.0f;      // Максимальное преследование от границы зоны

    // Combat ranges
    float attackRange = 150.0f;  // Дистанция атаки
    float attackCooldown = 2.0f; // Кулдаун между атаками в секундах

    // Chase behavior multipliers
    float chaseDistanceMultiplier = 2.0f; // Множитель дистанции преследования от aggroRange

    // Movement timing
    float chaseMovementInterval = 0.3f;   // Интервал движения при преследовании (секунды)
    float returnMovementInterval = 0.15f; // Интервал движения при возврате (секунды)

    // Network optimization
    float minimumMoveDistance = 50.0f; // Минимальное расстояние для отправки обновления
};

/**
 * @brief Combat state for mobs
 */
enum class MobCombatState
{
    PATROLLING = 0,       // Normal movement/patrolling
    CHASING = 1,          // Chasing a target
    PREPARING_ATTACK = 2, // Stopped and preparing to attack
    ATTACKING = 3,        // Currently attacking
    ATTACK_COOLDOWN = 4,  // Post-attack cooldown
    RETURNING = 5         // Returning to spawn
};

/**
 * @brief Movement data for individual mobs
 */
struct MobMovementData
{
    float nextMoveTime = 0.0f;
    float lastMoveTime = 0.0f;
    float movementDirectionX = 0.0f;
    float movementDirectionY = 0.0f;
    float speedMultiplier = 1.0f;
    float stepMultiplier = 0.0f;
    int resetStepCounter = 0;

    // AI behavior data
    int targetPlayerId = 0;          // ID игрока, которого преследует моб
    float lastAttackTime = 0.0f;     // Время последней атаки
    bool isReturningToSpawn = false; // Возвращается ли моб в зону спавна
    PositionStruct spawnPosition;    // Позиция спавна для возврата

    // Combat state system
    MobCombatState combatState = MobCombatState::PATROLLING;
    float stateChangeTime = 0.0f;    // Время последнего изменения состояния
    float attackPrepareTime = 1.0f;  // Время подготовки к атаке (секунды)
    float attackDuration = 3.f;      // Длительность атаки (секунды)
    float postAttackCooldown = 1.0f; // Кулдаун после атаки (секунды)

    // Network optimization
    PositionStruct lastSentPosition; // Последняя отправленная позиция

    // AI configuration (set from MobAIConfig)
    float aggroRange = 400.0f;
    float attackRange = 150.0f;
    float attackCooldown = 2.0f;
    float minimumMoveDistance = 50.0f;
};

/**
 * @brief Result of movement calculation
 */
struct MobMovementResult
{
    PositionStruct newPosition;
    float newDirectionX;
    float newDirectionY;
    bool validMovement;
};

/**
 * @brief Zone boundary utilities for AABB calculations
 */
struct ZoneBounds
{
    float minX, maxX, minY, maxY;

    // Constructor from SpawnZoneStruct
    ZoneBounds(const SpawnZoneStruct &zone)
    {
        minX = zone.posX - (zone.sizeX / 2.0f);
        maxX = zone.posX + (zone.sizeX / 2.0f);
        minY = zone.posY - (zone.sizeY / 2.0f);
        maxY = zone.posY + (zone.sizeY / 2.0f);
    }

    // Check if point is inside zone
    bool isPointInside(const PositionStruct &pos) const
    {
        return (pos.positionX >= minX && pos.positionX <= maxX &&
                pos.positionY >= minY && pos.positionY <= maxY);
    }

    // Calculate minimum distance from point to zone boundary
    // Returns 0 if point is inside zone
    float distanceToZone(const PositionStruct &pos) const
    {
        if (isPointInside(pos))
        {
            return 0.0f; // Point is inside zone
        }

        // Calculate distance to closest boundary
        float dx = 0.0f;
        float dy = 0.0f;

        if (pos.positionX < minX)
        {
            dx = minX - pos.positionX;
        }
        else if (pos.positionX > maxX)
        {
            dx = pos.positionX - maxX;
        }

        if (pos.positionY < minY)
        {
            dy = minY - pos.positionY;
        }
        else if (pos.positionY > maxY)
        {
            dy = pos.positionY - maxY;
        }

        return std::sqrt(dx * dx + dy * dy);
    }

    // Calculate distance from zone boundary outward (for range calculations)
    // Returns distance from zone edge + additional range
    float distanceFromZoneEdge(const PositionStruct &pos, float additionalRange) const
    {
        float distToZone = distanceToZone(pos);
        return distToZone > additionalRange ? (distToZone - additionalRange) : 0.0f;
    }

    // Get closest point on zone boundary to given position
    PositionStruct getClosestPointOnBoundary(const PositionStruct &pos) const
    {
        PositionStruct closest = pos;

        // Clamp to zone boundaries
        if (closest.positionX < minX)
            closest.positionX = minX;
        else if (closest.positionX > maxX)
            closest.positionX = maxX;

        if (closest.positionY < minY)
            closest.positionY = minY;
        else if (closest.positionY > maxY)
            closest.positionY = maxY;

        return closest;
    }
};