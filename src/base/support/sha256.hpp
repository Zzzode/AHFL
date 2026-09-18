#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ahfl::support {

/// A raw 32-byte SHA-256 digest (FIPS 180-4). Byte order is the standard
/// big-endian digest order (digest[0] is the most-significant byte of the first
/// state word), i.e. the same bytes the lowercase-hex form spells left to right.
using Sha256Digest = std::array<std::uint8_t, 32>;

/// Compute the SHA-256 digest of an arbitrary byte span.
///
/// Total-length domain: SHA-256 is defined only for messages shorter than
/// 2^64 bits, so an input whose size exceeds `floor(UINT64_MAX / 8)` bytes is
/// outside the algorithm's domain and throws `std::length_error` (the message
/// never enters the compressor). The diagnostic text is fixed and echoes neither
/// the size nor any input byte.
[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> data);

/// Lowercase-hex SHA-256 of an arbitrary byte span. Same domain/`std::length_error`
/// contract as `sha256`.
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> data);

/// Lowercase-hex SHA-256 of a text/byte view. Preserved byte-for-byte: for every
/// in-domain input it produces identical output to prior releases; it now delegates
/// to the shared span core and treats the view as raw bytes (embedded NUL bytes are
/// hashed, never truncated). Same domain/`std::length_error` contract as `sha256`
/// (an input above `floor(UINT64_MAX / 8)` bytes now throws instead of the prior
/// allocation/length failure).
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

/// HMAC-SHA-256 (RFC 2104 / FIPS 198-1) of `data` under `key`. Block size 64,
/// output 32 bytes. A key longer than 64 bytes is first SHA-256'd (then zero-padded
/// to 64); a shorter key is zero-padded to 64. `key` and `data` may be empty,
/// contain embedded NUL, or hold arbitrary bytes; neither is modified. Throws
/// `std::length_error` (fixed text, no key/data/size echo) if `key` or the inner
/// `64 + data.size()` message would exceed SHA-256's `floor(UINT64_MAX / 8)`-byte
/// domain — checked before any key derivation. The implementation best-effort wipes
/// its internal key-derived buffers (see the .cpp `best_effort_wipe` note; it
/// cannot erase register/compiler-hidden copies and is not a production
/// key-erasure guarantee).
[[nodiscard]] Sha256Digest hmac_sha256(std::span<const std::uint8_t> key,
                                       std::span<const std::uint8_t> data);

/// Fixed-work, no-early-exit BEST-EFFORT comparison of two equal-length byte
/// ranges: every byte is XOR-accumulated and a single boolean is produced at
/// the end. This is NOT a proof of constant-time on portable C++ (the compiler
/// may still optimize or the hardware may vary), only defense-in-depth against
/// a naive early-exit timing oracle on digests, auth tags and key ids. A length
/// mismatch returns false without comparing content (the length domain is not
/// secret for the call sites that use this: 32-byte digests/tags and 16-byte
/// key ids).
[[nodiscard]] inline bool fixed_work_equal(std::span<const std::uint8_t> a,
                                           std::span<const std::uint8_t> b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    }
    return diff == 0;
}

} // namespace ahfl::support
