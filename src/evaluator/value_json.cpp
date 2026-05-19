#include "ahfl/evaluator/value_json.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ahfl/support/json.hpp"

namespace ahfl::evaluator {

// ============================================================================
// Serialization
// ============================================================================

namespace {

void write_json_impl(const Value &v, std::ostream &out) {
    std::visit(
        [&out](const auto &inner) {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                out << "null";
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                out << (inner.value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, IntValue>) {
                out << inner.value;
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                if (std::isnan(inner.value)) {
                    out << "null";
                } else if (std::isinf(inner.value)) {
                    out << "null";
                } else {
                    // Use max precision for roundtrip fidelity
                    char buf[64];
                    auto [ptr, ec] = std::to_chars(
                        buf, buf + sizeof(buf), inner.value,
                        std::chars_format::general,
                        std::numeric_limits<double>::max_digits10);
                    std::string_view sv(buf, static_cast<std::size_t>(ptr - buf));
                    out << sv;
                    // Ensure output looks like a float (has '.' or 'e')
                    if (sv.find('.') == std::string_view::npos &&
                        sv.find('e') == std::string_view::npos &&
                        sv.find('E') == std::string_view::npos) {
                        out << ".0";
                    }
                }
            } else if constexpr (std::is_same_v<T, StringValue>) {
                ahfl::write_escaped_json_string(out, inner.value);
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                ahfl::write_escaped_json_string(out, inner.spelling);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                ahfl::write_escaped_json_string(out, inner.spelling);
            } else if constexpr (std::is_same_v<T, StructValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_type");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.type_name);
                for (const auto &[name, val] : inner.fields) {
                    out << ',';
                    ahfl::write_escaped_json_string(out, name);
                    out << ':';
                    if (val) {
                        write_json_impl(*val, out);
                    } else {
                        out << "null";
                    }
                }
                out << '}';
            } else if constexpr (std::is_same_v<T, ListValue>) {
                out << '[';
                for (std::size_t i = 0; i < inner.items.size(); ++i) {
                    if (i > 0)
                        out << ',';
                    if (inner.items[i]) {
                        write_json_impl(*inner.items[i], out);
                    } else {
                        out << "null";
                    }
                }
                out << ']';
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_enum");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.enum_name);
                out << ',';
                ahfl::write_escaped_json_string(out, "_variant");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.variant);
                out << '}';
            } else if constexpr (std::is_same_v<T, OptionalValue>) {
                if (inner.inner) {
                    write_json_impl(*inner.inner, out);
                } else {
                    out << "null";
                }
            }
        },
        v.node);
}

} // namespace

void write_value_json(const Value &v, std::ostream &out) {
    write_json_impl(v, out);
}

std::string value_to_json(const Value &v) {
    std::ostringstream oss;
    write_json_impl(v, oss);
    return oss.str();
}

// ============================================================================
// Deserialization (recursive-descent parser)
// ============================================================================

namespace {

class JsonParser {
  public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    std::optional<Value> parse() {
        skip_ws();
        auto result = parse_value();
        if (!result)
            return std::nullopt;
        skip_ws();
        if (pos_ != input_.size())
            return std::nullopt; // trailing garbage
        return result;
    }

  private:
    std::string_view input_;
    std::size_t pos_{0};

    // ========================================================================
    // Utility
    // ========================================================================

    [[nodiscard]] bool at_end() const { return pos_ >= input_.size(); }

    [[nodiscard]] char peek() const {
        if (at_end())
            return '\0';
        return input_[pos_];
    }

    char advance() { return input_[pos_++]; }

    bool consume(char c) {
        if (peek() == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consume_literal(std::string_view lit) {
        if (input_.substr(pos_).starts_with(lit)) {
            pos_ += lit.size();
            return true;
        }
        return false;
    }

    void skip_ws() {
        while (!at_end()) {
            char c = input_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    // ========================================================================
    // Value parsing
    // ========================================================================

    std::optional<Value> parse_value() {
        skip_ws();
        if (at_end())
            return std::nullopt;

        char c = peek();
        switch (c) {
        case 'n':
            return parse_null();
        case 't':
        case 'f':
            return parse_bool();
        case '"':
            return parse_string_value();
        case '[':
            return parse_array();
        case '{':
            return parse_object();
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return parse_number();
            }
            return std::nullopt;
        }
    }

    std::optional<Value> parse_null() {
        if (!consume_literal("null"))
            return std::nullopt;
        return Value{NoneValue{}};
    }

    std::optional<Value> parse_bool() {
        if (consume_literal("true"))
            return Value{BoolValue{true}};
        if (consume_literal("false"))
            return Value{BoolValue{false}};
        return std::nullopt;
    }

    std::optional<Value> parse_string_value() {
        auto s = parse_string();
        if (!s)
            return std::nullopt;
        return Value{StringValue{std::move(*s)}};
    }

    std::optional<Value> parse_number() {
        std::size_t start = pos_;
        bool is_float = false;

        // optional minus
        if (peek() == '-')
            ++pos_;

        // integer part
        if (at_end())
            return std::nullopt;
        if (peek() == '0') {
            ++pos_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (!at_end() && peek() >= '0' && peek() <= '9')
                ++pos_;
        } else {
            return std::nullopt;
        }

        // fractional part
        if (!at_end() && peek() == '.') {
            is_float = true;
            ++pos_;
            if (at_end() || peek() < '0' || peek() > '9')
                return std::nullopt;
            while (!at_end() && peek() >= '0' && peek() <= '9')
                ++pos_;
        }

        // exponent
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            ++pos_;
            if (!at_end() && (peek() == '+' || peek() == '-'))
                ++pos_;
            if (at_end() || peek() < '0' || peek() > '9')
                return std::nullopt;
            while (!at_end() && peek() >= '0' && peek() <= '9')
                ++pos_;
        }

        std::string_view num_str = input_.substr(start, pos_ - start);

        if (is_float) {
            double val = 0.0;
            auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
            if (ec != std::errc{})
                return std::nullopt;
            return Value{FloatValue{val}};
        }

        // Try integer first
        int64_t ival = 0;
        auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), ival);
        if (ec == std::errc{}) {
            return Value{IntValue{ival}};
        }
        // If integer overflow, fall back to float
        double dval = 0.0;
        auto [ptr2, ec2] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), dval);
        if (ec2 != std::errc{})
            return std::nullopt;
        return Value{FloatValue{dval}};
    }

    std::optional<Value> parse_array() {
        if (!consume('['))
            return std::nullopt;
        skip_ws();

        ListValue lv;
        if (peek() == ']') {
            ++pos_;
            return Value{std::move(lv)};
        }

        while (true) {
            auto val = parse_value();
            if (!val)
                return std::nullopt;
            lv.items.push_back(std::make_unique<Value>(std::move(*val)));
            skip_ws();
            if (consume(',')) {
                skip_ws();
                continue;
            }
            if (consume(']'))
                break;
            return std::nullopt; // unexpected char
        }

        return Value{std::move(lv)};
    }

    std::optional<Value> parse_object() {
        if (!consume('{'))
            return std::nullopt;
        skip_ws();

        // Parse all key-value pairs first
        std::vector<std::pair<std::string, Value>> pairs;

        if (peek() == '}') {
            ++pos_;
            // Empty object → StructValue with empty type_name
            return Value{StructValue{"", {}}};
        }

        while (true) {
            skip_ws();
            auto key = parse_string();
            if (!key)
                return std::nullopt;
            skip_ws();
            if (!consume(':'))
                return std::nullopt;
            auto val = parse_value();
            if (!val)
                return std::nullopt;
            pairs.emplace_back(std::move(*key), std::move(*val));
            skip_ws();
            if (consume(',')) {
                skip_ws();
                continue;
            }
            if (consume('}'))
                break;
            return std::nullopt;
        }

        // Determine object type based on special keys
        return interpret_object(std::move(pairs));
    }

    std::optional<Value> interpret_object(std::vector<std::pair<std::string, Value>> pairs) {
        // Check for EnumValue: has _enum and _variant
        std::string *enum_name = nullptr;
        std::string *variant_name = nullptr;
        std::string *type_name = nullptr;

        for (auto &[k, v] : pairs) {
            if (k == "_enum") {
                if (auto *sv = std::get_if<StringValue>(&v.node)) {
                    enum_name = &sv->value;
                }
            } else if (k == "_variant") {
                if (auto *sv = std::get_if<StringValue>(&v.node)) {
                    variant_name = &sv->value;
                }
            } else if (k == "_type") {
                if (auto *sv = std::get_if<StringValue>(&v.node)) {
                    type_name = &sv->value;
                }
            }
        }

        if (enum_name && variant_name) {
            return Value{EnumValue{std::move(*enum_name), std::move(*variant_name)}};
        }

        // StructValue
        StructValue sv;
        if (type_name) {
            sv.type_name = std::move(*type_name);
        }

        for (auto &[k, v] : pairs) {
            if (k == "_type")
                continue; // already consumed
            sv.fields.emplace(std::move(k), std::make_unique<Value>(std::move(v)));
        }

        return Value{std::move(sv)};
    }

    // ========================================================================
    // String parsing
    // ========================================================================

    std::optional<std::string> parse_string() {
        if (!consume('"'))
            return std::nullopt;

        std::string result;
        while (true) {
            if (at_end())
                return std::nullopt;
            char c = advance();
            if (c == '"') {
                return result;
            }
            if (c == '\\') {
                if (at_end())
                    return std::nullopt;
                char esc = advance();
                switch (esc) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'u': {
                    auto cp = parse_unicode_escape();
                    if (!cp)
                        return std::nullopt;
                    uint32_t codepoint = *cp;
                    // Handle surrogate pairs
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        // High surrogate, expect low surrogate
                        if (at_end() || advance() != '\\')
                            return std::nullopt;
                        if (at_end() || advance() != 'u')
                            return std::nullopt;
                        auto low = parse_unicode_escape();
                        if (!low)
                            return std::nullopt;
                        if (*low < 0xDC00 || *low > 0xDFFF)
                            return std::nullopt;
                        codepoint =
                            0x10000 + ((codepoint - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    encode_utf8(codepoint, result);
                    break;
                }
                default:
                    return std::nullopt; // invalid escape
                }
            } else {
                // Control characters are technically invalid in JSON strings
                if (static_cast<unsigned char>(c) < 0x20)
                    return std::nullopt;
                result += c;
            }
        }
    }

    std::optional<uint32_t> parse_unicode_escape() {
        if (pos_ + 4 > input_.size())
            return std::nullopt;
        uint32_t val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = input_[pos_++];
            val <<= 4;
            if (c >= '0' && c <= '9') {
                val |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                val |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        return val;
    }

    static void encode_utf8(uint32_t cp, std::string &out) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
};

} // namespace

std::optional<Value> value_from_json(std::string_view json) {
    JsonParser parser(json);
    return parser.parse();
}

} // namespace ahfl::evaluator
