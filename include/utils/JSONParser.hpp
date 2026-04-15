#pragma once

#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

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
    std::vector<RespawnZoneStruct> parseRespawnZonesList(const char *data, size_t length);
    std::vector<GameZoneStruct> parseGameZonesList(const char *data, size_t length);
    std::vector<StatusEffectTemplate> parseStatusEffectTemplates(const char *data, size_t length);
    std::vector<MobDataStruct> parseMobsList(const char *data, size_t length);
    std::vector<MobAttributeStruct> parseMobsAttributesList(const char *data, size_t length);
    std::vector<std::pair<int, std::vector<SkillStruct>>> parseMobsSkillsMapping(const char *data, size_t length);
    std::vector<CharacterAttributeStruct> parseCharacterAttributesList(const char *data, size_t length);
    std::vector<CharacterDataStruct> parseCharactersList(const char *data, size_t length);
    std::vector<ItemDataStruct> parseItemsList(const char *data, size_t length);
    std::vector<MobLootInfoStruct> parseMobLootInfo(const char *data, size_t length);
    std::vector<NPCDataStruct> parseNPCsList(const char *data, size_t length);
    std::vector<NPCAttributeStruct> parseNPCsAttributes(const char *data, size_t length);

    /// Parse the setMobWeaknessesResistances packet.
    /// Returns {weaknesses, resistances} maps: mob template id → list of element slugs.
    std::pair<std::unordered_map<int, std::vector<std::string>>,
        std::unordered_map<int, std::vector<std::string>>>
    parseMobWeaknessesResistances(const char *data, size_t length);

    // Dialogue and quest system parsers
    std::vector<DialogueGraphStruct> parseDialoguesList(const char *data, size_t length);
    std::vector<NPCDialogueMappingStruct> parseNPCDialogueMappings(const char *data, size_t length);
    std::vector<QuestStruct> parseQuestsList(const char *data, size_t length);
    std::vector<PlayerQuestProgressStruct> parsePlayerQuestProgress(const char *data, size_t length);
    std::vector<PlayerFlagStruct> parsePlayerFlags(const char *data, size_t length);
    std::vector<ActiveEffectStruct> parsePlayerActiveEffects(const char *data, size_t length);
    std::vector<PlayerInventoryItemStruct> parsePlayerInventory(const char *data, size_t length);
    // Parse attribute refresh response from game-server (characterId + attributesData array)
    std::pair<int, std::vector<CharacterAttributeStruct>> parseCharacterAttributesRefresh(const char *data, size_t length);

    // Experience system parsers
    std::vector<ExperienceLevelEntry> parseExpLevelTable(const char *data, size_t length);

    // Champion system parsers
    std::vector<TimedChampionTemplate> parseTimedChampionTemplates(const char *data, size_t length);

    // Combat system parsers
    nlohmann::json parseCombatActionData(const char *data, size_t length);

    // NPC Ambient Speech parser
    std::vector<NPCAmbientSpeechConfigStruct> parseNPCAmbientSpeech(const char *data, size_t length);

    // Timestamp parsing for lag compensation
    TimestampStruct parseTimestamps(const char *data, size_t length);
    TimestampStruct parseTimestamps(const nlohmann::json &jsonData);

    // RequestId parsing for packet synchronization
    std::string parseRequestId(const char *data, size_t length);
    std::string parseRequestId(const nlohmann::json &jsonData);
};
