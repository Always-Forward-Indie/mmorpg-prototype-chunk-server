#pragma once

#include <cstddef>

/// Single source of truth for wire-protocol framing limits (S14d: v2-only).
///
/// Mirrors `mmorpg-prototype-integration-tests/proto/mmo_proto.py`:
/// one length-prefixed frame `[u32 LE length][UTF-8 JSON]`, max 4MB.
namespace protocol {

/// Read chunk size for `async_read_some` (transport buffer, not a message cap).
inline constexpr std::size_t kMaxMessageBytes = 8 * 1024;

/// Protocol v2 (S14, docs/api/14-framing-v2-protocol.md): length-prefixed
/// frames `[u32 LE length][UTF-8 JSON]`, no `\n` delimiter.
inline constexpr std::size_t kMaxFrameBytes = 4 * 1024 * 1024;

/// Max buffered per connection (DoS guard). v2 frames arrive in 8KB TCP
/// chunks; a legit frame can be up to 4MB, hence the headroom.
inline constexpr std::size_t kMaxAccumulatedBytesV2 = kMaxFrameBytes + 64 * 1024;

/// Read chunk size for `async_read_some`.
inline constexpr std::size_t kReadChunkBytes = 8 * 1024;

/// Wire error codes for framing failures (additive).
inline constexpr const char *kErrOversize = "ERR_OVERSIZE";
inline constexpr const char *kErrMalformed = "ERR_MALFORMED";

/// v2 framing version offered/selected in joinGameClient `body.framing`.
inline constexpr int kFramingV2 = 2;

/// Error-shape convention (additive, old clients ignore unknown body keys):
/// every `status:error` response carries `body.errorCode` mirroring
/// `header.message` (injected centrally in `generateResponseMessage`;
/// framing paths set it directly). Handlers that already set errorCode
/// explicitly (dialogue/harvest/WIO) keep theirs.

/// Protocol version echoed in `header.version`.
inline constexpr const char *kProtocolVersion = "1.0";

} // namespace protocol
