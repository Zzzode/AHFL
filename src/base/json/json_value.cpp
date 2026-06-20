#include "base/json/json_value.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ahfl::json {

// ============================================================================
// JsonValue accessors
// ============================================================================

const JsonValue *JsonValue::get(std::string_view key) const {
    if (kind != Kind::Object) {
        return nullptr;
    }
    for (const auto &[k, v] : object_fields) {
        if (k == key) {
            return v.get();
        }
    }
    return nullptr;
}

JsonValue *JsonValue::get_mut(std::string_view key) {
    if (kind != Kind::Object) {
        return nullptr;
    }
    for (auto &[k, v] : object_fields) {
        if (k == key) {
            return v.get();
        }
    }
    return nullptr;
}

std::optional<std::string_view> JsonValue::as_string() const {
    if (kind != Kind::String) {
        return std::nullopt;
    }
    return std::string_view{string_val};
}

std::optional<int64_t> JsonValue::as_int() const {
    if (kind != Kind::Int) {
        return std::nullopt;
    }
    return int_val;
}

std::optional<double> JsonValue::as_float() const {
    if (kind == Kind::Float) {
        return float_val;
    }
    if (kind == Kind::Int) {
        return static_cast<double>(int_val);
    }
    return std::nullopt;
}

std::optional<bool> JsonValue::as_bool() const {
    if (kind != Kind::Bool) {
        return std::nullopt;
    }
    return bool_val;
}

// ============================================================================
// Factory helpers
// ============================================================================

std::unique_ptr<JsonValue> JsonValue::make_null() {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Null;
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_bool(bool b) {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Bool;
    v->bool_val = b;
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_int(int64_t i) {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Int;
    v->int_val = i;
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_float(double d) {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Float;
    v->float_val = d;
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_string(std::string s) {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::String;
    v->string_val = std::move(s);
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_array() {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Array;
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_object() {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Object;
    return v;
}

// ============================================================================
// Mutators
// ============================================================================

void JsonValue::push(std::unique_ptr<JsonValue> item) {
    array_items.push_back(std::move(item));
}

void JsonValue::set(std::string key, std::unique_ptr<JsonValue> value) {
    for (auto &[k, v] : object_fields) {
        if (k == key) {
            v = std::move(value);
            return;
        }
    }
    object_fields.emplace_back(std::move(key), std::move(value));
}

// ============================================================================
// Recursive-descent JSON parser
// ============================================================================

namespace {

[[nodiscard]] std::optional<double> parse_json_number_float(std::string_view text) {
    // Xcode 15 libc++ does not provide floating std::from_chars.
    std::string buffer{text};
    std::istringstream stream(buffer);
    stream.imbue(std::locale::classic());
    stream.unsetf(std::ios_base::skipws);

    double value{};
    if (!(stream >> value) || !std::isfinite(value)) {
        return std::nullopt;
    }

    char trailing{};
    if (stream >> trailing) {
        return std::nullopt;
    }

    return value;
}

class Parser {
  public:
    explicit Parser(std::string_view input) : input_(input), pos_(0) {}

    std::optional<std::unique_ptr<JsonValue>> parse() {
        skip_whitespace();
        auto result = parse_value();
        if (!result) {
            return std::nullopt;
        }
        skip_whitespace();
        if (pos_ != input_.size()) {
            return std::nullopt; // trailing content
        }
        return result;
    }

  private:
    std::string_view input_;
    std::size_t pos_;

    [[nodiscard]] bool at_end() const {
        return pos_ >= input_.size();
    }

    [[nodiscard]] char peek() const {
        if (at_end()) {
            return '\0';
        }
        return input_[pos_];
    }

    char advance() {
        char c = input_[pos_];
        ++pos_;
        return c;
    }

    bool consume(char expected) {
        if (peek() == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void skip_whitespace() {
        while (!at_end()) {
            char c = input_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    std::optional<std::unique_ptr<JsonValue>> parse_value() {
        skip_whitespace();
        if (at_end()) {
            return std::nullopt;
        }
        char c = peek();
        switch (c) {
        case '"':
            return parse_string_value();
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case 't':
        case 'f':
            return parse_bool();
        case 'n':
            return parse_null();
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return parse_number();
            }
            return std::nullopt;
        }
    }

    std::optional<std::unique_ptr<JsonValue>> parse_null() {
        const auto start = pos_;
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            auto value = JsonValue::make_null();
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }
        return std::nullopt;
    }

    std::optional<std::unique_ptr<JsonValue>> parse_bool() {
        const auto start = pos_;
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            auto value = JsonValue::make_bool(true);
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            auto value = JsonValue::make_bool(false);
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }
        return std::nullopt;
    }

    std::optional<std::unique_ptr<JsonValue>> parse_number() {
        std::size_t start = pos_;
        bool is_float = false;

        if (peek() == '-') {
            ++pos_;
        }

        if (at_end() || (peek() < '0' || peek() > '9')) {
            pos_ = start;
            return std::nullopt;
        }

        if (peek() == '0') {
            ++pos_;
        } else {
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }

        if (!at_end() && peek() == '.') {
            is_float = true;
            ++pos_;
            if (at_end() || peek() < '0' || peek() > '9') {
                pos_ = start;
                return std::nullopt;
            }
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }

        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            ++pos_;
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                ++pos_;
            }
            if (at_end() || peek() < '0' || peek() > '9') {
                pos_ = start;
                return std::nullopt;
            }
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                ++pos_;
            }
        }

        std::string_view num_str = input_.substr(start, pos_ - start);

        if (is_float) {
            auto val = parse_json_number_float(num_str);
            if (!val) {
                pos_ = start;
                return std::nullopt;
            }
            auto value = JsonValue::make_float(*val);
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }

        int64_t val{};
        const auto end = num_str.data() + num_str.size();
        auto [ptr, ec] = std::from_chars(num_str.data(), end, val);
        if (ec != std::errc{} || ptr != end) {
            // Overflow — try as float
            auto fval = parse_json_number_float(num_str);
            if (!fval) {
                pos_ = start;
                return std::nullopt;
            }
            auto value = JsonValue::make_float(*fval);
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }
        auto value = JsonValue::make_int(val);
        value->begin_offset = start;
        value->end_offset = pos_;
        return value;
    }

    std::optional<std::string> parse_string() {
        if (!consume('"')) {
            return std::nullopt;
        }
        std::string result;
        while (!at_end()) {
            char c = advance();
            if (c == '"') {
                return result;
            }
            if (c == '\\') {
                if (at_end()) {
                    return std::nullopt;
                }
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
                    auto code = parse_unicode_escape();
                    if (!code) {
                        return std::nullopt;
                    }
                    encode_utf8(*code, result);
                    break;
                }
                default:
                    return std::nullopt;
                }
            } else {
                if (static_cast<unsigned char>(c) < 0x20U) {
                    return std::nullopt;
                }
                result += c;
            }
        }
        return std::nullopt; // unterminated
    }

    std::optional<uint32_t> parse_unicode_escape() {
        if (pos_ + 4 > input_.size()) {
            return std::nullopt;
        }
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i) {
            char c = advance();
            code <<= 4U;
            if (c >= '0' && c <= '9') {
                code |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        // Handle surrogate pairs
        if (code >= 0xD800U && code <= 0xDBFFU) {
            if (pos_ + 6 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') {
                return std::nullopt;
            }
            pos_ += 2;
            auto low = parse_unicode_escape();
            if (!low || *low < 0xDC00U || *low > 0xDFFFU) {
                return std::nullopt;
            }
            code = 0x10000U + ((code - 0xD800U) << 10U) + (*low - 0xDC00U);
        }
        return code;
    }

    static void encode_utf8(uint32_t code, std::string &out) {
        if (code <= 0x7FU) {
            out += static_cast<char>(code);
        } else if (code <= 0x7FFU) {
            out += static_cast<char>(0xC0U | (code >> 6U));
            out += static_cast<char>(0x80U | (code & 0x3FU));
        } else if (code <= 0xFFFFU) {
            out += static_cast<char>(0xE0U | (code >> 12U));
            out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (code & 0x3FU));
        } else if (code <= 0x10FFFFU) {
            out += static_cast<char>(0xF0U | (code >> 18U));
            out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
            out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (code & 0x3FU));
        }
    }

    std::optional<std::unique_ptr<JsonValue>> parse_string_value() {
        const auto start = pos_;
        auto s = parse_string();
        if (!s) {
            return std::nullopt;
        }
        auto value = JsonValue::make_string(std::move(*s));
        value->begin_offset = start;
        value->end_offset = pos_;
        return value;
    }

    std::optional<std::unique_ptr<JsonValue>> parse_array() {
        const auto start = pos_;
        if (!consume('[')) {
            return std::nullopt;
        }
        auto arr = JsonValue::make_array();
        skip_whitespace();
        if (consume(']')) {
            arr->begin_offset = start;
            arr->end_offset = pos_;
            return arr;
        }
        while (true) {
            auto item = parse_value();
            if (!item) {
                return std::nullopt;
            }
            arr->push(std::move(*item));
            skip_whitespace();
            if (consume(']')) {
                arr->begin_offset = start;
                arr->end_offset = pos_;
                return arr;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    std::optional<std::unique_ptr<JsonValue>> parse_object() {
        const auto start = pos_;
        if (!consume('{')) {
            return std::nullopt;
        }
        auto obj = JsonValue::make_object();
        skip_whitespace();
        if (consume('}')) {
            obj->begin_offset = start;
            obj->end_offset = pos_;
            return obj;
        }
        while (true) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) {
                return std::nullopt;
            }
            skip_whitespace();
            if (!consume(':')) {
                return std::nullopt;
            }
            auto val = parse_value();
            if (!val) {
                return std::nullopt;
            }
            if (obj->get(*key) != nullptr) {
                return std::nullopt;
            }
            obj->set(std::move(*key), std::move(*val));
            skip_whitespace();
            if (consume('}')) {
                obj->begin_offset = start;
                obj->end_offset = pos_;
                return obj;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }
};

void serialize_impl(const JsonValue &v, std::ostringstream &out) {
    switch (v.kind) {
    case Kind::Null:
        out << "null";
        break;
    case Kind::Bool:
        out << (v.bool_val ? "true" : "false");
        break;
    case Kind::Int:
        out << v.int_val;
        break;
    case Kind::Float: {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf,
                                       buf + sizeof(buf),
                                       v.float_val,
                                       std::chars_format::general,
                                       std::numeric_limits<double>::max_digits10);
        out << std::string_view(buf, static_cast<std::size_t>(ptr - buf));
        break;
    }
    case Kind::String: {
        out << '"';
        for (char c : v.string_val) {
            switch (c) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    auto byte = static_cast<unsigned char>(c);
                    out << "\\u00" << hex[(byte >> 4U) & 0x0FU] << hex[byte & 0x0FU];
                } else {
                    out << c;
                }
                break;
            }
        }
        out << '"';
        break;
    }
    case Kind::Array: {
        out << '[';
        bool first = true;
        for (const auto &item : v.array_items) {
            if (!first) {
                out << ',';
            }
            first = false;
            serialize_impl(*item, out);
        }
        out << ']';
        break;
    }
    case Kind::Object: {
        out << '{';
        bool first = true;
        for (const auto &[key, val] : v.object_fields) {
            if (!first) {
                out << ',';
            }
            first = false;
            // Serialize key
            out << '"';
            for (char c : key) {
                switch (c) {
                case '\\':
                    out << "\\\\";
                    break;
                case '"':
                    out << "\\\"";
                    break;
                default:
                    out << c;
                    break;
                }
            }
            out << '"';
            out << ':';
            serialize_impl(*val, out);
        }
        out << '}';
        break;
    }
    }
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

std::optional<std::unique_ptr<JsonValue>> parse_json(std::string_view input) {
    Parser parser(input);
    return parser.parse();
}

std::string serialize_json(const JsonValue &v) {
    std::ostringstream out;
    serialize_impl(v, out);
    return out.str();
}

} // namespace ahfl::json
