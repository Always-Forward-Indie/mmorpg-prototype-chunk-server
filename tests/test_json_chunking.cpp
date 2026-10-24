// Unit tests for wire-protocol JSON chunking (utils/JsonLineChunking.hpp).
// Locks the client 8KB framing contract server-side: every emitted chunk
// must stay under the wire budget no matter how large or attribute-heavy
// the input mobs are. Pure logic, no sockets/world.
#include "utils/JsonLineChunking.hpp"
#include "utils/ProtocolLimits.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

namespace
{

// A full mob entry with attributes, mimicking mobToJson output (~0.5-3KB
// depending on attribute count).
nlohmann::json makeMob(int uid, int attrCount)
{
    nlohmann::json mob;
    mob["uid"] = uid;
    mob["zoneId"] = 9;
    mob["position"] = {{"x", 100.5}, {"y", 200.25}, {"z", 50.0}, {"rotationZ", 90.0}};
    mob["velocity"] = {{"dirX", 0.6}, {"dirY", 0.8}, {"speed", 300.0}};
    mob["combatState"] = 1;
    mob["stepTimestampMs"] = 123456789;
    nlohmann::json attrs = nlohmann::json::array();
    for (int i = 0; i < attrCount; ++i)
    {
        attrs.push_back({{"id", i}, {"name", "AttributeName" + std::to_string(i)}, {"slug", "attr_slug_value"}, {"value", 10}});
    }
    mob["attributes"] = std::move(attrs);
    return mob;
}

size_t dumpedBytes(const nlohmann::json &chunk)
{
    return chunk.dump().size();
}

} // namespace

TEST(JsonChunking, BudgetStaysUnderWireCap)
{
    // The chunk budget must leave headroom for the header object,
    // the trailing delimiter and multi-byte UTF-8 within the 8KB line cap.
    EXPECT_LT(wire::kChunkBytes, protocol::kMaxMessageBytes);
    EXPECT_LE(wire::kChunkBytes + 1024u, protocol::kMaxMessageBytes);
}

TEST(JsonChunking, EmptyArrayYieldsSingleEmptyChunk)
{
    // Callers unconditionally send one response per returned chunk;
    // an empty input must still produce one (empty) send, never zero.
    auto chunks = wire::chunkJsonArray(nlohmann::json::array(), 100);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_TRUE(chunks[0].is_array());
    EXPECT_TRUE(chunks[0].empty());
}

TEST(JsonChunking, SmallBatchStaysWhole)
{
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(makeMob(1, 2));
    arr.push_back(makeMob(2, 2));
    auto chunks = wire::chunkJsonArray(arr, 6 * 1024);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].size(), 2u);
    EXPECT_EQ(chunks[0][0]["uid"], 1);
    EXPECT_EQ(chunks[0][1]["uid"], 2);
}

TEST(JsonChunking, TwentyAttributeHeavyMobsStayUnderCap)
{
    // Regression: ~25 attributes/mob x ~110B = ~3KB per mob. A fixed
    // count-based chunking (e.g. 12 mobs) would still emit ~36KB lines.
    // Byte-budget chunking must keep every line under the 8KB wire cap.
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 20; ++i)
    {
        arr.push_back(makeMob(500 + i, 25));
    }
    auto chunks = wire::chunkJsonArray(arr, 6 * 1024);
    EXPECT_GT(chunks.size(), 1u);
    for (const auto &chunk : chunks)
    {
        EXPECT_LE(dumpedBytes(chunk), 6u * 1024u);
    }
}

TEST(JsonChunking, TwoHundredLightMobsStayUnderCap)
{
    // Move-update scale: 200 lean mobs must also fit the wire budget.
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 200; ++i)
    {
        arr.push_back(makeMob(1000 + i, 3));
    }
    auto chunks = wire::chunkJsonArray(arr, 6 * 1024);
    EXPECT_GT(chunks.size(), 1u);
    for (const auto &chunk : chunks)
    {
        EXPECT_LE(dumpedBytes(chunk), 6u * 1024u);
    }
}

TEST(JsonChunking, OrderPreservedAcrossChunks)
{
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 50; ++i)
    {
        arr.push_back(makeMob(7000 + i, 20));
    }
    auto chunks = wire::chunkJsonArray(arr, 6 * 1024);
    ASSERT_GT(chunks.size(), 1u);
    int expected = 7000;
    for (const auto &chunk : chunks)
    {
        for (const auto &mob : chunk)
        {
            EXPECT_EQ(mob["uid"], expected++);
        }
    }
    EXPECT_EQ(expected, 7050);
}

TEST(JsonChunking, OversizeSingleEntryStillGoesOutAlone)
{
    // A single pathological entry cannot be split; it goes out alone and
    // the oversize send guard (NetworkManager) flags it for fixing.
    nlohmann::json arr = nlohmann::json::array();
    nlohmann::json huge;
    huge["uid"] = 1;
    huge["blob"] = std::string(10 * 1024, 'x');
    arr.push_back(huge);
    arr.push_back(makeMob(2, 1));
    auto chunks = wire::chunkJsonArray(arr, 6 * 1024);
    ASSERT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0].size(), 1u);
    EXPECT_EQ(chunks[1].size(), 1u);
    EXPECT_EQ(chunks[1][0]["uid"], 2);
}

TEST(WireQuant, Round2TrimsWithoutDrift)
{
    EXPECT_DOUBLE_EQ(wire::round2(1234.56789), 1234.57);
    EXPECT_DOUBLE_EQ(wire::round2(-2345.678), -2345.68);
    EXPECT_DOUBLE_EQ(wire::round2(0.0), 0.0);
    EXPECT_DOUBLE_EQ(wire::round2(90.0), 90.0);
    // Serialized form stays short: no float dust past 2 decimals.
    EXPECT_LT(nlohmann::json(wire::round2(4700.115)).dump().size(), 12u);
}
