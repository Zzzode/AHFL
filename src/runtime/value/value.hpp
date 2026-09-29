#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::runtime {

// Forward declaration for recursive variant
struct Value;

// Interpreter-only closure alternative. A first-class closure is an opaque
// handle to storage OWNED by the tree-walking evaluator; this translation unit
// never dereferences the descriptor, so it stays incomplete here and the host
// wire layer keeps zero dependency on evaluator internals (no EvalContext, no
// ir::Expr, no executor). The definition lives in the evaluator
// (src/runtime/evaluator/evaluator.hpp) and is reachable only from there.
//
// It is carried in `Value` because the interpreter stores user bindings --
// including closures passed as arguments to user-defined functions
// (`apply(|\y| add_one(y), 41)`) and to higher-order stdlib wrappers
// (`option.map(f)`, `collections.fold`) -- in the same `Value`-typed scope
// maps as every other kind. A closure therefore has to be representable in
// `ValueNode`; the alternative is a second, parallel scope type in the
// evaluator.
//
// Identity is the monotonic `id`, assigned by `make_interpreter_closure`
// (Principle 2: index/id identity, never a heap address). Equality and the
// strict-weak ordering used for Set/Map canonicalization compare ONLY the id,
// so they are stable for the whole process and cannot alias through address
// reuse. The shared_ptr descriptor is pure lifetime management; it is never
// the identity and is never dereferenced on this layer. The id is process-local
// state and is intentionally NOT a wire value.
//
// Wire discipline: a closure is NOT a wire kind and never crosses the frame
// boundary (RFC 0026 KR6.8). The trust boundary MUST call
// `try_value_to_json` / the optional `hash_values`, which reject a closure
// (including one nested anywhere inside a composite) instead of emitting
// bytes. The plain `value_to_json` is observation-only (DAP, traces, tool
// output) and renders the closure as a fixed opaque JSON object so that output
// stays syntactically valid.
struct InterpreterClosure;
using InterpreterClosureRef = std::shared_ptr<const InterpreterClosure>;

struct InterpreterClosureHandle {
    std::uint64_t id{0};
    InterpreterClosureRef descriptor;

    [[nodiscard]] bool operator==(const InterpreterClosureHandle &other) const noexcept {
        return id == other.id;
    }
};

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

// Named field storage for struct values and struct-enum-variant payloads.
//
// RFC 0022 (durable resume) requires deterministic serialization: the same
// logical value must produce byte-identical `value_json` output across
// processes and allocators. A `std::unordered_map` cannot: its iteration order
// depends on the allocator and insertion history. `FieldMap` is a flat vector
// (Principle 3) kept sorted by field name, so iteration order is a pure
// function of the field-name set. The sorted position IS the field's canonical
// ordinal (Principle 2: index-based identity, not string-keyed hashing). The
// name is retained only for source-level lookup and diagnostic display.
//
// This mirrors the ordered-vector design of `MapValue`/`SetValue` below, and
// replaces the ad-hoc "collect names, std::sort, then iterate" workarounds that
// were previously scattered across value.cpp / value_json.cpp to paper over the
// map's nondeterminism.
class FieldMap {
  public:
    struct Entry {
        std::string name;
        std::unique_ptr<Value> value;
    };

    using const_iterator = std::vector<Entry>::const_iterator;
    using iterator = std::vector<Entry>::iterator;

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] iterator begin() noexcept { return entries_.begin(); }
    [[nodiscard]] iterator end() noexcept { return entries_.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return entries_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return entries_.end(); }

    // Insert or overwrite the entry for `name`, keeping entries sorted by name.
    // Mirrors the previous `unordered_map::emplace`/`operator[]=` call sites but
    // maintains the sorted invariant that guarantees deterministic iteration.
    void set(std::string name, std::unique_ptr<Value> value);

    // Lookup returning the entry iterator, or end() if absent. Preserves the
    // `find`/`end` idiom used throughout the runtime.
    [[nodiscard]] iterator find(std::string_view name);
    [[nodiscard]] const_iterator find(std::string_view name) const;

    // Value pointer for `name`, or nullptr if absent (or the entry is null).
    [[nodiscard]] Value *get(std::string_view name);
    [[nodiscard]] const Value *get(std::string_view name) const;

  private:
    [[nodiscard]] iterator lower_bound(std::string_view name);
    [[nodiscard]] const_iterator lower_bound(std::string_view name) const;

    // Sorted by `name`; sorted position is the canonical field ordinal.
    std::vector<Entry> entries_;
};

struct StructValue {
    std::string type_name;
    FieldMap fields;
};

struct ListValue {
    std::vector<std::unique_ptr<Value>> items;
};

struct EnumValue {
    std::string enum_name;
    std::string variant;
    // Positional enum payload. Unit variants keep this empty; tuple variants
    // store one entry per declared payload field.
    std::vector<std::unique_ptr<Value>> payload;
    // Named payload: RFC 0001 struct enum variants.
    FieldMap named_payload;
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

// Unit value: the sole runtime value of the `Unit` type (RFC 0013 P3-gaps-B).
// Zero-sized; its presence is its meaning.
struct UnitValue {};

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
                               SetValue,
                               MapValue,
                               UuidValue,
                               TimestampValue,
                               InterpreterClosureHandle,
                               UnitValue>;

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
    Unit,
};

// ============================================================================
// Helper functions
// ============================================================================

[[nodiscard]] ValueKind value_kind(const Value &v);

[[nodiscard]] bool is_none(const Value &v);

void print_value(const Value &v, std::ostream &out);

/// Canonical, locale-independent double→string rendering. Uses
/// std::to_chars (shortest round-trip at max_digits10 precision) so the output
/// is a pure function of the bit pattern — no locale, no ostream format flags,
/// no platform default precision. This is the single float formatter used by
/// both value_json and print_value, so a given double renders identically in
/// every artifact. RFC 0022 relies on this for deterministic replay.
///
/// When `json_mode` is true, non-finite values render as JSON `null` and an
/// integral result is suffixed with ".0" so it stays syntactically a float.
[[nodiscard]] std::string format_double(double value, bool json_mode);

/// Structural equality: two values are equal iff they have the same kind
/// and their contents compare equal.  Used by Set/Map canonicalization and
/// by builtin dispatchers for membership / lookup checks.
[[nodiscard]] bool structurally_equal(const Value &lhs, const Value &rhs);

/// Canonical strict-weak ordering: -1/0/1. Used by Set/Map canonicalization
/// to give every value a deterministic order; it is NOT semantic `<`. Every
/// kind — including the interpreter-only closure arm — has a stable
/// comparator (closures order by monotonic id, never heap address).
[[nodiscard]] int compare_values(const Value &lhs, const Value &rhs);

// ============================================================================
// Nominal accessors (preferred over spelling variant names at call sites)
// ============================================================================

// List accessors -----------------------------------------------------------
[[nodiscard]] inline bool is_list(const Value &v) {
    return std::holds_alternative<ListValue>(v.node);
}

[[nodiscard]] inline ListValue *get_list_if(Value &v) {
    return std::get_if<ListValue>(&v.node);
}

[[nodiscard]] inline const ListValue *get_list_if(const Value &v) {
    return std::get_if<ListValue>(&v.node);
}

[[nodiscard]] inline const std::vector<std::unique_ptr<Value>> *list_items(const Value &v) {
    if (const auto *lv = get_list_if(v)) {
        return &lv->items;
    }
    return nullptr;
}

[[nodiscard]] inline std::vector<std::unique_ptr<Value>> *list_items(Value &v) {
    if (auto *lv = get_list_if(v)) {
        return &lv->items;
    }
    return nullptr;
}

// Option accessors ---------------------------------------------------------
[[nodiscard]] inline bool is_optional(const Value &v) {
    const auto *value = std::get_if<EnumValue>(&v.node);
    return value != nullptr && value->enum_name == "std::option::Option";
}

[[nodiscard]] inline bool is_some(const Value &v) {
    const auto *value = std::get_if<EnumValue>(&v.node);
    return value != nullptr && value->enum_name == "std::option::Option" &&
           value->variant == "Some" && value->payload.size() == 1 &&
           value->payload.front() != nullptr;
}

[[nodiscard]] inline bool is_optional_none(const Value &v) {
    const auto *value = std::get_if<EnumValue>(&v.node);
    return value != nullptr && value->enum_name == "std::option::Option" &&
           value->variant == "None" && value->payload.empty();
}

[[nodiscard]] inline const Value *optional_inner(const Value &v) {
    const auto *value = std::get_if<EnumValue>(&v.node);
    if (value == nullptr || value->enum_name != "std::option::Option" ||
        value->variant != "Some" || value->payload.size() != 1) {
        return nullptr;
    }
    return value->payload.front().get();
}

[[nodiscard]] inline Value *optional_inner(Value &v) {
    auto *value = std::get_if<EnumValue>(&v.node);
    if (value == nullptr || value->enum_name != "std::option::Option" ||
        value->variant != "Some" || value->payload.size() != 1) {
        return nullptr;
    }
    return value->payload.front().get();
}

// ============================================================================
// Convenience constructors
// ============================================================================

[[nodiscard]] inline Value make_none() {
    return Value{NoneValue{}};
}

// RFC 0013 P3-gaps-B: the sole runtime value of Unit.
[[nodiscard]] inline Value make_unit() {
    return Value{UnitValue{}};
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
    return Value{EnumValue{
        .enum_name = std::move(enum_name),
        .variant = std::move(variant),
        .payload = {},
        .named_payload = {},
    }};
}

[[nodiscard]] Value
make_enum(std::string enum_name, std::string variant, std::vector<Value> payload);

[[nodiscard]] inline Value
make_enum(std::string enum_name, std::string variant, Value payload) {
    std::vector<Value> values;
    values.push_back(std::move(payload));
    return make_enum(std::move(enum_name), std::move(variant), std::move(values));
}

[[nodiscard]] Value
make_enum(std::string enum_name,
          std::string variant,
          std::unordered_map<std::string, Value> named_payload);

[[nodiscard]] inline Value make_option_some(Value inner) {
    std::vector<Value> payload;
    payload.push_back(std::move(inner));
    return make_enum("std::option::Option", "Some", std::move(payload));
}

[[nodiscard]] inline Value make_option_none() {
    return make_enum("std::option::Option", "None");
}

/// Wrap an evaluator-owned closure handle in a Value. The handle is opaque
/// here; only the evaluator layer constructs or unwraps it.
[[nodiscard]] Value make_interpreter_closure(InterpreterClosureRef closure);

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

} // namespace ahfl::runtime
