#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include "data/DataStructs.hpp"

class JSONParser
{
private:
    static constexpr size_t max_length = 1024;

public:
    JSONParser();
    
    CharacterDataStruct parseCharacterData(const std::array<char, max_length> &dataBuffer, size_t bytes_transferred);
    PositionStruct parsePositionData(const std::array<char, max_length> &dataBuffer, size_t bytes_transferred);
    ClientDataStruct parseClientData(const std::array<char, max_length> &dataBuffer, size_t bytes_transferred);
    MessageStruct parseMessage(const std::array<char, max_length> &dataBuffer, size_t bytes_transferred);
    std::string parseEventType(const std::array<char, max_length> &dataBuffer, size_t bytes_transferred);
};