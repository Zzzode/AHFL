#include "ahfl/base/support/const_literal.hpp"

#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>
namespace ahfl::support {

namespace {

[[nodiscard]] std::optional<std::int64_t> parse_int_const(std::string_view text) {
    std::int64_t value = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::int64_t> parse_duration_literal_milliseconds(std::string_view text) {
    std::int64_t multiplier = 0;
    if (text.ends_with("ms")) {
        multiplier = 1;
        text.remove_suffix(2);
    } else if (text.ends_with("s")) {
        multiplier = 1000;
        text.remove_suffix(1);
    } else if (text.ends_with("m")) {
        multiplier = 60 * 1000;
        text.remove_suffix(1);
    } else if (text.ends_with("h")) {
        multiplier = 60 * 60 * 1000;
        text.remove_suffix(1);
    } else {
        return std::nullopt;
    }

    const auto value = parse_int_const(text);
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    if (*value > std::numeric_limits<std::int64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return *value * multiplier;
}

std::optional<DecimalConstLiteral> parse_decimal_literal(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    if (text.back() == 'd' || text.back() == 'D') {
        text.remove_suffix(1);
    }

    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }

    const auto dot = text.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }

    std::string digits;
    digits.reserve(text.size() - 1);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == dot) {
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return std::nullopt;
        }
        digits.push_back(text[index]);
    }

    std::int64_t units = 0;
    const auto *begin = digits.data();
    const auto *end = begin + digits.size();
    const auto result = std::from_chars(begin, end, units);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    if (negative) {
        units = -units;
    }
    return DecimalConstLiteral{
        .units = units,
        .scale = static_cast<std::int64_t>(text.size() - dot - 1),
    };
}

std::optional<double> parse_float_literal(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

} // namespace ahfl::support
