#include "chunk_server/CharacterManager.hpp"
#include <pqxx/pqxx>
#include <iostream>
#include <vector>

// Constructor
CharacterManager::CharacterManager()
{
    // Initialize properties or perform any setup here
}

//TODO - Update methods where remove database calls and use the clientData object instead

// Method to select a character
CharacterDataStruct CharacterManager::getCharacterData(ClientData &clientData, int accountId, int characterId)
{
    // Create a CharacterDataStruct to save the character data from DB
    CharacterDataStruct characterDataStruct;

    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        // Execute the prepared query and assign the result to a pqxx::result object
        pqxx::result selectCharacterData = database.executeQueryWithTransaction(
                    transaction,
                    "get_character",
                    {accountId, characterId});

        if (selectCharacterData.empty())
        {
            transaction.abort(); // Rollback the transaction
            return characterDataStruct;
        }

        // Get the character data
        characterDataStruct.characterId = selectCharacterData[0][0].as<int>();
        characterDataStruct.characterLevel = selectCharacterData[0][1].as<int>();
        characterDataStruct.characterName = selectCharacterData[0][2].as<std::string>();
        characterDataStruct.characterClass = selectCharacterData[0][3].as<std::string>();

        transaction.commit(); // Commit the transaction
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return characterDataStruct;
    }

    return characterDataStruct;
}


// Method to get a character position
PositionStruct CharacterManager::getCharacterPosition(ClientData &clientData, int accountId, int characterId)
{
    // Create a characterPosition to save the character position data from DB
    PositionStruct characterPosition;

    try
    {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        // Execute the prepared query and assign the result to a pqxx::result object
        pqxx::result selectCharacterPosition = database.executeQueryWithTransaction(
                    transaction,
                    "get_character_position",
                    {characterId});

        if (selectCharacterPosition.empty())
        {
            transaction.abort(); // Rollback the transaction
            return characterPosition;
        }

        // Get the character position
        characterPosition.positionX = selectCharacterPosition[0][0].as<float>();
        characterPosition.positionY = selectCharacterPosition[0][1].as<float>();
        characterPosition.positionZ = selectCharacterPosition[0][2].as<float>();

        transaction.commit(); // Commit the transaction
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return characterPosition;
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

// update character position in the database
void CharacterManager::updateCharacterPosition(ClientData& clientData, int accountId, int characterId, PositionStruct &position){
    try {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        pqxx::result updateCharacterPosition = database.executeQueryWithTransaction(
                    transaction,
                    "set_character_position",
                    {characterId, position.positionX, position.positionY, position.positionZ});
        
        if (updateCharacterPosition.empty())
        {
            transaction.abort(); // Rollback the transaction
            return;
        }

        transaction.commit(); // Commit the transaction

        //update character position in object
        setCharacterPosition(clientData, accountId, position);
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return;
    }
}

// update character data in the database
void CharacterManager::updateCharacterData(ClientData& clientData, int accountId, int characterId, CharacterDataStruct &characterData){
    try {
        pqxx::work transaction(database.getConnection()); // Start a transaction
        pqxx::result updateCharacterData = database.executeQueryWithTransaction(
                    transaction,
                    "set_character_health",
                    {characterId, std::to_string(5)});
        
        if (updateCharacterData.empty())
        {
            transaction.abort(); // Rollback the transaction
            return;
        }

        transaction.commit(); // Commit the transaction

        //update character data in object
        setCharacterData(clientData, accountId, characterData);
    }
    catch (const std::exception &e)
    {
        // Handle database connection or query errors
        database.handleDatabaseError(e);
        // You might want to send an error response back to the client or log the error
        return;
    }
}