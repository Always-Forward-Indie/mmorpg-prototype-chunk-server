// Unit tests for wire-protocol framing v2 (utils/JsonLineChunking.hpp).
// The v1 line protocol ('\n'-terminated, 8KB cap, wireFrag reassembly,
// chunkJsonArray) was deleted in S14d: every peer speaks v2, every frame is
// [u32 LE length][UTF-8 JSON], no size-based drops. Pure logic, no sockets.
#include "utils/JsonLineChunking.hpp"
#include "utils/ProtocolLimits.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

TEST(FramingV2, CapIs4MB)
{
    EXPECT_EQ(protocol::kMaxFrameBytes, 4u * 1024u * 1024u);
    // Framing budget headroom: fits the largest known snapshots (~10KB)
    // with orders of magnitude to spare; only a DoS-sized payload trips it.
    EXPECT_GT(protocol::kMaxFrameBytes, 1024u * 1024u);
    EXPECT_GT(protocol::kMaxAccumulatedBytesV2, protocol::kMaxFrameBytes);
}

TEST(FramingV2, PackRoundTrip)
{
    const std::string body = "{\"header\":{\"eventType\":\"x\"},\"body\":{}}";
    const std::string packed = wire::packFramedMessage(body);
    ASSERT_EQ(packed.size(), 4 + body.size());
    const uint8_t *p = reinterpret_cast<const uint8_t *>(packed.data());
    const std::uint32_t n = static_cast<std::uint32_t>(p[0]) |
                            (static_cast<std::uint32_t>(p[1]) << 8) |
                            (static_cast<std::uint32_t>(p[2]) << 16) |
                            (static_cast<std::uint32_t>(p[3]) << 24);
    EXPECT_EQ(n, body.size());
    EXPECT_EQ(packed.substr(4), body);
    // Multibyte UTF-8: length counts BYTES, split codepoints stay intact.
    const std::string cyr = "{\"t\":\"\xD0\xBB\xD0\xB8\xD1\x81\"}";
    const std::string packedCyr = wire::packFramedMessage(cyr);
    EXPECT_EQ(packedCyr.substr(4), cyr);
}

TEST(FramingV2, OverCapRefused)
{
    EXPECT_TRUE(wire::packFramedMessage(std::string(protocol::kMaxFrameBytes + 1, 'z')).empty());
    EXPECT_FALSE(wire::packFramedMessage(std::string(protocol::kMaxFrameBytes, 'z')).empty());
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
