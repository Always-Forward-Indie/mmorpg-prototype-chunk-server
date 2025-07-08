#pragma once
#include <boost/asio.hpp>
#include <string>

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
    std::string message = "";
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
    std::string raceName = "";
    int level = 0;
    int currentHealth = 0;
    int currentMana = 0;
    int maxHealth = 0;
    int maxMana = 0;
    std::vector<MobAttributeStruct> attributes;
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
 * @brief Movement data for individual mobs
 */
struct MobMovementData
{
    float nextMoveTime = 0.0f;
    float movementDirectionX = 0.0f;
    float movementDirectionY = 0.0f;
    float speedMultiplier = 1.0f;
    float stepMultiplier = 0.0f;
    int resetStepCounter = 0;
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