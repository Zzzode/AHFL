#include "runtime/evaluator/scalar_spelling.hpp"

#include <charconv>
#include <limits>
#include <system_error>

namespace ahfl::evaluator::scalar_spelling {

namespace {

// True iff `digits` is a non-empty run of decimal digits (leading zeros allowed).
// The source-literal / source-unit families tolerate leading zeros.
[[nodiscard]] bool is_digits_allow_leading_zero(std::string_view digits) {
    if (digits.empty()) {
        return false;
    }
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// True iff `digits` is a canonical run of decimal digits with no leading zero
// (a lone "0" is canonical; "00"/"007" are not). Empty is not canonical.
[[nodiscard]] bool is_canonical_unsigned(std::string_view digits) {
    return is_digits_allow_leading_zero(digits) &&
           !(digits.size() > 1 && digits.front() == '0');
}

// Parse a canonical signed integer whose textual form is exactly what
// std::to_string(i64) would emit: an optional leading '-' (never '+'), then a
// canonical unsigned run, and never "-0". Returns nullopt otherwise or on i64
// overflow. This is the shared gate for the builtin-canonical mantissa/scale and
// for the bare-millis duration family.
[[nodiscard]] std::optional<std::int64_t> parse_canonical_i64(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::string_view digits = text;
    bool negative = false;
    if (text.front() == '-') {
        negative = true;
        digits = text.substr(1);
    }
    if (!is_canonical_unsigned(digits)) {
        return std::nullopt;
    }
    if (negative && digits == "0") { // reject "-0"
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

// Parse an unsigned integer allowing leading zeros (source-literal / source-unit
// families accept them), rejecting sign and non-digits, into i64. nullopt on
// overflow or malformed input.
[[nodiscard]] std::optional<std::int64_t> parse_unsigned_i64_allow_leading_zero(std::string_view text) {
    if (!is_digits_allow_leading_zero(text)) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// ---------------------------------------------------------------------------
// Decimal
// ---------------------------------------------------------------------------

std::string spell_builtin_decimal(std::int64_t mantissa, std::int32_t scale) {
    std::string out = "s";
    out += std::to_string(scale);
    out.push_back(':');
    out += std::to_string(mantissa);
    return out;
}

std::optional<DecimalParts> parse_builtin_decimal(std::string_view spelling) {
    if (!spelling.starts_with('s')) {
        return std::nullopt;
    }
    spelling.remove_prefix(1);
    const auto colon = spelling.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const auto scale_str = spelling.substr(0, colon);
    const auto mant_str = spelling.substr(colon + 1);

    const auto scale = parse_canonical_i64(scale_str);
    const auto mantissa = parse_canonical_i64(mant_str);
    if (!scale.has_value() || !mantissa.has_value()) {
        return std::nullopt;
    }
    if (*scale > std::numeric_limits<std::int32_t>::max() ||
        *scale < std::numeric_limits<std::int32_t>::min()) {
        return std::nullopt;
    }
    // Canonicality is guaranteed by parse_canonical_i64 on both fields, so
    // spell_builtin_decimal(mantissa, scale) == the original input by construction.
    return DecimalParts{*mantissa, static_cast<std::int32_t>(*scale)};
}

std::optional<DecimalParts> parse_source_decimal(std::string_view spelling) {
    if (spelling.ends_with('d')) {
        spelling.remove_suffix(1);
    }
    const auto dot = spelling.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt; // a source decimal always carries a '.'
    }
    if (spelling.find('.', dot + 1) != std::string_view::npos) {
        return std::nullopt; // more than one '.'
    }
    const auto int_part = spelling.substr(0, dot);
    const auto frac_part = spelling.substr(dot + 1);
    if (!is_digits_allow_leading_zero(int_part) ||
        !is_digits_allow_leading_zero(frac_part)) {
        return std::nullopt; // empty parts, a sign, or a non-digit
    }
    const auto scale = frac_part.size();
    if (scale > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    // The mantissa is the digit run with the point removed. Build it and parse
    // as i64; reject if it does not fit.
    std::string mantissa_text;
    mantissa_text.reserve(int_part.size() + frac_part.size());
    mantissa_text.append(int_part);
    mantissa_text.append(frac_part);
    const auto mantissa = parse_unsigned_i64_allow_leading_zero(mantissa_text);
    if (!mantissa.has_value()) {
        return std::nullopt;
    }
    return DecimalParts{*mantissa, static_cast<std::int32_t>(scale)};
}

std::optional<DecimalDecoded> parse_decimal(std::string_view spelling) {
    if (spelling.starts_with('s')) {
        if (const auto parts = parse_builtin_decimal(spelling); parts.has_value()) {
            return DecimalDecoded{*parts, DecimalFamily::BuiltinCanonical};
        }
        return std::nullopt;
    }
    if (const auto parts = parse_source_decimal(spelling); parts.has_value()) {
        return DecimalDecoded{*parts, DecimalFamily::SourceLiteral};
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Duration
// ---------------------------------------------------------------------------

std::optional<std::int64_t> parse_source_duration_millis(std::string_view spelling) {
    std::int64_t multiplier = 0;
    if (spelling.ends_with("ms")) {
        multiplier = 1;
        spelling.remove_suffix(2);
    } else if (spelling.ends_with("s")) {
        multiplier = 1000;
        spelling.remove_suffix(1);
    } else if (spelling.ends_with("m")) {
        multiplier = 60 * 1000;
        spelling.remove_suffix(1);
    } else if (spelling.ends_with("h")) {
        multiplier = 60 * 60 * 1000;
        spelling.remove_suffix(1);
    } else {
        return std::nullopt;
    }

    const auto value = parse_unsigned_i64_allow_leading_zero(spelling);
    if (!value.has_value()) {
        return std::nullopt;
    }
    // Pre-multiply overflow guard (multiplier >= 1, value >= 0).
    if (*value > std::numeric_limits<std::int64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return *value * multiplier;
}

std::optional<std::int64_t> parse_bare_millis(std::string_view spelling) {
    return parse_canonical_i64(spelling);
}

std::optional<DurationDecoded> parse_duration(std::string_view spelling) {
    if (spelling.ends_with("ms") || spelling.ends_with('s') || spelling.ends_with('m') ||
        spelling.ends_with('h')) {
        if (const auto millis = parse_source_duration_millis(spelling); millis.has_value()) {
            return DurationDecoded{*millis, DurationFamily::SourceUnit};
        }
        return std::nullopt;
    }
    if (const auto millis = parse_bare_millis(spelling); millis.has_value()) {
        return DurationDecoded{*millis, DurationFamily::BareMillis};
    }
    return std::nullopt;
}

} // namespace ahfl::evaluator::scalar_spelling
