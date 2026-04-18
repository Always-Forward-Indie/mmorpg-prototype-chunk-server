#pragma once
#include "SkillStructs.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// Equipment slot identifiers (must match equip_slots table slugs)
enum class EquipSlot : int
{
    NONE = 0,
    HEAD = 1,
    CHEST = 2,
    LEGS = 3,
    FEET = 4,
    HANDS = 5,
    WAIST = 6,
    NECKLACE = 7,
    RING_1 = 8,
    RING_2 = 9,
    MAIN_HAND = 10,
    OFF_HAND = 11,
    CLOAK = 12
};

// -----------------------------------------------------------------------
// Item use effect — one row from item_use_effects table.
// Describes what happens when the player uses an isUsable item.
// -----------------------------------------------------------------------
struct ItemUseEffectStruct
{
    std::string effectSlug;    // unique slug, e.g. "hp_restore", "strength_buff"
    std::string attributeSlug; // which attribute is affected, e.g. "hp", "mp", "strength"
    float value = 0.0f;        // amount: flat heal / mana / stat modifier value
    bool isInstant = true;     // true = one-shot effect; false = timed buff (HoT/stat)
    int durationSeconds = 0;   // 0 if instant; > 0 = buff duration in seconds
    int tickMs = 0;            // 0 if instant; > 0 = tick interval (HoT in ms)
    int cooldownSeconds = 30;  // per-item cooldown (0 = no cooldown)
};

struct ItemDataStruct
{
    int id = 0;
    std::string slug = ""; // used as localisation key on the client
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
    bool isTwoHanded = false;         // true = equipping blocks off_hand slot
    std::vector<int> allowedClassIds; // empty = no class restriction
    int setId = 0;                    // 0 = not part of any set
    std::string setSlug = "";         // slug of the item set
    std::vector<ItemAttributeStruct> attributes;
    std::vector<ItemUseEffectStruct> useEffects; // populated for isUsable items

    // Stage 4 — Mastery (migration 039)
    std::string masterySlug; ///< Mastery that grows when this weapon is used (e.g. 'sword_mastery')
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
    std::string lootTier = "common"; ///< 'common' | 'uncommon' | 'rare' | 'very_rare' (migration 040)
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
    float interactionRadius = 250.0f;                // How close player needs to be
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
    int inventoryItemId = 0;   // player_inventory.id of the source row (0 = new item, e.g. mob loot)
    int durabilityCurrent = 0; // preserved from the dropped inventory item (0 = not applicable)
    PositionStruct position;
    std::chrono::steady_clock::time_point dropTime;
    int droppedByMobUID = 0;                                 // UID of the mob that dropped it (0 = player drop)
    int droppedByCharacterId = 0;                            // character that dropped it (0 = mob drop)
    int reservedForCharacterId = 0;                          // 0 = free for all; > 0 = only this character can pick up until reservationExpiry
    std::chrono::steady_clock::time_point reservationExpiry; // after this time, anyone can pick up
    bool canBePickedUp = true;
};

struct PlayerInventoryItemStruct
{
    int id = 0;
    int characterId = 0;
    int itemId = 0;
    int quantity = 1;
    int slotIndex = -1;        // bag position (-1 = unassigned)
    int durabilityCurrent = 0; // 0 = not applicable / full
    bool isEquipped = false;   // true when in character_equipment table
    int killCount = 0;         // Item Soul: accumulated kills with this weapon instance
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

// Player drops an item from inventory onto the ground
struct ItemDropByPlayerRequestStruct
{
    int characterId = 0; // Server-side character ID from session
    int playerId = 0;    // Client-side player ID for security verification
    int itemId = 0;      // Item ID to drop
    int quantity = 1;    // Quantity to drop
    PositionStruct playerPosition;
};

// Player uses an item from inventory (potion, scroll, food, etc.)
struct ItemUseRequestStruct
{
    int characterId = 0; // Server-side character ID from session
    int playerId = 0;    // Client-side player ID for security verification
    int itemId = 0;      // Item ID to use
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

/// One hotbar slot assignment stored in CharacterDataStruct
struct SkillBarSlotStruct
{
    int slotIndex = -1;
    std::string skillSlug = "";
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
    int classId = 0; // DB id for class restriction checks
    std::string characterRace = "";
    PositionStruct characterPosition;
    std::vector<CharacterAttributeStruct> attributes;
    std::vector<SkillStruct> skills;

    // Dialogue / quest state (populated on character join)
    std::vector<PlayerFlagStruct> flags;
    std::vector<PlayerQuestProgressStruct> quests;

    // Active buffs/debuffs (populated on character join, checked vs expiresAt at runtime)
    std::vector<ActiveEffectStruct> activeEffects;

    // Experience debt: accumulated on death; 50% of earned XP pays it off before going to real progress
    int experienceDebt = 0;

    // Free skill points available for learning new skills
    int freeSkillPoints = 0;

    // Respawn bind point (0,0,0 = not set, use nearest respawn zone)
    PositionStruct respawnPosition;

    // Server-authoritative movement validation state
    // lastMoveSrvMs == 0 means uninitialized (first packet after join/respawn accepted unconditionally)
    PositionStruct lastValidatedPosition;
    int64_t lastMoveSrvMs = 0;

    // Runtime-only: last time this character took or dealt damage (combat timestamp).
    // Used by RegenManager to suppress regen during/after combat.
    // zero-initialised = "never been in combat" → regen is allowed immediately on join.
    std::chrono::steady_clock::time_point lastInCombatAt = {};

    // Hotbar slot assignments (loaded on join from character_skill_bar, updated on setSkillBarSlot)
    std::vector<SkillBarSlotStruct> skillBarSlots;

    // Analytics: session token generated once on joinGameCharacter.
    // Format: "sess_{characterId}_{unix_timestamp_ms}".
    // Passed in every AnalyticsEventStruct so all events of one session are groupable.
    std::string sessionId = "";
};

struct ClientDataStruct
{
    int clientId = 0;
    int accountId = 0; // DB owner_id / account id (== clientId in this system, explicit field for anti-alt checks)
    std::string hash = "";
    int characterId = 0;
    TimestampStruct timestamps; // Lag compensation timestamps
    bool isWorldReady = false;  // Set to true when client sends playerReady (scene loaded)
};

/**
 * @brief AI archetype for mob behavior.
 * Stored as an enum after parsing so comparisons in the hot path are cheap.
 */
enum class MobArchetype : uint8_t
{
    MELEE = 0,
    CASTER = 1,
    RANGED = 2,
    SUPPORT = 3,
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
    float fleeHpThreshold = 0.0f;                     // 0.0 = never flees; 0.25 = flee at 25% HP
    std::string aiArchetype = "melee";                // melee | caster | ranged | support
    MobArchetype archetypeType = MobArchetype::MELEE; // Parsed enum of aiArchetype (avoids hot-path string comparison)

    // Champion / rare mob flags (Stage 3, migration 038)
    bool isChampion = false;     ///< True after spawnChampion(); triggers onChampionKilled
    bool canEvolve = false;      ///< Loaded from mob_templates.can_evolve (Survival Champion)
    bool hasEvolved = false;     ///< Runtime: evolved once — do not evolve again
    int64_t spawnEpochSec = 0;   ///< Unix timestamp of spawn moment (for Survival threshold)
    float lootMultiplier = 1.0f; ///< Applied in LootManager: multiplies drop chances

    // Rare mob groundwork (migration 038, logic deferred until day/night cycle)
    bool isRare = false;            ///< True if this is a rare spawn mob
    float rareSpawnChance = 0.0f;   ///< Spawn chance per check [0..1]
    std::string rareSpawnCondition; ///< 'night' | 'day' | 'zone_event' | empty = any time

    // Stage 4 — Reputation (migration 039)
    std::string factionSlug; ///< Faction this mob belongs to (e.g. 'wolves', 'bandits')
    int repDeltaPerKill = 0; ///< Reputation delta awarded to killer on death (can be negative)

    // Bestiary static metadata (migration 040)
    std::string biomeSlug;   ///< e.g. 'forest', 'dungeon', 'swamp'
    std::string mobTypeSlug; ///< e.g. 'beast', 'undead', 'humanoid', 'elemental'
    int hpMin = 0;           ///< Min HP observable in the wild (for bestiary Tier-1)
    int hpMax = 0;           ///< Max HP observable in the wild

    // Define the equality operator
    bool operator==(const MobDataStruct &other) const
    {
        return uid == other.uid;
    }
};

/// Shape of a spawn zone boundary.
enum class ZoneShape : uint8_t
{
    RECT = 0,   ///< Axis-aligned bounding box (minX/maxX/minY/maxY)
    CIRCLE = 1, ///< Filled disc   (centerX/Y + outerRadius)
    ANNULUS = 2 ///< Ring / donut  (centerX/Y + innerRadius + outerRadius)
};

/// One mob-type entry inside a spawn zone (maps 1:1 to spawn_zone_mobs rows).
struct SpawnZoneMobEntry
{
    int szmId = 0;    ///< spawn_zone_mobs.id
    int mobId = 0;    ///< mob template id
    int maxCount = 0; ///< max simultaneous mobs of this type
    std::chrono::seconds respawnTime{60};
};

struct SpawnZoneStruct
{
    int zoneId = 0;
    std::string zoneName;

    // --- Geometry -------------------------------------------------
    ZoneShape shape = ZoneShape::RECT;

    // RECT: axis-aligned bounding box (renamed from posX/sizeX)
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;

    // CIRCLE + ANNULUS: centre and radii
    float centerX = 0.0f;
    float centerY = 0.0f;
    float innerRadius = 0.0f; ///< ANNULUS inner boundary (0 = CIRCLE)
    float outerRadius = 0.0f; ///< CIRCLE / ANNULUS outer boundary

    /// Optional: reject spawn candidates inside this game zone (e.g. safe village).
    int exclusionGameZoneId = 0;

    // --- Mob quota config (multi-mob support) ---------------------
    std::vector<SpawnZoneMobEntry> mobEntries;

    // --- Runtime state --------------------------------------------
    bool spawnEnabled = true;
    int spawnedMobsCount = 0; ///< running counter for logging/client display
    std::vector<int> spawnedMobsUIDList;
    std::vector<MobDataStruct> spawnedMobsList;

    // --- Computed helpers -----------------------------------------
    /// Total maximum mob population across all mob types.
    int totalSpawnCount() const
    {
        int total = 0;
        for (const auto &e : mobEntries)
            total += e.maxCount;
        return total;
    }

    /// True if at least one mob type is configured.
    bool hasSpawnConfig() const
    {
        return !mobEntries.empty();
    }

    /// Shortest respawn time across all mob entries (used for scheduler display).
    std::chrono::seconds minRespawnTime() const
    {
        std::chrono::seconds best{3600};
        for (const auto &e : mobEntries)
            best = std::min(best, e.respawnTime);
        return best;
    }
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
    // Initial wait before a freshly-spawned mob makes its first patrol step.
    // Was 10-40s — caused mobs to appear completely frozen for up to 40s after spawn.
    float moveTimeMin = 2.0f;
    float moveTimeMax = 6.0f;
    // Inter-step pause (used by calculateNextMoveTime via speedTimeMin/Max).
    // Was 12-28s — mobs moved once every ~20s on average, visually appeared static.
    // Reduced to 2-5s for visibly active patrol behaviour.
    float speedTimeMin = 2.0f;
    float speedTimeMax = 5.0f;
    // Extra random cooldown appended with 15% probability. Was 5-15s → up to 7.5s added.
    // Reduced to keep occasional pauses short.
    float cooldownMin = 1.0f;
    float cooldownMax = 3.0f;
    float borderAngleMin = 30.0f;
    float borderAngleMax = 100.0f;
    float stepMultiplierMin = 1.2f;
    float stepMultiplierMax = 3.0f;
    // Max initial spawn delay before first move. Was 5s; reduced to 1s so mobs
    // start wandering almost immediately after the server boots.
    float initialDelayMax = 1.0f;
    float rotationJitterMin = -5.0f;
    float rotationJitterMax = 5.0f;
    float directionAdjustMin = 0.2f;
    float directionAdjustMax = 0.6f;
    float borderThresholdPercent = 0.10f; // plan §5.4: reduced from 0.25 so mobs use more of the zone
    float maxStepSizePercent = 0.08f;
    float maxStepSizeAbsolute = 450.0f;
    int maxRetries = 8; // increased from 4: gives corner/cluster mobs more chances to find a free direction
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
    float chaseMovementInterval = 0.1f;   // Интервал движения при преследовании (секунды)
    float returnMovementInterval = 0.15f; // Интервал движения при возврате (секунды)

    // Chase speed — задаётся напрямую в units/sec, шаг = скорость * interval.
    // На клиенте совпадает с velocity.speed в пакете mobMoveUpdate, используется
    // для Dead Reckoning: TargetPos = ServerPos + velocity * dt.
    // Должен быть выше скорости игрока, иначе моб не догонит (player ~400-600 u/s).
    float chaseSpeedUnitsPerSec = 450.0f;

    // Return speed — скорость возврата к спавну (units/sec).
    // Намеренно ниже скорости преследования: моб не спринтует обратно.
    float returnSpeedUnitsPerSec = 200.0f;

    // Network optimization
    // Снижено до 10 потому что при реалистичной chase-скорости один шаг ~45 юнитов,
    // а порог 50 срезал бы большинство обновлений.
    float minimumMoveDistance = 10.0f; // Минимальное расстояние для отправки обновления

    // ---- Steering behavior parameters (chase movement) ----
    // Turn speed controls how fast the mob rotates toward the target direction.
    // Higher = snappier turns, lower = smoother/wider arcs.
    // Factor per tick: 1 - exp(-chaseTurnSpeed * chaseMovementInterval).
    // With turnSpeed=10 and interval=0.1s → factor≈0.63 (turn 63% toward target per tick).
    float chaseTurnSpeed = 10.0f;

    // Separation steering: soft repulsion force pushing mobs apart.
    // Eliminates hard-collision deflection angles and produces smooth crowd movement.
    float separationWeight = 0.35f;  // Blend weight of separation vs chase direction
    float separationRadius = 300.0f; // Radius within which other mobs exert repulsion

    // Arrival deceleration: slow down approaching attack range for smooth stop.
    // 0.0 = disabled (hard stop), > 0 = deceleration starts this many units before attack range.
    float arrivalSlowdownDistance = 80.0f;
    float arrivalMinSpeedFraction = 0.25f; // Minimum speed as fraction of full chase speed during arrival
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
    int64_t lastStepTimestampMs = 0;      // Unix ms when the last position step was computed (for client RTT compensation)
    bool forceNextUpdate = false;         // When true, skip distance threshold check once (set by forceMobStateUpdate)

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

    // Fellowship tracking: characterId -> time of last attack on this mob instance.
    // Used by CombatSystem::handleMobDeath to grant fellowship XP bonus.
    // Cleared alongside threatTable when the mob leashes.
    std::unordered_map<int, std::chrono::steady_clock::time_point> attackerTimestamps;

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

    // Melee slot queuing: prevents crowding jitter when too many mobs chase the
    // same player. Set by MobAIController in CHASING state when calculateDistance
    // is <= attackRange but all physical melee slots around the target are already
    // occupied by mobs in PREPARING_ATTACK / ATTACKING / ATTACK_COOLDOWN.
    // The movement manager parks the mob just outside the ring until a slot opens.
    bool waitingForMeleeSlot = false;

    // Chase deflection memory: sign of the last successfully used deflection angle
    // (+1.0 = positive side, -1.0 = negative side, 0.0 = direct path / not set).
    // Used by calculateChaseMovement to prefer same-side angles on the next tick,
    // preventing the ±90° oscillation that occurs when multiple mobs crowd the same path.
    float lastDeflectionSign = 0.0f;

    // Per-mob broadcast timestamp (ms). Used by unified mob tick to enforce
    // state-aware rate limiting: patrol=200ms min, combat=100ms min.
    // Replaces the global aggroLastBroadcastTime_ that serialised all zones.
    int64_t lastBroadcastMs = 0;
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
    float speed = 0.0f;          // Movement speed in units/second for this step (for client-side interpolation)
    float deflectionSign = 0.0f; // Sign of the deflection angle used (+1/-1) or 0 if direct path was taken
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
    float dirX = 0.0f;           // Normalized direction vector X
    float dirY = 0.0f;           // Normalized direction vector Y
    float speed = 0.0f;          // World-units per second (for dead reckoning on client)
    int combatState = 0;         // MobCombatState cast to int
    int64_t stepTimestampMs = 0; // Unix ms when this step was computed (allows client RTT compensation)

    // Patrol waypoint: when hasWaypoint=true the client moves toward (waypointX, waypointY)
    // at `speed` units/sec without waiting for the next packet. This allows the patrol
    // broadcast interval to be increased to ~200ms while keeping client motion smooth.
    float waypointX = 0.0f;
    float waypointY = 0.0f;
    bool hasWaypoint = false;
};

/**
 * @brief Zone boundary utilities — shape-aware (RECT / CIRCLE / ANNULUS).
 *
 * Primary interface is the static helper `contains(zone, x, y)`.
 * For RECT zones the legacy AABB members (minX/maxX/minY/maxY) are also available.
 */
struct ZoneBounds
{
    // AABB extents (populated for RECT zones and as the enclosing box for CIRCLE/ANNULUS)
    float minX = 0.0f, maxX = 0.0f;
    float minY = 0.0f, maxY = 0.0f;

    // Construct the AABB envelope from any zone shape.
    explicit ZoneBounds(const SpawnZoneStruct &zone)
    {
        if (zone.shape == ZoneShape::RECT)
        {
            minX = zone.minX;
            maxX = zone.maxX;
            minY = zone.minY;
            maxY = zone.maxY;
        }
        else
        {
            // Enclosing box for CIRCLE / ANNULUS
            minX = zone.centerX - zone.outerRadius;
            maxX = zone.centerX + zone.outerRadius;
            minY = zone.centerY - zone.outerRadius;
            maxY = zone.centerY + zone.outerRadius;
        }
    }

    // ----------------------------------------------------------------
    // Shape-aware containment (primary API)
    // ----------------------------------------------------------------

    /// Returns true if (x, y) lies inside the spawn zone's geometric boundary.
    static bool contains(const SpawnZoneStruct &zone, float x, float y)
    {
        switch (zone.shape)
        {
        case ZoneShape::CIRCLE:
        {
            float dx = x - zone.centerX;
            float dy = y - zone.centerY;
            float d2 = dx * dx + dy * dy;
            return d2 <= zone.outerRadius * zone.outerRadius;
        }
        case ZoneShape::ANNULUS:
        {
            float dx = x - zone.centerX;
            float dy = y - zone.centerY;
            float d2 = dx * dx + dy * dy;
            return d2 >= zone.innerRadius * zone.innerRadius &&
                   d2 <= zone.outerRadius * zone.outerRadius;
        }
        default: // RECT
            return (x >= zone.minX && x <= zone.maxX &&
                    y >= zone.minY && y <= zone.maxY);
        }
    }

    /// Overload for PositionStruct.
    static bool contains(const SpawnZoneStruct &zone, const PositionStruct &pos)
    {
        return contains(zone, pos.positionX, pos.positionY);
    }

    /// Returns the geometric centre of the zone.
    static PositionStruct getCenter(const SpawnZoneStruct &zone)
    {
        PositionStruct p;
        p.positionX = zone.centerX;
        p.positionY = zone.centerY;
        p.positionZ = 200.0f;
        return p;
    }

    /// Returns a representative "radius" for step-size / border-threshold calculations.
    /// For RECT this is half the larger dimension; for circular shapes it is outerRadius.
    static float getEffectiveRadius(const SpawnZoneStruct &zone)
    {
        if (zone.shape == ZoneShape::RECT)
            return std::max(zone.maxX - zone.minX, zone.maxY - zone.minY) * 0.5f;
        return zone.outerRadius;
    }

    // ----------------------------------------------------------------
    // Legacy AABB helpers (RECT zones only — kept for compat)
    // ----------------------------------------------------------------

    bool isPointInside(const PositionStruct &pos) const
    {
        return (pos.positionX >= minX && pos.positionX <= maxX &&
                pos.positionY >= minY && pos.positionY <= maxY);
    }

    float distanceToZone(const PositionStruct &pos) const
    {
        if (isPointInside(pos))
            return 0.0f;
        float dx = 0.0f, dy = 0.0f;
        if (pos.positionX < minX)
            dx = minX - pos.positionX;
        else if (pos.positionX > maxX)
            dx = pos.positionX - maxX;
        if (pos.positionY < minY)
            dy = minY - pos.positionY;
        else if (pos.positionY > maxY)
            dy = pos.positionY - maxY;
        return std::sqrt(dx * dx + dy * dy);
    }

    float distanceFromZoneEdge(const PositionStruct &pos, float additionalRange) const
    {
        float d = distanceToZone(pos);
        return d > additionalRange ? (d - additionalRange) : 0.0f;
    }

    PositionStruct getClosestPointOnBoundary(const PositionStruct &pos) const
    {
        PositionStruct closest = pos;
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
    std::string npcType = ""; // "vendor", "quest_giver", "blacksmith", "guard", "trainer", "general"
    bool isInteractable = true;
    std::string dialogueId = "";
    std::vector<std::string> questSlugs; ///< Slugs of quests this NPC gives or accepts turn-in for
    int radius = 200;                    // Interaction/spawn radius. Used for dialogue range check.

    // Stage 4 — Reputation (migration 039)
    std::string factionSlug; ///< Faction this NPC belongs to (for rep-gated dialogue & vendor discount)

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
    int classId = 0;                                          ///< character class id (0 = unknown)
    std::unordered_map<std::string, bool> flagsBool;          ///< flag_key → bool_value
    std::unordered_map<std::string, int> flagsInt;            ///< flag_key → int_value
    std::unordered_map<std::string, std::string> questStates; ///< quest_slug → state string
    std::unordered_map<std::string, int> questCurrentStep;    ///< quest_slug → current step index (0-based)
    std::unordered_map<int, nlohmann::json> questProgress;    ///< quest_id → progress json

    // Stage 4 additions
    std::unordered_map<std::string, int> reputations; ///< faction_slug → value
    std::unordered_map<std::string, float> masteries; ///< mastery_slug → value [0..100]

    // Skill system
    int freeSkillPoints = 0;
    std::unordered_set<std::string> learnedSkillSlugs; ///< set of skill slugs the character has learned
};

// ============= QUEST SYSTEM STRUCTS =============

/// Reward entry for quest completion
struct QuestRewardStruct
{
    std::string rewardType = ""; ///< "item" | "exp" | "gold"
    int itemId = 0;
    int quantity = 1;
    int64_t amount = 0;
    bool isHidden = false;            ///< TRUE = client shows "???" until quest_turned_in reveals it
    std::vector<int> allowedClassIds; ///< empty = all classes; non-empty = only these class ids receive this reward
};

/// Single step of a quest
struct QuestStepStruct
{
    int id = 0;
    int questId = 0;
    int stepIndex = 0;
    std::string stepType = "";           ///< "kill" | "collect" | "talk" | "reach" | "custom"
    std::string completionMode = "auto"; ///< "auto" = auto-advance on condition met; "manual" = advance only via dialogue action
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
    std::string reputationFactionSlug = ""; ///< faction slug for auto rep change; empty = none
    int reputationOnComplete = 0;           ///< reputation delta when quest is turned in
    int reputationOnFail = 0;               ///< reputation delta when quest fails (negative = penalty)
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

// ============= VENDOR SYSTEM STRUCTS =============

/// One item slot in a vendor's shop inventory
struct VendorInventoryItemStruct
{
    int itemId = 0;
    int stockCurrent = -1; // -1 = unlimited
    int stockMax = -1;     // -1 = unlimited
    int restockAmount = 0;
    int restockIntervalSec = 3600;
    int priceOverrideBuy = 0;  // 0 = use item.vendorPriceBuy
    int priceOverrideSell = 0; // 0 = use item.vendorPriceSell
};

/// Vendor NPC data loaded from DB at chunk startup
struct VendorNPCDataStruct
{
    int npcId = 0;
    std::vector<VendorInventoryItemStruct> items;
};

/// Client → chunk: open vendor shop window
struct OpenVendorShopRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: buy item from vendor
struct BuyItemRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    int itemId = 0;
    int quantity = 1;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: sell item to vendor
struct SellItemRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    int inventoryItemId = 0; // player_inventory.id
    int quantity = 1;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: one entry in a batch buy request
struct BuyBatchItemEntry
{
    int itemId = 0;
    int quantity = 1;
};

/// Client → chunk: buy up to MAX_VENDOR_BATCH_SIZE items in a single round-trip
struct BuyBatchRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    std::vector<BuyBatchItemEntry> items;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: one entry in a batch sell request
struct SellBatchItemEntry
{
    int inventoryItemId = 0; // player_inventory.id
    int quantity = 1;
};

/// Client → chunk: sell up to MAX_VENDOR_BATCH_SIZE items in a single round-trip
struct SellBatchRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    std::vector<SellBatchItemEntry> items;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Game-server → chunk: update stock count for one vendor item (after restock)
struct VendorStockUpdateStruct
{
    int npcId = 0;
    int itemId = 0;
    int newStock = -1;
};

// ============= REPAIR SHOP STRUCTS =============

/// Client → chunk: open blacksmith repair shop
struct OpenRepairShopRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: repair one item
struct RepairItemRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    int inventoryItemId = 0; // player_inventory.id
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: repair all equipped durable items
struct RepairAllRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

// ============= P2P TRADE STRUCTS =============

/// One item slot in a trade offer
struct TradeOfferItemStruct
{
    int inventoryItemId = 0; // player_inventory.id
    int itemId = 0;          // denorm for client display
    int quantity = 1;
};

/// Active P2P trade session (in-memory only)
struct TradeSessionStruct
{
    std::string sessionId = ""; ///< "trade_{charA}_{charB}_{ts_ms}"
    int charAId = 0;
    int charBId = 0;
    int clientAId = 0;
    int clientBId = 0;
    std::vector<TradeOfferItemStruct> offerA; ///< items offered by charA
    std::vector<TradeOfferItemStruct> offerB; ///< items offered by charB
    int goldA = 0;                            ///< gold offered by charA
    int goldB = 0;                            ///< gold offered by charB
    bool confirmedA = false;
    bool confirmedB = false;
    std::chrono::steady_clock::time_point lastActivity;
    static constexpr int TTL_SECONDS = 60;
};

/// Client → chunk: initiate P2P trade request
struct TradeRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int targetCharacterId = 0;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: accept / decline trade invite
struct TradeRespondStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string sessionId = "";
    bool accept = false;
    TimestampStruct timestamps;
};

/// Client → chunk: update own offer in active session
struct TradeOfferUpdateStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string sessionId = "";
    std::vector<TradeOfferItemStruct> items;
    int gold = 0;
    TimestampStruct timestamps;
};

/// Client → chunk: confirm or cancel active session
struct TradeConfirmCancelStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string sessionId = "";
    TimestampStruct timestamps;
};

// ============= EQUIPMENT SYSTEM STRUCTS =============

/// Client → chunk: equip an inventory item
struct EquipItemRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int inventoryItemId = 0; // player_inventory.id
    TimestampStruct timestamps;
};

/// Client → chunk: unequip item in a specific slot
struct UnequipItemRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string equipSlotSlug = "";
    TimestampStruct timestamps;
};

/// Client → chunk: request current equipment state
struct GetEquipmentRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    TimestampStruct timestamps;
};

/// In-memory state of one equipment slot
struct EquipmentSlotItemStruct
{
    int inventoryItemId = 0;
    int itemId = 0;
    std::string itemSlug = "";
    int durabilityCurrent = 0;
    int durabilityMax = 0;
    bool isDurabilityWarning = false;
    bool blockedByTwoHanded = false; // only meaningful on off_hand when empty
};

/// In-memory equipment state for one character
struct CharacterEquipmentStruct
{
    int characterId = 0;
    // slug → slot item (slot is present & non-zero inventoryItemId = occupied)
    std::unordered_map<std::string, EquipmentSlotItemStruct> slots;
    bool twoHandedActive = false; // true when main_hand holds a two-handed weapon
};

/// Chunk → game-server: persist an equip or unequip operation
struct SaveEquipmentChangeStruct
{
    int characterId = 0;
    std::string action = ""; // "equip" | "unequip"
    int inventoryItemId = 0; // player_inventory.id  (0 when unequipping by slot)
    std::string equipSlotSlug = "";
};

// ============= DURABILITY UPDATE (server → client) =============

/// One item's durability snapshot sent to client
struct DurabilityEntryStruct
{
    int inventoryItemId = 0;
    int itemId = 0;
    int durabilityCurrent = 0;
    int durabilityMax = 0;
};

/// Batch durability update event (chunk → client)
struct DurabilityUpdateStruct
{
    int characterId = 0;
    std::vector<DurabilityEntryStruct> entries;
};

// ============= PERSISTENCE STRUCTS for trading/durability =============

/// Chunk → game-server: save a vendor currency transaction
struct SaveCurrencyTransactionStruct
{
    int characterId = 0;
    int npcId = 0;
    int itemId = 0;
    int quantity = 0;
    int totalPrice = 0;
    std::string transactionType = ""; ///< "buy" | "sell" | "repair"
};

/// Chunk → game-server: persist durability_current for one item
struct SaveDurabilityChangeStruct
{
    int characterId = 0;
    int inventoryItemId = 0;
    int durabilityCurrent = 0;
};

// ============= RESPAWN ZONES =============

/// A safe respawn point in the world (town / camp / shrine)
struct RespawnZoneStruct
{
    int id = 0;
    std::string name = "";
    PositionStruct position; ///< World coordinates of the respawn point
    int zoneId = 0;          ///< Game zone this point belongs to
    bool isDefault = false;  ///< Fallback when no closer zone is found
};

// ============= GAME ZONES =============

/// A named world zone with AABB bounds used for zone detection and exploration rewards.
struct GameZoneStruct
{
    int id = 0;
    std::string slug;
    std::string name;
    int minLevel = 0;
    int maxLevel = 0;
    bool isPvp = false;
    bool isSafeZone = false;
    // AABB (always present; for CIRCLE/ANNULUS this is the enclosing box)
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    // Shape-aware geometry (migration 062)
    ZoneShape shape = ZoneShape::RECT;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float innerRadius = 0.0f; ///< ANNULUS inner exclusion radius; 0 for RECT/CIRCLE
    float outerRadius = 0.0f; ///< CIRCLE/ANNULUS boundary radius; 0 for RECT
    int explorationXpReward = 100;
    int championThresholdKills = 100; ///< Kills of a mob type in this zone before champion spawns

    /// Returns true if (x,y) lies inside this zone, respecting shape.
    [[nodiscard]] bool contains(float x, float y) const noexcept
    {
        switch (shape)
        {
        case ZoneShape::CIRCLE:
        {
            float dx = x - centerX, dy = y - centerY;
            return (dx * dx + dy * dy) <= (outerRadius * outerRadius);
        }
        case ZoneShape::ANNULUS:
        {
            float dx = x - centerX, dy = y - centerY;
            float d2 = dx * dx + dy * dy;
            return d2 >= (innerRadius * innerRadius) && d2 <= (outerRadius * outerRadius);
        }
        default: // RECT
            return x >= minX && x <= maxX && y >= minY && y <= maxY;
        }
    }
};

// ============= TIMED CHAMPION TEMPLATES =============

/// A world boss that spawns on an interval schedule (loaded from timed_champion_templates table).
struct TimedChampionTemplate
{
    int id = 0;
    std::string slug;        ///< Unique identifier (e.g. "alpha_wolf")
    int gameZoneId = 0;      ///< zones.id  (game zone, not spawn zone)
    int mobTemplateId = 0;   ///< mob_templates.id of the base mob
    int intervalHours = 6;   ///< Respawn interval in hours
    int windowMinutes = 15;  ///< How long champion stays before despawning
    int64_t nextSpawnAt = 0; ///< Unix timestamp of next spawn
    std::string announceKey; ///< Localisation key for pre-spawn announcement
};

// ============= STATUS EFFECT TEMPLATES =============

/// A single modifier row from status_effect_modifiers
struct StatusEffectModifierDef
{
    std::string modifierType;  ///< "flat" | "percent" | "percent_all"
    std::string attributeSlug; ///< "" when modifierType == "percent_all"
    double value = 0.0;        ///< Magnitude (negative = penalty)
};

/// Full template for a named status effect (loaded from DB at startup)
struct StatusEffectTemplate
{
    std::string slug;     ///< e.g. "resurrection_sickness"
    std::string category; ///< "buff" | "debuff" | "dot" | "hot" | "cc"
    int durationSec = 0;  ///< 0 = permanent
    std::vector<StatusEffectModifierDef> modifiers;
};

/// Client → chunk-server: player requests respawn after death
struct RespawnRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    TimestampStruct timestamps;
};

// ============= SKILL TRAINER STRUCTS =============

/// One entry from class_skill_tree for a single teachable skill.
/// Populated by game-server at startup via setTrainerData and held in TrainerManager.
struct ClassSkillTreeEntryStruct
{
    int skillId = 0;
    std::string skillSlug;
    std::string skillName;
    bool isPassive = false;
    int requiredLevel = 1;
    int spCost = 1;   ///< Skill points required to learn
    int goldCost = 0; ///< Gold coins required to learn (0 = free)
    bool requiresBook = false;
    int bookItemId = 0;                ///< 0 if no book required
    std::string prerequisiteSkillSlug; ///< "" if no prerequisite
};

/// Trainer NPC data: the set of skills one trainer teaches.
/// Keyed by npcId and held in TrainerManager (analogous to VendorNPCDataStruct).
struct TrainerNPCDataStruct
{
    int npcId = 0;
    std::vector<ClassSkillTreeEntryStruct> skills;
};

/// Client → chunk: open skill trainer shop window directly (without dialogue)
struct OpenSkillShopRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Client → chunk: learn a specific skill from the skill shop window
struct RequestLearnSkillRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int npcId = 0;
    std::string skillSlug;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Chunk-server → game-server: a timed champion was killed (need DB update).
struct TimedChampionKilledStruct
{
    std::string slug; ///< timed_champion_templates.slug
    int killerCharId = 0;
    int64_t killedAt = 0; ///< Unix timestamp
};

// ── Chat system ─────────────────────────────────────────────────────────────

enum class ChatChannel
{
    LOCAL,   ///< Visible to players within localRadius units of sender
    ZONE,    ///< Visible to all players on this chunk server
    WHISPER, ///< Private message to a single player by character name
};

/// Client → chunk-server: player sends a chat message
struct ChatMessageStruct
{
    int senderClientId = 0;
    int senderCharId = 0;
    std::string senderName; ///< populated server-side, not trusted from client
    ChatChannel channel = ChatChannel::ZONE;
    std::string text;          ///< max 255 chars, validated server-side
    std::string targetName;    ///< only used for WHISPER channel
    float localRadius = 50.0f; ///< only used for LOCAL channel
    TimestampStruct timestamps;
};

// ── Title system ─────────────────────────────────────────────────────────────

/// A single attribute bonus granted by a title
struct TitleBonusStruct
{
    std::string attributeSlug; ///< e.g. "physical_attack", "crit_chance"
    float value = 0.0f;        ///< additive flat bonus (same unit as ActiveEffectStruct::value)
};

/// Static title definition loaded from game-server (table: titles)
struct TitleDefinitionStruct
{
    int id = 0;
    std::string slug;                      ///< unique key, e.g. "wolf_slayer"
    std::string displayName;               ///< e.g. "Wolf Slayer"
    std::string description;               ///< flavour text / unlock condition hint
    std::vector<TitleBonusStruct> bonuses; ///< stat bonuses while this title is equipped
    /// How the title may be earned: "quest" | "reputation" | "mastery" | "bestiary" | "level" | "admin_grant"
    std::string earnCondition;
    /// Data-driven condition parameters (JSONB from DB).
    /// Examples:
    ///   earnCondition="bestiary"   → {"mobSlug":"forest_wolf","minTier":6}
    ///   earnCondition="mastery"    → {"masterySlug":"sword_mastery","minTier":3}
    ///   earnCondition="reputation" → {"factionSlug":"bandits","minTierName":"ally"}
    ///   earnCondition="level"      → {"level":10}
    ///   earnCondition="quest"      → {"questSlug":"main_story_finale"}
    nlohmann::json conditionParams = nlohmann::json::object();
};

/// Runtime state of a character's title collection
struct PlayerTitleStateStruct
{
    int characterId = 0;
    std::vector<std::string> earnedSlugs; ///< all titles the player has unlocked
    std::string equippedSlug;             ///< currently displayed title (empty = none)
};

/// Client → chunk-server: request to equip/unequip a title
struct EquipTitleRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    /// Slug to equip, or empty string "" to unequip current title
    std::string titleSlug;
    TimestampStruct timestamps;
};

// ── Skill Bar ─────────────────────────────────────────────────────────────

/// Client → chunk-server: assign or clear a hotbar slot
struct SetSkillBarSlotRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    int slotIndex = -1;
    std::string skillSlug = ""; ///< empty = clear slot
    TimestampStruct timestamps;
};

// ── Emote system ─────────────────────────────────────────────────────────────

/// Static emote definition loaded from game-server (table: emote_definitions)
struct EmoteDefinitionStruct
{
    int id = 0;
    std::string slug;          ///< unique key, e.g. "dance_basic"
    std::string displayName;   ///< localised display name, e.g. "Танцевать"
    std::string animationName; ///< client-side animation clip name, e.g. "emote_dance_basic"
    std::string category;      ///< UI grouping: "basic" | "social" | "dance" | "sit" | ...
    bool isDefault = false;    ///< TRUE = granted automatically to every character
    int sortOrder = 0;         ///< ordering within category for the UI list
};

/// Client → chunk-server: request to play an emote animation
struct UseEmoteRequestStruct
{
    int characterId = 0;
    int clientId = 0;
    std::string emoteSlug; ///< slug of the emote to play
    TimestampStruct timestamps;
};

// ── NPC Ambient Speech system ────────────────────────────────────────────────

/// Single ambient speech line definition loaded from game-server
struct NPCAmbientLineStruct
{
    int id = 0;
    int npcId = 0;
    std::string lineKey = "";             ///< Localisation key sent to client, e.g. "npc.blacksmith.idle_1"
    std::string triggerType = "periodic"; ///< "periodic" | "proximity"
    int triggerRadius = 400;
    int priority = 0;              ///< Higher = more important pool; highest non-empty pool wins
    int weight = 10;               ///< Relative weight for weighted-random within same priority group
    int cooldownSec = 60;          ///< Per-client minimum seconds between shows of this line
    nlohmann::json conditionGroup; ///< null = always valid, otherwise DialogueConditionEvaluator tree
};

/// Per-NPC ambient speech config: timing + lines
struct NPCAmbientSpeechConfigStruct
{
    int npcId = 0;
    int minIntervalSec = 20;
    int maxIntervalSec = 60;
    std::vector<NPCAmbientLineStruct> lines;
};

// ============= WORLD INTERACTIVE OBJECTS (migration 043) =================

/// Static definition received from game-server via setWorldObjects packet.
struct WorldObjectDataStruct
{
    int id = 0;
    std::string slug = "";
    std::string nameKey = "";         ///< Localisation key forwarded to client
    std::string objectType = "";      ///< "examine" | "search" | "activate" | "use_with_item" | "channeled"
    std::string scope = "per_player"; ///< "per_player" | "global"
    PositionStruct position;
    int zoneId = 0;
    int dialogueId = 0;     ///< 0 = no dialogue
    int lootTableId = 0;    ///< 0 = no loot
    int requiredItemId = 0; ///< 0 = no required item
    float interactionRadius = 250.0f;
    int channelTimeSec = 0; ///< 0 = instant
    int respawnSec = 0;     ///< 0 = never respawns
    bool isActiveByDefault = true;
    int minLevel = 0;
    nlohmann::json conditionGroup;       ///< null = no conditions
    std::string initialState = "active"; ///< state read from DB at startup

    bool operator==(const WorldObjectDataStruct &other) const
    {
        return id == other.id;
    }
};

/// Runtime global instance state (only for scope="global" objects).
struct WorldObjectInstanceStruct
{
    int objectId = 0;
    std::string state = "active"; ///< "active" | "depleted" | "disabled"
    std::chrono::steady_clock::time_point depletedAt{};
    int respawnSec = 0;
};

/// Sent by client to request interaction with a world object.
struct WorldObjectInteractRequestStruct
{
    int characterId = 0;
    int objectId = 0;
    PositionStruct playerPosition;
    TimestampStruct timestamps;
};

/// Sent by client to cancel an in-progress channeled interaction.
struct WorldObjectChannelCancelStruct
{
    int characterId = 0;
    int objectId = 0;
};

// ── Analytics system (migration 058) ─────────────────────────────────────────

/// Chunk Server → Game Server: fire-and-forget analytics event.
/// Built at the point of a game event and forwarded via GameServerWorker.sendDataToGameServer().
/// Game Server performs an INSERT INTO game_analytics without replying.
struct AnalyticsEventStruct
{
    std::string analyticsType = ""; ///< event_type value: "player_death", "level_up", "quest_accept", etc.
    int characterId = 0;
    std::string sessionId = ""; ///< copied from CharacterDataStruct::sessionId
    int level = 0;              ///< characterLevel at the time of the event
    int zoneId = 0;
    nlohmann::json payload = {}; ///< event-specific fields matching the spec in analytics-system-plan.md
};