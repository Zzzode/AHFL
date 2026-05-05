#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ahfl::evaluator {

// Forward declaration for recursive variant
struct Value;

// ============================================================================
// Value Variants
// ============================================================================

struct NoneValue {};

struct BoolValue {
    bool value{false};
};

struct IntValue {
    int64_t value{0};
};

struct FloatValue {
    double value{0.0};
};

struct StringValue {
    std::string value;
};

struct DecimalValue {
    std::string spelling; // preserve precision
};

struct DurationValue {
    std::string spelling; // preserve original format (e.g. "5s", "100ms")
};

struct StructValue {
    std::string type_name;
    std::unordered_map<std::string, std::unique_ptr<Value>> fields;
};

struct ListValue {
    std::vector<std::unique_ptr<Value>> items;
};

struct EnumValue {
    std::string enum_name;
    std::string variant;
};

struct OptionalValue {
    std::unique_ptr<Value> inner; // nullptr means none
};

// ============================================================================
// Value type (variant)
// ============================================================================

using ValueNode = std::variant<NoneValue,
                               BoolValue,
                               IntValue,
                               FloatValue,
                               StringValue,
                               DecimalValue,
                               DurationValue,
                               StructValue,
                               ListValue,
                               EnumValue,
                               OptionalValue>;

struct Value {
    ValueNode node;
};

// ============================================================================
// ValueKind enum
// ============================================================================

enum class ValueKind {
    None,
    Bool,
    Int,
    Float,
    String,
    Decimal,
    Duration,
    Struct,
    List,
    Enum,
    Optional,
};

// ============================================================================
// Helper functions
// ============================================================================

[[nodiscard]] ValueKind value_kind(const Value &v);

[[nodiscard]] bool is_none(const Value &v);

void print_value(const Value &v, std::ostream &out);

// ============================================================================
// Convenience constructors
// ============================================================================

[[nodiscard]] inline Value make_none() {
    return Value{NoneValue{}};
}

[[nodiscard]] inline Value make_bool(bool b) {
    return Value{BoolValue{b}};
}

[[nodiscard]] inline Value make_int(int64_t i) {
    return Value{IntValue{i}};
}

[[nodiscard]] inline Value make_float(double d) {
    return Value{FloatValue{d}};
}

[[nodiscard]] inline Value make_string(std::string s) {
    return Value{StringValue{std::move(s)}};
}

[[nodiscard]] inline Value make_decimal(std::string spelling) {
    return Value{DecimalValue{std::move(spelling)}};
}

[[nodiscard]] inline Value make_duration(std::string spelling) {
    return Value{DurationValue{std::move(spelling)}};
}

[[nodiscard]] inline Value make_enum(std::string enum_name, std::string variant) {
    return Value{EnumValue{std::move(enum_name), std::move(variant)}};
}

[[nodiscard]] Value make_optional_some(Value inner);

[[nodiscard]] Value make_optional_none();

[[nodiscard]] Value make_struct(std::string type_name,
                                std::unordered_map<std::string, Value> fields);

[[nodiscard]] Value make_list(std::vector<Value> items);

// Deep copy
[[nodiscard]] Value clone_value(const Value &v);

} // namespace ahfl::evaluator
