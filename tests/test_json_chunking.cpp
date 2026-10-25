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

// ─── Transport fragmentation (issues #121/#122) ─────────────────────────────

TEST(WireFrag, BudgetUnderWireCap)
{
    EXPECT_LT(wire::kFragPayloadBudget, protocol::kMaxMessageBytes);
    EXPECT_LE(wire::kFragPayloadBudget + 1024u, protocol::kMaxMessageBytes);
}

TEST(WireFrag, SmallMessageStaysWhole)
{
    const std::string msg = R"({"header":{"eventType":"getPlayerInventory"},"body":{"gold":5}})";
    auto lines = wire::packWireMessage(msg, 1);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], msg + "\n");
    EXPECT_LT(lines[0].size(), protocol::kMaxMessageBytes);
}

TEST(WireFrag, OversizeSplitsIntoFramesUnderCap)
{
    // 20KB logical message (fat inventory shape) must come out as small
    // '\n'-terminated lines, each parseable and under the client cap.
    const std::string msg(20 * 1024, 'y');
    auto lines = wire::packWireMessage(msg, 42);
    ASSERT_GT(lines.size(), 1u);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        EXPECT_LT(lines[i].size(), protocol::kMaxMessageBytes);
        EXPECT_EQ(lines[i].back(), '\n');
        auto frame = nlohmann::json::parse(lines[i]);
        EXPECT_EQ(frame["header"]["eventType"], wire::kFragEventType);
        EXPECT_EQ(frame["body"]["streamId"], 42u);
        EXPECT_EQ(frame["body"]["index"], i);
        EXPECT_EQ(frame["body"]["total"], lines.size());
    }
    // Byte-exact roundtrip through concatenation.
    std::string glued;
    for (const auto &ln : lines)
        glued += nlohmann::json::parse(ln)["body"]["payload"].get<std::string>();
    EXPECT_EQ(glued, msg);
}

TEST(WireFrag, Utf8SplitAcrossFramesRoundtrips)
{
    // Payloads are substrings of the ENCODED string; splitting mid-codepoint
    // is safe because parsing happens after concatenation.
    std::string msg;
    for (int i = 0; i < 3000; ++i)
        msg += "лис"; // 2 x 2-byte codepoints per unit
    auto lines = wire::packWireMessage(msg, 7);
    ASSERT_GT(lines.size(), 1u);
    std::string glued;
    for (const auto &ln : lines)
        glued += nlohmann::json::parse(ln)["body"]["payload"].get<std::string>();
    EXPECT_EQ(glued, msg);
}

TEST(WireFrag, OverSanityCapRefused)
{
    const std::string msg(wire::kMaxLogicalBytes + 1, 'z');
    EXPECT_TRUE(wire::packWireMessage(msg, 9).empty());
}

TEST(WireFrag, ReassemblerInOrderAndOutOfOrder)
{
    const std::string msg(20 * 1024, 'q');
    auto lines = wire::packWireMessage(msg, 100);
    ASSERT_GT(lines.size(), 1u);

    // In order.
    wire::FragReassembler r1;
    wire::FragReassembler::FeedResult last;
    for (const auto &ln : lines)
    {
        auto f = nlohmann::json::parse(ln);
        last = r1.feed(f["body"]["streamId"], f["body"]["index"], f["body"]["total"],
            f["body"]["payload"].get<std::string>());
    }
    EXPECT_TRUE(last.complete);
    EXPECT_EQ(last.message, msg);

    // Reversed + duplicate first frame.
    wire::FragReassembler r2;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    {
        auto f = nlohmann::json::parse(*it);
        last = r2.feed(f["body"]["streamId"], f["body"]["index"], f["body"]["total"],
            f["body"]["payload"].get<std::string>());
    }
    EXPECT_TRUE(last.complete);
    {
        auto f = nlohmann::json::parse(lines[0]);
        last = r2.feed(f["body"]["streamId"], f["body"]["index"], f["body"]["total"],
            f["body"]["payload"].get<std::string>());
    }
    // A retransmitted frame after completion opens a fresh incomplete stream
    // (no completion, cleaned by reapIdle) — duplicates never fabricate data.
    EXPECT_FALSE(last.complete);
    EXPECT_EQ(r2.pendingStreams(), 1u);
    r2.reapIdle(std::chrono::seconds(0));
    EXPECT_EQ(r2.pendingStreams(), 0u);
}

TEST(WireFrag, ReassemblerRejectsMalformed)
{
    wire::FragReassembler r;
    // total 0
    auto d = r.feed(1, 0, 0, "x");
    EXPECT_TRUE(d.dropped);
    // index out of range
    d = r.feed(2, 5, 3, "x");
    EXPECT_TRUE(d.dropped);
    // mismatched total mid-stream resets
    d = r.feed(3, 0, 2, "ab");
    EXPECT_FALSE(d.complete);
    EXPECT_EQ(r.pendingStreams(), 1u);
    d = r.feed(3, 1, 3, "c");
    EXPECT_TRUE(d.dropped);
    EXPECT_EQ(r.pendingStreams(), 0u);
    // valid two-frame stream still completes afterwards
    d = r.feed(4, 1, 2, "CD");
    EXPECT_FALSE(d.complete);
    d = r.feed(4, 0, 2, "AB");
    EXPECT_TRUE(d.complete);
    EXPECT_EQ(d.message, "ABCD");
}

TEST(WireFrag, ReassemblerStreamCapAndReap)
{
    wire::FragReassembler r;
    for (std::uint64_t s = 1; s <= wire::kMaxReassemblyStreams + 2; ++s)
    {
        auto d = r.feed(s, 0, 2, "a");
        if (s <= wire::kMaxReassemblyStreams)
            EXPECT_FALSE(d.dropped) << s;
        else
            EXPECT_TRUE(d.dropped) << s;
    }
    EXPECT_EQ(r.pendingStreams(), wire::kMaxReassemblyStreams);
    r.reapIdle(std::chrono::seconds(0));
    EXPECT_EQ(r.pendingStreams(), 0u);
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

TEST(WireFrag, ParseFragOptIn)
{
    // S13-S1: single source of truth shared by handleJoinGameClient and
    // handlePlayerReady. Honored from the FIRST message carrying it.
    EXPECT_TRUE(wire::parseFragOptIn({{"frag", true}}));
    EXPECT_TRUE(wire::parseFragOptIn({{"characterId", 615}, {"frag", true}}));
    // Absent = legacy (oversize sent whole).
    EXPECT_FALSE(wire::parseFragOptIn(nlohmann::json::object()));
    EXPECT_FALSE(wire::parseFragOptIn({{"characterId", 615}}));
    // Mistyped = legacy, never throws (old code swallowed parse errors).
    EXPECT_FALSE(wire::parseFragOptIn({{"frag", "true"}}));
    EXPECT_FALSE(wire::parseFragOptIn({{"frag", 1}}));
    EXPECT_FALSE(wire::parseFragOptIn({{"frag", nullptr}}));
    EXPECT_FALSE(wire::parseFragOptIn(nlohmann::json::array()));
}
