#include "utils/JSONParser.hpp"
#include <iostream>

JSONParser::JSONParser() {}

JSONParser::~JSONParser() {}

CharacterDataStruct
JSONParser::parseCharacterData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    CharacterDataStruct characterData;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("id") && jsonData["body"]["id"].is_number_integer())
    {
        characterData.characterId = jsonData["body"]["id"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("level") && jsonData["body"]["level"].is_number_integer())
    {
        characterData.characterLevel = jsonData["body"]["level"].get<int>();
    }

    // get character experience points for next level
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("expForNextLevel") && jsonData["body"]["expForNextLevel"].is_number_integer())
    {
        characterData.expForNextLevel = jsonData["body"]["expForNextLevel"].get<int>();
    }

    // get character experience points
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("currentExp") && jsonData["body"]["currentExp"].is_number_integer())
    {
        characterData.characterExperiencePoints = jsonData["body"]["currentExp"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("currentHealth") && jsonData["body"]["currentHealth"].is_number_integer())
    {
        characterData.characterCurrentHealth = jsonData["body"]["currentHealth"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("currentMana") && jsonData["body"]["currentMana"].is_number_integer())
    {
        characterData.characterCurrentMana = jsonData["body"]["currentMana"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("maxHealth") && jsonData["body"]["maxHealth"].is_number_integer())
    {
        characterData.characterMaxHealth = jsonData["body"]["maxHealth"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("maxMana") && jsonData["body"]["maxMana"].is_number_integer())
    {
        characterData.characterMaxMana = jsonData["body"]["maxMana"].get<int>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("name") && jsonData["body"]["name"].is_string())
    {
        characterData.characterName = jsonData["body"]["name"].get<std::string>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("class") && jsonData["body"]["class"].is_string())
    {
        characterData.characterClass = jsonData["body"]["class"].get<std::string>();
    }

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("race") && jsonData["body"]["race"].is_string())
    {
        characterData.characterRace = jsonData["body"]["race"].get<std::string>();
    }

    // get character attributes
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("attributesData") && jsonData["body"]["attributesData"].is_array())
    {
        for (const auto &attribute : jsonData["body"]["attributesData"])
        {
            CharacterAttributeStruct attributeData;
            if (attribute.contains("id") && attribute["id"].is_number_integer())
            {
                attributeData.id = attribute["id"].get<int>();
            }
            if (attribute.contains("name") && attribute["name"].is_string())
            {
                attributeData.name = attribute["name"].get<std::string>();
            }
            if (attribute.contains("slug") && attribute["slug"].is_string())
            {
                attributeData.slug = attribute["slug"].get<std::string>();
            }
            if (attribute.contains("value") && attribute["value"].is_number_integer())
            {
                attributeData.value = attribute["value"].get<int>();
            }
            characterData.attributes.push_back(attributeData);
        }
    }

    return characterData;
}

PositionStruct
JSONParser::parsePositionData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    PositionStruct positionData;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posX") && (jsonData["body"]["posX"].is_number_float() || jsonData["body"]["posX"].is_number_integer()))
    {
        positionData.positionX = jsonData["body"]["posX"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posY") && (jsonData["body"]["posY"].is_number_float() || jsonData["body"]["posY"].is_number_integer()))
    {
        positionData.positionY = jsonData["body"]["posY"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posZ") && (jsonData["body"]["posZ"].is_number_float() || jsonData["body"]["posZ"].is_number_integer()))
    {
        positionData.positionZ = jsonData["body"]["posZ"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("rotZ") && (jsonData["body"]["rotZ"].is_number_float() || jsonData["body"]["rotZ"].is_number_integer()))
    {
        positionData.rotationZ = jsonData["body"]["rotZ"].get<float>();
    }

    return positionData;
}

ClientDataStruct
JSONParser::parseClientData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    ClientDataStruct clientData;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("clientId") && jsonData["header"]["clientId"].is_number_integer())
    {
        clientData.clientId = jsonData["header"]["clientId"].get<int>();
    }

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("hash") && jsonData["header"]["hash"].is_string())
    {
        clientData.hash = jsonData["header"]["hash"].get<std::string>();
    }

    return clientData;
}

// parse character attributes
std::vector<CharacterAttributeStruct>
JSONParser::parseCharacterAttributesList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<CharacterAttributeStruct> attributesList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("attributesData") && jsonData["body"]["attributesData"].is_array())
    {
        for (const auto &attribute : jsonData["body"]["attributesData"])
        {
            CharacterAttributeStruct attributeData;
            if (attribute.contains("id") && attribute["id"].is_number_integer())
            {
                attributeData.id = attribute["id"].get<int>();
            }
            if (attribute.contains("name") && attribute["name"].is_string())
            {
                attributeData.name = attribute["name"].get<std::string>();
            }
            if (attribute.contains("slug") && attribute["slug"].is_string())
            {
                attributeData.slug = attribute["slug"].get<std::string>();
            }
            if (attribute.contains("value") && attribute["value"].is_number_integer())
            {
                attributeData.value = attribute["value"].get<int>();
            }
            attributesList.push_back(attributeData);
        }
    }

    return attributesList;
}

// parse characters list
std::vector<CharacterDataStruct>
JSONParser::parseCharactersList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<CharacterDataStruct> charactersList;

    if (jsonData.contains("body") && jsonData["body"].is_array())
    {
        for (const auto &character : jsonData["body"])
        {
            CharacterDataStruct characterData;
            if (character.contains("id") && character["id"].is_number_integer())
            {
                characterData.characterId = character["id"].get<int>();
            }

            charactersList.push_back(characterData);
        }
    }

    return charactersList;
}

// parse message
MessageStruct
JSONParser::parseMessage(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    MessageStruct message;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("status") && jsonData["header"]["status"].is_string())
    {
        message.status = jsonData["header"]["status"].get<std::string>();
    }

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("message") && jsonData["header"]["message"].is_string())
    {
        message.message = jsonData["header"]["message"].get<std::string>();
    }

    return message;
}

std::string
JSONParser::parseEventType(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::string eventType;

    if (jsonData.contains("header") && jsonData["header"].is_object() &&
        jsonData["header"].contains("eventType") && jsonData["header"]["eventType"].is_string())
    {
        eventType = jsonData["header"]["eventType"].get<std::string>();
    }

    return eventType;
}

// parse chunk info
ChunkInfoStruct
JSONParser::parseChunkInfo(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    ChunkInfoStruct chunkInfo;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("id") && jsonData["body"]["id"].is_number_integer())
    {
        chunkInfo.id = jsonData["body"]["id"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("ip") && jsonData["body"]["ip"].is_string())
    {
        chunkInfo.ip = jsonData["body"]["ip"].get<std::string>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("port") && jsonData["body"]["port"].is_number_integer())
    {
        chunkInfo.port = jsonData["body"]["port"].get<int>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posX") && (jsonData["body"]["posX"].is_number_float() || jsonData["body"]["posX"].is_number_integer()))
    {
        chunkInfo.posX = jsonData["body"]["posX"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posY") && (jsonData["body"]["posY"].is_number_float() || jsonData["body"]["posY"].is_number_integer()))
    {
        chunkInfo.posY = jsonData["body"]["posY"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("posZ") && (jsonData["body"]["posZ"].is_number_float() || jsonData["body"]["posZ"].is_number_integer()))
    {
        chunkInfo.posZ = jsonData["body"]["posZ"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("sizeX") && (jsonData["body"]["sizeX"].is_number_float() || jsonData["body"]["sizeX"].is_number_integer()))
    {
        chunkInfo.sizeX = jsonData["body"]["sizeX"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("sizeY") && (jsonData["body"]["sizeY"].is_number_float() || jsonData["body"]["sizeY"].is_number_integer()))
    {
        chunkInfo.sizeY = jsonData["body"]["sizeY"].get<float>();
    }
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("sizeZ") && (jsonData["body"]["sizeZ"].is_number_float() || jsonData["body"]["sizeZ"].is_number_integer()))
    {
        chunkInfo.sizeZ = jsonData["body"]["sizeZ"].get<float>();
    }

    return chunkInfo;
}

// parse spawn zones list
std::vector<SpawnZoneStruct>
JSONParser::parseSpawnZonesList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<SpawnZoneStruct> spawnZonesList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("spawnZonesData") && jsonData["body"]["spawnZonesData"].is_array())
    {
        for (const auto &zone : jsonData["body"]["spawnZonesData"])
        {
            SpawnZoneStruct zoneData;
            if (zone.contains("id") && zone["id"].is_number_integer())
            {
                zoneData.zoneId = zone["id"].get<int>();
            }
            if (zone.contains("name") && zone["name"].is_string())
            {
                zoneData.zoneName = zone["name"].get<std::string>();
            }
            if (zone.contains("posX") && (zone["posX"].is_number_float() || zone["posX"].is_number_integer()))
            {
                zoneData.posX = zone["posX"].get<float>();
            }
            if (zone.contains("sizeX") && (zone["sizeX"].is_number_float() || zone["sizeX"].is_number_integer()))
            {
                zoneData.sizeX = zone["sizeX"].get<float>();
            }
            if (zone.contains("posY") && (zone["posY"].is_number_float() || zone["posY"].is_number_integer()))
            {
                zoneData.posY = zone["posY"].get<float>();
            }
            if (zone.contains("sizeY") && (zone["sizeY"].is_number_float() || zone["sizeY"].is_number_integer()))
            {
                zoneData.sizeY = zone["sizeY"].get<float>();
            }
            if (zone.contains("posZ") && (zone["posZ"].is_number_float() || zone["posZ"].is_number_integer()))
            {
                zoneData.posZ = zone["posZ"].get<float>();
            }
            if (zone.contains("sizeZ") && (zone["sizeZ"].is_number_float() || zone["sizeZ"].is_number_integer()))
            {
                zoneData.sizeZ = zone["sizeZ"].get<float>();
            }
            if (zone.contains("spawnMobId") && zone["spawnMobId"].is_number_integer())
            {
                zoneData.spawnMobId = zone["spawnMobId"].get<int>();
            }
            if (zone.contains("maxMobSpawnCount") && zone["maxMobSpawnCount"].is_number_integer())
            {
                zoneData.spawnCount = zone["maxMobSpawnCount"].get<int>();
            }
            if (zone.contains("respawnTime") && zone["respawnTime"].is_number_integer())
            {
                zoneData.respawnTime = std::chrono::seconds(zone["respawnTime"].get<int>());
            }
            spawnZonesList.push_back(zoneData);
        }
    }

    return spawnZonesList;
}

// parse mobs list
std::vector<MobDataStruct>
JSONParser::parseMobsList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<MobDataStruct> mobsList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("mobsList") && jsonData["body"]["mobsList"].is_array())
    {
        for (const auto &mob : jsonData["body"]["mobsList"])
        {
            MobDataStruct mobData;
            if (mob.contains("id") && mob["id"].is_number_integer())
            {
                mobData.id = mob["id"].get<int>();
            }
            if (mob.contains("UID") && mob["UID"].is_number_integer())
            {
                mobData.uid = mob["UID"].get<int>();
            }
            if (mob.contains("zoneId") && mob["zoneId"].is_number_integer())
            {
                mobData.zoneId = mob["zoneId"].get<int>();
            }
            if (mob.contains("name") && mob["name"].is_string())
            {
                mobData.name = mob["name"].get<std::string>();
            }
            if (mob.contains("slug") && mob["slug"].is_string())
            {
                mobData.slug = mob["slug"].get<std::string>();
            }
            if (mob.contains("race") && mob["race"].is_string())
            {
                mobData.raceName = mob["race"].get<std::string>();
            }
            if (mob.contains("level") && mob["level"].is_number_integer())
            {
                mobData.level = mob["level"].get<int>();
            }
            if (mob.contains("currentHealth") && mob["currentHealth"].is_number_integer())
            {
                mobData.currentHealth = mob["currentHealth"].get<int>();
            }
            if (mob.contains("currentMana") && mob["currentMana"].is_number_integer())
            {
                mobData.currentMana = mob["currentMana"].get<int>();
            }
            if (mob.contains("maxMana") && mob["maxMana"].is_number_integer())
            {
                mobData.maxMana = mob["maxMana"].get<int>();
            }
            if (mob.contains("maxHealth") && mob["maxHealth"].is_number_integer())
            {
                mobData.maxHealth = mob["maxHealth"].get<int>();
            }
            if (mob.contains("posX") && (mob["posX"].is_number_float() || mob["posX"].is_number_integer()))
            {
                mobData.position.positionX = mob["posX"].get<float>();
            }
            if (mob.contains("posY") && (mob["posY"].is_number_float() || mob["posY"].is_number_integer()))
            {
                mobData.position.positionY = mob["posY"].get<float>();
            }
            if (mob.contains("posZ") && (mob["posZ"].is_number_float() || mob["posZ"].is_number_integer()))
            {
                mobData.position.positionZ = mob["posZ"].get<float>();
            }
            if (mob.contains("rotZ") && (mob["rotZ"].is_number_float() || mob["rotZ"].is_number_integer()))
            {
                mobData.position.rotationZ = mob["rotZ"].get<float>();
            }
            if (mob.contains("isAggressive") && mob["isAggressive"].is_boolean())
            {
                mobData.isAggressive = mob["isAggressive"].get<bool>();
            }
            if (mob.contains("isDead") && mob["isDead"].is_boolean())
            {
                mobData.isDead = mob["isDead"].get<bool>();
            }
            mobsList.push_back(mobData);
        }
    }

    return mobsList;
}

// parse mob attributes list
std::vector<MobAttributeStruct>
JSONParser::parseMobsAttributesList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<MobAttributeStruct> mobAttributesList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("mobsAttributesList") && jsonData["body"]["mobsAttributesList"].is_array())
    {
        for (const auto &attribute : jsonData["body"]["mobsAttributesList"])
        {
            MobAttributeStruct attributeData;
            if (attribute.contains("id") && attribute["id"].is_number_integer())
            {
                attributeData.id = attribute["id"].get<int>();
            }
            if (attribute.contains("mob_id") && attribute["mob_id"].is_number_integer())
            {
                attributeData.mob_id = attribute["mob_id"].get<int>();
            }
            if (attribute.contains("name") && attribute["name"].is_string())
            {
                attributeData.name = attribute["name"].get<std::string>();
            }
            if (attribute.contains("slug") && attribute["slug"].is_string())
            {
                attributeData.slug = attribute["slug"].get<std::string>();
            }
            if (attribute.contains("value") && attribute["value"].is_number_integer())
            {
                attributeData.value = attribute["value"].get<int>();
            }
            mobAttributesList.push_back(attributeData);
        }
    }

    return mobAttributesList;
}

// parse combat action data from message body
nlohmann::json
JSONParser::parseCombatActionData(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);

    if (jsonData.contains("body") && jsonData["body"].is_object())
    {
        return jsonData["body"];
    }

    // Return empty JSON object if no body found
    return nlohmann::json::object();
}

// parse items list
std::vector<ItemDataStruct>
JSONParser::parseItemsList(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<ItemDataStruct> itemsList;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("itemsList") && jsonData["body"]["itemsList"].is_array())
    {
        for (const auto &item : jsonData["body"]["itemsList"])
        {
            ItemDataStruct itemData;
            if (item.contains("id") && item["id"].is_number_integer())
            {
                itemData.id = item["id"].get<int>();
            }
            if (item.contains("name") && item["name"].is_string())
            {
                itemData.name = item["name"].get<std::string>();
            }
            if (item.contains("slug") && item["slug"].is_string())
            {
                itemData.slug = item["slug"].get<std::string>();
            }
            if (item.contains("description") && item["description"].is_string())
            {
                itemData.description = item["description"].get<std::string>();
            }
            if (item.contains("isQuestItem") && item["isQuestItem"].is_boolean())
            {
                itemData.isQuestItem = item["isQuestItem"].get<bool>();
            }
            if (item.contains("itemType") && item["itemType"].is_number_integer())
            {
                itemData.itemType = item["itemType"].get<int>();
            }
            if (item.contains("itemTypeName") && item["itemTypeName"].is_string())
            {
                itemData.itemTypeName = item["itemTypeName"].get<std::string>();
            }
            if (item.contains("itemTypeSlug") && item["itemTypeSlug"].is_string())
            {
                itemData.itemTypeSlug = item["itemTypeSlug"].get<std::string>();
            }
            if (item.contains("isContainer") && item["isContainer"].is_boolean())
            {
                itemData.isContainer = item["isContainer"].get<bool>();
            }
            if (item.contains("isDurable") && item["isDurable"].is_boolean())
            {
                itemData.isDurable = item["isDurable"].get<bool>();
            }
            if (item.contains("isTradable") && item["isTradable"].is_boolean())
            {
                itemData.isTradable = item["isTradable"].get<bool>();
            }
            if (item.contains("isEquippable") && item["isEquippable"].is_boolean())
            {
                itemData.isEquippable = item["isEquippable"].get<bool>();
            }
            if (item.contains("isHarvest") && item["isHarvest"].is_boolean())
            {
                itemData.isHarvest = item["isHarvest"].get<bool>();
            }
            if (item.contains("weight") && item["weight"].is_number())
            {
                itemData.weight = item["weight"].get<float>();
            }
            if (item.contains("rarityId") && item["rarityId"].is_number_integer())
            {
                itemData.rarityId = item["rarityId"].get<int>();
            }
            if (item.contains("rarityName") && item["rarityName"].is_string())
            {
                itemData.rarityName = item["rarityName"].get<std::string>();
            }
            if (item.contains("raritySlug") && item["raritySlug"].is_string())
            {
                itemData.raritySlug = item["raritySlug"].get<std::string>();
            }
            if (item.contains("stackMax") && item["stackMax"].is_number_integer())
            {
                itemData.stackMax = item["stackMax"].get<int>();
            }
            if (item.contains("durabilityMax") && item["durabilityMax"].is_number_integer())
            {
                itemData.durabilityMax = item["durabilityMax"].get<int>();
            }
            if (item.contains("vendorPriceBuy") && item["vendorPriceBuy"].is_number_integer())
            {
                itemData.vendorPriceBuy = item["vendorPriceBuy"].get<int>();
            }
            if (item.contains("vendorPriceSell") && item["vendorPriceSell"].is_number_integer())
            {
                itemData.vendorPriceSell = item["vendorPriceSell"].get<int>();
            }
            if (item.contains("equipSlot") && item["equipSlot"].is_number_integer())
            {
                itemData.equipSlot = item["equipSlot"].get<int>();
            }
            if (item.contains("equipSlotName") && item["equipSlotName"].is_string())
            {
                itemData.equipSlotName = item["equipSlotName"].get<std::string>();
            }
            if (item.contains("equipSlotSlug") && item["equipSlotSlug"].is_string())
            {
                itemData.equipSlotSlug = item["equipSlotSlug"].get<std::string>();
            }
            if (item.contains("levelRequirement") && item["levelRequirement"].is_number_integer())
            {
                itemData.levelRequirement = item["levelRequirement"].get<int>();
            }

            // Parse attributes
            if (item.contains("attributes") && item["attributes"].is_array())
            {
                for (const auto &attribute : item["attributes"])
                {
                    ItemAttributeStruct itemAttribute;
                    if (attribute.contains("id") && attribute["id"].is_number_integer())
                    {
                        itemAttribute.id = attribute["id"].get<int>();
                    }
                    if (attribute.contains("item_id") && attribute["item_id"].is_number_integer())
                    {
                        itemAttribute.item_id = attribute["item_id"].get<int>();
                    }
                    if (attribute.contains("name") && attribute["name"].is_string())
                    {
                        itemAttribute.name = attribute["name"].get<std::string>();
                    }
                    if (attribute.contains("slug") && attribute["slug"].is_string())
                    {
                        itemAttribute.slug = attribute["slug"].get<std::string>();
                    }
                    if (attribute.contains("value") && attribute["value"].is_number_integer())
                    {
                        itemAttribute.value = attribute["value"].get<int>();
                    }
                    itemData.attributes.push_back(itemAttribute);
                }
            }

            itemsList.push_back(itemData);
        }
    }

    return itemsList;
}

// parse mob loot info
std::vector<MobLootInfoStruct>
JSONParser::parseMobLootInfo(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    std::vector<MobLootInfoStruct> mobLootInfo;

    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("mobLootInfo") && jsonData["body"]["mobLootInfo"].is_array())
    {
        for (const auto &loot : jsonData["body"]["mobLootInfo"])
        {
            MobLootInfoStruct lootData;
            if (loot.contains("id") && loot["id"].is_number_integer())
            {
                lootData.id = loot["id"].get<int>();
            }
            if (loot.contains("mobId") && loot["mobId"].is_number_integer())
            {
                lootData.mobId = loot["mobId"].get<int>();
            }
            if (loot.contains("itemId") && loot["itemId"].is_number_integer())
            {
                lootData.itemId = loot["itemId"].get<int>();
            }
            if (loot.contains("dropChance") && loot["dropChance"].is_number())
            {
                lootData.dropChance = loot["dropChance"].get<float>();
            }

            mobLootInfo.push_back(lootData);
        }
    }

    return mobLootInfo;
}