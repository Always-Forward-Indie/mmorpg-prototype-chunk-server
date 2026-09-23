#pragma once

#include <cstddef>

/// Single source of truth for wire-protocol framing limits.
///
/// Mirrors `mmorpg-prototype-integration-tests/proto/mmo_proto.py`:
/// one JSON object per line, `\n`-terminated, UTF-8, max 8KB/message.
namespace protocol {

/// Max bytes per single `\n`-framed JSON message (8KB, matches test harness).
inline constexpr std::size_t kMaxMessageBytes = 8 * 1024;

/// Max bytes buffered per connection without a `\n` delimiter (DoS guard).
inline constexpr std::size_t kMaxAccumulatedBytes = 64 * 1024;

/// Read chunk size for `async_read_some`.
inline constexpr std::size_t kReadChunkBytes = 8 * 1024;

/// Wire error codes for framing failures (protocol v1, additive only).
inline constexpr const char *kErrOversize = "ERR_OVERSIZE";
inline constexpr const char *kErrMalformed = "ERR_MALFORMED";

/// Error-shape convention (additive, old clients ignore unknown body keys):
/// every `status:error` response carries `body.errorCode` mirroring
/// `header.message` (injected centrally in `generateResponseMessage`;
/// framing paths set it directly). Handlers that already set errorCode
/// explicitly (dialogue/harvest/WIO) keep theirs.

/// Protocol version echoed in `header.version`.
inline constexpr const char *kProtocolVersion = "1.0";

} // namespace protocol
