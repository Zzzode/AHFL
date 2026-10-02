#pragma once

// WH-5c.7 fix-forward (P1-1): the single source of truth for rebuilding a
// Decimal/Duration source spelling from the bare i64 word the P6 frame
// carries. The packer (frame_packer.cpp) and the reader (frame_reader.cpp)
// BOTH include this header so the spelling the reader rebuilds is provably
// the spelling the packer validated against the source value. The frame
// carries only mantissa/millis; the spelling family (source-unit vs
// bare-millis, source-literal vs builtin-canonical) is lost in the frame, so
// the rebuild targets the canonical source form the smoke / conformance
// fixtures use.

#include "runtime/value/scalar_spelling.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ahfl::runtime::wasm_host {

// Rebuild a source-literal Decimal spelling `DIGIT+ "." DIGIT+` from
// (mantissa, scale). Negative mantissas or scale <= 0 fall back to the builtin
// canonical form `s<scale>:<mantissa>` (the source grammar has no sign).
[[nodiscard]] inline std::string
format_decimal_spelling(std::int64_t mantissa, std::int32_t scale) {
    if (scale <= 0 || mantissa < 0) {
        return scalar_spelling::spell_builtin_decimal(mantissa, scale);
    }
    std::string digits = std::to_string(mantissa);
    const auto frac = static_cast<std::size_t>(scale);
    if (digits.size() <= frac) {
        digits.insert(digits.begin(), frac + 1 - digits.size(), '0');
    }
    digits.insert(digits.begin() + static_cast<std::ptrdiff_t>(digits.size() - frac),
                  '.');
    return digits;
}

// Rebuild a source-unit Duration spelling from i64 milliseconds, choosing the
// largest unit (h > m > s > ms) that divides the value exactly.
[[nodiscard]] inline std::string
format_duration_spelling(std::int64_t millis) {
    if (millis % 3600000 == 0) {
        return std::to_string(millis / 3600000) + "h";
    }
    if (millis % 60000 == 0) {
        return std::to_string(millis / 60000) + "m";
    }
    if (millis % 1000 == 0) {
        return std::to_string(millis / 1000) + "s";
    }
    return std::to_string(millis) + "ms";
}

} // namespace ahfl::runtime::wasm_host
