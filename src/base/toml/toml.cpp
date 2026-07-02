#include "base/toml/toml.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

namespace ahfl::toml {
namespace {

[[nodiscard]] bool is_bare_key_char(char ch) noexcept {
    const auto c = static_cast<unsigned char>(ch);
    return std::isalnum(c) != 0 || ch == '_' || ch == '-';
}

[[nodiscard]] bool is_space(char ch) noexcept {
    return ch == ' ' || ch == '\t';
}

[[nodiscard]] bool is_newline(char ch) noexcept {
    return ch == '\n' || ch == '\r';
}

[[nodiscard]] std::string remove_underscores(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        if (ch != '_') {
            out.push_back(ch);
        }
    }
    return out;
}

[[nodiscard]] std::unique_ptr<Value> make_value(ValueKind kind, SourceRange range) {
    auto value = std::make_unique<Value>();
    value->kind = kind;
    value->range = range;
    return value;
}

[[nodiscard]] Entry *find_entry_in(Value &table, std::string_view key) {
    for (auto &entry : table.table_fields) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const Entry *find_entry_in(const Value &table, std::string_view key) {
    for (const auto &entry : table.table_fields) {
        if (entry.key == key) {
            return &entry;
        }
    }
    return nullptr;
}

class Parser {
  public:
    explicit Parser(std::string_view input) : input_(input) {
        document_.root.kind = ValueKind::Table;
        document_.root.range = SourceRange{.begin_offset = 0, .end_offset = input.size()};
    }

    [[nodiscard]] ParseResult parse_document() {
        Value *current_table = &document_.root;

        while (!at_end()) {
            skip_space_and_newlines();
            if (at_end()) {
                break;
            }

            if (peek() == '#') {
                skip_comment();
                continue;
            }

            if (peek() == '[') {
                current_table = parse_table_header();
                consume_line_tail();
                continue;
            }

            parse_key_value(*current_table);
            consume_line_tail();
        }

        return ParseResult{.document = std::move(document_)};
    }

  private:
    std::string_view input_;
    std::size_t pos_{0};
    Document document_{};

    [[nodiscard]] bool at_end() const noexcept {
        return pos_ >= input_.size();
    }

    [[nodiscard]] char peek() const noexcept {
        return at_end() ? '\0' : input_[pos_];
    }

    [[nodiscard]] bool starts_with(std::string_view text) const noexcept {
        return input_.substr(pos_, text.size()) == text;
    }

    [[nodiscard]] char advance() noexcept {
        return input_[pos_++];
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        if (!at_end() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void add_error(std::string code, std::string message, SourceRange range) {
        document_.diagnostics.push_back(Diagnostic{
            .code = std::move(code),
            .message = std::move(message),
            .range = range,
        });
    }

    void skip_spaces() {
        while (!at_end() && is_space(peek())) {
            ++pos_;
        }
    }

    void skip_space_and_newlines() {
        while (!at_end()) {
            if (is_space(peek()) || is_newline(peek())) {
                ++pos_;
                continue;
            }
            if (peek() == '#') {
                skip_comment();
                continue;
            }
            break;
        }
    }

    void skip_comment() {
        while (!at_end() && !is_newline(peek())) {
            ++pos_;
        }
    }

    void consume_line_tail() {
        skip_spaces();
        if (peek() == '#') {
            skip_comment();
        }

        if (at_end()) {
            return;
        }

        if (peek() == '\r') {
            ++pos_;
            if (peek() == '\n') {
                ++pos_;
            }
            return;
        }

        if (peek() == '\n') {
            ++pos_;
            return;
        }

        const auto begin = pos_;
        add_error("toml.trailing_content",
                  "unexpected content after TOML statement",
                  SourceRange{.begin_offset = begin, .end_offset = line_end(begin)});
        pos_ = line_end(begin);
    }

    [[nodiscard]] std::size_t line_end(std::size_t from) const noexcept {
        std::size_t index = from;
        while (index < input_.size() && input_[index] != '\n' && input_[index] != '\r') {
            ++index;
        }
        return index;
    }

    struct KeyParse {
        std::vector<std::string> parts;
        SourceRange range{};
        bool ok{false};
    };

    [[nodiscard]] KeyParse parse_key() {
        KeyParse result;
        skip_spaces();
        result.range.begin_offset = pos_;
        std::size_t last_part_end = pos_;

        while (!at_end()) {
            skip_spaces();
            auto part = parse_key_part();
            if (!part.has_value()) {
                break;
            }
            result.parts.push_back(std::move(*part));
            last_part_end = pos_;
            skip_spaces();
            if (!consume('.')) {
                break;
            }
        }

        result.range.end_offset = last_part_end;
        result.ok = !result.parts.empty();
        if (!result.ok) {
            add_error("toml.invalid_key",
                      "expected TOML key",
                      SourceRange{.begin_offset = result.range.begin_offset, .end_offset = pos_});
        }
        return result;
    }

    [[nodiscard]] std::optional<std::string> parse_key_part() {
        if (at_end()) {
            return std::nullopt;
        }
        if (peek() == '"') {
            return parse_basic_string_text();
        }
        if (peek() == '\'') {
            return parse_literal_string_text();
        }
        if (!is_bare_key_char(peek())) {
            return std::nullopt;
        }

        const auto begin = pos_;
        while (!at_end() && is_bare_key_char(peek())) {
            ++pos_;
        }
        return std::string{input_.substr(begin, pos_ - begin)};
    }

    [[nodiscard]] std::optional<std::string> parse_basic_string_text() {
        const auto begin = pos_;
        if (!consume('"')) {
            return std::nullopt;
        }

        if (starts_with("\"\"")) {
            pos_ += 2;
            return parse_multiline_basic_string_body(begin);
        }

        std::string out;
        while (!at_end()) {
            const char ch = advance();
            if (ch == '"') {
                return out;
            }
            if (is_newline(ch)) {
                add_error("toml.unterminated_string",
                          "basic string is not terminated before newline",
                          SourceRange{.begin_offset = begin, .end_offset = pos_});
                return std::nullopt;
            }
            if (ch == '\\') {
                auto escaped = parse_escape(begin);
                if (!escaped.has_value()) {
                    return std::nullopt;
                }
                out += *escaped;
                continue;
            }
            out.push_back(ch);
        }

        add_error("toml.unterminated_string",
                  "basic string is not terminated",
                  SourceRange{.begin_offset = begin, .end_offset = pos_});
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> parse_multiline_basic_string_body(std::size_t begin) {
        if (starts_with("\r\n")) {
            pos_ += 2;
        } else if (starts_with("\n")) {
            ++pos_;
        }

        std::string out;
        while (!at_end()) {
            if (starts_with("\"\"\"")) {
                pos_ += 3;
                return out;
            }
            const char ch = advance();
            if (ch == '\\') {
                auto escaped = parse_escape(begin);
                if (!escaped.has_value()) {
                    return std::nullopt;
                }
                out += *escaped;
                continue;
            }
            out.push_back(ch);
        }

        add_error("toml.unterminated_string",
                  "multi-line basic string is not terminated",
                  SourceRange{.begin_offset = begin, .end_offset = pos_});
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> parse_literal_string_text() {
        const auto begin = pos_;
        if (!consume('\'')) {
            return std::nullopt;
        }

        if (starts_with("''")) {
            pos_ += 2;
            return parse_multiline_literal_string_body(begin);
        }

        const auto content_begin = pos_;
        while (!at_end()) {
            const char ch = advance();
            if (ch == '\'') {
                return std::string{input_.substr(content_begin, (pos_ - 1) - content_begin)};
            }
            if (is_newline(ch)) {
                add_error("toml.unterminated_string",
                          "literal string is not terminated before newline",
                          SourceRange{.begin_offset = begin, .end_offset = pos_});
                return std::nullopt;
            }
        }

        add_error("toml.unterminated_string",
                  "literal string is not terminated",
                  SourceRange{.begin_offset = begin, .end_offset = pos_});
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string>
    parse_multiline_literal_string_body(std::size_t begin) {
        if (starts_with("\r\n")) {
            pos_ += 2;
        } else if (starts_with("\n")) {
            ++pos_;
        }

        const auto content_begin = pos_;
        while (!at_end()) {
            if (starts_with("'''")) {
                const auto content_end = pos_;
                pos_ += 3;
                return std::string{input_.substr(content_begin, content_end - content_begin)};
            }
            ++pos_;
        }

        add_error("toml.unterminated_string",
                  "multi-line literal string is not terminated",
                  SourceRange{.begin_offset = begin, .end_offset = pos_});
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> parse_escape(std::size_t string_begin) {
        if (at_end()) {
            add_error("toml.invalid_escape",
                      "escape sequence is incomplete",
                      SourceRange{.begin_offset = string_begin, .end_offset = pos_});
            return std::nullopt;
        }

        const char ch = advance();
        switch (ch) {
        case 'b':
            return std::string(1, '\b');
        case 't':
            return std::string(1, '\t');
        case 'n':
            return std::string(1, '\n');
        case 'f':
            return std::string(1, '\f');
        case 'r':
            return std::string(1, '\r');
        case '"':
            return std::string(1, '"');
        case '\\':
            return std::string(1, '\\');
        case 'u':
            return parse_unicode_escape(4, string_begin);
        case 'U':
            return parse_unicode_escape(8, string_begin);
        default:
            add_error("toml.invalid_escape",
                      "invalid TOML escape sequence",
                      SourceRange{.begin_offset = pos_ - 2, .end_offset = pos_});
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<std::string> parse_unicode_escape(std::size_t digits,
                                                                  std::size_t string_begin) {
        if (pos_ + digits > input_.size()) {
            add_error("toml.invalid_escape",
                      "unicode escape sequence is incomplete",
                      SourceRange{.begin_offset = string_begin, .end_offset = pos_});
            return std::nullopt;
        }

        std::uint32_t value = 0;
        for (std::size_t i = 0; i < digits; ++i) {
            const char ch = input_[pos_ + i];
            value <<= 4U;
            if (ch >= '0' && ch <= '9') {
                value += static_cast<std::uint32_t>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value += static_cast<std::uint32_t>(10 + ch - 'a');
            } else if (ch >= 'A' && ch <= 'F') {
                value += static_cast<std::uint32_t>(10 + ch - 'A');
            } else {
                add_error("toml.invalid_escape",
                          "unicode escape contains a non-hex digit",
                          SourceRange{.begin_offset = pos_, .end_offset = pos_ + digits});
                pos_ += i + 1;
                return std::nullopt;
            }
        }
        pos_ += digits;

        if (value <= 0x7F) {
            return std::string(1, static_cast<char>(value));
        }

        // Keep the parser dependency-free. Non-ASCII escapes are preserved as a
        // replacement marker until a later source-display layer needs UTF-8.
        return std::string{"?"};
    }

    void parse_key_value(Value &current_table) {
        const auto key = parse_key();
        skip_spaces();
        if (!consume('=')) {
            add_error("toml.expected_equals",
                      "expected '=' after TOML key",
                      SourceRange{.begin_offset = key.range.end_offset, .end_offset = pos_});
            pos_ = line_end(pos_);
            return;
        }
        skip_spaces();

        auto value = parse_value();
        if (!value) {
            pos_ = line_end(pos_);
            return;
        }

        insert_dotted_value(current_table, key, std::move(value));
    }

    [[nodiscard]] std::unique_ptr<Value> parse_value() {
        skip_spaces();
        const auto begin = pos_;
        if (at_end()) {
            add_error("toml.expected_value",
                      "expected TOML value",
                      SourceRange{.begin_offset = begin, .end_offset = begin});
            return nullptr;
        }

        if (peek() == '"') {
            auto text = parse_basic_string_text();
            if (!text.has_value()) {
                return nullptr;
            }
            auto value = make_value(ValueKind::String,
                                    SourceRange{.begin_offset = begin, .end_offset = pos_});
            value->string_value = std::move(*text);
            return value;
        }

        if (peek() == '\'') {
            auto text = parse_literal_string_text();
            if (!text.has_value()) {
                return nullptr;
            }
            auto value = make_value(ValueKind::String,
                                    SourceRange{.begin_offset = begin, .end_offset = pos_});
            value->string_value = std::move(*text);
            return value;
        }

        if (peek() == '[') {
            return parse_array();
        }

        if (peek() == '{') {
            return parse_inline_table();
        }

        const auto token_begin = pos_;
        while (!at_end() && !is_space(peek()) && !is_newline(peek()) && peek() != '#' &&
               peek() != ',' && peek() != ']' && peek() != '}') {
            ++pos_;
        }

        const auto token = input_.substr(token_begin, pos_ - token_begin);
        if (token == "true" || token == "false") {
            auto value = make_value(ValueKind::Boolean,
                                    SourceRange{.begin_offset = token_begin, .end_offset = pos_});
            value->bool_value = (token == "true");
            return value;
        }

        if (looks_like_datetime(token)) {
            auto value = make_value(ValueKind::DateTime,
                                    SourceRange{.begin_offset = token_begin, .end_offset = pos_});
            value->string_value = std::string{token};
            return value;
        }

        return parse_number_token(token, token_begin);
    }

    [[nodiscard]] bool looks_like_datetime(std::string_view token) const noexcept {
        return token.find('-') != std::string_view::npos ||
               token.find(':') != std::string_view::npos;
    }

    [[nodiscard]] std::unique_ptr<Value> parse_number_token(std::string_view token,
                                                            std::size_t token_begin) {
        if (token.empty()) {
            add_error("toml.expected_value",
                      "expected TOML value",
                      SourceRange{.begin_offset = token_begin, .end_offset = pos_});
            return nullptr;
        }

        const auto normalized = remove_underscores(token);
        const bool is_float = normalized.find('.') != std::string::npos ||
                              normalized.find('e') != std::string::npos ||
                              normalized.find('E') != std::string::npos || normalized == "inf" ||
                              normalized == "+inf" || normalized == "-inf" || normalized == "nan" ||
                              normalized == "+nan" || normalized == "-nan";
        if (is_float) {
            std::istringstream stream(normalized);
            stream.imbue(std::locale::classic());
            stream.unsetf(std::ios_base::skipws);
            double parsed = 0.0;
            if (!(stream >> parsed)) {
                if (normalized == "inf" || normalized == "+inf") {
                    parsed = HUGE_VAL;
                } else if (normalized == "-inf") {
                    parsed = -HUGE_VAL;
                } else if (normalized == "nan" || normalized == "+nan" || normalized == "-nan") {
                    parsed = std::nan("");
                } else {
                    add_error("toml.invalid_number",
                              "invalid TOML float",
                              SourceRange{.begin_offset = token_begin, .end_offset = pos_});
                    return nullptr;
                }
            }

            auto value = make_value(ValueKind::Float,
                                    SourceRange{.begin_offset = token_begin, .end_offset = pos_});
            value->float_value = parsed;
            return value;
        }

        int base = 10;
        std::size_t offset = 0;
        bool negative = false;
        if (normalized[offset] == '+' || normalized[offset] == '-') {
            negative = normalized[offset] == '-';
            ++offset;
        }
        if (normalized.substr(offset, 2) == "0x") {
            base = 16;
            offset += 2;
        } else if (normalized.substr(offset, 2) == "0o") {
            base = 8;
            offset += 2;
        } else if (normalized.substr(offset, 2) == "0b") {
            base = 2;
            offset += 2;
        }

        std::int64_t parsed = 0;
        const auto digits = normalized.substr(offset);
        const auto *begin = digits.data();
        const auto *end = digits.data() + digits.size();
        const auto result = std::from_chars(begin, end, parsed, base);
        if (result.ec != std::errc{} || result.ptr != end) {
            add_error("toml.invalid_number",
                      "invalid TOML integer",
                      SourceRange{.begin_offset = token_begin, .end_offset = pos_});
            return nullptr;
        }
        if (negative) {
            parsed = -parsed;
        }

        auto value = make_value(ValueKind::Integer,
                                SourceRange{.begin_offset = token_begin, .end_offset = pos_});
        value->integer_value = parsed;
        return value;
    }

    [[nodiscard]] std::unique_ptr<Value> parse_array() {
        const auto begin = pos_;
        static_cast<void>(consume('['));
        auto value = make_value(ValueKind::Array, SourceRange{.begin_offset = begin});

        skip_spaces_and_comments_inside();
        while (!at_end() && peek() != ']') {
            auto item = parse_value();
            if (!item) {
                return nullptr;
            }
            value->array_items.push_back(std::move(item));
            skip_spaces_and_comments_inside();
            if (!consume(',')) {
                break;
            }
            skip_spaces_and_comments_inside();
        }

        if (!consume(']')) {
            add_error("toml.unclosed_array",
                      "array is not closed",
                      SourceRange{.begin_offset = begin, .end_offset = pos_});
            return nullptr;
        }
        value->range.end_offset = pos_;
        return value;
    }

    void skip_spaces_and_comments_inside() {
        while (!at_end()) {
            if (is_space(peek()) || is_newline(peek())) {
                ++pos_;
                continue;
            }
            if (peek() == '#') {
                skip_comment();
                continue;
            }
            break;
        }
    }

    [[nodiscard]] std::unique_ptr<Value> parse_inline_table() {
        const auto begin = pos_;
        static_cast<void>(consume('{'));
        auto value = make_value(ValueKind::InlineTable, SourceRange{.begin_offset = begin});
        skip_spaces();
        while (!at_end() && peek() != '}') {
            auto key = parse_key();
            skip_spaces();
            if (!consume('=')) {
                add_error("toml.expected_equals",
                          "expected '=' after inline table key",
                          SourceRange{.begin_offset = key.range.end_offset, .end_offset = pos_});
                return nullptr;
            }
            skip_spaces();
            auto child = parse_value();
            if (!child) {
                return nullptr;
            }
            insert_dotted_value(*value, key, std::move(child));
            skip_spaces();
            if (!consume(',')) {
                break;
            }
            skip_spaces();
        }
        if (!consume('}')) {
            add_error("toml.unclosed_inline_table",
                      "inline table is not closed",
                      SourceRange{.begin_offset = begin, .end_offset = pos_});
            return nullptr;
        }
        value->range.end_offset = pos_;
        return value;
    }

    void insert_dotted_value(Value &table, const KeyParse &key, std::unique_ptr<Value> value) {
        if (!key.ok || key.parts.empty()) {
            return;
        }

        Value *owner = &table;
        for (std::size_t i = 0; i + 1 < key.parts.size(); ++i) {
            owner = get_or_create_table(*owner, key.parts[i], key.range);
        }

        const std::string &leaf = key.parts.back();
        if (find_entry_in(*owner, leaf) != nullptr) {
            add_error("toml.duplicate_key", "duplicate TOML key '" + leaf + "'", key.range);
            return;
        }

        owner->table_fields.push_back(Entry{
            .key = leaf,
            .key_path = key.parts,
            .key_range = key.range,
            .value_range = value->range,
            .value = std::move(value),
        });
    }

    [[nodiscard]] Value *
    get_or_create_table(Value &table, const std::string &key, SourceRange key_range) {
        if (auto *entry = find_entry_in(table, key); entry != nullptr) {
            if (entry->value->kind != ValueKind::Table) {
                add_error("toml.invalid_table",
                          "dotted key parent '" + key + "' is not a table",
                          key_range);
            }
            return entry->value.get();
        }

        auto child = make_value(ValueKind::Table, key_range);
        auto *raw = child.get();
        table.table_fields.push_back(Entry{
            .key = key,
            .key_path = {key},
            .key_range = key_range,
            .value_range = key_range,
            .value = std::move(child),
        });
        return raw;
    }

    [[nodiscard]] Value *parse_table_header() {
        const auto begin = pos_;
        const bool array_table = starts_with("[[");
        pos_ += array_table ? 2 : 1;
        const auto key = parse_key();
        skip_spaces();
        const bool closed = array_table ? starts_with("]]") : starts_with("]");
        if (!closed) {
            add_error("toml.invalid_table_header",
                      "table header is not closed",
                      SourceRange{.begin_offset = begin, .end_offset = pos_});
            pos_ = line_end(pos_);
            return &document_.root;
        }
        pos_ += array_table ? 2 : 1;

        if (!key.ok || key.parts.empty()) {
            return &document_.root;
        }

        if (array_table) {
            return create_array_table(key);
        }
        return create_standard_table(key, SourceRange{.begin_offset = begin, .end_offset = pos_});
    }

    [[nodiscard]] Value *create_standard_table(const KeyParse &key, SourceRange header_range) {
        Value *owner = &document_.root;
        for (std::size_t i = 0; i < key.parts.size(); ++i) {
            const auto &part = key.parts[i];
            auto *entry = find_entry_in(*owner, part);
            if (entry == nullptr) {
                auto child = make_value(ValueKind::Table, header_range);
                auto *raw = child.get();
                owner->table_fields.push_back(Entry{
                    .key = part,
                    .key_path = {part},
                    .key_range = key.range,
                    .value_range = header_range,
                    .value = std::move(child),
                });
                owner = raw;
                continue;
            }

            if (entry->value->kind != ValueKind::Table) {
                add_error("toml.invalid_table",
                          "table header path conflicts with non-table value",
                          header_range);
                return &document_.root;
            }

            owner = entry->value.get();
            if (i + 1 == key.parts.size() && !owner->table_fields.empty()) {
                add_error(
                    "toml.duplicate_table", "duplicate TOML table '" + part + "'", header_range);
            }
        }
        return owner;
    }

    [[nodiscard]] Value *create_array_table(const KeyParse &key) {
        Value *owner = &document_.root;
        for (std::size_t i = 0; i + 1 < key.parts.size(); ++i) {
            owner = get_or_create_table(*owner, key.parts[i], key.range);
        }

        const auto &leaf = key.parts.back();
        Entry *entry = find_entry_in(*owner, leaf);
        if (entry == nullptr) {
            auto array = make_value(ValueKind::Array, key.range);
            auto table = make_value(ValueKind::Table, key.range);
            auto *raw_table = table.get();
            array->array_items.push_back(std::move(table));
            owner->table_fields.push_back(Entry{
                .key = leaf,
                .key_path = key.parts,
                .key_range = key.range,
                .value_range = key.range,
                .value = std::move(array),
            });
            return raw_table;
        }

        if (entry->value->kind != ValueKind::Array) {
            add_error("toml.invalid_table",
                      "array table header path conflicts with non-array value",
                      key.range);
            return &document_.root;
        }

        auto table = make_value(ValueKind::Table, key.range);
        auto *raw_table = table.get();
        entry->value->array_items.push_back(std::move(table));
        return raw_table;
    }
};

} // namespace

const Entry *Document::find_entry(std::initializer_list<std::string_view> path) const {
    const Value *current = &root;
    const Entry *entry = nullptr;
    for (const auto part : path) {
        if (current == nullptr || current->kind != ValueKind::Table) {
            return nullptr;
        }
        entry = find_entry_in(*current, part);
        if (entry == nullptr) {
            return nullptr;
        }
        current = entry->value.get();
    }
    return entry;
}

const Value *Document::find(std::initializer_list<std::string_view> path) const {
    const Entry *entry = find_entry(path);
    return entry == nullptr ? nullptr : entry->value.get();
}

ParseResult parse(std::string_view input) {
    return Parser(input).parse_document();
}

std::string_view kind_name(ValueKind kind) noexcept {
    switch (kind) {
    case ValueKind::String:
        return "string";
    case ValueKind::Integer:
        return "integer";
    case ValueKind::Float:
        return "float";
    case ValueKind::Boolean:
        return "boolean";
    case ValueKind::DateTime:
        return "datetime";
    case ValueKind::Array:
        return "array";
    case ValueKind::InlineTable:
        return "inline-table";
    case ValueKind::Table:
        return "table";
    }
    return "unknown";
}

} // namespace ahfl::toml
