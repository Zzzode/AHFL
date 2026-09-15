// RFC 0026 KR6.5 E4-B2-D2b (FOUNDATION): pure IdempotencyToken authority.
// See core_wasm_idempotency_token.hpp for the locked preimage contract.

#include "runtime/engine/core_wasm_idempotency_token.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ahfl::runtime::core_wasm_idempotency_token {

namespace {

// ASCII domain separator, exactly 19 bytes, no trailing NUL.
constexpr std::string_view kDomain{"AHFL-IDEMPOTENCY-v1"};

// Fixed-width field sizes.
constexpr std::size_t kDomainSize = 19;
constexpr std::size_t kAuthoritySize = 16;
constexpr std::size_t kU32Size = 4;
constexpr std::size_t kU64Size = 8;
constexpr std::size_t kDigestSize = 32;

// Field offsets in the canonical preimage (documented in the seam doc order).
constexpr std::size_t kOffAuthority = kDomainSize;                   // 19
constexpr std::size_t kOffWorkflow = kOffAuthority + kAuthoritySize; // 35
constexpr std::size_t kOffCheckpoint = kOffWorkflow + kU32Size;      // 39
constexpr std::size_t kOffNode = kOffCheckpoint + kU64Size;          // 47
constexpr std::size_t kOffOrdinal = kOffNode + kU32Size;             // 51
constexpr std::size_t kOffCapability = kOffOrdinal + kU64Size;       // 59
constexpr std::size_t kOffSourceSymbol = kOffCapability + kU32Size;  // 63
constexpr std::size_t kOffParamDigest = kOffSourceSymbol + kU64Size; // 71
constexpr std::size_t kPreimageSize = kOffParamDigest + kDigestSize; // 103

void write_u32_le(std::span<std::uint8_t> dst, std::size_t offset, std::uint32_t value) noexcept {
    dst[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
    dst[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    dst[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    dst[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void write_u64_le(std::span<std::uint8_t> dst, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t i = 0; i < kU64Size; ++i) {
        dst[offset + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu);
    }
}

static_assert(kDomain.size() == kDomainSize, "idempotency domain must be exactly 19 ASCII bytes");
static_assert(kPreimageSize == 103, "idempotency preimage must be exactly 103 fixed bytes");

} // namespace

IdempotencyToken compute_idempotency_token(const IdempotencyCoordinate &coordinate) noexcept {
    // One fixed stack buffer: every write goes to a compile-time-constant
    // bounded offset, so the preimage can neither allocate nor truncate.
    std::array<std::uint8_t, kPreimageSize> preimage{};
    std::span<std::uint8_t> out{preimage};

    for (std::size_t i = 0; i < kDomainSize; ++i) {
        out[i] = static_cast<std::uint8_t>(kDomain[i]);
    }
    for (std::size_t i = 0; i < kAuthoritySize; ++i) {
        out[kOffAuthority + i] = coordinate.authority.bytes[i];
    }
    write_u32_le(out, kOffWorkflow, coordinate.workflow.value);
    write_u64_le(out, kOffCheckpoint, coordinate.checkpoint.value);
    write_u32_le(out, kOffNode, coordinate.node.value);
    write_u64_le(out, kOffOrdinal, coordinate.ordinal.value);
    write_u32_le(out, kOffCapability, coordinate.capability.value);
    write_u64_le(out, kOffSourceSymbol, coordinate.source_symbol);
    for (std::size_t i = 0; i < kDigestSize; ++i) {
        out[kOffParamDigest + i] = coordinate.param_digest[i];
    }

    // 103 bytes is far inside SHA-256's length domain; the span overload is
    // noexcept here in practice (it only throws for astronomically large
    // inputs).
    IdempotencyToken token;
    token.bytes = support::sha256(std::span<const std::uint8_t>{preimage});
    return token;
}

} // namespace ahfl::runtime::core_wasm_idempotency_token
