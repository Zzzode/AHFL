#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// RFC 0026 KR6.5 E4-B0-C2b: the single runtime-owned authority for Decimal and
// Duration Value *spelling* semantics. Both the decimal builtins and the shared
// wire codec parse/validate spellings through THIS module so there is exactly one
// grammar per family in the runtime trust paths (Codex C2b policy rev2, P0-2).
//
// Scope boundary: this is the runtime host-value layer's Value-spelling
// authority ONLY. The compiler's const-evaluator keeps its own
// `parse_duration_milliseconds` / decimal-scale helpers (compiler-owned);
// unifying compiler + runtime would cross a layer boundary and must go through
// a separate protected-base shared-change gate, not C2b.
//
// This module NEVER normalizes or rewrites a spelling. Parsing yields
// mantissa/scale/milliseconds ONLY for validity, scale, and overflow checks; the
// runtime Value keeps the caller's original spelling bytes verbatim (P0-1).

namespace ahfl::runtime::scalar_spelling {

// ---------------------------------------------------------------------------
// Decimal
// ---------------------------------------------------------------------------

// A decimal parsed into its exact (mantissa, scale) identity. `mantissa` is an
// i64 unscaled integer; `scale` is the number of fractional digits (i32).
struct DecimalParts {
    std::int64_t mantissa{0};
    std::int32_t scale{0};
};

// The two disjoint, both-legal Decimal spelling families (C2b policy rev2 P0-3):
//   * SourceLiteral: `DIGIT+ "." DIGIT+ ["d"]` — the user/source form. Leading
//     zeros allowed (grammar already allows them); no sign. The mantissa is the
//     digits with the point removed; scale == count of fractional digits.
//   * BuiltinCanonical: `s<scale>:<mantissa>` — the decimal-builtin form, signed
//     on both fields. Accepted iff it is already canonical, i.e.
//     spell(parse(x)) == x (rejects `+`, leading zeros, `-0`).
enum class DecimalFamily { SourceLiteral, BuiltinCanonical };

// Emit the canonical builtin spelling `s<scale>:<mantissa>`. This is the exact
// form the decimal builtins have always produced.
[[nodiscard]] std::string spell_builtin_decimal(std::int64_t mantissa, std::int32_t scale);

// Parse a builtin-canonical `s<scale>:<mantissa>` spelling. Returns nullopt on
// any non-canonical or malformed input (missing `s`/`:`, `+`, leading zero,
// `-0`, out-of-range scale, out-of-range mantissa). Guarantees
// spell_builtin_decimal(mantissa, scale) == the input on success.
[[nodiscard]] std::optional<DecimalParts> parse_builtin_decimal(std::string_view spelling);

// Parse a source-literal `DIGIT+ "." DIGIT+ ["d"]` spelling. Returns nullopt on
// a missing/multiple point, a non-digit body, an empty integer/fraction part, a
// sign, or a mantissa that does not fit in i64. Leading zeros are accepted.
[[nodiscard]] std::optional<DecimalParts> parse_source_decimal(std::string_view spelling);

// The single decode/validate entry point the wire codec calls. Dispatches by the
// disjoint prefix (`s` -> BuiltinCanonical, else SourceLiteral) and returns the
// parsed identity plus which family matched, or nullopt if neither accepts it.
struct DecimalDecoded {
    DecimalParts parts;
    DecimalFamily family;
};
[[nodiscard]] std::optional<DecimalDecoded> parse_decimal(std::string_view spelling);

// ---------------------------------------------------------------------------
// Duration
// ---------------------------------------------------------------------------

// The two disjoint, both-legal Duration spelling families (C2b policy rev2 P0-3):
//   * SourceUnit: `DIGIT+ (ms|s|m|h)` — the user/source form, normalized to i64
//     milliseconds with a checked (pre-multiply) overflow guard. Leading zeros
//     allowed; no sign.
//   * BareMillis: the exact output of `std::to_string(i64)` — the form the
//     `duration_between` builtin produces. Signed (negative allowed); rejects
//     `+`, leading zeros, and `-0`.
enum class DurationFamily { SourceUnit, BareMillis };

// Parse a source-unit `DIGIT+ (ms|s|m|h)` spelling to i64 milliseconds. Mirrors
// the live evaluator millisecond semantics (ms=1, s=1000, m=60000,
// h=3600000). Rejects a missing/unknown unit, a non-digit body, a sign, or a
// value whose unit conversion overflows i64. Leading zeros are accepted.
[[nodiscard]] std::optional<std::int64_t> parse_source_duration_millis(std::string_view spelling);

// Parse a bare-millis spelling: exactly `std::to_string(i64)`. Negative allowed;
// rejects `+`, leading zeros, `-0`, and anything out of i64 range.
[[nodiscard]] std::optional<std::int64_t> parse_bare_millis(std::string_view spelling);

// The single decode/validate entry point the wire codec calls. Dispatches by
// whether a unit suffix is present and returns the millisecond value plus which
// family matched, or nullopt if neither accepts it.
struct DurationDecoded {
    std::int64_t millis{0};
    DurationFamily family;
};
[[nodiscard]] std::optional<DurationDecoded> parse_duration(std::string_view spelling);

} // namespace ahfl::runtime::scalar_spelling
