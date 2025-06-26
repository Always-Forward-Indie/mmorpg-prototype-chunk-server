#pragma once

#include <iostream>
#include <vector>
#include "data/DataStructs.hpp"
#include <utils/Logger.hpp>
#include <shared_mutex>

class CharacterManager {
public:
    // Constructor
    CharacterManager(Logger& logger);

    // Load characters list
    void loadCharactersList(std::vector<CharacterDataStruct> charactersList);

    // Load character data
    void loadCharacterData(CharacterDataStruct characterData);

    // Load character Attributes
    void loadCharacterAttributes(std::vector<CharacterAttributeStruct> characterAttributes);

    // set character position
    void setCharacterPosition(int characterID, PositionStruct position);

    // Get characters list
    std::vector<CharacterDataStruct> getCharactersList();

    // Get basic character data by character ID
    CharacterDataStruct getCharacterData(int characterID);

    // Get character attributes by character ID
    std::vector<CharacterAttributeStruct> getCharacterAttributes(int characterID);

    // Get character position by character ID
    PositionStruct getCharacterPosition(int characterID);


private:
    Logger& logger_;
    // characters list
    std::vector<CharacterDataStruct> charactersList_;

    // Mutex for the characters list
    mutable std::shared_mutex mutex_;
};