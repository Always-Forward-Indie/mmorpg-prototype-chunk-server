#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "utils/ProtocolLimits.hpp"

/// Wire framing helpers, protocol v2 (S14, docs/api/14-framing-v2-protocol.md).
///
/// Single source of truth for packing length-prefixed frames and probing
/// event types. The v1 line protocol ('\\n'-terminated, 8KB cap, wireFrag
/// reassembly, chunkJsonArray) was deleted in S14d: every peer speaks v2,
/// every frame is [u32 LE length][UTF-8 JSON], no size-based drops.
namespace wire
{

/// Round a world coordinate for the wire (S4): 2 decimals = 0.01uu, far below
/// visual/physics relevance, and trims ~2-4 chars per float at 10-20 Hz.
/// Server keeps full precision internally; only the emission is rounded.
inline double round2(double v)
{
    return std::round(v * 100.0) / 100.0;
}

/// Best-effort eventType so the client can correlate a framing error.
/// Parses at most the first 512 bytes (header lives at the start);
/// unparseable input yields "unknown" instead of throwing.
inline std::string probeEventType(const std::string &message)
{
    try
    {
        auto probe = nlohmann::json::parse(message.substr(0, 512), nullptr, false);
        if (!probe.is_discarded() && probe.is_object() && probe.contains("header") &&
            probe["header"].is_object() && probe["header"].contains("eventType") &&
            probe["header"]["eventType"].is_string())
            return probe["header"]["eventType"].get<std::string>();
    }
    catch (...)
    {
    }
    return "unknown";
}

/// Pack one logical message: [u32 LE length][UTF-8 JSON], no delimiter.
/// Length over kMaxFrameBytes is the caller's error (DoS guard) — empty.
inline std::string packFramedMessage(const std::string &message)
{
    if (message.size() > protocol::kMaxFrameBytes)
    {
        return {};
    }
    std::string out;
    out.reserve(4 + message.size());
    const std::uint32_t n = static_cast<std::uint32_t>(message.size());
    out.push_back(static_cast<char>(n & 0xFF));
    out.push_back(static_cast<char>((n >> 8) & 0xFF));
    out.push_back(static_cast<char>((n >> 16) & 0xFF));
    out.push_back(static_cast<char>((n >> 24) & 0xFF));
    out += message;
    return out;
}

} // namespace wire
