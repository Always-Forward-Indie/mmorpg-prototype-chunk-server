#pragma once
#include "SkillStructs.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Timestamp structure for lag compensation
 * Contains timing information for client-server communication and request synchronization
 */
struct TimestampStruct
{
    long long serverRecvMs = 0;     // When server received the packet (milliseconds since epoch)
    long long serverSendMs = 0;     // When server sends the response (milliseconds since epoch)
    long long clientSendMsEcho = 0; // Echo of client timestamp from original request (milliseconds since epoch)
    std::string requestId = "";     // Echo of client requestId for packet synchronization (format: sync_timestamp_session_sequence_hash)
};

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
    TimestampStruct timestamps; // Lag compensation timestamps
};

struct MessageStruct
{
    std::string status = "";
    nlohmann::json message;
    TimestampStruct timestamps; // Lag compensation timestamps
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
    std::string apply_on = "equip"; // 'equip' | 'use'
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
    bool isUsable = false;  // TRUE = can be used from inventory (potion, scroll, food)
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
    bool isHarvestOnly = false; // true = only drops from harvesting, not regular kill
    int minQuantity = 1;
    int maxQuantity = 1;
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
    TimestampStruct timestamps;                      // Lag compensation timestamps
};

// Structure for client request to get list of available loot in a corpse
struct CorpseLootInspectRequestStruct
{
    int characterId = 0;        // Server-side character ID from session
    int playerId = 0;           // Client-side player ID for verification
    int corpseUID = 0;          // UID of the corpse to inspect
    TimestampStruct timestamps; // Lag compensation timestamps
};

// Structure for harvest request from client
struct HarvestRequestStruct
{
    int characterId = 0;        // Server-side character ID from session
    int playerId = 0;           // Client-side player ID for verification
    int corpseUID = 0;          // UID of the corpse to harvest
    TimestampStruct timestamps; // Lag compensation timestamps
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
    TimestampStruct timestamps; // Lag compensation timestamps
};

// ============= PLAYER FLAG STRUCT =============
/**
 * @brief Arbitrary player flag (bool or int counter) for dialogue/quest conditions
 */
struct PlayerFlagStruct
{
    std::string flagKey = "";
    std::optional<bool> boolValue;
    std::optional<int> intValue;
};

// ============= PLAYER QUEST PROGRESS (in-memory) =============
/**
 * @brief In-memory quest progress for a single quest of a specific character
 */
struct PlayerQuestProgressStruct
{
    int characterId = 0;
    int questId = 0;
    std::string questSlug = "";
    std::string state = ""; // offered|active|completed|turned_in|failed
    int currentStep = 0;
    nlohmann::json progress = nlohmann::json::object();
    bool isDirty = false;
    std::chrono::steady_clock::time_point updatedAt;
};

// Runtime buff/debuff applied to a character (sourced from skill, item, quest, dialogue)
struct ActiveEffectStruct
{
    int64_t id = 0;                  // player_active_effect.id
    int effectId = 0;                // skill_effects.id
    std::string effectSlug = "";     // e.g. "physical_attack"
    std::string effectTypeSlug = ""; // e.g. "damage", "dot", "hot"
    int attributeId = 0;             // entity_attributes.id (0 = non-stat effect)
    std::string attributeSlug = "";  // slug matched against CharacterAttributeStruct::slug
    float value = 0.0f;              // stat: additive modifier; dot/hot: per-tick amount
    std::string sourceType = "";     // quest|skill|item|dialogue
    int64_t expiresAt = 0;           // Unix timestamp (seconds); 0 = permanent
    int tickMs = 0;                  // tick interval (ms); 0 = non-tick (stat modifier only)
    // Runtime-only: when the next tick should fire. Not serialized to/from DB.
    std::chrono::steady_clock::time_point nextTickAt = {};
};

// Result of one DoT/HoT tick, used by CombatSystem to build broadcast packets
struct EffectTickResult
{
    int characterId = 0;
    std::string effectSlug;
    std::string effectTypeSlug; // "dot" or "hot"
    float value = 0.0f;         // absolute per-tick damage (dot) or heal (hot)
    int newHealth = 0;
    int newMana = 0;
    bool targetDied = false;
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

    // Dialogue / quest state (populated on character join)
    std::vector<PlayerFlagStruct> flags;
    std::vector<PlayerQuestProgressStruct> quests;

    // Active buffs/debuffs (populated on character join, checked vs expiresAt at runtime)
    std::vector<ActiveEffectStruct> activeEffects;
};

struct ClientDataStruct
{
    int clientId = 0;
    std::string hash = "";
    int characterId = 0;
    TimestampStruct timestamps; // Lag compensation timestamps
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
    int baseExperience = 0;
    int radius = 0;
    bool isAggressive = false;
    bool isDead = false;

    /// Recorded when markMobAsDead() is called. Used by the cleanup task to
    /// keep the corpse in the world for CORPSE_DURATION_MS before removing it.
    std::chrono::steady_clock::time_point deathTimestamp = {};

    // Per-mob AI configuration (migration 011, from mob table columns).
    // Set from MobDataStruct template when spawning; used by MobMovementManager
    // instead of global MobAIConfig so each mob type can behave differently.
    float aggroRange = 400.0f;    // Aggro detection radius (world units)
    float attackRange = 150.0f;   // Melee/ranged attack range (world units)
    float attackCooldown = 2.0f;  // Seconds between attacks
    float chaseMultiplier = 2.0f; // aggroRange * chaseMultiplier = max chase distance
    float patrolSpeed = 1.0f;     // Patrol movement speed multiplier

    // Social and chase behaviour (migration 012)
    bool isSocial = false;       // Group aggro / passive-social enabled
    float chaseDuration = 30.0f; // Max seconds of pursuit before leashing

    // Rank info (used to scale XP reward)
    int rankId = 1;
    std::string rankCode = "normal";
    float rankMult = 1.0f;

    // AI depth (migration 016)
    float fleeHpThreshold = 0.0f;      // 0.0 = never flees; 0.25 = flee at 25% HP
    std::string aiArchetype = "melee"; // melee | caster | ranged | support

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
    std::string fullMessage;    // For storing the complete JSON message
    TimestampStruct timestamps; // Lag compensation timestamps
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
    float borderThresholdPercent = 0.10f; // plan §5.4: reduced from 0.25 so mobs use more of the zone
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
    RETURNING = 5,        // Returning to spawn (leashing)
    EVADING = 6,          // Brief invulnerability window after reaching spawn
    FLEEING = 7           // Low-HP panic flee (still takes damage, not immune)
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
    PositionStruct lastSentPosition;      // Последняя отправленная позиция
    float currentSpeedUnitsPerSec = 0.0f; // Last computed movement speed (units/second) for client interpolation

    // Leash regen tick tracker: time of last HP restoration while RETURNING.
    // Zero = regen not yet started for current leash.
    float lastRegenTime = 0.0f;

    // EVADING state: absolute time when the invulnerability window ends.
    // Zero = not in EVADING state.
    float evadeEndTime = 0.0f;

    // Threat table: playerId -> accumulated threat points.
    // Used to pick the highest-threat target when the mob searches for a new target.
    // Cleared when the mob begins returning to spawn.
    std::unordered_map<int, int> threatTable;

    // Skill-driven combat timing (plan §2.1).
    // Slug of the skill chosen at CHASING→PREPARING_ATTACK.
    // Empty string = no pre-selected skill (will be chosen by CombatSystem).
    std::string pendingSkillSlug = "";

    // Per-skill last-use absolute timestamp (getCurrentGameTime()).
    // Checked against skill.cooldownMs to avoid using skills before their CD expires.
    std::unordered_map<std::string, float> skillLastUsedTime;

    // Absolute game-time of the last threat-decay pass (plan §4.1).
    // 0.0f until the mob enters CHASING for the first time.
    float lastThreatDecayTime = 0.0f;

    // Flee behavior (migration 016).
    // Set when the mob transitions into FLEEING state.
    bool isFleeing = false;            // Movement manager uses flee movement when true
    PositionStruct fleeTargetPosition; // Destination to flee toward
    float fleeStartTime = 0.0f;        // Absolute time when FLEEING started

    // Caster archetype backpedal.
    // Set when caster is too close to its target and needs to create distance.
    bool isBackpedaling = false;

    // Patrol waypoint (plan §5.1).
    // When hasPatrolTarget is true the mob steers toward patrolTargetPoint
    // until it arrives, then picks a new random point inside the zone.
    PositionStruct patrolTargetPoint;
    bool hasPatrolTarget = false;
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
    float speed = 0.0f; // Movement speed in units/second for this step (for client-side interpolation)
};

/**
 * @brief Lightweight movement update sent to clients every tick.
 * Contains only position + velocity. Full mob data (name, stats, attributes, skills)
 * is sent once via spawnMobsInZone and never repeated in move packets.
 */
struct MobMoveUpdateStruct
{
    int uid = 0;
    int zoneId = 0;
    PositionStruct position;
    float dirX = 0.0f;   // Normalized direction vector X
    float dirY = 0.0f;   // Normalized direction vector Y
    float speed = 0.0f;  // World-units per second (for dead reckoning on client)
    int combatState = 0; // MobCombatState cast to int
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

// ============= EXPERIENCE SYSTEM STRUCTURES =============

/**
 * @brief Структура для данных о получении/потере опыта
 */
struct ExperienceEventStruct
{
    int characterId = 0;
    int experienceChange = 0; // Положительное значение - получение, отрицательное - потеря
    int oldExperience = 0;
    int newExperience = 0;
    int oldLevel = 0;
    int newLevel = 0;
    int expForCurrentLevel = 0; // Опыт требуемый для текущего уровня
    int expForNextLevel = 0;    // Опыт требуемый для следующего уровня
    std::string reason = "";    // Причина получения/потери опыта (например "mob_kill", "death_penalty")
    int sourceId = 0;           // ID источника опыта (например ID убитого моба)
    TimestampStruct timestamps; // Временные метки
};

/**
 * @brief Структура для запроса опыта с клиента (обычно не используется, но может пригодиться)
 */
struct ExperienceRequestStruct
{
    int characterId = 0;
    int playerId = 0; // Клиентский ID для верификации
    TimestampStruct timestamps;
};

/**
 * @brief Результат начисления опыта
 */
struct ExperienceGrantResult
{
    bool success = false;
    std::string errorMessage = "";
    ExperienceEventStruct experienceEvent;
    bool levelUp = false;
    std::vector<std::string> newAbilities; // Новые способности или скиллы при повышении уровня
};

/**
 * @brief Запись таблицы опыта для конкретного уровня
 */
struct ExperienceLevelEntry
{
    int level = 0;
    int experiencePoints = 0;
};

/**
 * @brief Таблица опыта, кешируемая на чанк-сервере
 */
struct ExperienceLevelTable
{
    std::vector<ExperienceLevelEntry> levels;
    bool isLoaded = false;
    std::chrono::system_clock::time_point lastUpdated;

    /**
     * @brief Получить опыт для указанного уровня
     */
    int getExperienceForLevel(int level) const
    {
        for (const auto &entry : levels)
        {
            if (entry.level == level)
            {
                return entry.experiencePoints;
            }
        }
        return 0; // Если уровень не найден
    }

    /**
     * @brief Получить максимальный уровень в таблице
     */
    int getMaxLevel() const
    {
        int maxLevel = 0;
        for (const auto &entry : levels)
        {
            if (entry.level > maxLevel)
            {
                maxLevel = entry.level;
            }
        }
        return maxLevel;
    }

    /**
     * @brief Очистить таблицу
     */
    void clear()
    {
        levels.clear();
        isLoaded = false;
    }
};

// NPC Attribute Structure
struct NPCAttributeStruct
{
    int id = 0;
    int npc_id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

// NPC Data Structure
struct NPCDataStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    std::string raceName = "";
    int level = 0;
    int currentHealth = 0;
    int currentMana = 0;
    int maxHealth = 0;
    int maxMana = 0;
    std::vector<NPCAttributeStruct> attributes;
    std::vector<SkillStruct> skills;
    PositionStruct position;

    // NPC specific properties
    std::string npcType = ""; // "vendor", "quest_giver", "general", etc.
    bool isInteractable = true;
    std::string dialogueId = "";
    std::string questId = "";
    int radius = 200; // Interaction/spawn radius. Used for dialogue range check.

    // Define the equality operator
    bool operator==(const NPCDataStruct &other) const
    {
        return id == other.id;
    }
};

// ============= DIALOGUE SYSTEM STRUCTS =============

/// Node of the dialogue graph
struct DialogueNodeStruct
{
    int id = 0;
    int dialogueId = 0;
    std::string type = ""; ///< "line" | "choice_hub" | "action" | "jump" | "end"
    int speakerNpcId = 0;
    std::string clientNodeKey = "";
    nlohmann::json conditionGroup; ///< null JSON → always true
    nlohmann::json actionGroup;    ///< null JSON → no actions
    int jumpTargetNodeId = 0;
};

/// Edge (player choice) in the dialogue graph
struct DialogueEdgeStruct
{
    int id = 0;
    int fromNodeId = 0;
    int toNodeId = 0;
    int orderIndex = 0;
    std::string clientChoiceKey = "";
    nlohmann::json conditionGroup;
    nlohmann::json actionGroup;
    bool hideIfLocked = false;
};

/// Entire dialogue graph cached in memory
struct DialogueGraphStruct
{
    int id = 0;
    std::string slug = "";
    int version = 0;
    int startNodeId = 0;
    std::unordered_map<int, DialogueNodeStruct> nodes;              ///< nodeId → node
    std::unordered_map<int, std::vector<DialogueEdgeStruct>> edges; ///< fromNodeId → edges
};

/// Binding NPC → Dialogue with priority
struct NPCDialogueMappingStruct
{
    int npcId = 0;
    int dialogueId = 0;
    int priority = 0;
    nlohmann::json conditionGroup;
};

/// Active in-memory dialogue session for one player
struct DialogueSessionStruct
{
    std::string sessionId = ""; ///< "dlg_{clientId}_{timestamp}"
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    int dialogueId = 0;
    int currentNodeId = 0;
    std::chrono::steady_clock::time_point lastActivity;
    static constexpr int TTL_SECONDS = 300;
};

/// Player context snapshot used to evaluate conditions
struct PlayerContextStruct
{
    int characterId = 0;
    int characterLevel = 0;
    std::unordered_map<std::string, bool> flagsBool;          ///< flag_key → bool_value
    std::unordered_map<std::string, int> flagsInt;            ///< flag_key → int_value
    std::unordered_map<std::string, std::string> questStates; ///< quest_slug → state string
    std::unordered_map<int, nlohmann::json> questProgress;    ///< quest_id → progress json
};

// ============= QUEST SYSTEM STRUCTS =============

/// Reward entry for quest completion
struct QuestRewardStruct
{
    std::string rewardType = ""; ///< "item" | "exp" | "gold"
    int itemId = 0;
    int quantity = 1;
    int64_t amount = 0;
};

/// Single step of a quest
struct QuestStepStruct
{
    int id = 0;
    int questId = 0;
    int stepIndex = 0;
    std::string stepType = ""; ///< "kill" | "collect" | "talk" | "reach" | "custom"
    nlohmann::json params;
    std::string clientStepKey = "";
};

/// Static quest definition
struct QuestStruct
{
    int id = 0;
    std::string slug = "";
    int minLevel = 0;
    bool repeatable = false;
    int cooldownSec = 0;
    int giverNpcId = 0;
    int turninNpcId = 0;
    std::string clientQuestKey = "";
    std::vector<QuestStepStruct> steps;
    std::vector<QuestRewardStruct> rewards;
};

// ============= REQUEST STRUCTS (client → chunk) =============

/// Client requests interaction with an NPC to start dialogue
struct NPCInteractRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    int playerId = 0; ///< client-side player ID for verification
    TimestampStruct timestamps;
};

/// Client picks a dialogue edge
struct DialogueChoiceRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string sessionId = "";
    int edgeId = 0;
    int playerId = 0;
    TimestampStruct timestamps;
};

/// Client closes the dialogue
struct DialogueCloseRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string sessionId = "";
    int playerId = 0;
    TimestampStruct timestamps;
};

// ============= PERSISTENCE STRUCTS (chunk → game-server) =============

/// Chunk-server sends quest progress to game-server for DB persistance
struct UpdatePlayerQuestProgressStruct
{
    int characterId = 0;
    int questId = 0;
    std::string questSlug = "";
    std::string state = "";
    int currentStep = 0;
    nlohmann::json progress;
};

/// Chunk-server sends flag update to game-server
struct UpdatePlayerFlagStruct
{
    int characterId = 0;
    std::string flagKey = "";
    std::optional<bool> boolValue;
    std::optional<int> intValue;
};