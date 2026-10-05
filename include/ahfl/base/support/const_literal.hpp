#pragma once

// Compile-time constant parsers for the two i64-word builtin scalar literals.
//
// This is the compiler-side constant SSOT (formerly private to
// semantics/const_sema.cpp), relocated below the frontend so that a low-level
// backend (the Core-Wasm frame lane) embeds the SAME constant identity the
// semantic const-evaluator uses, without pulling the frontend/AST stack into
// the backend layer.
//
//   * Decimal  -> (units, scale): the signed integer the digits spell with the
//                 decimal point removed, plus the fractional-digit count. The
//                 P4-D physical word is `units`; the host renders the builtin
//                 `s<scale>:<units>` spelling at encode time.
//   * Duration -> bare milliseconds (i64).
//
// Parsing never normalizes beyond the integer conversion: the caller keeps its
// source spelling when it needs one (the evaluator Value does); these helpers
// yield only the constant word the fixed page embeds.

#include <cstdint>
#include <optional>
#include <string_view>

namespace ahfl::support {

struct DecimalConstLiteral {
    std::int64_t units{0};
    std::int64_t scale{0};
};

/// Parse `DIGITS+ "." DIGITS+ ["d"|"D"]` with an optional leading sign. Returns
/// nullopt on a missing point, a non-digit body, or i64 overflow of the
/// de-pointed integer.
[[nodiscard]] std::optional<DecimalConstLiteral>
parse_decimal_literal(std::string_view text);

/// Parse `DIGITS+ ("ms"|"s"|"m"|"h")` into milliseconds with a checked
/// pre-multiply overflow guard. Returns nullopt on a bad suffix, a non-digit
/// run, a negative value, or multiplication overflow.
[[nodiscard]] std::optional<std::int64_t>
parse_duration_literal_milliseconds(std::string_view text);

/// Parse `DIGITS+ "." DIGITS+ EXPONENT?` (the AHFL FLOAT_LITERAL token) into a
/// double. Returns nullopt on a malformed spelling or a value that does not
/// fit the IEEE 754 binary64 range.
[[nodiscard]] std::optional<double>
parse_float_literal(std::string_view text);

} // namespace ahfl::support
