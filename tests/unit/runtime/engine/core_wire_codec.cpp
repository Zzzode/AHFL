#include "runtime/engine/core_wire_codec.hpp"

#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/json/json_value.hpp"
#include "runtime/evaluator/builtins.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/scalar_spelling.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// RFC 0026 KR6.5 E4-B0-C2b focused cross-policy matrix for the shared wire codec:
// ONE schema-guided semantics under two DIRECT policies (decode_json /
// validate_value) consumed via a VerifiedWireSchemaBinding. Coverage:
//   * scalar_spelling SSOT: both Decimal families + both Duration families, and
//     the tightenings vs the old parsers (sign, leading zero, -0, overflow);
//   * decode_json + validate_value per shape, positive and fail-closed negatives;
//   * trust-boundary invariants: finite-only Float (no int widening), exact
//     Uuid/Timestamp objects + strict hex, exact Struct/Enum forms (dup/missing/
//     extra/null/wrong payload-kind/arity), Option exactness incl. bare-None
//     rejection, canonical Set/Map incl. a nested-null crash negative control;
//   * INT32 scale narrowing-bypass negative control (P0-5);
//   * per-shape value_json encode -> decode_json round-trip;
//   * real affected builtin callers (timestamp_add/sub m/h + overflow + non-
//     canonical rejects; decimal builtins reject +/leading-zero/-0).

namespace {

using namespace ahfl;
using namespace ahfl::ir::core;
using ahfl::runtime::wire_codec::decode_json;
using ahfl::runtime::wire_codec::validate_value;
using ahfl::runtime::wire_codec::WireDecodeResult;
using ahfl::runtime::SchemaValidationResult;
using evaluator::Value;

int test_count = 0;
int fail_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (!condition) {
        ++fail_count;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// --- table / binding builders --------------------------------------------

[[nodiscard]] std::optional<VerifiedWireSchemaBinding>
mint_result_binding(std::vector<CoreWireSchemaNode> nodes, CoreWireSchemaNodeId result) {
    CoreWireSchemaTable table;
    table.nodes = std::move(nodes);
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.source_symbol = 42;
    cap.result = result;
    table.capabilities.push_back(cap);

    CoreWireRootSelector selector;
    selector.capability = CoreCapabilityId{0};
    selector.expected_source_symbol = 42;
    selector.kind = CoreWireRootKind::Result;
    selector.param_index = 0;

    std::vector<CoreLowerDiagnostic> diagnostics;
    return make_wire_binding_from_transported_table(std::move(table), selector, diagnostics);
}

[[nodiscard]] std::optional<VerifiedWireSchemaBinding> mint_leaf(CoreWireSchemaShape shape) {
    std::vector<CoreWireSchemaNode> nodes;
    nodes.push_back(CoreWireSchemaNode{std::move(shape)});
    return mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{0});
}

[[nodiscard]] std::unique_ptr<json::JsonValue> parse(const std::string &text) {
    auto parsed = json::parse_json(text);
    return parsed.has_value() ? std::move(*parsed) : nullptr;
}

// Direct native constructors that bypass normalizing helpers, for hostile inputs.
[[nodiscard]] Value raw_uuid(std::string hex) {
    return Value{evaluator::UuidValue{std::move(hex)}};
}

[[nodiscard]] Value make_ptr_list(std::vector<Value> items) {
    return evaluator::make_list(std::move(items));
}

// Value is move-only (holds unique_ptrs), so brace-init of a std::vector<Value>
// or a std::unordered_map<std::string, Value> would try to copy. These variadic
// helpers move each element in instead.
template <typename... Vs>
[[nodiscard]] std::vector<Value> vals(Vs &&...vs) {
    std::vector<Value> out;
    out.reserve(sizeof...(vs));
    (out.push_back(std::forward<Vs>(vs)), ...);
    return out;
}

// One named field for make_struct / make_enum(struct payload), moving the value.
[[nodiscard]] std::pair<std::string, Value> field(std::string name, Value value) {
    return {std::move(name), std::move(value)};
}

// Assemble a field map from moved (name, value) pairs. Variadic so no
// initializer_list copy of the move-only Value occurs.
template <typename... Ps>
[[nodiscard]] std::unordered_map<std::string, Value> fields(Ps &&...ps) {
    std::unordered_map<std::string, Value> out;
    (out.emplace(std::forward<Ps>(ps).first, std::move(std::forward<Ps>(ps).second)), ...);
    return out;
}

// One key/value entry for make_map, moving both halves.
[[nodiscard]] std::vector<std::pair<Value, Value>> one_entry(Value key, Value value) {
    std::vector<std::pair<Value, Value>> out;
    out.emplace_back(std::move(key), std::move(value));
    return out;
}

// =========================================================================
// scalar_spelling SSOT (C2b-1)
// =========================================================================
void test_scalar_spelling() {
    using namespace ahfl::evaluator::scalar_spelling;

    // Decimal family A (source literal).
    {
        const auto a = parse_decimal("1.23");
        check(a.has_value() && a->family == DecimalFamily::SourceLiteral && a->parts.scale == 2,
              "decimal A 1.23 scale 2");
    }
    check(parse_decimal("007.50").has_value(), "decimal A leading zeros ok");
    check(parse_decimal("3.14d").has_value(), "decimal A trailing d ok");
    check(!parse_decimal("+1.23").has_value(), "decimal A reject +");
    check(!parse_decimal("-1.23").has_value(), "decimal A reject -");
    check(!parse_decimal("123").has_value(), "decimal A reject no dot");
    check(!parse_decimal("1.").has_value(), "decimal A reject empty frac");

    // Decimal family B (builtin canonical): spell(parse(x))==x.
    {
        const auto b = parse_decimal("s2:123");
        check(b.has_value() && b->family == DecimalFamily::BuiltinCanonical &&
                  b->parts.mantissa == 123 && b->parts.scale == 2,
              "decimal B s2:123");
    }
    check(parse_decimal("s0:-45").has_value(), "decimal B negative mantissa");
    check(parse_decimal("s-3:7").has_value(), "decimal B negative scale");
    check(!parse_decimal("s+2:3").has_value(), "decimal B reject +scale");
    check(!parse_decimal("s2:+3").has_value(), "decimal B reject +mantissa");
    check(!parse_decimal("s02:3").has_value(), "decimal B reject leading-zero scale");
    check(!parse_decimal("s2:03").has_value(), "decimal B reject leading-zero mantissa");
    check(!parse_decimal("s-0:3").has_value(), "decimal B reject -0 scale");
    check(!parse_decimal("s2:-0").has_value(), "decimal B reject -0 mantissa");

    // Duration source-unit family. Parse once, then guard before dereferencing.
    {
        const auto ms = parse_duration("100ms");
        check(ms.has_value() && ms->millis == 100, "duration 100ms");
    }
    {
        const auto s = parse_duration("5s");
        check(s.has_value() && s->millis == 5000, "duration 5s");
    }
    {
        const auto m = parse_duration("2m");
        check(m.has_value() && m->millis == 120000, "duration 2m (m fix)");
    }
    {
        const auto h = parse_duration("1h");
        check(h.has_value() && h->millis == 3600000, "duration 1h (h fix)");
    }
    check(parse_duration("005s").has_value(), "duration leading zeros ok");
    check(!parse_duration("+5s").has_value(), "duration reject +unit");
    check(!parse_duration("-5s").has_value(), "duration reject -unit");
    // Duration bare-ms family.
    {
        const auto d = parse_duration("1500");
        check(d.has_value() && d->family == DurationFamily::BareMillis && d->millis == 1500,
              "duration bare-ms 1500");
    }
    check(parse_duration("-500").has_value(), "duration bare-ms negative ok");
    check(!parse_duration("+500").has_value(), "duration bare-ms reject +");
    check(!parse_duration("0500").has_value(), "duration bare-ms reject leading zero");
    check(!parse_duration("-0").has_value(), "duration bare-ms reject -0");
    check(!parse_duration("9999999999999999999h").has_value(), "duration overflow reject");
}

// =========================================================================
// decode_json + validate_value — scalars
// =========================================================================
void test_scalars() {
    { // Unit
        auto b = mint_leaf(CoreWireSchemaUnit{});
        check(b && decode_json(*parse("null"), *b).ok(), "decode unit null");
        check(b && !decode_json(*parse("1"), *b).ok(), "decode unit rejects int");
        check(b && validate_value(evaluator::make_unit(), *b).valid, "validate unit");
        check(b && !validate_value(evaluator::make_int(1), *b).valid, "validate unit rejects int");
    }
    { // Bool
        auto b = mint_leaf(CoreWireSchemaBool{});
        check(b && decode_json(*parse("true"), *b).ok(), "decode bool");
        check(b && !decode_json(*parse("1"), *b).ok(), "decode bool rejects int");
        check(b && validate_value(evaluator::make_bool(true), *b).valid, "validate bool");
    }
    { // Int + bounds
        CoreWireSchemaInt s;
        s.bounds = std::make_pair<std::int64_t, std::int64_t>(0, 100);
        auto b = mint_leaf(s);
        check(b && decode_json(*parse("50"), *b).ok(), "decode int in bounds");
        check(b && !decode_json(*parse("101"), *b).ok(), "decode int out of bounds");
        check(b && !decode_json(*parse("1.5"), *b).ok(), "decode int rejects float");
        check(b && validate_value(evaluator::make_int(50), *b).valid, "validate int in bounds");
        check(b && !validate_value(evaluator::make_int(200), *b).valid, "validate int out of bounds");
    }
    { // Int provenance gate (P0-10): hostile numbers fail closed on the SignedInteger
      // gate, NOT merely on bounds. Use unbounded Int so the reject is provenance-
      // driven (a high-bit unsigned or an out-of-range integer token the base DOM
      // classes as UnsignedInteger / IntegerFallback -- never as_int()).
        auto b = mint_leaf(CoreWireSchemaInt{});
        check(b && decode_json(*parse("-9223372036854775808"), *b).ok(),
              "decode int INT64_MIN (signed) ok");
        check(b && decode_json(*parse("9223372036854775807"), *b).ok(),
              "decode int INT64_MAX (signed) ok");
        check(b && !decode_json(*parse("18446744073709551615"), *b).ok(),
              "decode int rejects high-uint (unsigned provenance)");
        check(b && !decode_json(*parse("99999999999999999999999"), *b).ok(),
              "decode int rejects positive integer-fallback");
        check(b && !decode_json(*parse("-99999999999999999999999"), *b).ok(),
              "decode int rejects negative integer-fallback");
    }
    { // Float finite-only, no int widening
        auto b = mint_leaf(CoreWireSchemaFloat{});
        check(b && decode_json(*parse("1.5"), *b).ok(), "decode float");
        check(b && !decode_json(*parse("3"), *b).ok(), "decode float rejects int (no widening)");
        auto inf = json::JsonValue::make_float(std::numeric_limits<double>::infinity());
        check(b && !decode_json(*inf, *b).ok(), "decode float rejects Inf");
        auto nan = json::JsonValue::make_float(std::numeric_limits<double>::quiet_NaN());
        check(b && !decode_json(*nan, *b).ok(), "decode float rejects NaN");
        // P0-10: an IntegerFallback (an out-of-uint64 integer token the DOM stores as
        // an approximate Float) must NOT be laundered through the float branch, and
        // neither may a high-bit unsigned (Kind::Int, UnsignedInteger provenance).
        // Only genuine FloatSyntax is accepted, in either sign.
        check(b && !decode_json(*parse("18446744073709551615"), *b).ok(),
              "decode float rejects high-uint (no widening)");
        check(b && !decode_json(*parse("99999999999999999999999"), *b).ok(),
              "decode float rejects positive integer-fallback");
        check(b && !decode_json(*parse("-99999999999999999999999"), *b).ok(),
              "decode float rejects negative integer-fallback");
        check(b && decode_json(*parse("1e3"), *b).ok(), "decode float accepts exponent syntax");
        check(b && validate_value(evaluator::make_float(1.5), *b).valid, "validate finite float");
        check(b && !validate_value(evaluator::make_float(std::numeric_limits<double>::quiet_NaN()),
                                   *b)
                        .valid,
              "validate rejects NaN");
        check(b && !validate_value(evaluator::make_float(std::numeric_limits<double>::infinity()),
                                   *b)
                        .valid,
              "validate rejects Inf");
        check(b && !validate_value(evaluator::make_int(3), *b).valid,
              "validate float rejects int (no widening)");
    }
    { // String length bounds
        CoreWireSchemaString s;
        s.length_bounds = std::make_pair<std::int64_t, std::int64_t>(1, 3);
        auto b = mint_leaf(s);
        check(b && decode_json(*parse("\"hi\""), *b).ok(), "decode string in bounds");
        check(b && !decode_json(*parse("\"toolong\""), *b).ok(), "decode string out of bounds");
        check(b && validate_value(evaluator::make_string("ok"), *b).valid, "validate string");
        check(b && !validate_value(evaluator::make_string("toolong"), *b).valid,
              "validate string out of bounds");
    }
}

void test_decimal_duration() {
    { // Decimal both families, scale check, spelling preserved, no widening
        auto b = mint_leaf(CoreWireSchemaDecimal{2});
        check(b.has_value(), "decimal binding minted");
        if (b) {
            auto ra = decode_json(*parse("\"1.23\""), *b);
            check(ra.ok() && std::get<evaluator::DecimalValue>(ra.value->node).spelling == "1.23",
                  "decode decimal A preserves spelling");
            auto rb = decode_json(*parse("\"s2:123\""), *b);
            check(rb.ok() && std::get<evaluator::DecimalValue>(rb.value->node).spelling == "s2:123",
                  "decode decimal B preserves spelling");
            check(!decode_json(*parse("\"1.2\""), *b).ok(), "decode decimal scale mismatch");
            check(!decode_json(*parse("123"), *b).ok(), "decode decimal rejects int");
            check(!decode_json(*parse("1.23"), *b).ok(), "decode decimal rejects float");
            check(validate_value(evaluator::make_decimal("1.23"), *b).valid, "validate decimal A");
            check(!validate_value(evaluator::make_decimal("1.2"), *b).valid,
                  "validate decimal scale mismatch");
            check(!validate_value(evaluator::make_int(1), *b).valid, "validate decimal rejects int");
            // i64 mantissa overflow, one per family (scale 2 so fraction digits match).
            // Family A: 21 integer digits + 2 fraction digits -> 23-digit mantissa > INT64_MAX.
            check(!decode_json(*parse("\"992233720368547758080.99\""), *b).ok(),
                  "decode decimal A rejects mantissa overflow");
            // Family B: explicit mantissa beyond INT64_MAX.
            check(!decode_json(*parse("\"s2:99999999999999999999\""), *b).ok(),
                  "decode decimal B rejects mantissa overflow");
        }
    }
    { // P0-5: schema.scale = INT32_MAX + 1. The spelling "s-2147483648:1" parses
      // to a PERFECTLY LEGAL scale of INT32_MIN; the real proof is that the
      // i64-widened compare of INT32_MIN against schema.scale (INT32_MAX + 1) is
      // unequal, so it must be rejected — NOT because the parser refuses the
      // scale. If the compare narrowed schema.scale to int32 it would wrap to
      // INT32_MIN and wrongly accept. Both JSON decode and native validate reject.
        const std::int64_t big =
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1;
        auto b = mint_leaf(CoreWireSchemaDecimal{big});
        check(b.has_value(), "INT32-scale decimal binding minted");
        if (b) {
            check(!decode_json(*parse("\"s-2147483648:1\""), *b).ok(),
                  "decode decimal INT32 narrowing bypass rejected");
            check(!validate_value(evaluator::make_decimal("s-2147483648:1"), *b).valid,
                  "validate decimal INT32 narrowing bypass rejected");
        }
    }
    { // Duration source-unit + bare-ms, spelling preserved
        auto b = mint_leaf(CoreWireSchemaDuration{});
        check(b.has_value(), "duration binding minted");
        if (b) {
            auto r = decode_json(*parse("\"5s\""), *b);
            check(r.ok() && std::get<evaluator::DurationValue>(r.value->node).spelling == "5s",
                  "decode duration source-unit preserves spelling");
            check(decode_json(*parse("\"1500\""), *b).ok(), "decode duration bare-ms");
            check(decode_json(*parse("\"-500\""), *b).ok(), "decode duration negative bare-ms");
            check(!decode_json(*parse("\"+5s\""), *b).ok(), "decode duration rejects +unit");
            check(!decode_json(*parse("500"), *b).ok(), "decode duration rejects int");
            check(validate_value(evaluator::make_duration("2m"), *b).valid, "validate duration m");
            check(!validate_value(evaluator::make_duration("+5s"), *b).valid,
                  "validate duration rejects +unit");
        }
    }
}

void test_uuid_timestamp() {
    { // UUID exact object + strict hex
        auto b = mint_leaf(CoreWireSchemaUuid{});
        check(b && decode_json(*parse("{\"_uuid\":\"0123456789abcdef0123456789abcdef\"}"), *b).ok(),
              "decode uuid exact object");
        check(b && !decode_json(*parse("\"0123456789abcdef0123456789abcdef\""), *b).ok(),
              "decode uuid rejects bare string");
        check(b && !decode_json(*parse("{\"_uuid\":\"0123456789ABCDEF0123456789ABCDEF\"}"), *b).ok(),
              "decode uuid rejects uppercase");
        check(b && !decode_json(*parse("{\"_uuid\":\"0123-4567\"}"), *b).ok(),
              "decode uuid rejects dashed/short");
        check(b && !decode_json(*parse("{\"_uuid\":\"abc\",\"x\":1}"), *b).ok(),
              "decode uuid rejects extra field");
        // native: strict hex gate on a hand-built UuidValue (bypass make_uuid).
        check(b && validate_value(raw_uuid("0123456789abcdef0123456789abcdef"), *b).valid,
              "validate uuid canonical");
        check(b && !validate_value(raw_uuid("0123456789ABCDEF0123456789ABCDEF"), *b).valid,
              "validate uuid rejects uppercase");
        check(b && !validate_value(raw_uuid("0123-4567"), *b).valid,
              "validate uuid rejects dashed/short");
    }
    { // Timestamp exact object
        auto b = mint_leaf(CoreWireSchemaTimestamp{});
        check(b && decode_json(*parse("{\"_timestamp\":1000}"), *b).ok(), "decode timestamp object");
        check(b && !decode_json(*parse("1000"), *b).ok(), "decode timestamp rejects bare int");
        check(b && !decode_json(*parse("{\"_timestamp\":\"x\"}"), *b).ok(),
              "decode timestamp rejects non-int");
        check(b && !decode_json(*parse("{\"_timestamp\":1,\"x\":2}"), *b).ok(),
              "decode timestamp rejects extra field");
        // P0-10: the _timestamp field goes through the same SignedInteger gate as a
        // plain Int -- a high-bit unsigned or an out-of-range integer token fails
        // closed rather than degrading.
        check(b && !decode_json(*parse("{\"_timestamp\":18446744073709551615}"), *b).ok(),
              "decode timestamp rejects high-uint");
        check(b && !decode_json(*parse("{\"_timestamp\":99999999999999999999999}"), *b).ok(),
              "decode timestamp rejects positive integer-fallback");
        check(b && !decode_json(*parse("{\"_timestamp\":-99999999999999999999999}"), *b).ok(),
              "decode timestamp rejects negative integer-fallback");
        check(b && validate_value(evaluator::make_timestamp(5), *b).valid, "validate timestamp");
    }
}

void test_option() {
    std::vector<CoreWireSchemaNode> nodes;
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}});
    auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{1});
    check(b.has_value(), "option binding minted");
    if (!b) {
        return;
    }
    // decode: compact form
    auto none = decode_json(*parse("null"), *b);
    check(none.ok() && evaluator::is_optional_none(*none.value), "decode option null->None");
    auto some = decode_json(*parse("99"), *b);
    check(some.ok() && evaluator::is_some(*some.value), "decode option 99->Some");
    check(!decode_json(*parse("1.5"), *b).ok(), "decode option Some child type-checked");
    // validate: exact Option only
    check(validate_value(evaluator::make_option_none(), *b).valid, "validate option None");
    check(validate_value(evaluator::make_option_some(evaluator::make_int(1)), *b).valid,
          "validate option Some");
    check(!validate_value(evaluator::make_none(), *b).valid,
          "validate rejects bare NoneValue as Option");
    // Some with wrong child type.
    check(!validate_value(evaluator::make_option_some(evaluator::make_bool(true)), *b).valid,
          "validate option Some wrong child type");
    // None with a non-empty named_payload (hand-built) must be rejected.
    {
        evaluator::EnumValue ev;
        ev.enum_name = "std::option::Option";
        ev.variant = "None";
        ev.named_payload.set("x", std::make_unique<Value>(evaluator::make_int(1)));
        check(!validate_value(Value{std::move(ev)}, *b).valid,
              "validate option None with payload rejected");
    }
    // Some with arity 2 (hand-built) must be rejected.
    {
        evaluator::EnumValue ev;
        ev.enum_name = "std::option::Option";
        ev.variant = "Some";
        ev.payload.push_back(std::make_unique<Value>(evaluator::make_int(1)));
        ev.payload.push_back(std::make_unique<Value>(evaluator::make_int(2)));
        check(!validate_value(Value{std::move(ev)}, *b).valid,
              "validate option Some wrong arity rejected");
    }
    // Some with a NULL payload element (hand-built) must be rejected, not crash.
    {
        evaluator::EnumValue ev;
        ev.enum_name = "std::option::Option";
        ev.variant = "Some";
        ev.payload.push_back(std::unique_ptr<Value>{});
        check(!validate_value(Value{std::move(ev)}, *b).valid,
              "validate option Some null payload rejected");
    }
    // Some carrying a non-empty named_payload (hand-built) must be rejected.
    {
        evaluator::EnumValue ev;
        ev.enum_name = "std::option::Option";
        ev.variant = "Some";
        ev.payload.push_back(std::make_unique<Value>(evaluator::make_int(1)));
        ev.named_payload.set("x", std::make_unique<Value>(evaluator::make_int(2)));
        check(!validate_value(Value{std::move(ev)}, *b).valid,
              "validate option Some with named_payload rejected");
    }
}

void test_set_map() {
    { // Set<Int>
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaSequence seq;
        seq.kind = CoreWireSequenceKind::Set;
        seq.element = CoreWireSchemaNodeId{0};
        seq.capacity = std::optional<std::uint64_t>{3};
        nodes.push_back(CoreWireSchemaNode{seq});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{1});
        check(b && decode_json(*parse("[1,2,3]"), *b).ok(), "decode set ok");
        check(b && !decode_json(*parse("[1,2,2]"), *b).ok(), "decode set rejects dup");
        // Unsorted JSON input is accepted and constructed canonical; the decoded
        // Set must then pass validate_value under the same binding (guards against
        // ever tightening decode to require a pre-sorted array).
        if (b) {
            auto decoded = decode_json(*parse("[2,1]"), *b);
            check(decoded.ok(), "decode set accepts unsorted input");
            if (decoded.ok()) {
                check(validate_value(*decoded.value, *b).valid,
                      "decoded unsorted set is canonical");
            }
        }
        check(b && !decode_json(*parse("[1,2,3,4]"), *b).ok(), "decode set capacity");
        check(b && !decode_json(*parse("{\"_type\":\"x\"}"), *b).ok(), "decode set rejects object");
        // native: canonical accepted; List instead of Set rejected.
        check(b && validate_value(evaluator::make_set(vals(evaluator::make_int(1),
                                                          evaluator::make_int(2))),
                                  *b)
                        .valid,
              "validate set canonical");
        check(b && !validate_value(make_ptr_list(vals(evaluator::make_int(1))), *b).valid,
              "validate set rejects List");
        // non-canonical (unordered) Set built by hand.
        {
            evaluator::SetValue sv;
            sv.items.push_back(std::make_unique<Value>(evaluator::make_int(2)));
            sv.items.push_back(std::make_unique<Value>(evaluator::make_int(1)));
            check(b && !validate_value(Value{std::move(sv)}, *b).valid,
                  "validate set rejects unordered");
        }
        // native Set with a structurally-equal duplicate.
        {
            evaluator::SetValue sv;
            sv.items.push_back(std::make_unique<Value>(evaluator::make_int(1)));
            sv.items.push_back(std::make_unique<Value>(evaluator::make_int(1)));
            check(b && !validate_value(Value{std::move(sv)}, *b).valid,
                  "validate set rejects duplicate");
        }
        // native Set with a NULL element must fail-closed (not crash).
        {
            evaluator::SetValue sv;
            sv.items.push_back(std::unique_ptr<Value>{});
            check(b && !validate_value(Value{std::move(sv)}, *b).valid,
                  "validate set rejects null element");
        }
        // native Set over schema capacity (schema cap = 3).
        {
            evaluator::SetValue sv;
            for (int i = 0; i < 4; ++i) {
                sv.items.push_back(std::make_unique<Value>(evaluator::make_int(i)));
            }
            check(b && !validate_value(Value{std::move(sv)}, *b).valid,
                  "validate set rejects over-capacity");
        }
    }
    { // List<Int> vs Set differentiation + capacity
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaSequence seq;
        seq.kind = CoreWireSequenceKind::List;
        seq.element = CoreWireSchemaNodeId{0};
        seq.capacity = std::optional<std::uint64_t>{2};
        nodes.push_back(CoreWireSchemaNode{seq});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{1});
        check(b && decode_json(*parse("[1,1]"), *b).ok(), "decode list allows dup");
        check(b && !decode_json(*parse("[1,2,3]"), *b).ok(), "decode list capacity");
        check(b && validate_value(make_ptr_list(vals(evaluator::make_int(1))), *b).valid,
              "validate list");
        check(b && !validate_value(evaluator::make_set(vals(evaluator::make_int(1))), *b).valid,
              "validate list rejects Set");
        // native List over schema capacity (schema cap = 2).
        check(b && !validate_value(make_ptr_list(vals(evaluator::make_int(1),
                                                      evaluator::make_int(2),
                                                      evaluator::make_int(3))),
                                   *b)
                        .valid,
              "validate list rejects over-capacity");
        // native List with a NULL element.
        {
            evaluator::ListValue lv;
            lv.items.push_back(std::unique_ptr<Value>{});
            check(b && !validate_value(Value{std::move(lv)}, *b).valid,
                  "validate list rejects null element");
        }
    }
    { // Map<String,Int>
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}});
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaMap m;
        m.key = CoreWireSchemaNodeId{0};
        m.value = CoreWireSchemaNodeId{1};
        nodes.push_back(CoreWireSchemaNode{m});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{2});
        check(b && decode_json(*parse("{\"a\":1,\"b\":2}"), *b).ok(), "decode map ok");
        // Unsorted JSON object keys are accepted and constructed canonical; the
        // decoded Map must pass validate_value under the same binding.
        if (b) {
            auto decoded = decode_json(*parse("{\"b\":2,\"a\":1}"), *b);
            check(decoded.ok(), "decode map accepts unsorted keys");
            if (decoded.ok()) {
                check(validate_value(*decoded.value, *b).valid,
                      "decoded unsorted map is canonical");
            }
        }
        // hand-built duplicate-key DOM
        {
            auto dup = json::JsonValue::make_object();
            dup->set("a", json::JsonValue::make_int(1));
            dup->object_fields.emplace_back("a", json::JsonValue::make_int(2));
            check(b && !decode_json(*dup, *b).ok(), "decode map rejects dup key");
        }
        // native canonical vs unordered vs wrong key variant vs null.
        check(b && validate_value(evaluator::make_map(one_entry(evaluator::make_string("a"),
                                                                evaluator::make_int(1))),
                                  *b)
                        .valid,
              "validate map canonical");
        {
            evaluator::MapValue mv; // unordered keys
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("b")),
                                    std::make_unique<Value>(evaluator::make_int(2)));
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("a")),
                                    std::make_unique<Value>(evaluator::make_int(1)));
            check(b && !validate_value(Value{std::move(mv)}, *b).valid,
                  "validate map rejects unordered");
        }
        {
            evaluator::MapValue mv; // wrong key variant (int key)
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_int(1)),
                                    std::make_unique<Value>(evaluator::make_int(1)));
            check(b && !validate_value(Value{std::move(mv)}, *b).valid,
                  "validate map rejects non-string key");
        }
        {
            evaluator::MapValue mv; // duplicate key
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("a")),
                                    std::make_unique<Value>(evaluator::make_int(1)));
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("a")),
                                    std::make_unique<Value>(evaluator::make_int(2)));
            check(b && !validate_value(Value{std::move(mv)}, *b).valid,
                  "validate map rejects duplicate key");
        }
        {
            evaluator::MapValue mv; // null key
            mv.entries.emplace_back(std::unique_ptr<Value>{},
                                    std::make_unique<Value>(evaluator::make_int(1)));
            check(b && !validate_value(Value{std::move(mv)}, *b).valid,
                  "validate map rejects null key");
        }
        {
            evaluator::MapValue mv; // null value
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("a")),
                                    std::unique_ptr<Value>{});
            check(b && !validate_value(Value{std::move(mv)}, *b).valid,
                  "validate map rejects null value");
        }
    }
    { // Map<BoundedString(1,3), Int>: the key inherits String length_bounds; an
      // over-long key is rejected on JSON decode AND native validate.
        std::vector<CoreWireSchemaNode> nodes;
        CoreWireSchemaString key_schema;
        key_schema.length_bounds = std::make_pair<std::int64_t, std::int64_t>(1, 3);
        nodes.push_back(CoreWireSchemaNode{key_schema});
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaMap m;
        m.key = CoreWireSchemaNodeId{0};
        m.value = CoreWireSchemaNodeId{1};
        nodes.push_back(CoreWireSchemaNode{m});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{2});
        check(b && decode_json(*parse("{\"ok\":1}"), *b).ok(), "decode map key within bounds");
        check(b && !decode_json(*parse("{\"toolong\":1}"), *b).ok(),
              "decode map rejects over-long key");
        check(b && validate_value(evaluator::make_map(one_entry(evaluator::make_string("ok"),
                                                                evaluator::make_int(1))),
                                  *b)
                        .valid,
              "validate map key within bounds");
        check(b && !validate_value(evaluator::make_map(one_entry(evaluator::make_string("toolong"),
                                                                 evaluator::make_int(1))),
                                   *b)
                        .valid,
              "validate map rejects over-long key");
    }
    { // Map<String,Int> with capacity=1: decode + native both enforce it.
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}});
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaMap m;
        m.key = CoreWireSchemaNodeId{0};
        m.value = CoreWireSchemaNodeId{1};
        m.capacity = std::optional<std::uint64_t>{1};
        nodes.push_back(CoreWireSchemaNode{m});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{2});
        check(b && decode_json(*parse("{\"a\":1}"), *b).ok(), "decode map within capacity");
        check(b && !decode_json(*parse("{\"a\":1,\"b\":2}"), *b).ok(), "decode map over capacity");
        check(b && validate_value(evaluator::make_map(one_entry(evaluator::make_string("a"),
                                                                evaluator::make_int(1))),
                                  *b)
                        .valid,
              "validate map within capacity");
        {
            evaluator::MapValue mv;
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("a")),
                                    std::make_unique<Value>(evaluator::make_int(1)));
            mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("b")),
                                    std::make_unique<Value>(evaluator::make_int(2)));
            check(b && !validate_value(Value{std::move(mv)}, *b).valid,
                  "validate map rejects over-capacity");
        }
    }
    { // P0-7: nested malformed Map-in-Set with a NULL map value must fail-closed,
      // not crash. Set<Map<String,Int>>.
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 0 key
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 1 val
        CoreWireSchemaMap m;
        m.key = CoreWireSchemaNodeId{0};
        m.value = CoreWireSchemaNodeId{1};
        nodes.push_back(CoreWireSchemaNode{m}); // 2 map
        CoreWireSchemaSequence seq;
        seq.kind = CoreWireSequenceKind::Set;
        seq.element = CoreWireSchemaNodeId{2};
        nodes.push_back(CoreWireSchemaNode{seq}); // 3 set
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{3});
        // Build a Set containing a Map whose single value is a NULL unique_ptr.
        evaluator::MapValue mv;
        mv.entries.emplace_back(std::make_unique<Value>(evaluator::make_string("k")),
                                std::unique_ptr<Value>{}); // NULL value
        evaluator::SetValue sv;
        sv.items.push_back(std::make_unique<Value>(Value{std::move(mv)}));
        check(b && !validate_value(Value{std::move(sv)}, *b).valid,
              "validate nested null Map-in-Set fail-closed (no crash)");
    }
}

void test_struct_enum() {
    { // Struct{n:Int}
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaStruct st;
        st.wire_name = "app::Point";
        st.fields.push_back(CoreWireSchemaField{"n", CoreWireSchemaNodeId{0}});
        nodes.push_back(CoreWireSchemaNode{st});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{1});
        check(b && decode_json(*parse("{\"_type\":\"app::Point\",\"n\":5}"), *b).ok(),
              "decode struct exact");
        check(b && !decode_json(*parse("{\"n\":5}"), *b).ok(), "decode struct missing _type");
        check(b && !decode_json(*parse("{\"_type\":\"app::Point\",\"n\":5,\"x\":1}"), *b).ok(),
              "decode struct extra field");
        check(b && !decode_json(*parse("{\"_type\":\"other\",\"n\":5}"), *b).ok(),
              "decode struct wrong _type");
        check(b && !decode_json(*parse("{\"_type\":\"app::Point\",\"n\":true}"), *b).ok(),
              "decode struct wrong child type");
        // hand-built duplicate-field DOM
        {
            auto dup = json::JsonValue::make_object();
            dup->set("_type", json::JsonValue::make_string("app::Point"));
            dup->set("n", json::JsonValue::make_int(1));
            dup->object_fields.emplace_back("n", json::JsonValue::make_int(2));
            check(b && !decode_json(*dup, *b).ok(), "decode struct rejects dup field");
        }
        // native: exact type + field set + non-null.
        check(b && validate_value(evaluator::make_struct(
                                      "app::Point", fields(field("n", evaluator::make_int(5)))),
                                  *b)
                        .valid,
              "validate struct exact");
        check(b && !validate_value(
                       evaluator::make_struct("wrong", fields(field("n", evaluator::make_int(5)))), *b)
                        .valid,
              "validate struct wrong type_name");
        check(b && !validate_value(evaluator::make_struct(
                                       "app::Point",
                                       fields(field("n", evaluator::make_int(5)),
                                              field("x", evaluator::make_int(1)))),
                                   *b)
                        .valid,
              "validate struct extra field");
        // native struct missing the declared field (empty field map).
        check(b && !validate_value(evaluator::make_struct("app::Point", fields()), *b).valid,
              "validate struct rejects missing field");
        // native struct with a NULL field value (hand-built).
        {
            evaluator::StructValue sv;
            sv.type_name = "app::Point";
            sv.fields.set("n", std::unique_ptr<Value>{});
            check(b && !validate_value(Value{std::move(sv)}, *b).valid,
                  "validate struct rejects null child");
        }
        // native struct with a wrong-typed field.
        check(b && !validate_value(evaluator::make_struct(
                                       "app::Point", fields(field("n", evaluator::make_bool(true)))),
                                   *b)
                        .valid,
              "validate struct rejects wrong child type");
    }
    { // Enum with Unit, Tuple, Struct variants
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 1
        CoreWireSchemaEnum en;
        en.wire_name = "app::Shape";
        en.variants.push_back(CoreWireSchemaVariant{"Dot", CoreWirePayloadKind::Unit, {}});
        CoreWireSchemaVariant line;
        line.wire_name = "Line";
        line.payload_kind = CoreWirePayloadKind::Tuple;
        line.slots.push_back(CoreWireSchemaField{"", CoreWireSchemaNodeId{0}}); // tuple slots are unnamed
        en.variants.push_back(line);
        CoreWireSchemaVariant tag;
        tag.wire_name = "Tag";
        tag.payload_kind = CoreWirePayloadKind::Struct;
        tag.slots.push_back(CoreWireSchemaField{"label", CoreWireSchemaNodeId{1}});
        en.variants.push_back(tag);
        nodes.push_back(CoreWireSchemaNode{en}); // 2
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{2});
        // Unit
        check(b && decode_json(*parse("{\"_enum\":\"app::Shape\",\"_variant\":\"Dot\"}"), *b).ok(),
              "decode enum unit");
        check(b &&
                  !decode_json(
                       *parse("{\"_enum\":\"app::Shape\",\"_variant\":\"Dot\",\"_payload\":[1]}"),
                       *b)
                       .ok(),
              "decode enum unit rejects payload");
        check(b && !decode_json(*parse("{\"_enum\":\"wrong\",\"_variant\":\"Dot\"}"), *b).ok(),
              "decode enum wrong _enum");
        // Tuple
        check(b &&
                  decode_json(
                      *parse("{\"_enum\":\"app::Shape\",\"_variant\":\"Line\",\"_payload\":[7]}"),
                      *b)
                      .ok(),
              "decode enum tuple");
        check(b &&
                  !decode_json(
                       *parse("{\"_enum\":\"app::Shape\",\"_variant\":\"Line\",\"_payload\":[7,8]}"),
                       *b)
                       .ok(),
              "decode enum tuple wrong arity");
        // Struct payload
        check(b &&
                  decode_json(*parse("{\"_enum\":\"app::Shape\",\"_variant\":\"Tag\",\"_named_"
                                     "payload\":{\"label\":\"hi\"}}"),
                              *b)
                      .ok(),
              "decode enum struct payload");
        check(b &&
                  !decode_json(*parse("{\"_enum\":\"app::Shape\",\"_variant\":\"Tag\",\"_named_"
                                      "payload\":{\"label\":\"hi\",\"x\":1}}"),
                               *b)
                       .ok(),
              "decode enum struct payload extra field");
        // native
        check(b && validate_value(evaluator::make_enum("app::Shape", "Dot"), *b).valid,
              "validate enum unit");
        check(b && validate_value(evaluator::make_enum("app::Shape", "Line",
                                                       vals(evaluator::make_int(3))),
                                  *b)
                        .valid,
              "validate enum tuple");
        check(b && !validate_value(evaluator::make_enum("app::Shape", "Line",
                                                        vals(evaluator::make_int(3),
                                                             evaluator::make_int(4))),
                                   *b)
                        .valid,
              "validate enum tuple wrong arity");
        check(b && !validate_value(evaluator::make_enum("wrong", "Dot"), *b).valid,
              "validate enum wrong name");
        // hand-built enum DOM with a duplicated key.
        {
            auto dup = json::JsonValue::make_object();
            dup->set("_enum", json::JsonValue::make_string("app::Shape"));
            dup->set("_variant", json::JsonValue::make_string("Dot"));
            dup->object_fields.emplace_back("_variant", json::JsonValue::make_string("Dot"));
            check(b && !decode_json(*dup, *b).ok(), "decode enum rejects dup DOM field");
        }
        // native struct-payload variant: correct.
        check(b && validate_value(evaluator::make_enum("app::Shape", "Tag",
                                                       fields(field("label",
                                                                    evaluator::make_string("hi")))),
                                  *b)
                        .valid,
              "validate enum struct payload");
        // native struct-payload variant: wrong field name.
        check(b && !validate_value(evaluator::make_enum("app::Shape", "Tag",
                                                        fields(field("wrong",
                                                                     evaluator::make_string("hi")))),
                                   *b)
                        .valid,
              "validate enum struct payload wrong field");
        // native struct-payload variant: null field value (hand-built).
        {
            evaluator::EnumValue ev;
            ev.enum_name = "app::Shape";
            ev.variant = "Tag";
            ev.named_payload.set("label", std::unique_ptr<Value>{});
            check(b && !validate_value(Value{std::move(ev)}, *b).valid,
                  "validate enum struct payload null field");
        }
        // native unit variant carrying a payload (hand-built) must be rejected.
        {
            evaluator::EnumValue ev;
            ev.enum_name = "app::Shape";
            ev.variant = "Dot";
            ev.payload.push_back(std::make_unique<Value>(evaluator::make_int(1)));
            check(b && !validate_value(Value{std::move(ev)}, *b).valid,
                  "validate enum unit variant rejects payload");
        }
    }
}

// =========================================================================
// P0-8 security: error messages must never echo observed payload bytes.
// =========================================================================
void test_error_no_payload_echo() {
    constexpr const char *kMarker = "TOP_SECRET_MARKER";

    auto contains_marker = [](const std::string &s) {
        return s.find(kMarker) != std::string::npos;
    };

    // A struct whose duplicate field name is the secret marker: the decode error
    // must not repeat it.
    {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaStruct st;
        st.wire_name = "app::Point";
        st.fields.push_back(CoreWireSchemaField{kMarker, CoreWireSchemaNodeId{0}});
        nodes.push_back(CoreWireSchemaNode{st});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{1});
        // hand-built object with a duplicated secret-named field.
        auto obj = json::JsonValue::make_object();
        obj->set("_type", json::JsonValue::make_string("app::Point"));
        obj->set(kMarker, json::JsonValue::make_int(1));
        obj->object_fields.emplace_back(kMarker, json::JsonValue::make_int(2));
        auto r = b ? decode_json(*obj, *b) : WireDecodeResult::failure("no binding");
        check(b && !r.ok(), "secret dup field rejected");
        check(b && !contains_marker(r.error), "dup-field error omits secret marker");
    }

    // An enum whose observed _variant is the secret marker: neither the JSON
    // decode error nor the native validate error may repeat it.
    {
        std::vector<CoreWireSchemaNode> nodes;
        CoreWireSchemaEnum en;
        en.wire_name = "app::Color";
        en.variants.push_back(CoreWireSchemaVariant{"Red", CoreWirePayloadKind::Unit, {}});
        nodes.push_back(CoreWireSchemaNode{en});
        auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{0});
        // JSON decode with an unknown, secret-named variant.
        const std::string json_text =
            std::string("{\"_enum\":\"app::Color\",\"_variant\":\"") + kMarker + "\"}";
        auto dom = parse(json_text);
        auto r = b ? decode_json(*dom, *b) : WireDecodeResult::failure("no binding");
        check(b && !r.ok(), "secret variant (decode) rejected");
        check(b && !contains_marker(r.error), "decode unknown-variant error omits secret marker");
        // native validate with an unknown, secret-named variant.
        auto v = b ? validate_value(evaluator::make_enum("app::Color", kMarker), *b)
                   : SchemaValidationResult::fail("no binding");
        check(b && !v.valid, "secret variant (validate) rejected");
        check(b && !contains_marker(v.error), "validate unknown-variant error omits secret marker");
    }
}

// =========================================================================
// Real affected builtin callers (owning-evaluator tightening)
// =========================================================================
void test_builtin_callers() {
    using namespace ahfl::evaluator;
    const BuiltinTable &table = BuiltinTable::instance();
    EvalContext ctx;

    const BuiltinFn *time_add = table.find("time_add");
    const BuiltinFn *time_sub = table.find("time_sub");
    const BuiltinFn *dec_scale = table.find("decimal_raw_scale");
    check(time_add != nullptr, "builtin time_add registered");
    check(time_sub != nullptr, "builtin time_sub registered");
    check(dec_scale != nullptr, "builtin decimal_raw_scale registered");
    if (time_add == nullptr || time_sub == nullptr || dec_scale == nullptr) {
        return;
    }

    auto add = [&](const std::string &spelling) {
        std::vector<Value> args = vals(make_timestamp(0), make_duration(spelling));
        return (*time_add)(args, ctx);
    };
    // m/h now honored, with the EXACT converted milliseconds (previously silently
    // failed under the old ms/s-only parser).
    {
        const auto r = add("2m");
        check(!r.diagnostics.has_error(), "time_add honors m");
        const auto *ts = std::get_if<TimestampValue>(&r.value.node);
        check(ts != nullptr && ts->unix_ms == 120000, "time_add 2m == 120000ms");
    }
    {
        const auto r = add("1h");
        check(!r.diagnostics.has_error(), "time_add honors h");
        const auto *ts = std::get_if<TimestampValue>(&r.value.node);
        check(ts != nullptr && ts->unix_ms == 3600000, "time_add 1h == 3600000ms");
    }
    // signed source-unit + non-canonical bare-ms rejected (tightening).
    check(add("+5s").diagnostics.has_error(), "time_add rejects +unit");
    check(add("-5s").diagnostics.has_error(), "time_add rejects -unit");
    check(add("0500").diagnostics.has_error(), "time_add rejects leading-zero bare-ms");
    // Pre-multiply overflow guard: a PARSABLE hour count whose *3600000 overflows
    // i64 (not an i64-parse-stage failure).
    {
        const std::int64_t overflow_hours =
            std::numeric_limits<std::int64_t>::max() / 3600000 + 1;
        check(add(std::to_string(overflow_hours) + "h").diagnostics.has_error(),
              "time_add rejects hour overflow (pre-multiply guard)");
    }

    auto sub = [&](const std::string &spelling) {
        std::vector<Value> args = vals(make_timestamp(0), make_duration(spelling));
        return (*time_sub)(args, ctx);
    };
    {
        const auto r = sub("2m");
        check(!r.diagnostics.has_error(), "time_sub honors m");
        const auto *ts = std::get_if<TimestampValue>(&r.value.node);
        check(ts != nullptr && ts->unix_ms == -120000, "time_sub 2m == -120000ms");
    }
    check(sub("+5s").diagnostics.has_error(), "time_sub rejects +unit");

    // P0-12: checked timestamp arithmetic (separate from source-unit multiply
    // overflow). unix_ms at the i64 boundary + a positive/negative duration must
    // fail-closed rather than wrap (UB).
    {
        std::vector<Value> args = vals(make_timestamp(std::numeric_limits<std::int64_t>::max()),
                                       make_duration("1ms"));
        check((*time_add)(args, ctx).diagnostics.has_error(),
              "time_add INT64_MAX + 1ms overflow rejected");
    }
    {
        std::vector<Value> args = vals(make_timestamp(std::numeric_limits<std::int64_t>::min()),
                                       make_duration("1ms"));
        check((*time_sub)(args, ctx).diagnostics.has_error(),
              "time_sub INT64_MIN - 1ms overflow rejected");
    }
    // time_duration_between: end - start at the boundary must fail-closed.
    {
        const BuiltinFn *between = table.find("time_duration_between");
        check(between != nullptr, "builtin time_duration_between registered");
        if (between != nullptr) {
            std::vector<Value> args = vals(make_timestamp(std::numeric_limits<std::int64_t>::min()),
                                           make_timestamp(std::numeric_limits<std::int64_t>::max()));
            check((*between)(args, ctx).diagnostics.has_error(),
                  "time_duration_between max-min overflow rejected");
        }
    }

    // Decimal builtin raw parse (decimal_raw_scale takes one Decimal, parses via
    // the raw B family): reject +/leading-zero/-0 (tightening vs old stoll).
    auto scale = [&](const std::string &spelling) {
        std::vector<Value> args = vals(make_decimal(spelling));
        return (*dec_scale)(args, ctx);
    };
    check(!scale("s2:123").diagnostics.has_error(), "decimal_raw_scale accepts canonical");
    check(scale("s+2:3").diagnostics.has_error(), "decimal_raw_scale rejects +scale");
    check(scale("s2:03").diagnostics.has_error(), "decimal_raw_scale rejects leading-zero mantissa");
    check(scale("s2:-0").diagnostics.has_error(), "decimal_raw_scale rejects -0");
}

// =========================================================================
// Per-shape round-trip: value_json encode -> decode_json == original
// =========================================================================
void round_trip_case(const std::string &name, const Value &value,
                     std::vector<CoreWireSchemaNode> nodes, CoreWireSchemaNodeId root) {
    auto b = mint_result_binding(std::move(nodes), root);
    if (!b) {
        check(false, "round-trip binding: " + name);
        return;
    }
    const std::string json_text = evaluator::value_to_json(value);
    auto dom = parse(json_text);
    if (!dom) {
        check(false, "round-trip parse: " + name);
        return;
    }
    auto decoded = decode_json(*dom, *b);
    check(decoded.ok() && evaluator::structurally_equal(*decoded.value, value),
          "round-trip " + name);
    // plan §6: every Value a decode produces MUST pass validate_value under the
    // SAME binding, so per-shape round-trip is also a two-policy consistency proof.
    if (decoded.ok()) {
        check(validate_value(*decoded.value, *b).valid, "round-trip validate " + name);
    }
}

// =========================================================================
// Recursive binding: Node{next: Option<Node>} decodes + validates a finite value
// (plan §3 rev4 termination proof).
// =========================================================================
void test_recursive_binding() {
    std::vector<CoreWireSchemaNode> nodes;
    // 0 Node struct (field next -> Option<Node> at 1); 1 Option<Node> -> 0.
    CoreWireSchemaStruct node_struct;
    node_struct.wire_name = "app::Node";
    node_struct.fields.push_back(CoreWireSchemaField{"next", CoreWireSchemaNodeId{1}});
    nodes.push_back(CoreWireSchemaNode{node_struct});                                   // 0
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1
    auto b = mint_result_binding(std::move(nodes), CoreWireSchemaNodeId{0});
    check(b.has_value(), "recursive Node binding minted");
    if (!b) {
        return;
    }
    // Terminating value: Node{next: None}.
    {
        auto decoded = decode_json(*parse("{\"_type\":\"app::Node\",\"next\":null}"), *b);
        check(decoded.ok(), "decode Node{next:None}");
        if (decoded.ok()) {
            check(validate_value(*decoded.value, *b).valid, "validate Node{next:None}");
        }
    }
    // One level of Some: Node{next: Some(Node{next: None})}.
    {
        auto decoded = decode_json(
            *parse("{\"_type\":\"app::Node\",\"next\":{\"_type\":\"app::Node\",\"next\":null}}"),
            *b);
        check(decoded.ok(), "decode Node{next:Some(Node{next:None})}");
        if (decoded.ok()) {
            check(validate_value(*decoded.value, *b).valid,
                  "validate Node{next:Some(Node{next:None})}");
        }
    }
}

void test_round_trip() {
    auto leaf = [](CoreWireSchemaShape s) {
        std::vector<CoreWireSchemaNode> n;
        n.push_back(CoreWireSchemaNode{std::move(s)});
        return n;
    };
    round_trip_case("unit", evaluator::make_unit(), leaf(CoreWireSchemaUnit{}),
                    CoreWireSchemaNodeId{0});
    round_trip_case("bool", evaluator::make_bool(true), leaf(CoreWireSchemaBool{}),
                    CoreWireSchemaNodeId{0});
    round_trip_case("int", evaluator::make_int(7), leaf(CoreWireSchemaInt{}),
                    CoreWireSchemaNodeId{0});
    round_trip_case("float", evaluator::make_float(1.5), leaf(CoreWireSchemaFloat{}),
                    CoreWireSchemaNodeId{0});
    round_trip_case("string", evaluator::make_string("hi"), leaf(CoreWireSchemaString{}),
                    CoreWireSchemaNodeId{0});
    round_trip_case("decimal", evaluator::make_decimal("s2:123"), leaf(CoreWireSchemaDecimal{2}),
                    CoreWireSchemaNodeId{0});
    round_trip_case("duration", evaluator::make_duration("5s"), leaf(CoreWireSchemaDuration{}),
                    CoreWireSchemaNodeId{0});
    {
        auto uuid = evaluator::make_uuid("0123456789abcdef0123456789abcdef");
        if (uuid.has_value()) {
            round_trip_case("uuid", *uuid, leaf(CoreWireSchemaUuid{}), CoreWireSchemaNodeId{0});
        } else {
            check(false, "round-trip uuid construct");
        }
    }
    round_trip_case("timestamp", evaluator::make_timestamp(1234), leaf(CoreWireSchemaTimestamp{}),
                    CoreWireSchemaNodeId{0});
    { // Option None + Some
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}});
        round_trip_case("option-none", evaluator::make_option_none(),
                        std::vector<CoreWireSchemaNode>(nodes), CoreWireSchemaNodeId{1});
        round_trip_case("option-some", evaluator::make_option_some(evaluator::make_int(9)),
                        std::move(nodes), CoreWireSchemaNodeId{1});
    }
    { // List
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaSequence seq;
        seq.kind = CoreWireSequenceKind::List;
        seq.element = CoreWireSchemaNodeId{0};
        nodes.push_back(CoreWireSchemaNode{seq});
        round_trip_case("list", make_ptr_list(vals(evaluator::make_int(1), evaluator::make_int(2))),
                        std::move(nodes), CoreWireSchemaNodeId{1});
    }
    { // Set
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaSequence seq;
        seq.kind = CoreWireSequenceKind::Set;
        seq.element = CoreWireSchemaNodeId{0};
        nodes.push_back(CoreWireSchemaNode{seq});
        round_trip_case("set", evaluator::make_set(vals(evaluator::make_int(1), evaluator::make_int(2))),
                        std::move(nodes), CoreWireSchemaNodeId{1});
    }
    { // Map
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}});
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaMap m;
        m.key = CoreWireSchemaNodeId{0};
        m.value = CoreWireSchemaNodeId{1};
        nodes.push_back(CoreWireSchemaNode{m});
        round_trip_case("map",
                        evaluator::make_map(one_entry(evaluator::make_string("a"),
                                                      evaluator::make_int(1))),
                        std::move(nodes), CoreWireSchemaNodeId{2});
    }
    { // Struct
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        CoreWireSchemaStruct st;
        st.wire_name = "app::Point";
        st.fields.push_back(CoreWireSchemaField{"n", CoreWireSchemaNodeId{0}});
        nodes.push_back(CoreWireSchemaNode{st});
        round_trip_case("struct",
                        evaluator::make_struct("app::Point", fields(field("n", evaluator::make_int(5)))),
                        std::move(nodes), CoreWireSchemaNodeId{1});
    }
    { // Enum three payload kinds
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 1
        CoreWireSchemaEnum en;
        en.wire_name = "app::Shape";
        en.variants.push_back(CoreWireSchemaVariant{"Dot", CoreWirePayloadKind::Unit, {}});
        CoreWireSchemaVariant line;
        line.wire_name = "Line";
        line.payload_kind = CoreWirePayloadKind::Tuple;
        line.slots.push_back(CoreWireSchemaField{"", CoreWireSchemaNodeId{0}}); // tuple slots are unnamed
        en.variants.push_back(line);
        CoreWireSchemaVariant tag;
        tag.wire_name = "Tag";
        tag.payload_kind = CoreWirePayloadKind::Struct;
        tag.slots.push_back(CoreWireSchemaField{"label", CoreWireSchemaNodeId{1}});
        en.variants.push_back(tag);
        nodes.push_back(CoreWireSchemaNode{en}); // 2
        round_trip_case("enum-unit", evaluator::make_enum("app::Shape", "Dot"),
                        std::vector<CoreWireSchemaNode>(nodes), CoreWireSchemaNodeId{2});
        round_trip_case("enum-tuple",
                        evaluator::make_enum("app::Shape", "Line",
                                             vals(evaluator::make_int(3))),
                        std::vector<CoreWireSchemaNode>(nodes), CoreWireSchemaNodeId{2});
        round_trip_case(
            "enum-struct",
            evaluator::make_enum("app::Shape", "Tag",
                                 fields(field("label", evaluator::make_string("hi")))),
            std::move(nodes), CoreWireSchemaNodeId{2});
    }
    { // Tuple
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaBool{}});   // 1
        CoreWireSchemaTuple tup;
        tup.elements.push_back(CoreWireSchemaNodeId{0});
        tup.elements.push_back(CoreWireSchemaNodeId{1});
        nodes.push_back(CoreWireSchemaNode{tup}); // 2
        round_trip_case("tuple",
                        make_ptr_list(vals(evaluator::make_int(1), evaluator::make_bool(true))),
                        std::move(nodes), CoreWireSchemaNodeId{2});
    }
}

} // namespace

int main() {
    test_scalar_spelling();
    test_scalars();
    test_decimal_duration();
    test_uuid_timestamp();
    test_option();
    test_recursive_binding();
    test_set_map();
    test_struct_enum();
    test_builtin_callers();
    test_error_no_payload_echo();
    test_round_trip();

    std::cerr << (test_count - fail_count) << "/" << test_count << " checks passed\n";
    return fail_count == 0 ? 0 : 1;
}
