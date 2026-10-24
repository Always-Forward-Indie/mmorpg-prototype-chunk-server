#pragma once

#include <cstddef>
#include <vector>

#include <nlohmann/json.hpp>

/// Wire-contract chunking for '\n'-framed JSON responses.
///
/// Single source of truth for keeping every outbound line under the client
/// 8KB framing cap (FNetFraming::MaxMessageBytes, mirrored in
/// utils/ProtocolLimits.hpp as protocol::kMaxMessageBytes). The 6KB budget
/// leaves headroom for the header, the trailing delimiter and multi-byte
/// UTF-8. Pure and dependency-free (no sockets, no game state) so unit
/// tests can pin it: see tests/test_json_chunking.cpp.
namespace wire
{

/// Max serialized bytes per emitted chunk (header room included).
inline constexpr std::size_t kChunkBytes = 6 * 1024;

/// Round a world coordinate for the wire (S4): 2 decimals = 0.01uu, far below
/// visual/physics relevance, and trims ~2-4 chars per float at 10-20 Hz.
/// Server keeps full precision internally; only the emission is rounded.
inline double round2(double v)
{
    return std::round(v * 100.0) / 100.0;
}

/// Split a JSON array into chunks whose dumped size stays under maxBytes.
/// Always returns >= 1 chunk (empty input yields one empty chunk, preserving
/// the "empty list still sends" convention of the call sites). Order is
/// preserved; a single element larger than maxBytes still goes out alone
/// (callers should avoid building such entries; the oversize send guard
/// in NetworkManager::enqueueWrite will flag it).
inline std::vector<nlohmann::json> chunkJsonArray(const nlohmann::json &arr, std::size_t maxBytes = kChunkBytes)
{
    std::vector<nlohmann::json> chunks;
    if (!arr.is_array() || arr.empty())
    {
        chunks.push_back(nlohmann::json::array());
        return chunks;
    }
    nlohmann::json cur = nlohmann::json::array();
    size_t curBytes = 2; // "[]"
    for (const auto &entry : arr)
    {
        const size_t entryBytes = entry.dump().size() + 1; // +comma separator
        if (!cur.empty() && curBytes + entryBytes > maxBytes)
        {
            chunks.push_back(std::move(cur));
            cur = nlohmann::json::array();
            curBytes = 2;
        }
        cur.push_back(entry);
        curBytes += entryBytes;
    }
    if (!cur.empty() || chunks.empty())
    {
        chunks.push_back(std::move(cur));
    }
    return chunks;
}

} // namespace wire
