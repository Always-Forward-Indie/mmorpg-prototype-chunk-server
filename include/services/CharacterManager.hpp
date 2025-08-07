#pragma once

#include "data/DataStructs.hpp"
#include <iostream>
#include <shared_mutex>
#include <utils/Logger.hpp>
#include <vector>

class CharacterManager
{
  public:
    // Constructor
    CharacterManager(Logger &logger);

    // add character to the list
    void addCharacter(const CharacterDataStruct &characterData);

    // Remove character by ID
    void removeCharacter(int characterID);

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

    // Update character health
    void updateCharacterHealth(int characterID, int newHealth);

    // Get characters in a specific zone
    std::vector<CharacterDataStruct> getCharactersInZone(float centerX, float centerY, float radius);

    // Get character by ID (returns empty struct if not found)
    CharacterDataStruct getCharacterById(int characterID);

    // Calculate distance between two positions
    float calculateDistance(const PositionStruct &pos1, const PositionStruct &pos2);

  private:
    Logger &logger_;
    // characters list
    std::vector<CharacterDataStruct> charactersList_;

    // Mutex for the characters list
    mutable std::shared_mutex mutex_;
};