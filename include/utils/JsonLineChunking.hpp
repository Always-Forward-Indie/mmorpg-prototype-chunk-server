#pragma once

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
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

// ─── Transport fragmentation (Phase 1, issues #121/#122) ────────────────────
// Generic reassembly-capable framing for '\n'-delimited JSON.
//
// Problem: the client 8KB line cap (FNetFraming::MaxMessageBytes) drops any
// bigger line outright, and NetworkManager only warns before sending it
// anyway. Per-event array chunking (chunkJsonArray above) covers mobs but
// every new array event must remember to use it — inventory, ground loot,
// vendor shops, corpses and character lists all forgot (silent holes).
//
// This layer fixes the class, not the instance: the sender's transport
// (NetworkManager::enqueueWrite) routes every oversize logical message
// through packWireMessage, which splits it into small wireFrag frames;
// the receiver's transport reassembles by streamId and delivers the
// original JSON to game logic untouched. Game handlers and the UE game
// code never see fragments. Old peers (no frag capability) keep the
// legacy behavior — oversize as before, no regression.

/// Frame marker: carried as an ordinary line so framing stays unchanged.
inline constexpr const char *kFragEventType = "wireFrag";
/// Max payload bytes per frame (header room included, mirrors kChunkBytes).
inline constexpr std::size_t kFragPayloadBudget = 6 * 1024;
/// Sanity cap for one logical message (DoS guard). Larger messages are
/// NOT fragmented — the caller keeps the legacy warn-and-send path.
inline constexpr std::size_t kMaxLogicalBytes = 1024 * 1024;
/// Max frames per logical message (budget-implied: 1MB / 6KB < 256).
inline constexpr std::uint64_t kMaxFramesPerMessage = 1024;
/// Max concurrent in-flight reassembly streams per connection.
inline constexpr std::size_t kMaxReassemblyStreams = 16;

/// Split one logical message (WITHOUT trailing '\n') into '\n'-terminated
/// wire lines. Small messages come back as a single line (no frames, no
/// overhead); oversize ones as wireFrag frames. Returns an empty vector
/// when the message exceeds kMaxLogicalBytes (caller falls back).
inline std::vector<std::string> packWireMessage(const std::string &message, std::uint64_t streamId)
{
    if (message.size() <= kFragPayloadBudget)
    {
        return {message + "\n"};
    }
    if (message.size() > kMaxLogicalBytes)
    {
        return {};
    }
    const std::uint64_t total =
        static_cast<std::uint64_t>((message.size() + kFragPayloadBudget - 1) / kFragPayloadBudget);
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(total));
    for (std::uint64_t i = 0; i < total; ++i)
    {
        const std::size_t begin = static_cast<std::size_t>(i * kFragPayloadBudget);
        const std::size_t len = std::min(kFragPayloadBudget, message.size() - begin);
        nlohmann::json frame;
        frame["header"]["eventType"] = kFragEventType;
        frame["header"]["status"] = "pending";
        frame["header"]["clientId"] = 0;
        frame["header"]["hash"] = "";
        frame["body"]["streamId"] = streamId;
        frame["body"]["index"] = i;
        frame["body"]["total"] = total;
        frame["body"]["payload"] = message.substr(begin, len);
        out.push_back(frame.dump() + "\n");
    }
    return out;
}

/// Receiver-side reassembly (mirrored in Python by mmo_proto.py and by the
/// UE client's FNetFraming reassembly buffer — keep the three in sync).
class FragReassembler
{
  public:
    struct FeedResult
    {
        bool complete = false; ///< last needed frame arrived; message holds it
        bool dropped = false;  ///< frame rejected (limits/shape), stream reset
        std::string message;   ///< completed logical message (no trailing '\n')
    };

    /// Feed one wireFrag frame body. Out-of-order and duplicate frames are
    /// fine; mismatched totals or over-limit shapes reset the stream.
    FeedResult feed(std::uint64_t streamId, std::uint64_t index, std::uint64_t total, const std::string &payload)
    {
        FeedResult r;
        if (total == 0 || total > kMaxFramesPerMessage || index >= total || payload.size() > kFragPayloadBudget + 1024)
        {
            streams_.erase(streamId);
            r.dropped = true;
            return r;
        }
        auto it = streams_.find(streamId);
        if (it == streams_.end())
        {
            if (streams_.size() >= kMaxReassemblyStreams)
            {
                r.dropped = true;
                return r;
            }
            Stream s;
            s.total = total;
            s.parts.assign(static_cast<std::size_t>(total), "");
            s.filled.assign(static_cast<std::size_t>(total), 0);
            s.have = 0;
            s.lastUpdate = std::chrono::steady_clock::now();
            it = streams_.emplace(streamId, std::move(s)).first;
        }
        Stream &s = it->second;
        if (s.total != total)
        {
            streams_.erase(it);
            r.dropped = true;
            return r;
        }
        if (!s.filled[static_cast<std::size_t>(index)])
        {
            s.filled[static_cast<std::size_t>(index)] = true;
            ++s.have;
        }
        s.parts[static_cast<std::size_t>(index)] = payload;
        s.lastUpdate = std::chrono::steady_clock::now();
        if (s.have == s.total)
        {
            std::string msg;
            std::size_t bytes = 0;
            for (const auto &p : s.parts)
                bytes += p.size();
            msg.reserve(bytes);
            for (const auto &p : s.parts)
                msg += p;
            streams_.erase(it);
            r.complete = true;
            r.message = std::move(msg);
        }
        return r;
    }

    /// Drop streams idle longer than ttl (call periodically per connection).
    void reapIdle(std::chrono::seconds ttl = std::chrono::seconds(30))
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = streams_.begin(); it != streams_.end();)
        {
            if (now - it->second.lastUpdate > ttl)
                it = streams_.erase(it);
            else
                ++it;
        }
    }

    std::size_t pendingStreams() const { return streams_.size(); }

  private:
    struct Stream
    {
        std::uint64_t total = 0;
        std::vector<std::string> parts;
        std::vector<char> filled;
        std::uint64_t have = 0;
        std::chrono::steady_clock::time_point lastUpdate{};
    };
    std::unordered_map<std::uint64_t, Stream> streams_;
};

} // namespace wire
