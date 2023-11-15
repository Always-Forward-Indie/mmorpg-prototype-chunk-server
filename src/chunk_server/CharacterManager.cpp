#include "chunk_server/CharacterManager.hpp"
#include <pqxx/pqxx>
#include <iostream>
#include <vector>

// Constructor
CharacterManager::CharacterManager()
{
    // Initialize properties or perform any setup here
}

// Method to select a character
CharacterDataStruct CharacterManager::getCharacterData(ClientData &clientData, int accountId, int characterId)
{
    // Create a CharacterDataStruct to save the character data
    CharacterDataStruct characterDataStruct;

    const ClientDataStruct *currentClientData = clientData.getClientData(accountId);

    if (currentClientData != nullptr)
    {
        // Get the character data
        characterDataStruct = currentClientData->characterData;
    }

    return characterDataStruct;
}


// Method to get a character position
PositionStruct CharacterManager::getCharacterPosition(ClientData &clientData, int accountId, int characterId)
{
    // Create a characterPosition to save the character position data from DB
    PositionStruct characterPosition;

    const ClientDataStruct *currentClientData = clientData.getClientData(accountId);

    if (currentClientData != nullptr)
    {
        // Get the character position data
        characterPosition = currentClientData->characterData.characterPosition;
    }

    return characterPosition;
}

//update character position in object
void CharacterManager::setCharacterPosition(ClientData& clientData, int accountId, PositionStruct &position){
    clientData.updateCharacterPositionData(accountId, position);
}

//update character data in object
void CharacterManager::setCharacterData(ClientData& clientData, int accountId, CharacterDataStruct &characterData){
    clientData.updateCharacterData(accountId, characterData);
}