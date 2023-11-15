// ClientData.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

struct PositionStruct
{
    float positionX;
    float positionY;
    float positionZ;
};

struct CharacterDataStruct
{
    int characterId;
    int characterLevel;
    std::string characterName;
    std::string characterClass;
    std::string characterRace;
    PositionStruct characterPosition;
};

struct ClientDataStruct
{
    int clientId;
    std::string hash;
    CharacterDataStruct characterData;
};

class ClientData
{
public:
    ClientData();

    void storeClientData(const ClientDataStruct &clientData);
    void updateClientData(const int &clientID, const std::string &field, const std::string &value);
    void updateCharacterData(const int &clientID, const CharacterDataStruct &characterData);
    void updateCharacterPositionData(const int &clientID, const PositionStruct &positionData);
    const ClientDataStruct *getClientData(const int &clientID) const;

private:
    std::unordered_map<int, ClientDataStruct> clientDataMap_;
    mutable std::mutex clientDataMutex_; // mutex for each significant data segment if needed
};