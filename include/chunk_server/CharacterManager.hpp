#include <iostream>
#include <vector>
#include <chunk_server/ClientData.hpp>

class CharacterManager {
public:
    // Constructor
    CharacterManager();

    // Method to get a character
    CharacterDataStruct getCharacterData(ClientData& clientData, int accountId, int characterId);
    // Method to get a character position
    PositionStruct getCharacterPosition(ClientData& clientData, int accountId, int characterId);

    //update character position in object
    void setCharacterPosition(ClientData& clientData, int accountId, PositionStruct &position);
    //update character data in object
    void setCharacterData(ClientData& clientData, int accountId, CharacterDataStruct &characterData);
};