#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::json {

/// JSON value kinds
enum class Kind {
    Null,
    Bool,
    Int,
    Float,
    String,
    Array,
    Object
};

/// A generic JSON DOM node (zero external dependency).
struct JsonValue {
    Kind kind{Kind::Null};

    std::size_t begin_offset{0};
    std::size_t end_offset{0};

    bool bool_val{};
    int64_t int_val{};
    double float_val{};
    std::string string_val;
    std::vector<std::unique_ptr<JsonValue>> array_items;
    std::vector<std::pair<std::string, std::unique_ptr<JsonValue>>> object_fields;

    // ---------- Accessors ----------

    /// Lookup an object field by key. Returns nullptr if not found or not an object.
    [[nodiscard]] const JsonValue *get(std::string_view key) const;

    /// Mutable lookup.
    [[nodiscard]] JsonValue *get_mut(std::string_view key);

    [[nodiscard]] std::optional<std::string_view> as_string() const;
    [[nodiscard]] std::optional<int64_t> as_int() const;
    [[nodiscard]] std::optional<double> as_float() const;
    [[nodiscard]] std::optional<bool> as_bool() const;

    [[nodiscard]] bool is_null() const {
        return kind == Kind::Null;
    }
    [[nodiscard]] bool is_object() const {
        return kind == Kind::Object;
    }
    [[nodiscard]] bool is_array() const {
        return kind == Kind::Array;
    }

    // ---------- Factory helpers ----------

    static std::unique_ptr<JsonValue> make_null();
    static std::unique_ptr<JsonValue> make_bool(bool v);
    static std::unique_ptr<JsonValue> make_int(int64_t v);
    static std::unique_ptr<JsonValue> make_float(double v);
    static std::unique_ptr<JsonValue> make_string(std::string s);
    static std::unique_ptr<JsonValue> make_array();
    static std::unique_ptr<JsonValue> make_object();

    // ---------- Mutators (for building) ----------

    /// Push an item into an array node.
    void push(std::unique_ptr<JsonValue> item);

    /// Set a field on an object node.
    void set(std::string key, std::unique_ptr<JsonValue> value);
};

// ---------- Parse / Serialize ----------

/// Parse a JSON string into a JsonValue tree. Returns nullopt on parse error.
[[nodiscard]] std::optional<std::unique_ptr<JsonValue>> parse_json(std::string_view input);

/// Serialize a JsonValue tree to a compact JSON string.
[[nodiscard]] std::string serialize_json(const JsonValue &v);

} // namespace ahfl::json
