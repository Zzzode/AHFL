#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ahfl {

// FNV-1a 64-bit hash (Fowler/Noll/Vo). Deterministic across platforms and
// processes, so a digest of a canonical name is stable across identical
// compilations. Used to disambiguate sanitized resource / component names
// whose original spelling contains bytes the target grammar forbids.
[[nodiscard]] inline std::uint64_t fnv1a_64(std::string_view data) noexcept {
    constexpr std::uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kPrime = 0x100000001b3ULL;
    std::uint64_t hash = kOffsetBasis;
    for (const unsigned char byte : data) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kPrime;
    }
    return hash;
}

// Lowercase hexadecimal suffix of the FNV-1a digest: the low `digits` hex
// digits (1..16), most-significant first. Sanitized names append this to stay
// unique after charset rewriting without embedding the original qualified
// name.
[[nodiscard]] inline std::string fnv1a_hex_suffix(std::string_view data, std::size_t digits = 10) {
    static constexpr char kHex[] = "0123456789abcdef";
    if (digits > 16) {
        digits = 16;
    }
    const std::uint64_t hash = fnv1a_64(data);
    std::string suffix(digits, '0');
    for (std::size_t i = 0; i < digits; ++i) {
        const auto shift = static_cast<unsigned>(4U * i);
        suffix[digits - 1 - i] = kHex[(hash >> shift) & 0x0FU];
    }
    return suffix;
}

} // namespace ahfl
