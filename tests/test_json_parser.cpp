// Unit tests for JSONParser wire shapes (default ctor, no world).
#include "utils/JSONParser.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

namespace
{

nlohmann::json hdr(const std::string &type)
{
    return nlohmann::json{{"header", {{"eventType", type}}}};
}

} // namespace

TEST(JsonParser, SpawnZonesGameZoneId)
{
    // Origin attribution needs gameZoneId on the wire; legacy pushes
    // without it must parse as 0 (position fallback).
    JSONParser p;
    nlohmann::json with;
    with["body"]["spawnZonesData"] = nlohmann::json::array(
        {{{"id", 9}, {"name", "arena"}, {"gameZoneId", 7}, {"shape", "RECT"}}});
    std::string rawWith = with.dump();
    auto listWith = p.parseSpawnZonesList(rawWith.c_str(), rawWith.size());
    ASSERT_EQ(listWith.size(), 1u);
    EXPECT_EQ(listWith[0].zoneId, 9);
    EXPECT_EQ(listWith[0].gameZoneId, 7);

    nlohmann::json without;
    without["body"]["spawnZonesData"] = nlohmann::json::array(
        {{{"id", 9}, {"name", "arena"}, {"shape", "RECT"}}});
    std::string rawWithout = without.dump();
    auto listWithout = p.parseSpawnZonesList(rawWithout.c_str(), rawWithout.size());
    ASSERT_EQ(listWithout.size(), 1u);
    EXPECT_EQ(listWithout[0].gameZoneId, 0);
}

TEST(JsonParser, EventType)
{
    JSONParser p;
    EXPECT_EQ(p.parseEventType(hdr("pingClient")), "pingClient");
    EXPECT_EQ(p.parseEventType(nlohmann::json::object()), "");
    std::string raw = hdr("moveCharacter").dump();
    EXPECT_EQ(p.parseEventType(raw.c_str(), raw.size()), "moveCharacter");
}

TEST(JsonParser, PositionFlatAndNested)
{
    JSONParser p;
    nlohmann::json flat;
    flat["body"]["posX"] = 10;
    flat["body"]["posY"] = 20;
    flat["body"]["posZ"] = 30;
    flat["body"]["rotZ"] = 90;
    PositionStruct a = p.parsePositionData(flat);
    EXPECT_FLOAT_EQ(a.positionX, 10.0f);
    EXPECT_FLOAT_EQ(a.positionY, 20.0f);
    EXPECT_FLOAT_EQ(a.positionZ, 30.0f);
    EXPECT_FLOAT_EQ(a.rotationZ, 90.0f);

    nlohmann::json nested;
    nested["body"]["playerPosition"] = {{"x", 1}, {"y", 2}, {"z", 3}};
    PositionStruct b = p.parsePositionData(nested);
    EXPECT_FLOAT_EQ(b.positionX, 1.0f);
    EXPECT_FLOAT_EQ(b.positionY, 2.0f);
    EXPECT_FLOAT_EQ(b.positionZ, 3.0f);

    PositionStruct c = p.parsePositionData(nlohmann::json::object());
    EXPECT_FLOAT_EQ(c.positionX, 0.0f);
}

TEST(JsonParser, ClientDataAndMessage)
{
    JSONParser p;
    nlohmann::json j;
    j["header"]["clientId"] = 7;
    j["header"]["hash"] = "abc";
    j["header"]["status"] = "success";
    j["header"]["message"] = "Pong!";
    ClientDataStruct c = p.parseClientData(j);
    EXPECT_EQ(c.clientId, 7);
    EXPECT_EQ(c.hash, "abc");
    MessageStruct m = p.parseMessage(j);
    EXPECT_EQ(m.status, "success");
    EXPECT_EQ(m.message.dump(), "\"Pong!\"");
}

TEST(JsonParser, AttributesAndCharactersLists)
{
    JSONParser p;
    nlohmann::json attrs;
    attrs["body"]["attributesData"] = {{{"id", 1}, {"name", "Strength"}, {"slug", "strength"}, {"value", 15}}};
    std::string raw = attrs.dump();
    auto list = p.parseCharacterAttributesList(raw.c_str(), raw.size());
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].slug, "strength");
    EXPECT_EQ(list[0].value, 15);
    EXPECT_TRUE(p.parseCharacterAttributesList("{}", 2).empty());

    nlohmann::json chars;
    chars["body"] = {{{"id", 5}}, {{"id", 6}}};
    std::string raw2 = chars.dump();
    auto cl = p.parseCharactersList(raw2.c_str(), raw2.size());
    ASSERT_EQ(cl.size(), 2u);
    EXPECT_EQ(cl[0].characterId, 5);
}

TEST(JsonParser, TimestampsAndRequestId)
{
    JSONParser p;
    nlohmann::json j;
    j["header"]["clientSendMs"] = 555;
    j["header"]["requestId"] = "sync_3";
    TimestampStruct ts = p.parseTimestamps(j);
    EXPECT_EQ(ts.clientSendMsEcho, 555);
    EXPECT_EQ(ts.requestId, "sync_3");
    EXPECT_EQ(p.parseRequestId(j), "sync_3");
    EXPECT_EQ(p.parseRequestId(nlohmann::json::object()), "");
}

TEST(JsonParser, ExpLevelTable)
{
    JSONParser p;
    nlohmann::json j;
    j["body"]["expLevelTable"] = {{{"level", 1}, {"experiencePoints", 0}},
        {{"level", 2}, {"experiencePoints", 100}},
        {{"level", 0}, {"experiencePoints", 5}}}; // invalid: skipped
    std::string raw = j.dump();
    auto table = p.parseExpLevelTable(raw.c_str(), raw.size());
    ASSERT_EQ(table.size(), 2u);
    EXPECT_EQ(table[0].level, 1);
    EXPECT_EQ(table[1].experiencePoints, 100);
    EXPECT_TRUE(p.parseExpLevelTable("not json", 8).empty());
}

TEST(JsonParser, NpcsList)
{
    JSONParser p;
    nlohmann::json j;
    j["body"]["npcsList"] = {{{"id", 3}, {"name", "Bob"}, {"slug", "bob"},
        {"level", 4}, {"posX", 7}, {"posY", 8}, {"posZ", 9}}};
    std::string raw = j.dump();
    auto npcs = p.parseNPCsList(raw.c_str(), raw.size());
    ASSERT_EQ(npcs.size(), 1u);
    EXPECT_EQ(npcs[0].id, 3);
    EXPECT_EQ(npcs[0].name, "Bob");
    EXPECT_EQ(npcs[0].level, 4);
    EXPECT_FLOAT_EQ(npcs[0].position.positionX, 7.0f);
    EXPECT_TRUE(p.parseNPCsList("{}", 2).empty());
}
