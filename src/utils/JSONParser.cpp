#include "utils/JSONParser.hpp"
#include "utils/TimestampUtils.hpp"
#include <algorithm>
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
        jsonData["body"].contains("classId") && jsonData["body"]["classId"].is_number_integer())
    {
        characterData.classId = jsonData["body"]["classId"].get<int>();
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

            // Используем атрибуты для установки максимального здоровья и маны если основные поля неправильные
            if (attributeData.slug == "max_health" && characterData.characterMaxHealth <= 0)
            {
                characterData.characterMaxHealth = attributeData.value;
            }
            else if (attributeData.slug == "max_mana" && characterData.characterMaxMana <= 0)
            {
                characterData.characterMaxMana = attributeData.value;
            }
        }
    }

    // get character skills
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("skillsData") && jsonData["body"]["skillsData"].is_array())
    {
        for (const auto &skill : jsonData["body"]["skillsData"])
        {
            SkillStruct skillData;
            if (skill.contains("skillName") && skill["skillName"].is_string())
            {
                skillData.skillName = skill["skillName"].get<std::string>();
            }
            if (skill.contains("skillSlug") && skill["skillSlug"].is_string())
            {
                skillData.skillSlug = skill["skillSlug"].get<std::string>();
            }
            if (skill.contains("scaleStat") && skill["scaleStat"].is_string())
            {
                skillData.scaleStat = skill["scaleStat"].get<std::string>();
            }
            if (skill.contains("school") && skill["school"].is_string())
            {
                skillData.school = skill["school"].get<std::string>();
            }
            if (skill.contains("skillEffectType") && skill["skillEffectType"].is_string())
            {
                skillData.skillEffectType = skill["skillEffectType"].get<std::string>();
            }
            if (skill.contains("skillLevel") && skill["skillLevel"].is_number_integer())
            {
                skillData.skillLevel = skill["skillLevel"].get<int>();
            }
            if (skill.contains("coeff") && (skill["coeff"].is_number_float() || skill["coeff"].is_number_integer()))
            {
                skillData.coeff = skill["coeff"].get<float>();
            }
            if (skill.contains("flatAdd") && (skill["flatAdd"].is_number_float() || skill["flatAdd"].is_number_integer()))
            {
                skillData.flatAdd = skill["flatAdd"].get<float>();
            }
            if (skill.contains("cooldownMs") && skill["cooldownMs"].is_number_integer())
            {
                skillData.cooldownMs = skill["cooldownMs"].get<int>();
            }
            if (skill.contains("gcdMs") && skill["gcdMs"].is_number_integer())
            {
                skillData.gcdMs = skill["gcdMs"].get<int>();
            }
            if (skill.contains("castMs") && skill["castMs"].is_number_integer())
            {
                skillData.castMs = skill["castMs"].get<int>();
            }
            if (skill.contains("costMp") && skill["costMp"].is_number_integer())
            {
                skillData.costMp = skill["costMp"].get<int>();
            }
            if (skill.contains("maxRange") && (skill["maxRange"].is_number_float() || skill["maxRange"].is_number_integer()))
            {
                skillData.maxRange = skill["maxRange"].get<float>();
            }
            if (skill.contains("areaRadius") && skill["areaRadius"].is_number())
            {
                skillData.areaRadius = skill["areaRadius"].get<float>();
            }
            if (skill.contains("swingMs") && skill["swingMs"].is_number_integer())
            {
                skillData.swingMs = skill["swingMs"].get<int>();
            }
            if (skill.contains("animationName") && skill["animationName"].is_string())
            {
                skillData.animationName = skill["animationName"].get<std::string>();
            }
            if (skill.contains("isPassive") && skill["isPassive"].is_boolean())
            {
                skillData.isPassive = skill["isPassive"].get<bool>();
            }
            // Parse skill effect definitions (buff/debuff/dot/hot applied on cast)
            if (skill.contains("effects") && skill["effects"].is_array())
            {
                for (const auto &eff : skill["effects"])
                {
                    SkillEffectDefinitionStruct ed;
                    if (eff.contains("effectSlug") && eff["effectSlug"].is_string())
                        ed.effectSlug = eff["effectSlug"].get<std::string>();
                    if (eff.contains("effectTypeSlug") && eff["effectTypeSlug"].is_string())
                        ed.effectTypeSlug = eff["effectTypeSlug"].get<std::string>();
                    if (eff.contains("attributeSlug") && eff["attributeSlug"].is_string())
                        ed.attributeSlug = eff["attributeSlug"].get<std::string>();
                    if (eff.contains("value") && eff["value"].is_number())
                        ed.value = eff["value"].get<float>();
                    if (eff.contains("durationSeconds") && eff["durationSeconds"].is_number_integer())
                        ed.durationSeconds = eff["durationSeconds"].get<int>();
                    if (eff.contains("tickMs") && eff["tickMs"].is_number_integer())
                        ed.tickMs = eff["tickMs"].get<int>();
                    skillData.effects.push_back(ed);
                }
            }
            characterData.skills.push_back(skillData);
        }
    }

    // experience debt (sent by game server on character join)
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("experienceDebt") && jsonData["body"]["experienceDebt"].is_number_integer())
    {
        characterData.experienceDebt = jsonData["body"]["experienceDebt"].get<int>();
    }

    // free skill points (sent by game server on character join)
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("freeSkillPoints") && jsonData["body"]["freeSkillPoints"].is_number_integer())
    {
        characterData.freeSkillPoints = jsonData["body"]["freeSkillPoints"].get<int>();
    }

    // skill bar slot assignments (sent by game server on character join)
    if (jsonData.contains("body") && jsonData["body"].is_object() &&
        jsonData["body"].contains("skillBarData") && jsonData["body"]["skillBarData"].is_array())
    {
        for (const auto &slot : jsonData["body"]["skillBarData"])
        {
            SkillBarSlotStruct s;
            if (slot.contains("slotIndex") && slot["slotIndex"].is_number_integer())
                s.slotIndex = slot["slotIndex"].get<int>();
            if (slot.contains("skillSlug") && slot["skillSlug"].is_string())
                s.skillSlug = slot["skillSlug"].get<std::string>();
            if (s.slotIndex >= 0 && !s.skillSlug.empty())
                characterData.skillBarSlots.push_back(s);
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

    // Fallback: Unreal-style nested playerPosition object { x, y, z }
    if (positionData.positionX == 0.0f && positionData.positionY == 0.0f && positionData.positionZ == 0.0f)
    {
        if (jsonData.contains("body") && jsonData["body"].is_object() &&
            jsonData["body"].contains("playerPosition") && jsonData["body"]["playerPosition"].is_object())
        {
            const auto &pos = jsonData["body"]["playerPosition"];
            if (pos.contains("x") && pos["x"].is_number())
                positionData.positionX = pos["x"].get<float>();
            if (pos.contains("y") && pos["y"].is_number())
                positionData.positionY = pos["y"].get<float>();
            if (pos.contains("z") && pos["z"].is_number())
                positionData.positionZ = pos["z"].get<float>();
        }
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
            if (mob.contains("baseExperience") && mob["baseExperience"].is_number_integer())
            {
                mobData.baseExperience = mob["baseExperience"].get<int>();
            }
            if (mob.contains("radius") && mob["radius"].is_number_integer())
            {
                mobData.radius = mob["radius"].get<int>();
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

            // Per-mob AI config (migration 011)
            if (mob.contains("aggroRange") && mob["aggroRange"].is_number())
                mobData.aggroRange = mob["aggroRange"].get<float>();
            if (mob.contains("attackRange") && mob["attackRange"].is_number())
                mobData.attackRange = mob["attackRange"].get<float>();
            if (mob.contains("attackCooldown") && mob["attackCooldown"].is_number())
                mobData.attackCooldown = mob["attackCooldown"].get<float>();
            if (mob.contains("chaseMultiplier") && mob["chaseMultiplier"].is_number())
                mobData.chaseMultiplier = mob["chaseMultiplier"].get<float>();
            if (mob.contains("patrolSpeed") && mob["patrolSpeed"].is_number())
                mobData.patrolSpeed = mob["patrolSpeed"].get<float>();

            // Social behaviour (migration 012)
            if (mob.contains("isSocial") && mob["isSocial"].is_boolean())
                mobData.isSocial = mob["isSocial"].get<bool>();
            if (mob.contains("chaseDuration") && mob["chaseDuration"].is_number())
                mobData.chaseDuration = mob["chaseDuration"].get<float>();

            // Rank / difficulty tier (migration 023)
            if (mob.contains("rankId") && mob["rankId"].is_number_integer())
                mobData.rankId = mob["rankId"].get<int>();
            if (mob.contains("rankCode") && mob["rankCode"].is_string())
                mobData.rankCode = mob["rankCode"].get<std::string>();
            if (mob.contains("rankMult") && mob["rankMult"].is_number())
                mobData.rankMult = mob["rankMult"].get<float>();

            // AI depth: flee + archetype (migration 016)
            if (mob.contains("fleeHpThreshold") && mob["fleeHpThreshold"].is_number())
                mobData.fleeHpThreshold = mob["fleeHpThreshold"].get<float>();
            if (mob.contains("aiArchetype") && mob["aiArchetype"].is_string())
            {
                mobData.aiArchetype = mob["aiArchetype"].get<std::string>();
                if (mobData.aiArchetype == "caster")
                    mobData.archetypeType = MobArchetype::CASTER;
                else if (mobData.aiArchetype == "ranged")
                    mobData.archetypeType = MobArchetype::RANGED;
                else if (mobData.aiArchetype == "support")
                    mobData.archetypeType = MobArchetype::SUPPORT;
                else
                    mobData.archetypeType = MobArchetype::MELEE;
            }

            // Survival / Rare mob groundwork (Stage 3, migration 038)
            if (mob.contains("canEvolve") && mob["canEvolve"].is_boolean())
                mobData.canEvolve = mob["canEvolve"].get<bool>();
            if (mob.contains("isRare") && mob["isRare"].is_boolean())
                mobData.isRare = mob["isRare"].get<bool>();
            if (mob.contains("rareSpawnChance") && mob["rareSpawnChance"].is_number())
                mobData.rareSpawnChance = mob["rareSpawnChance"].get<float>();
            if (mob.contains("rareSpawnCondition") && mob["rareSpawnCondition"].is_string())
                mobData.rareSpawnCondition = mob["rareSpawnCondition"].get<std::string>();

            // Social systems (Stage 4, migration 039)
            if (mob.contains("factionSlug") && mob["factionSlug"].is_string())
                mobData.factionSlug = mob["factionSlug"].get<std::string>();
            if (mob.contains("repDeltaPerKill") && mob["repDeltaPerKill"].is_number_integer())
                mobData.repDeltaPerKill = mob["repDeltaPerKill"].get<int>();

            // Bestiary metadata (migration 040)
            if (mob.contains("biomeSlug") && mob["biomeSlug"].is_string())
                mobData.biomeSlug = mob["biomeSlug"].get<std::string>();
            if (mob.contains("mobTypeSlug") && mob["mobTypeSlug"].is_string())
                mobData.mobTypeSlug = mob["mobTypeSlug"].get<std::string>();
            if (mob.contains("hpMin") && mob["hpMin"].is_number_integer())
                mobData.hpMin = mob["hpMin"].get<int>();
            if (mob.contains("hpMax") && mob["hpMax"].is_number_integer())
                mobData.hpMax = mob["hpMax"].get<int>();

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
            if (item.contains("slug") && item["slug"].is_string())
            {
                itemData.slug = item["slug"].get<std::string>();
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
            if (item.contains("isUsable") && item["isUsable"].is_boolean())
            {
                itemData.isUsable = item["isUsable"].get<bool>();
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
            if (item.contains("isTwoHanded") && item["isTwoHanded"].is_boolean())
            {
                itemData.isTwoHanded = item["isTwoHanded"].get<bool>();
            }
            if (item.contains("allowedClassIds") && item["allowedClassIds"].is_array())
            {
                for (const auto &classIdEntry : item["allowedClassIds"])
                {
                    if (classIdEntry.is_number_integer())
                        itemData.allowedClassIds.push_back(classIdEntry.get<int>());
                }
            }
            if (item.contains("setId") && item["setId"].is_number_integer())
            {
                itemData.setId = item["setId"].get<int>();
            }
            if (item.contains("setSlug") && item["setSlug"].is_string())
            {
                itemData.setSlug = item["setSlug"].get<std::string>();
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

            // Parse use effects
            if (item.contains("useEffects") && item["useEffects"].is_array())
            {
                for (const auto &ue : item["useEffects"])
                {
                    ItemUseEffectStruct useEffect;
                    if (ue.contains("effectSlug") && ue["effectSlug"].is_string())
                        useEffect.effectSlug = ue["effectSlug"].get<std::string>();
                    if (ue.contains("attributeSlug") && ue["attributeSlug"].is_string())
                        useEffect.attributeSlug = ue["attributeSlug"].get<std::string>();
                    if (ue.contains("value") && ue["value"].is_number())
                        useEffect.value = ue["value"].get<float>();
                    if (ue.contains("isInstant") && ue["isInstant"].is_boolean())
                        useEffect.isInstant = ue["isInstant"].get<bool>();
                    if (ue.contains("durationSeconds") && ue["durationSeconds"].is_number_integer())
                        useEffect.durationSeconds = ue["durationSeconds"].get<int>();
                    if (ue.contains("tickMs") && ue["tickMs"].is_number_integer())
                        useEffect.tickMs = ue["tickMs"].get<int>();
                    if (ue.contains("cooldownSeconds") && ue["cooldownSeconds"].is_number_integer())
                        useEffect.cooldownSeconds = ue["cooldownSeconds"].get<int>();
                    itemData.useEffects.push_back(useEffect);
                }
            }

            // Social systems (Stage 4, migration 039)
            if (item.contains("masterySlug") && item["masterySlug"].is_string())
                itemData.masterySlug = item["masterySlug"].get<std::string>();

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
            if (loot.contains("isHarvestOnly") && loot["isHarvestOnly"].is_boolean())
            {
                lootData.isHarvestOnly = loot["isHarvestOnly"].get<bool>();
            }
            if (loot.contains("minQuantity") && loot["minQuantity"].is_number_integer())
            {
                lootData.minQuantity = loot["minQuantity"].get<int>();
            }
            if (loot.contains("maxQuantity") && loot["maxQuantity"].is_number_integer())
            {
                lootData.maxQuantity = loot["maxQuantity"].get<int>();
            }
            if (loot.contains("lootTier") && loot["lootTier"].is_string())
                lootData.lootTier = loot["lootTier"].get<std::string>();

            mobLootInfo.push_back(lootData);
        }
    }

    return mobLootInfo;
}

std::pair<std::unordered_map<int, std::vector<std::string>>,
    std::unordered_map<int, std::vector<std::string>>>
JSONParser::parseMobWeaknessesResistances(const char *data, size_t length)
{
    std::unordered_map<int, std::vector<std::string>> weaknesses;
    std::unordered_map<int, std::vector<std::string>> resistances;

    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("data") || !j["body"]["data"].is_array())
            return {weaknesses, resistances};

        for (const auto &entry : j["body"]["data"])
        {
            int mobId = entry.value("mobId", 0);
            if (mobId <= 0)
                continue;

            if (entry.contains("weaknesses") && entry["weaknesses"].is_array())
            {
                for (const auto &el : entry["weaknesses"])
                    if (el.is_string())
                        weaknesses[mobId].push_back(el.get<std::string>());
            }
            if (entry.contains("resistances") && entry["resistances"].is_array())
            {
                for (const auto &el : entry["resistances"])
                    if (el.is_string())
                        resistances[mobId].push_back(el.get<std::string>());
            }
        }
    }
    catch (const std::exception &)
    {
    }
    return {std::move(weaknesses), std::move(resistances)};
}

std::vector<std::pair<int, std::vector<SkillStruct>>>
JSONParser::parseMobsSkillsMapping(const char *data, size_t length)
{
    std::vector<std::pair<int, std::vector<SkillStruct>>> mobsSkillsMapping;

    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);

        if (jsonData.contains("body") && jsonData["body"].is_object() &&
            jsonData["body"].contains("mobsSkills") && jsonData["body"]["mobsSkills"].is_array())
        {
            for (const auto &mobSkillsData : jsonData["body"]["mobsSkills"])
            {
                if (mobSkillsData.contains("mobId") && mobSkillsData["mobId"].is_number_integer() &&
                    mobSkillsData.contains("skills") && mobSkillsData["skills"].is_array())
                {
                    int mobId = mobSkillsData["mobId"].get<int>();
                    std::vector<SkillStruct> skills;

                    for (const auto &skillData : mobSkillsData["skills"])
                    {
                        SkillStruct skill;

                        if (skillData.contains("skillName") && skillData["skillName"].is_string())
                        {
                            skill.skillName = skillData["skillName"].get<std::string>();
                        }
                        if (skillData.contains("skillSlug") && skillData["skillSlug"].is_string())
                        {
                            skill.skillSlug = skillData["skillSlug"].get<std::string>();
                        }
                        if (skillData.contains("scaleStat") && skillData["scaleStat"].is_string())
                        {
                            skill.scaleStat = skillData["scaleStat"].get<std::string>();
                        }
                        if (skillData.contains("school") && skillData["school"].is_string())
                        {
                            skill.school = skillData["school"].get<std::string>();
                        }
                        if (skillData.contains("skillEffectType") && skillData["skillEffectType"].is_string())
                        {
                            skill.skillEffectType = skillData["skillEffectType"].get<std::string>();
                        }
                        if (skillData.contains("skillLevel") && skillData["skillLevel"].is_number_integer())
                        {
                            skill.skillLevel = skillData["skillLevel"].get<int>();
                        }
                        if (skillData.contains("coeff") && skillData["coeff"].is_number())
                        {
                            skill.coeff = skillData["coeff"].get<float>();
                        }
                        if (skillData.contains("flatAdd") && skillData["flatAdd"].is_number())
                        {
                            skill.flatAdd = skillData["flatAdd"].get<float>();
                        }
                        if (skillData.contains("cooldownMs") && skillData["cooldownMs"].is_number_integer())
                        {
                            skill.cooldownMs = skillData["cooldownMs"].get<int>();
                        }
                        if (skillData.contains("gcdMs") && skillData["gcdMs"].is_number_integer())
                        {
                            skill.gcdMs = skillData["gcdMs"].get<int>();
                        }
                        if (skillData.contains("castMs") && skillData["castMs"].is_number_integer())
                        {
                            skill.castMs = skillData["castMs"].get<int>();
                        }
                        if (skillData.contains("costMp") && skillData["costMp"].is_number_integer())
                        {
                            skill.costMp = skillData["costMp"].get<int>();
                        }
                        if (skillData.contains("maxRange") && skillData["maxRange"].is_number())
                        {
                            skill.maxRange = skillData["maxRange"].get<float>();
                        }
                        if (skillData.contains("areaRadius") && skillData["areaRadius"].is_number())
                        {
                            skill.areaRadius = skillData["areaRadius"].get<float>();
                        }
                        if (skillData.contains("swingMs") && skillData["swingMs"].is_number_integer())
                        {
                            skill.swingMs = skillData["swingMs"].get<int>();
                        }
                        if (skillData.contains("animationName") && skillData["animationName"].is_string())
                        {
                            skill.animationName = skillData["animationName"].get<std::string>();
                        }
                        // Parse skill effect definitions
                        if (skillData.contains("effects") && skillData["effects"].is_array())
                        {
                            for (const auto &eff : skillData["effects"])
                            {
                                SkillEffectDefinitionStruct ed;
                                if (eff.contains("effectSlug") && eff["effectSlug"].is_string())
                                    ed.effectSlug = eff["effectSlug"].get<std::string>();
                                if (eff.contains("effectTypeSlug") && eff["effectTypeSlug"].is_string())
                                    ed.effectTypeSlug = eff["effectTypeSlug"].get<std::string>();
                                if (eff.contains("attributeSlug") && eff["attributeSlug"].is_string())
                                    ed.attributeSlug = eff["attributeSlug"].get<std::string>();
                                if (eff.contains("value") && eff["value"].is_number())
                                    ed.value = eff["value"].get<float>();
                                if (eff.contains("durationSeconds") && eff["durationSeconds"].is_number_integer())
                                    ed.durationSeconds = eff["durationSeconds"].get<int>();
                                if (eff.contains("tickMs") && eff["tickMs"].is_number_integer())
                                    ed.tickMs = eff["tickMs"].get<int>();
                                skill.effects.push_back(ed);
                            }
                        }

                        skills.push_back(skill);
                    }

                    mobsSkillsMapping.push_back(std::make_pair(mobId, skills));
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        // Handle parsing error
    }

    return mobsSkillsMapping;
}

TimestampStruct
JSONParser::parseTimestamps(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    return parseTimestamps(jsonData);
}

TimestampStruct
JSONParser::parseTimestamps(const nlohmann::json &jsonData)
{
    TimestampStruct timestamps;

    try
    {
        // Try to parse from header first
        if (jsonData.contains("header") && jsonData["header"].is_object())
        {
            const auto &header = jsonData["header"];

            if (header.contains("serverRecvMs") && header["serverRecvMs"].is_number())
            {
                timestamps.serverRecvMs = header["serverRecvMs"].get<long long>();
            }

            if (header.contains("serverSendMs") && header["serverSendMs"].is_number())
            {
                timestamps.serverSendMs = header["serverSendMs"].get<long long>();
            }

            if (header.contains("clientSendMsEcho") && header["clientSendMsEcho"].is_number())
            {
                timestamps.clientSendMsEcho = header["clientSendMsEcho"].get<long long>();
            }

            // Also check for clientSendMs (from client request)
            if (header.contains("clientSendMs") && header["clientSendMs"].is_number())
            {
                timestamps.clientSendMsEcho = header["clientSendMs"].get<long long>();
            }

            // Parse requestId from header
            if (header.contains("requestId") && header["requestId"].is_string())
            {
                timestamps.requestId = header["requestId"].get<std::string>();
            }
        }

        // Try to parse from body as fallback
        if (jsonData.contains("body") && jsonData["body"].is_object())
        {
            const auto &body = jsonData["body"];

            if (body.contains("serverRecvMs") && body["serverRecvMs"].is_number())
            {
                timestamps.serverRecvMs = body["serverRecvMs"].get<long long>();
            }

            if (body.contains("serverSendMs") && body["serverSendMs"].is_number())
            {
                timestamps.serverSendMs = body["serverSendMs"].get<long long>();
            }

            if (body.contains("clientSendMsEcho") && body["clientSendMsEcho"].is_number())
            {
                timestamps.clientSendMsEcho = body["clientSendMsEcho"].get<long long>();
            }

            if (body.contains("clientSendMs") && body["clientSendMs"].is_number())
            {
                timestamps.clientSendMsEcho = body["clientSendMs"].get<long long>();
            }

            // Parse requestId from body
            if (body.contains("requestId") && body["requestId"].is_string())
            {
                timestamps.requestId = body["requestId"].get<std::string>();
            }
        }

        // Try to parse from root level
        if (jsonData.contains("serverRecvMs") && jsonData["serverRecvMs"].is_number())
        {
            timestamps.serverRecvMs = jsonData["serverRecvMs"].get<long long>();
        }

        if (jsonData.contains("serverSendMs") && jsonData["serverSendMs"].is_number())
        {
            timestamps.serverSendMs = jsonData["serverSendMs"].get<long long>();
        }

        if (jsonData.contains("clientSendMsEcho") && jsonData["clientSendMsEcho"].is_number())
        {
            timestamps.clientSendMsEcho = jsonData["clientSendMsEcho"].get<long long>();
        }

        if (jsonData.contains("clientSendMs") && jsonData["clientSendMs"].is_number())
        {
            timestamps.clientSendMsEcho = jsonData["clientSendMs"].get<long long>();
        }

        // Parse requestId from root level
        if (jsonData.contains("requestId") && jsonData["requestId"].is_string())
        {
            timestamps.requestId = jsonData["requestId"].get<std::string>();
        }
    }
    catch (const std::exception &e)
    {
        // Handle parsing error, return empty timestamps
    }

    return timestamps;
}

std::string
JSONParser::parseRequestId(const char *data, size_t length)
{
    nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
    return parseRequestId(jsonData);
}

std::string
JSONParser::parseRequestId(const nlohmann::json &jsonData)
{
    std::string requestId = "";

    try
    {
        // Try to parse from header first
        if (jsonData.contains("header") && jsonData["header"].is_object())
        {
            const auto &header = jsonData["header"];
            if (header.contains("requestId") && header["requestId"].is_string())
            {
                requestId = header["requestId"].get<std::string>();
                return requestId; // Return early if found in header
            }
        }

        // Try to parse from body as fallback
        if (jsonData.contains("body") && jsonData["body"].is_object())
        {
            const auto &body = jsonData["body"];
            if (body.contains("requestId") && body["requestId"].is_string())
            {
                requestId = body["requestId"].get<std::string>();
                return requestId; // Return early if found in body
            }
        }

        // Try to parse from root level
        if (jsonData.contains("requestId") && jsonData["requestId"].is_string())
        {
            requestId = jsonData["requestId"].get<std::string>();
        }
    }
    catch (const std::exception &e)
    {
        // Handle parsing error, return empty string
    }

    return requestId;
}

std::vector<ExperienceLevelEntry>
JSONParser::parseExpLevelTable(const char *data, size_t length)
{
    std::vector<ExperienceLevelEntry> expLevelTable;

    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);

        // Проверяем наличие массива таблицы опыта в body
        if (jsonData.contains("body") && jsonData["body"].is_object() &&
            jsonData["body"].contains("expLevelTable") && jsonData["body"]["expLevelTable"].is_array())
        {
            auto expTableArray = jsonData["body"]["expLevelTable"];

            for (const auto &entryJson : expTableArray)
            {
                ExperienceLevelEntry entry;

                if (entryJson.contains("level") && entryJson["level"].is_number_integer())
                {
                    entry.level = entryJson["level"].get<int>();
                }

                if (entryJson.contains("experiencePoints") && entryJson["experiencePoints"].is_number_integer())
                {
                    entry.experiencePoints = entryJson["experiencePoints"].get<int>();
                }

                // Добавляем запись только если у неё есть правильные данные
                if (entry.level > 0 && entry.experiencePoints >= 0)
                {
                    expLevelTable.push_back(entry);
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        // Handle parsing error, return empty vector
        expLevelTable.clear();
    }

    return expLevelTable;
}

std::vector<NPCDataStruct>
JSONParser::parseNPCsList(const char *data, size_t length)
{
    std::vector<NPCDataStruct> npcsList;

    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);

        if (jsonData.contains("body") && jsonData["body"].is_object() &&
            jsonData["body"].contains("npcsList") && jsonData["body"]["npcsList"].is_array())
        {
            auto npcsArray = jsonData["body"]["npcsList"];

            for (const auto &npcJson : npcsArray)
            {
                NPCDataStruct npc;

                if (npcJson.contains("id") && npcJson["id"].is_number_integer())
                    npc.id = npcJson["id"].get<int>();

                if (npcJson.contains("name") && npcJson["name"].is_string())
                    npc.name = npcJson["name"].get<std::string>();

                if (npcJson.contains("slug") && npcJson["slug"].is_string())
                    npc.slug = npcJson["slug"].get<std::string>();

                if (npcJson.contains("race") && npcJson["race"].is_string())
                    npc.raceName = npcJson["race"].get<std::string>();

                if (npcJson.contains("level") && npcJson["level"].is_number_integer())
                    npc.level = npcJson["level"].get<int>();

                if (npcJson.contains("currentHealth") && npcJson["currentHealth"].is_number_integer())
                    npc.currentHealth = npcJson["currentHealth"].get<int>();

                if (npcJson.contains("currentMana") && npcJson["currentMana"].is_number_integer())
                    npc.currentMana = npcJson["currentMana"].get<int>();

                if (npcJson.contains("maxHealth") && npcJson["maxHealth"].is_number_integer())
                    npc.maxHealth = npcJson["maxHealth"].get<int>();

                if (npcJson.contains("maxMana") && npcJson["maxMana"].is_number_integer())
                    npc.maxMana = npcJson["maxMana"].get<int>();

                if (npcJson.contains("npcType") && npcJson["npcType"].is_string())
                    npc.npcType = npcJson["npcType"].get<std::string>();

                if (npcJson.contains("isInteractable") && npcJson["isInteractable"].is_boolean())
                    npc.isInteractable = npcJson["isInteractable"].get<bool>();

                if (npcJson.contains("dialogueId") && npcJson["dialogueId"].is_string())
                    npc.dialogueId = npcJson["dialogueId"].get<std::string>();

                if (npcJson.contains("questSlugs") && npcJson["questSlugs"].is_array())
                {
                    for (const auto &slugVal : npcJson["questSlugs"])
                    {
                        if (slugVal.is_string())
                            npc.questSlugs.push_back(slugVal.get<std::string>());
                    }
                }

                // Social systems (Stage 4, migration 039)
                if (npcJson.contains("factionSlug") && npcJson["factionSlug"].is_string())
                    npc.factionSlug = npcJson["factionSlug"].get<std::string>();

                // Parse position
                if (npcJson.contains("posX") && npcJson["posX"].is_number())
                    npc.position.positionX = npcJson["posX"].get<float>();

                if (npcJson.contains("posY") && npcJson["posY"].is_number())
                    npc.position.positionY = npcJson["posY"].get<float>();

                if (npcJson.contains("posZ") && npcJson["posZ"].is_number())
                    npc.position.positionZ = npcJson["posZ"].get<float>();

                if (npcJson.contains("rotZ") && npcJson["rotZ"].is_number())
                    npc.position.rotationZ = npcJson["rotZ"].get<float>();

                npcsList.push_back(npc);
            }
        }
    }
    catch (const std::exception &e)
    {
        npcsList.clear();
    }

    return npcsList;
}

std::vector<NPCAttributeStruct>
JSONParser::parseNPCsAttributes(const char *data, size_t length)
{
    std::vector<NPCAttributeStruct> npcsAttributes;

    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);

        if (jsonData.contains("body") && jsonData["body"].is_object() &&
            jsonData["body"].contains("npcsAttributesList") && jsonData["body"]["npcsAttributesList"].is_array())
        {
            auto attributesArray = jsonData["body"]["npcsAttributesList"];

            for (const auto &attrJson : attributesArray)
            {
                NPCAttributeStruct attribute;

                if (attrJson.contains("id") && attrJson["id"].is_number_integer())
                    attribute.id = attrJson["id"].get<int>();

                if (attrJson.contains("npc_id") && attrJson["npc_id"].is_number_integer())
                    attribute.npc_id = attrJson["npc_id"].get<int>();

                if (attrJson.contains("name") && attrJson["name"].is_string())
                    attribute.name = attrJson["name"].get<std::string>();

                if (attrJson.contains("slug") && attrJson["slug"].is_string())
                    attribute.slug = attrJson["slug"].get<std::string>();

                if (attrJson.contains("value") && attrJson["value"].is_number_integer())
                    attribute.value = attrJson["value"].get<int>();

                npcsAttributes.push_back(attribute);
            }
        }
    }
    catch (const std::exception &e)
    {
        npcsAttributes.clear();
    }

    return npcsAttributes;
}

// =============================================================================
// Dialogue and quest system parsers
// =============================================================================

std::vector<DialogueGraphStruct>
JSONParser::parseDialoguesList(const char *data, size_t length)
{
    std::vector<DialogueGraphStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("dialogues"))
            return result;

        for (const auto &dj : j["body"]["dialogues"])
        {
            // Helper: parse a field that may be a JSON string or already a JSON object
            auto parseJsonField = [](const nlohmann::json &obj, const std::string &key) -> nlohmann::json
            {
                if (!obj.contains(key))
                    return nullptr;
                const auto &v = obj[key];
                if (v.is_null())
                    return nullptr;
                if (v.is_string())
                {
                    const std::string s = v.get<std::string>();
                    if (s.empty())
                        return nullptr;
                    try
                    {
                        return nlohmann::json::parse(s);
                    }
                    catch (...)
                    {
                        return nullptr;
                    }
                }
                return v;
            };

            DialogueGraphStruct graph;
            graph.id = dj.value("id", 0);
            graph.slug = dj.value("slug", "");
            graph.version = dj.value("version", 0);
            graph.startNodeId = dj.value("startNodeId", 0);

            if (dj.contains("nodes") && dj["nodes"].is_array())
            {
                for (const auto &nj : dj["nodes"])
                {
                    DialogueNodeStruct node;
                    node.id = nj.value("id", 0);
                    node.dialogueId = graph.id;
                    node.type = nj.value("type", "");
                    node.speakerNpcId = nj.value("speakerNpcId", 0);
                    node.clientNodeKey = nj.value("clientNodeKey", "");
                    node.jumpTargetNodeId = nj.value("jumpTargetNodeId", 0);
                    node.conditionGroup = parseJsonField(nj, "conditionGroup");
                    node.actionGroup = parseJsonField(nj, "actionGroup");
                    graph.nodes[node.id] = std::move(node);
                }
            }

            if (dj.contains("edges") && dj["edges"].is_array())
            {
                for (const auto &ej : dj["edges"])
                {
                    DialogueEdgeStruct edge;
                    edge.id = ej.value("id", 0);
                    edge.fromNodeId = ej.value("fromNodeId", 0);
                    edge.toNodeId = ej.value("toNodeId", 0);
                    edge.orderIndex = ej.value("orderIndex", 0);
                    edge.clientChoiceKey = ej.value("clientChoiceKey", "");
                    edge.hideIfLocked = ej.value("hideIfLocked", false);
                    edge.conditionGroup = parseJsonField(ej, "conditionGroup");
                    edge.actionGroup = parseJsonField(ej, "actionGroup");
                    graph.edges[edge.fromNodeId].push_back(std::move(edge));
                }
            }

            result.push_back(std::move(graph));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::vector<NPCDialogueMappingStruct>
JSONParser::parseNPCDialogueMappings(const char *data, size_t length)
{
    std::vector<NPCDialogueMappingStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("mappings"))
            return result;

        for (const auto &mj : j["body"]["mappings"])
        {
            NPCDialogueMappingStruct m;
            m.npcId = mj.value("npcId", 0);
            m.dialogueId = mj.value("dialogueId", 0);
            m.priority = mj.value("priority", 0);
            if (mj.contains("conditionGroup") && !mj["conditionGroup"].is_null())
                m.conditionGroup = mj["conditionGroup"];
            result.push_back(std::move(m));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::vector<QuestStruct>
JSONParser::parseQuestsList(const char *data, size_t length)
{
    std::vector<QuestStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("quests"))
            return result;

        for (const auto &qj : j["body"]["quests"])
        {
            QuestStruct q;
            q.id = qj.value("id", 0);
            q.slug = qj.value("slug", "");
            q.minLevel = qj.value("minLevel", 0);
            q.repeatable = qj.value("repeatable", false);
            q.cooldownSec = qj.value("cooldownSec", 0);
            q.giverNpcId = qj.value("giverNpcId", 0);
            q.turninNpcId = qj.value("turninNpcId", 0);
            q.clientQuestKey = qj.value("clientQuestKey", "");

            if (qj.contains("steps") && qj["steps"].is_array())
            {
                for (const auto &sj : qj["steps"])
                {
                    QuestStepStruct step;
                    step.id = sj.value("id", 0);
                    step.questId = q.id;
                    step.stepIndex = sj.value("stepIndex", 0);
                    step.stepType = sj.value("stepType", "");
                    step.completionMode = sj.value("completionMode", "auto");
                    step.clientStepKey = sj.value("clientStepKey", "");
                    if (sj.contains("params"))
                    {
                        const auto &pv = sj["params"];
                        if (pv.is_string())
                        {
                            std::string ps = pv.get<std::string>();
                            step.params = ps.empty() ? nlohmann::json::object() : nlohmann::json::parse(ps);
                        }
                        else
                        {
                            step.params = pv;
                        }
                    }
                    q.steps.push_back(std::move(step));
                }
                // Sort steps by step_index
                std::sort(q.steps.begin(), q.steps.end(), [](const QuestStepStruct &a, const QuestStepStruct &b)
                    { return a.stepIndex < b.stepIndex; });
            }

            if (qj.contains("rewards") && qj["rewards"].is_array())
            {
                for (const auto &rj : qj["rewards"])
                {
                    QuestRewardStruct reward;
                    reward.rewardType = rj.value("rewardType", "");
                    reward.itemId = rj.value("itemId", 0);
                    reward.quantity = rj.value("quantity", 1);
                    reward.amount = rj.value("amount", 0);
                    q.rewards.push_back(std::move(reward));
                }
            }

            result.push_back(std::move(q));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::vector<PlayerQuestProgressStruct>
JSONParser::parsePlayerQuestProgress(const char *data, size_t length)
{
    std::vector<PlayerQuestProgressStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("quests"))
            return result;

        int bodyCharacterId = j["body"].value("characterId", 0);
        for (const auto &pq : j["body"]["quests"])
        {
            PlayerQuestProgressStruct p;
            p.characterId = bodyCharacterId;
            p.questId = pq.value("questId", 0);
            p.questSlug = pq.value("questSlug", "");
            p.state = pq.value("state", "");
            p.currentStep = pq.value("currentStep", 0);
            if (pq.contains("progress"))
            {
                const auto &pv = pq["progress"];
                if (pv.is_string())
                {
                    std::string ps = pv.get<std::string>();
                    p.progress = ps.empty() ? nlohmann::json::object() : nlohmann::json::parse(ps);
                }
                else
                {
                    p.progress = pv;
                }
            }
            else
            {
                p.progress = nlohmann::json::object();
            }
            p.isDirty = false;
            p.updatedAt = std::chrono::steady_clock::now();
            result.push_back(std::move(p));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::vector<PlayerFlagStruct>
JSONParser::parsePlayerFlags(const char *data, size_t length)
{
    std::vector<PlayerFlagStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("flags"))
            return result;

        for (const auto &fj : j["body"]["flags"])
        {
            PlayerFlagStruct flag;
            // Game-server sends camelCase keys
            flag.flagKey = fj.value("flagKey", "");
            if (fj.contains("boolValue") && fj["boolValue"].is_boolean())
                flag.boolValue = fj["boolValue"].get<bool>();
            if (fj.contains("intValue") && fj["intValue"].is_number_integer())
                flag.intValue = fj["intValue"].get<int>();
            result.push_back(std::move(flag));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::vector<ActiveEffectStruct>
JSONParser::parsePlayerActiveEffects(const char *data, size_t length)
{
    std::vector<ActiveEffectStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("effects"))
            return result;

        for (const auto &ej : j["body"]["effects"])
        {
            ActiveEffectStruct eff;
            eff.id = ej.value("id", int64_t(0));
            eff.effectId = ej.value("effectId", 0);
            eff.effectSlug = ej.value("effectSlug", std::string(""));
            eff.effectTypeSlug = ej.value("effectTypeSlug", std::string("damage"));
            eff.attributeId = ej.value("attributeId", 0);
            eff.attributeSlug = ej.value("attributeSlug", std::string(""));
            eff.value = ej.value("value", 0.0f);
            eff.sourceType = ej.value("sourceType", std::string(""));
            eff.expiresAt = ej.value("expiresAt", int64_t(0));
            eff.tickMs = ej.value("tickMs", 0);
            // Schedule first tick immediately on load; non-tick effects: nextTickAt stays default
            if (eff.tickMs > 0)
                eff.nextTickAt = std::chrono::steady_clock::now();
            result.push_back(std::move(eff));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::vector<PlayerInventoryItemStruct>
JSONParser::parsePlayerInventory(const char *data, size_t length)
{
    std::vector<PlayerInventoryItemStruct> result;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        if (!j.contains("body") || !j["body"].contains("items"))
            return result;

        int characterId = j["body"].value("characterId", 0);
        for (const auto &ij : j["body"]["items"])
        {
            PlayerInventoryItemStruct item;
            item.id = static_cast<int>(ij.value("id", int64_t(0)));
            item.characterId = characterId;
            item.itemId = ij.value("itemId", 0);
            item.quantity = ij.value("quantity", 1);
            item.slotIndex = ij.value("slotIndex", -1);
            item.durabilityCurrent = ij.value("durabilityCurrent", 0);
            item.isEquipped = ij.value("isEquipped", false);
            item.killCount = ij.value("killCount", 0);
            if (item.itemId > 0)
                result.push_back(std::move(item));
        }
    }
    catch (const std::exception &)
    {
        result.clear();
    }
    return result;
}

std::pair<int, std::vector<CharacterAttributeStruct>>
JSONParser::parseCharacterAttributesRefresh(const char *data, size_t length)
{
    std::pair<int, std::vector<CharacterAttributeStruct>> result{0, {}};
    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
        if (!jsonData.contains("body") || !jsonData["body"].is_object())
            return result;

        result.first = jsonData["body"].value("characterId", 0);

        if (jsonData["body"].contains("attributesData") && jsonData["body"]["attributesData"].is_array())
        {
            for (const auto &attr : jsonData["body"]["attributesData"])
            {
                CharacterAttributeStruct entry;
                if (attr.contains("id") && attr["id"].is_number_integer())
                    entry.id = attr["id"].get<int>();
                if (attr.contains("name") && attr["name"].is_string())
                    entry.name = attr["name"].get<std::string>();
                if (attr.contains("slug") && attr["slug"].is_string())
                    entry.slug = attr["slug"].get<std::string>();
                if (attr.contains("value") && attr["value"].is_number_integer())
                    entry.value = attr["value"].get<int>();
                result.second.push_back(std::move(entry));
            }
        }
    }
    catch (const std::exception &)
    {
        result.first = 0;
        result.second.clear();
    }
    return result;
}

std::vector<RespawnZoneStruct>
JSONParser::parseRespawnZonesList(const char *data, size_t length)
{
    std::vector<RespawnZoneStruct> zones;
    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
        if (!jsonData.contains("body") || !jsonData["body"].contains("respawnZonesData") ||
            !jsonData["body"]["respawnZonesData"].is_array())
            return zones;

        for (const auto &z : jsonData["body"]["respawnZonesData"])
        {
            RespawnZoneStruct zone;
            if (z.contains("id") && z["id"].is_number_integer())
                zone.id = z["id"].get<int>();
            if (z.contains("name") && z["name"].is_string())
                zone.name = z["name"].get<std::string>();
            if (z.contains("x") && z["x"].is_number())
                zone.position.positionX = z["x"].get<float>();
            if (z.contains("y") && z["y"].is_number())
                zone.position.positionY = z["y"].get<float>();
            if (z.contains("z") && z["z"].is_number())
                zone.position.positionZ = z["z"].get<float>();
            if (z.contains("zoneId") && z["zoneId"].is_number_integer())
                zone.zoneId = z["zoneId"].get<int>();
            if (z.contains("isDefault") && z["isDefault"].is_boolean())
                zone.isDefault = z["isDefault"].get<bool>();
            zones.push_back(std::move(zone));
        }
    }
    catch (const std::exception &)
    {
        zones.clear();
    }
    return zones;
}

std::vector<GameZoneStruct>
JSONParser::parseGameZonesList(const char *data, size_t length)
{
    std::vector<GameZoneStruct> zones;
    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
        if (!jsonData.contains("body") || !jsonData["body"].contains("gameZonesData") ||
            !jsonData["body"]["gameZonesData"].is_array())
            return zones;

        for (const auto &z : jsonData["body"]["gameZonesData"])
        {
            GameZoneStruct zone;
            if (z.contains("id") && z["id"].is_number_integer())
                zone.id = z["id"].get<int>();
            if (z.contains("slug") && z["slug"].is_string())
                zone.slug = z["slug"].get<std::string>();
            if (z.contains("name") && z["name"].is_string())
                zone.name = z["name"].get<std::string>();
            if (z.contains("minLevel") && z["minLevel"].is_number_integer())
                zone.minLevel = z["minLevel"].get<int>();
            if (z.contains("maxLevel") && z["maxLevel"].is_number_integer())
                zone.maxLevel = z["maxLevel"].get<int>();
            if (z.contains("isPvp") && z["isPvp"].is_boolean())
                zone.isPvp = z["isPvp"].get<bool>();
            if (z.contains("isSafeZone") && z["isSafeZone"].is_boolean())
                zone.isSafeZone = z["isSafeZone"].get<bool>();
            if (z.contains("minX") && z["minX"].is_number())
                zone.minX = z["minX"].get<float>();
            if (z.contains("maxX") && z["maxX"].is_number())
                zone.maxX = z["maxX"].get<float>();
            if (z.contains("minY") && z["minY"].is_number())
                zone.minY = z["minY"].get<float>();
            if (z.contains("maxY") && z["maxY"].is_number())
                zone.maxY = z["maxY"].get<float>();
            if (z.contains("explorationXpReward") && z["explorationXpReward"].is_number_integer())
                zone.explorationXpReward = z["explorationXpReward"].get<int>();
            if (z.contains("championThresholdKills") && z["championThresholdKills"].is_number_integer())
                zone.championThresholdKills = z["championThresholdKills"].get<int>();
            zones.push_back(std::move(zone));
        }
    }
    catch (const std::exception &)
    {
        zones.clear();
    }
    return zones;
}

std::vector<StatusEffectTemplate>
JSONParser::parseStatusEffectTemplates(const char *data, size_t length)
{
    std::vector<StatusEffectTemplate> templates;
    try
    {
        nlohmann::json jsonData = nlohmann::json::parse(data, data + length);
        if (!jsonData.contains("body") || !jsonData["body"].contains("templates") ||
            !jsonData["body"]["templates"].is_array())
            return templates;

        for (const auto &t : jsonData["body"]["templates"])
        {
            StatusEffectTemplate tmpl;
            if (t.contains("effectSlug") && t["effectSlug"].is_string())
                tmpl.slug = t["effectSlug"].get<std::string>();
            if (t.contains("category") && t["category"].is_string())
                tmpl.category = t["category"].get<std::string>();
            if (t.contains("durationSec") && t["durationSec"].is_number_integer())
                tmpl.durationSec = t["durationSec"].get<int>();

            if (t.contains("modifiers") && t["modifiers"].is_array())
            {
                for (const auto &m : t["modifiers"])
                {
                    StatusEffectModifierDef mod;
                    if (m.contains("modifierType") && m["modifierType"].is_string())
                        mod.modifierType = m["modifierType"].get<std::string>();
                    if (m.contains("attributeSlug") && m["attributeSlug"].is_string())
                        mod.attributeSlug = m["attributeSlug"].get<std::string>();
                    if (m.contains("value") && m["value"].is_number())
                        mod.value = m["value"].get<double>();
                    tmpl.modifiers.push_back(std::move(mod));
                }
            }

            if (!tmpl.slug.empty())
                templates.push_back(std::move(tmpl));
        }
    }
    catch (const std::exception &)
    {
        templates.clear();
    }
    return templates;
}

std::vector<TimedChampionTemplate>
JSONParser::parseTimedChampionTemplates(const char *data, size_t length)
{
    std::vector<TimedChampionTemplate> templates;
    try
    {
        nlohmann::json j = nlohmann::json::parse(data, data + length);
        const auto &arr = j.at("body").at("timedChampionTemplates");
        if (!arr.is_array())
            return templates;

        for (const auto &t : arr)
        {
            TimedChampionTemplate tmpl;
            tmpl.id = t.value("id", 0);
            tmpl.slug = t.value("slug", std::string{});
            tmpl.gameZoneId = t.value("gameZoneId", 0);
            tmpl.mobTemplateId = t.value("mobTemplateId", 0);
            tmpl.intervalHours = t.value("intervalHours", 6);
            tmpl.windowMinutes = t.value("windowMinutes", 15);
            tmpl.nextSpawnAt = t.value("nextSpawnAt", int64_t{0});
            tmpl.announceKey = t.value("announceKey", std::string{});

            if (!tmpl.slug.empty() && tmpl.mobTemplateId > 0)
                templates.push_back(std::move(tmpl));
        }
    }
    catch (const std::exception &)
    {
        templates.clear();
    }
    return templates;
}
