#pragma once
#include "data/DataStructs.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

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
    std::vector<MobDataStruct>,
    std::vector<SpawnZoneStruct>,
    std::vector<MobAttributeStruct>,
    std::vector<CharacterDataStruct>,
    std::vector<CharacterAttributeStruct>,
    std::vector<ClientDataStruct>>;
