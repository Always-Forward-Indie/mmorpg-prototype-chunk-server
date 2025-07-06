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
    std::string uid = "";
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
    std::vector<std::string> spawnedMobsUIDList;
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