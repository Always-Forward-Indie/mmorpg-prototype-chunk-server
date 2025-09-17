#pragma once

#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>

class JSONParser
{
  public:
    JSONParser();
    ~JSONParser();

    CharacterDataStruct parseCharacterData(const char *data, size_t length);
    PositionStruct parsePositionData(const char *data, size_t length);
    ClientDataStruct parseClientData(const char *data, size_t length);
    MessageStruct parseMessage(const char *data, size_t length);
    std::string parseEventType(const char *data, size_t length);
    ChunkInfoStruct parseChunkInfo(const char *data, size_t length);
    std::vector<SpawnZoneStruct> parseSpawnZonesList(const char *data, size_t length);
    std::vector<MobDataStruct> parseMobsList(const char *data, size_t length);
    std::vector<MobAttributeStruct> parseMobsAttributesList(const char *data, size_t length);
    std::vector<std::pair<int, std::vector<SkillStruct>>> parseMobsSkillsMapping(const char *data, size_t length);
    std::vector<CharacterAttributeStruct> parseCharacterAttributesList(const char *data, size_t length);
    std::vector<CharacterDataStruct> parseCharactersList(const char *data, size_t length);
    std::vector<ItemDataStruct> parseItemsList(const char *data, size_t length);
    std::vector<MobLootInfoStruct> parseMobLootInfo(const char *data, size_t length);
    std::vector<NPCDataStruct> parseNPCsList(const char *data, size_t length);
    std::vector<NPCAttributeStruct> parseNPCsAttributes(const char *data, size_t length);

    // Experience system parsers
    std::vector<ExperienceLevelEntry> parseExpLevelTable(const char *data, size_t length);

    // Combat system parsers
    nlohmann::json parseCombatActionData(const char *data, size_t length);

    // Timestamp parsing for lag compensation
    TimestampStruct parseTimestamps(const char *data, size_t length);
    TimestampStruct parseTimestamps(const nlohmann::json &jsonData);

    // RequestId parsing for packet synchronization
    std::string parseRequestId(const char *data, size_t length);
    std::string parseRequestId(const nlohmann::json &jsonData);
};
