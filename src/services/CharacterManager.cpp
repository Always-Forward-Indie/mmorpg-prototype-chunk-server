#include "services/CharacterManager.hpp"
#include <iostream>
#include <random>
#include <vector>

CharacterManager::CharacterManager(Logger &logger)
    : logger_(logger)
{
    // Initialize properties or perform any setup here
}

void
CharacterManager::loadCharactersList(std::vector<CharacterDataStruct> charactersList)
{
    try
    {
        if (charactersList.empty())
        {
            // Log that the data is empty
            logger_.logError("No characters found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : charactersList)
        {
            CharacterDataStruct characterData;
            characterData.characterId = row.characterId;

            charactersList_.push_back(characterData);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading characters: " + std::string(e.what()));
    }
}

void
CharacterManager::loadCharacterAttributes(std::vector<CharacterAttributeStruct> characterAttributes)
{
    try
    {
        if (characterAttributes.empty())
        {
            // Log that the data is empty
            logger_.logError("No character attributes found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (const auto &row : characterAttributes)
        {
            CharacterAttributeStruct attributeData;
            attributeData.character_id = row.character_id;
            attributeData.id = row.id;
            attributeData.name = row.name;
            attributeData.slug = row.slug;
            attributeData.value = row.value;

            charactersList_[attributeData.character_id].attributes.push_back(attributeData);
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading character attributes: " + std::string(e.what()));
    }
}

// load character data
void
CharacterManager::loadCharacterData(CharacterDataStruct characterData)
{
    try
    {
        if (characterData.characterId == 0)
        {
            // Log that the data is empty
            logger_.logError("No character data found in GS");
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        // get character according id from list and set his data
        for (auto &character : charactersList_)
        {
            if (character.characterId == characterData.characterId)
            {
                character.characterLevel = characterData.characterLevel;
                character.characterExperiencePoints = characterData.characterExperiencePoints;
                character.expForNextLevel = characterData.expForNextLevel;
                character.characterCurrentHealth = characterData.characterCurrentHealth;
                character.characterCurrentMana = characterData.characterCurrentMana;
                character.characterMaxHealth = characterData.characterMaxHealth;
                character.characterMaxMana = characterData.characterMaxMana;
                character.characterName = characterData.characterName;
                character.characterClass = characterData.characterClass;
                character.characterRace = characterData.characterRace;
                character.characterPosition = characterData.characterPosition;
                character.attributes = characterData.attributes;
            }
        }
    }
    catch (const std::exception &e)
    {
        logger_.logError("Error loading character data: " + std::string(e.what()));
    }
}

void
CharacterManager::addCharacter(const CharacterDataStruct &characterData)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Проверяем, существует ли уже персонаж с таким ID
    for (const auto &character : charactersList_)
    {
        if (character.characterId == characterData.characterId)
        {
            logger_.logError("Character with ID " + std::to_string(characterData.characterId) + " already exists. Skipping add.");
            return;
        }
    }

    // Добавляем нового персонажа
    charactersList_.push_back(characterData);
    logger_.log("Character with ID " + std::to_string(characterData.characterId) + " added.");
}

// remove character by ID
void
CharacterManager::removeCharacter(int characterID)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = std::remove_if(charactersList_.begin(), charactersList_.end(), [characterID](const CharacterDataStruct &character)
        { return character.characterId == characterID; });
    if (it != charactersList_.end())
    {
        charactersList_.erase(it, charactersList_.end());
        logger_.log("Character with ID " + std::to_string(characterID) + " removed.");
    }
    else
    {
        logger_.logError("Character with ID " + std::to_string(characterID) + " not found. Cannot remove.");
    }
}

std::vector<CharacterDataStruct>
CharacterManager::getCharactersList()
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return charactersList_;
}

CharacterDataStruct
CharacterManager::getCharacterData(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &character : charactersList_)
    {
        if (character.characterId == characterID)
        {
            return character;
        }
    }

    return CharacterDataStruct();
}

std::vector<CharacterAttributeStruct>
CharacterManager::getCharacterAttributes(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &character : charactersList_)
    {
        if (character.characterId == characterID)
        {
            return character.attributes;
        }
    }

    return std::vector<CharacterAttributeStruct>();
}

PositionStruct
CharacterManager::getCharacterPosition(int characterID)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto &character : charactersList_)
    {
        if (character.characterId == characterID)
        {
            return character.characterPosition;
        }
    }

    return PositionStruct();
}

void
CharacterManager::setCharacterPosition(int characterID, PositionStruct position)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &character : charactersList_)
    {
        if (character.characterId == characterID)
        {
            character.characterPosition = position;
        }
    }
}

void
CharacterManager::updateCharacterHealth(int characterID, int newHealth)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto &character : charactersList_)
    {
        if (character.characterId == characterID)
        {
            character.characterCurrentHealth = newHealth;
            logger_.log("Updated character " + std::to_string(characterID) + " health to " + std::to_string(newHealth));
            return;
        }
    }
    logger_.logError("Character " + std::to_string(characterID) + " not found when updating health");
}
