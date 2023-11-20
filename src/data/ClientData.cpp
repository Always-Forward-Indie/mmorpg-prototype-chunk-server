#include "data/ClientData.hpp"
#include <iostream>
#include <mutex>

ClientData::ClientData()
{
    clientDataMap_.reserve(10000); // Reserve space for 10,000 clients, adjust as needed
}

void ClientData::storeClientData(const ClientDataStruct &clientData)
{
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    clientDataMap_[clientData.clientId] = clientData;
    // std::cout << "ClientDataStruct stored in ClientData class with hash = " << clientData.hash << std::endl;
}

// Update client data
void ClientData::updateClientData(const int &clientID, const std::string &field, const std::string &value)
{
    // Assuming that clientDataMap_ is an unordered_map with the key as the hash and the value as ClientDataStruct.
    auto it = clientDataMap_.find(clientID);
    if (it != clientDataMap_.end())
    {
        if (field == "characterId")
        {
            it->second.characterData.characterId = std::stoi(value);
        }
        else if (field == "characterLevel")
        {
            it->second.characterData.characterLevel = std::stoi(value);
        }
        else if (field == "characterName")
        {
            it->second.characterData.characterName = value;
        }
        else if (field == "characterClass")
        {
            it->second.characterData.characterClass = value;
        }
    }
}

// Update client character data with argument CharacterDataStruct
void ClientData::updateCharacterData(const int &clientID, const CharacterDataStruct &characterData)
{
    std::lock_guard<std::mutex> lock(clientDataMutex_); // lock_guard is a mutex wrapper that provides a convenient RAII-style mechanism for owning a mutex for the duration of a scoped block.
    auto it = clientDataMap_.find(clientID); // Find the client data in the map
    if (it != clientDataMap_.end())
    {
        // Update the character data
        it->second.characterData = characterData;
    }
}

// Update client character position data with argument PositionStruct
void ClientData::updateCharacterPositionData(const int &clientID, const PositionStruct &positionData)
{
    std::lock_guard<std::mutex> lock(clientDataMutex_); // lock_guard is a mutex wrapper that provides a convenient RAII-style mechanism for owning a mutex for the duration of a scoped block.
    auto it = clientDataMap_.find(clientID); // Find the client data in the map
    if (it != clientDataMap_.end())
    {
        // Update the character position data
        it->second.characterData.characterPosition = positionData;
    }
}

const ClientDataStruct *ClientData::getClientData(const int &clientID) const
{
    if (clientID == 0)
    {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(clientDataMutex_); // lock_guard is a mutex wrapper that provides a convenient RAII-style mechanism for owning a mutex for the duration of a scoped block.
    auto it = clientDataMap_.find(clientID); // Find the client data in the map
    if (it != clientDataMap_.end())
    {
        return &it->second;
    }
    return nullptr;
}