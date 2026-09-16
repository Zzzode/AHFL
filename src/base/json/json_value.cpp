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
    // Signed only: an UnsignedInteger high-bit value or an IntegerFallback is not
    // representable as int64 and must be rejected here (use as_uint / inspect
    // number_provenance).
    if (kind != Kind::Int || number_provenance != NumberProvenance::SignedInteger) {
        return std::nullopt;
    }
    return int_val;
}

std::optional<uint64_t> JsonValue::as_uint() const {
    // Accepts the full 64-bit unsigned magnitude: an UnsignedInteger node (stored
    // in uint_val) and a non-negative SignedInteger node. A true-negative signed
    // value, an IntegerFallback, or a non-integer node returns nullopt.
    if (kind != Kind::Int) {
        return std::nullopt;
    }
    if (number_provenance == NumberProvenance::UnsignedInteger) {
        return uint_val;
    }
    if (number_provenance == NumberProvenance::SignedInteger && int_val >= 0) {
        return static_cast<uint64_t>(int_val);
    }
    return std::nullopt;
}

std::optional<double> JsonValue::as_float() const {
    // Compatibility widening: signed/unsigned integers widen to double, and both
    // float provenances return their (possibly approximate) float_val. A consumer
    // that must reject an integer-token fallback inspects number_provenance.
    if (kind == Kind::Float) {
        if (number_provenance == NumberProvenance::FloatSyntax ||
            number_provenance == NumberProvenance::IntegerFallback) {
            return float_val;
        }
        return std::nullopt;
    }
    if (kind == Kind::Int) {
        if (number_provenance == NumberProvenance::SignedInteger) {
            return static_cast<double>(int_val);
        }
        if (number_provenance == NumberProvenance::UnsignedInteger) {
            return static_cast<double>(uint_val);
        }
        return std::nullopt;
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
    v->number_provenance = NumberProvenance::SignedInteger;
    return v;
}

std::unique_ptr<JsonValue> JsonValue::make_float(double d) {
    auto v = std::make_unique<JsonValue>();
    v->kind = Kind::Float;
    v->float_val = d;
    v->number_provenance = NumberProvenance::FloatSyntax;
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

        // Syntax-first (RFC 0026 C2b P0-10): a `.`/`e`/`E` token is a FloatSyntax
        // float. A pure integer token is classified int64 (SignedInteger) ->
        // uint64 (UnsignedInteger, stored in uint_val, NOT a signed bit pattern)
        // -> otherwise IntegerFallback (a float-approximated value whose exact
        // magnitude exceeds both 64-bit integer ranges). Provenance lets trust
        // consumers fail closed on the ambiguous forms.
        if (is_float) {
            auto val = parse_json_number_float(num_str);
            if (!val) {
                pos_ = start;
                return std::nullopt;
            }
            auto value = JsonValue::make_float(*val); // FloatSyntax
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }

        int64_t val{};
        const auto end = num_str.data() + num_str.size();
        auto [ptr, ec] = std::from_chars(num_str.data(), end, val);
        if (ec == std::errc{} && ptr == end) {
            auto value = JsonValue::make_int(val); // SignedInteger
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }

        // Too large for int64: try uint64 (a high-bit unsigned magnitude).
        uint64_t uval{};
        auto [uptr, uec] = std::from_chars(num_str.data(), end, uval);
        if (uec == std::errc{} && uptr == end) {
            auto value = std::make_unique<JsonValue>();
            value->kind = Kind::Int;
            value->uint_val = uval;
            value->number_provenance = NumberProvenance::UnsignedInteger;
            value->begin_offset = start;
            value->end_offset = pos_;
            return value;
        }

        // A pure integer token beyond both int64 and uint64 (very large, or a
        // large negative below INT64_MIN): keep an approximate float value but
        // tag it IntegerFallback so consumers can reject it.
        auto fval = parse_json_number_float(num_str);
        if (!fval) {
            pos_ = start;
            return std::nullopt;
        }
        auto value = std::make_unique<JsonValue>();
        value->kind = Kind::Float;
        value->float_val = *fval;
        value->number_provenance = NumberProvenance::IntegerFallback;
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
    // Reject invalid (Kind, NumberProvenance) pairings up front (RFC 0026 C2b
    // P0-10). A non-numeric Kind must carry NotNumeric; a numeric provenance on a
    // Null/Bool/String/Array/Object node is a corrupt/hand-built state and fails
    // closed via the stream failbit (serialize_json then returns an empty string).
    // The Int/Float branches below check their own numeric-provenance validity.
    const bool numeric_kind = (v.kind == Kind::Int || v.kind == Kind::Float);
    if (!numeric_kind && v.number_provenance != NumberProvenance::NotNumeric) {
        out.setstate(std::ios_base::failbit);
        return;
    }
    switch (v.kind) {
    case Kind::Null:
        out << "null";
        break;
    case Kind::Bool:
        out << (v.bool_val ? "true" : "false");
        break;
    case Kind::Int:
        // SignedInteger -> int_val; UnsignedInteger -> uint_val (exact decimal,
        // not a signed bit pattern). Any other provenance on an Int node is an
        // invalid combination: fail closed via the stream's failbit.
        if (v.number_provenance == NumberProvenance::SignedInteger) {
            out << v.int_val;
        } else if (v.number_provenance == NumberProvenance::UnsignedInteger) {
            out << v.uint_val;
        } else {
            out.setstate(std::ios_base::failbit);
        }
        break;
    case Kind::Float: {
        // FloatSyntax and IntegerFallback both serialize through the existing
        // shortest-round-trip float formatter (A does NOT change these bytes; a
        // FloatSyntax integral value stays a bare integer, so v1/v2 snapshot bytes
        // are unchanged). Any other provenance on a Float node is invalid.
        if (v.number_provenance != NumberProvenance::FloatSyntax &&
            v.number_provenance != NumberProvenance::IntegerFallback) {
            out.setstate(std::ios_base::failbit);
            break;
        }
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

std::string format_wire_float(double value) {
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf,
                                   buf + sizeof(buf),
                                   value,
                                   std::chars_format::general,
                                   std::numeric_limits<double>::max_digits10);
    (void)ec; // 64 bytes always suffices for a finite double in general format.
    std::string_view sv(buf, static_cast<std::size_t>(ptr - buf));
    std::string out(sv);
    // Keep the spelling recognizably a float (has '.' or exponent). The input
    // wire codec rejects integer tokens at Float nodes ("no int widening"), so
    // the canonical wire spelling of an integral float must keep ".0".
    if (sv.find('.') == std::string_view::npos && sv.find('e') == std::string_view::npos &&
        sv.find('E') == std::string_view::npos) {
        out += ".0";
    }
    return out;
}

std::string serialize_json(const JsonValue &v) {
    std::ostringstream out;
    serialize_impl(v, out);
    // A node carrying an invalid (Kind, NumberProvenance) combination sets the
    // stream's failbit (see serialize_impl). Fail closed: return an empty string
    // rather than emit a partial / misleading serialization.
    if (out.fail()) {
        return {};
    }
    return out.str();
}

} // namespace ahfl::json
