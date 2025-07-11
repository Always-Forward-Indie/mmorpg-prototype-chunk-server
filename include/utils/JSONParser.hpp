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
    std::vector<CharacterAttributeStruct> parseCharacterAttributesList(const char *data, size_t length);
    std::vector<CharacterDataStruct> parseCharactersList(const char *data, size_t length);

    // Combat system parsers
    nlohmann::json parseCombatActionData(const char *data, size_t length);
};
