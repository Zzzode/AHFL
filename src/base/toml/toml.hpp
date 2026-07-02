#pragma once

#include "ahfl/base/support/source.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::toml {

enum class ValueKind {
    String,
    Integer,
    Float,
    Boolean,
    DateTime,
    Array,
    InlineTable,
    Table,
};

struct Value;

struct Entry {
    std::string key;
    std::vector<std::string> key_path;
    SourceRange key_range{};
    SourceRange value_range{};
    std::unique_ptr<Value> value;
};

struct Value {
    ValueKind kind{ValueKind::Table};
    SourceRange range{};
    std::string string_value;
    std::int64_t integer_value{0};
    double float_value{0.0};
    bool bool_value{false};
    std::vector<std::unique_ptr<Value>> array_items;
    std::vector<Entry> table_fields;
};

struct Diagnostic {
    std::string code;
    std::string message;
    SourceRange range{};
};

struct Document {
    Value root;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] const Value *find(std::initializer_list<std::string_view> path) const;
    [[nodiscard]] const Entry *find_entry(std::initializer_list<std::string_view> path) const;
};

struct ParseResult {
    Document document;

    [[nodiscard]] bool has_errors() const {
        return !document.diagnostics.empty();
    }
};

[[nodiscard]] ParseResult parse(std::string_view input);
[[nodiscard]] std::string_view kind_name(ValueKind kind) noexcept;

} // namespace ahfl::toml
