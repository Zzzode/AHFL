#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::ir {
struct Expr;
}

namespace ahfl::evaluator {

class EvalContext;

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
    // Vector payload: Result-style multi-field variants (base HEAD convention).
    std::vector<std::unique_ptr<Value>> payload;
    // Associated single payload: nominal Option::Some & similar single-data
    // variants (P5.11a evaluator-internal construction).
    std::unique_ptr<Value> associated; // nullable
};

// Kept for external API compatibility (JSON, response validator, prompt builder).
// Evaluator-internal construction uses the nominal EnumValue option below.
struct OptionalValue {
    std::unique_ptr<Value> inner; // nullptr means none
};

struct CallableValue {
    std::vector<std::string> params;
    const ir::Expr *body{nullptr};
    std::shared_ptr<const EvalContext> captured_context;
};

// Set value: ordered + de-duplicated vector of items (RFC P7).
// Ordering enables deterministic equality / printing independent of insertion
// order; de-duplication mirrors mathematical set semantics.
struct SetValue {
    std::vector<std::unique_ptr<Value>> items;
};

// Map value: ordered key/value pairs (RFC P7).
// Stored as an ordered vector to keep iteration / equality deterministic;
// duplicate-key insertion keeps the last value (last-write-wins).
struct MapValue {
    std::vector<std::pair<std::unique_ptr<Value>, std::unique_ptr<Value>>> entries;
};

// UUID value: canonical 32-char lowercase hex string (RFC P7).
// Chosen over a 128-bit int to preserve spellings / round-tripping without
// platform-dependent 128-bit integer support.
struct UuidValue {
    std::string hex; // 32 lowercase hex chars (no dashes)
};

// Timestamp value: Unix epoch milliseconds (RFC P7).
struct TimestampValue {
    int64_t unix_ms{0};
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
                               OptionalValue,
                               SetValue,
                               MapValue,
                               UuidValue,
                               TimestampValue,
                               CallableValue>;

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
    Set,
    Map,
    Uuid,
    Timestamp,
    Callable,
};

// ============================================================================
// Helper functions
// ============================================================================

[[nodiscard]] ValueKind value_kind(const Value &v);

[[nodiscard]] bool is_none(const Value &v);

void print_value(const Value &v, std::ostream &out);

/// Structural equality: two values are equal iff they have the same kind
/// and their contents compare equal.  Used by Set/Map canonicalization and
/// by builtin dispatchers for membership / lookup checks.
[[nodiscard]] bool structurally_equal(const Value &lhs, const Value &rhs);

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
    return Value{EnumValue{std::move(enum_name), std::move(variant), {}, nullptr}};
}

[[nodiscard]] Value
make_enum(std::string enum_name, std::string variant, std::vector<Value> payload);

[[nodiscard]] inline Value make_enum(std::string enum_name,
                                     std::string variant,
                                     std::unique_ptr<Value> associated) {
    return Value{EnumValue{std::move(enum_name), std::move(variant), {}, std::move(associated)}};
}

// Nominal option constructors (evaluator-internal representation)
[[nodiscard]] inline Value make_option_some(Value inner) {
    return Value{EnumValue{"std::option::Option",
                           "Some",
                           {},
                           std::make_unique<Value>(std::move(inner))}};
}

[[nodiscard]] inline Value make_option_none() {
    return Value{EnumValue{"std::option::Option", "None", {}, nullptr}};
}

// Legacy constructors — kept for external JSON/compatibility consumers (P5.11b)
[[nodiscard]] Value make_optional_some(Value inner);

[[nodiscard]] Value make_optional_none();

[[nodiscard]] Value make_callable(std::vector<std::string> params,
                                  const ir::Expr *body,
                                  std::shared_ptr<const EvalContext> captured_context);

[[nodiscard]] Value make_struct(std::string type_name,
                                std::unordered_map<std::string, Value> fields);

[[nodiscard]] Value make_list(std::vector<Value> items);

// RFC P7 runtime additions ---------------------------------------------------

/// Construct a Set value. Items are normalized: de-duplicated and ordered so
/// that structurally-equal sets always produce identical values regardless of
/// input order. Order is defined by a stable structural comparison.
[[nodiscard]] Value make_set(std::vector<Value> items);

/// Construct a Map value. Duplicate keys keep the last value (last-write-wins)
/// and entries are ordered by key for deterministic iteration / equality.
[[nodiscard]] Value make_map(std::vector<std::pair<Value, Value>> entries);

/// Construct a UUID value from a canonical 32-char lowercase hex spelling.
/// The hex string is normalized (dashes stripped, lowercased) and validated for
/// length; returns nullopt on malformed input.
[[nodiscard]] std::optional<Value> make_uuid(std::string spelling);

/// Construct a Timestamp value from Unix epoch milliseconds.
[[nodiscard]] Value make_timestamp(int64_t unix_ms);

// Deep copy
[[nodiscard]] Value clone_value(const Value &v);

} // namespace ahfl::evaluator
