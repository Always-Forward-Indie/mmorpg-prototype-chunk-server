#include <iostream>
#include <vector>
#include <helpers/Database.hpp>
#include <chunk_server/ClientData.hpp>

class CharacterManager {
public:
    // Constructor
    CharacterManager();

    // Method to get characters list
    std::vector<CharacterDataStruct> getCharactersList(Database& database, ClientData& clientData, int accountId);
    // Method to get a character
    CharacterDataStruct getCharacterData(Database& database, ClientData& clientData, int accountId, int characterId);
    // Method to get a character position
    PositionStruct getCharacterPosition(Database& database, ClientData& clientData, int accountId, int characterId);

    //update character position in object
    void setCharacterPosition(ClientData& clientData, int accountId, PositionStruct &position);
    //update character data in object
    void setCharacterData(ClientData& clientData, int accountId, CharacterDataStruct &characterData);

    // update character position in the database
    void updateCharacterPosition(Database& database, ClientData& clientData, int accountId, int characterId, PositionStruct &position);
    // update character data in the database
    void updateCharacterData(Database& database, ClientData& clientData, int accountId, int characterId, CharacterDataStruct &characterData);
};