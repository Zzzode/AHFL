#pragma once

#include <cstddef>
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

/// Provenance of a number node's value (RFC 0026 C2b P0-10). The JSON DOM cannot
/// distinguish, from `Kind` alone, a legal `INT64_MIN` from a high-bit unsigned
/// magnitude folded into a signed slot, nor a genuine float token from an integer
/// literal too large for any 64-bit integer. Recording the parser's decision (and
/// how a value was hand-built) lets trust-boundary consumers fail closed on the
/// ambiguous cases without a raw lexeme.
///
/// The ONLY legal (Kind, NumberProvenance) combinations are:
///   * any non-numeric Kind (Null/Bool/String/Array/Object) + NotNumeric
///   * Int   + SignedInteger      (value in int_val, within [INT64_MIN, INT64_MAX])
///   * Int   + UnsignedInteger    (value in uint_val, within (INT64_MAX, UINT64_MAX])
///   * Float + FloatSyntax        (a `.`/`e`/`E` token; value in float_val)
///   * Float + IntegerFallback    (an integer token too large for int64 or uint64;
///                                  approximate value in float_val)
/// Any other pairing is invalid; numeric accessors return nullopt for it and
/// serialize_json fails closed (see json_value.cpp).
enum class NumberProvenance {
    NotNumeric,
    SignedInteger,
    UnsignedInteger,
    FloatSyntax,
    IntegerFallback,
};

/// A generic JSON DOM node (zero external dependency).
struct JsonValue {
    Kind kind{Kind::Null};

    std::size_t begin_offset{0};
    std::size_t end_offset{0};

    bool bool_val{};
    int64_t int_val{};
    uint64_t uint_val{}; // holds the value iff number_provenance == UnsignedInteger
    double float_val{};
    NumberProvenance number_provenance{NumberProvenance::NotNumeric};
    std::string string_val;
    std::vector<std::unique_ptr<JsonValue>> array_items;
    std::vector<std::pair<std::string, std::unique_ptr<JsonValue>>> object_fields;

    // ---------- Accessors ----------

    /// Lookup an object field by key. Returns nullptr if not found or not an object.
    [[nodiscard]] const JsonValue *get(std::string_view key) const;

    /// Mutable lookup.
    [[nodiscard]] JsonValue *get_mut(std::string_view key);

    [[nodiscard]] std::optional<std::string_view> as_string() const;
    /// A signed 64-bit integer. Returns nullopt for an UnsignedInteger (use
    /// as_uint), an IntegerFallback, or a non-integer node.
    [[nodiscard]] std::optional<int64_t> as_int() const;
    /// A full 64-bit unsigned magnitude. Accepts an UnsignedInteger node and a
    /// non-negative SignedInteger node; returns nullopt for a true-negative
    /// SignedInteger, an IntegerFallback, or a non-integer node.
    [[nodiscard]] std::optional<uint64_t> as_uint() const;
    /// A double. Widens SignedInteger/UnsignedInteger and returns the (approximate)
    /// float_val for FloatSyntax/IntegerFallback. A codec that must reject an
    /// integer-token fallback has to inspect number_provenance directly.
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

/// The maximum `[` / `{` nesting depth `parse_json` will descend to. The parser
/// is recursive descent, so this is an untrusted-input admission bound: without
/// it a whitespace-cheap document of ~10^5 nested brackets overflows the native
/// stack before any consumer runs (e.g. the Core-IR reader's own region depth
/// bound, which only sees the tree AFTER the parse).
///
/// This is a STACK-SAFETY bound and is deliberately looser than the semantic
/// bounds a consumer applies to the parsed tree. In particular the Core-IR
/// reader's `kMaxRegionNestingDepth` counts *region* nesting, and one region
/// level in the wire shape costs ~3 bracket levels, so this constant must stay
/// above `3 * kMaxRegionNestingDepth` for that reader's own diagnostic to remain
/// reachable on region-shaped input. It is still far below the depth at which
/// the parse+read walk overflows an 8 MiB stack.
inline constexpr std::size_t kMaxJsonNestingDepth = 4096;

/// Parse a JSON string into a JsonValue tree. Returns nullopt on parse error,
/// including a document nested deeper than `kMaxJsonNestingDepth`.
[[nodiscard]] std::optional<std::unique_ptr<JsonValue>> parse_json(std::string_view input);

/// Serialize a JsonValue tree to a compact JSON string.
[[nodiscard]] std::string serialize_json(const JsonValue &v);

/// Canonical, locale-independent wire spelling of a finite double. Uses
/// std::to_chars shortest round-trip at max_digits10, then guarantees the
/// result stays syntactically a JSON float: an integral result is suffixed
/// with ".0" (so 1.0 emits "1.0", never the bare integer "1"). This is the
/// single float renderer shared by the runtime `value_to_json` path and the
/// conformance-case canonicality gate, so a given double has exactly one
/// canonical wire spelling everywhere. Non-finite values are not representable
/// in JSON; callers must handle them before calling (the evaluator maps them
/// to "null" / "NaN" / "Infinity").
[[nodiscard]] std::string format_wire_float(double value);

} // namespace ahfl::json
