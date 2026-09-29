// RFC 0026 KR6.5 E4-B2-D1a-4 permanent regression for the runtime-owned canonical
// max-JSON-size authority. Hand-rolled check()/main() to match the runtime-engine
// test style. The bound is a CONSERVATIVE, never-underestimating upper bound over
// the productive-edge subgraph reachable from a Verified binding root; it is not
// claimed tight where element distinctness (Set/Map) or the fixed 20-byte Int
// width can leave slack. Coverage: per-shape formula goldens (equality only where
// tight, actual<=bound elsewhere), the 256-byte escape SSOT lock, the Unbounded /
// zero-capacity-cut / overflow / priority matrices, a deep no-stack-overflow
// chain, root-locality, and a table-driven differential that runs the REAL
// value_to_json against a validated conforming value for every bounded shape
// family. FOUNDATION: per-Result bound only; NOT the TOTAL/one-page verdict, NOT
// durable resume, NOT B2-E, NOT a production host.

#include "runtime/engine/core_wire_canonical_size.hpp"

#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"

#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/support/json.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace ahfl::runtime::core_wire_canonical_size;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWireCapabilitySchema;
using ahfl::ir::core::CoreWirePayloadKind;
using ahfl::ir::core::CoreWireRootKind;
using ahfl::ir::core::CoreWireRootSelector;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaTable;
using ahfl::ir::core::CoreWireSchemaVariant;
using ahfl::ir::core::CoreWireSequenceKind;
using ahfl::ir::core::CoreLowerDiagnostic;
using ahfl::ir::core::VerifiedWireSchemaBinding;
namespace core = ahfl::ir::core;
namespace eval = ahfl::runtime;

int g_failures = 0;
int g_total = 0;

void check(bool ok, std::string_view name) {
    ++g_total;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// Build a binding whose Result root is node `root_index` of `nodes`. The single
// capability uses id 0 / source_symbol 0 and no params, so the Result selector
// resolves to `root_index`. Returns nullopt if the table fails admission (used by
// negative fixtures that must still be legal wire schemas).
[[nodiscard]] std::optional<VerifiedWireSchemaBinding>
make_result_binding(std::vector<CoreWireSchemaNode> nodes, std::uint32_t root_index) {
    CoreWireSchemaTable table;
    table.format_version = 1;
    table.nodes = std::move(nodes);
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.source_symbol = 0;
    cap.result = CoreWireSchemaNodeId{root_index};
    table.capabilities.push_back(std::move(cap));

    auto verified = core::make_verified_wire_schema_table(std::move(table));
    if (!verified.ok()) {
        return std::nullopt;
    }
    CoreWireRootSelector selector;
    selector.capability = CoreCapabilityId{0};
    selector.expected_source_symbol = 0;
    selector.kind = CoreWireRootKind::Result;
    selector.param_index = 0;
    std::vector<CoreLowerDiagnostic> diags;
    return core::make_wire_binding_from_verified_table(*verified.table, selector, diags);
}

// Shorthand shape constructors.
CoreWireSchemaNode unit_node() { return {core::CoreWireSchemaUnit{}}; }
CoreWireSchemaNode bool_node() { return {core::CoreWireSchemaBool{}}; }
CoreWireSchemaNode int_node() { return {core::CoreWireSchemaInt{}}; }
CoreWireSchemaNode float_node() { return {core::CoreWireSchemaFloat{}}; }
CoreWireSchemaNode timestamp_node() { return {core::CoreWireSchemaTimestamp{}}; }
CoreWireSchemaNode uuid_node() { return {core::CoreWireSchemaUuid{}}; }
CoreWireSchemaNode decimal_node() { return {core::CoreWireSchemaDecimal{}}; }
CoreWireSchemaNode duration_node() { return {core::CoreWireSchemaDuration{}}; }
CoreWireSchemaNode string_node(std::optional<std::int64_t> max_bytes) {
    core::CoreWireSchemaString s;
    if (max_bytes.has_value()) {
        s.length_bounds = std::pair<std::int64_t, std::int64_t>{0, *max_bytes};
    }
    return {s};
}
CoreWireSchemaNode option_node(std::uint32_t child) {
    return {core::CoreWireSchemaOption{CoreWireSchemaNodeId{child}}};
}
CoreWireSchemaNode seq_node(CoreWireSequenceKind kind, std::uint32_t elem,
                            std::optional<std::uint64_t> cap) {
    core::CoreWireSchemaSequence s;
    s.kind = kind;
    s.element = CoreWireSchemaNodeId{elem};
    s.capacity = cap;
    return {s};
}
CoreWireSchemaNode map_node(std::uint32_t key, std::uint32_t value,
                            std::optional<std::uint64_t> cap) {
    core::CoreWireSchemaMap m;
    m.key = CoreWireSchemaNodeId{key};
    m.value = CoreWireSchemaNodeId{value};
    m.capacity = cap;
    return {m};
}

// Convenience: bound of a single-node schema whose sole node is the root.
[[nodiscard]] std::optional<std::uint64_t> bound_of_single(CoreWireSchemaNode node) {
    auto binding = make_result_binding({std::move(node)}, 0);
    if (!binding.has_value()) {
        return std::nullopt;
    }
    auto r = max_canonical_json_size(*binding);
    return r.has_value() ? std::optional<std::uint64_t>(*r) : std::nullopt;
}

// Same, for a multi-node table rooted at `root`.
[[nodiscard]] std::optional<std::uint64_t>
bound_of_single_pair(std::vector<CoreWireSchemaNode> nodes, std::uint32_t root) {
    auto binding = make_result_binding(std::move(nodes), root);
    if (!binding.has_value()) {
        return std::nullopt;
    }
    auto r = max_canonical_json_size(*binding);
    return r.has_value() ? std::optional<std::uint64_t>(*r) : std::nullopt;
}

// ---------------------------------------------------------------------------
// Escape SSOT lock: every single byte expands to at most 6 payload bytes; a
// control byte hits exactly 6. This pins the S(B)=2+6B constant against drift in
// write_escaped_json_string.
void test_escape_ssot_lock() {
    bool all_le6 = true;
    for (int b = 0; b < 256; ++b) {
        const std::string one(1, static_cast<char>(b));
        std::ostringstream oss;
        ahfl::write_escaped_json_string(oss, one);
        // oss holds "..." including the 2 quotes; payload = size()-2.
        if (oss.str().size() - 2 > 6) {
            all_le6 = false;
        }
    }
    check(all_le6, "escape.all_bytes_le6");
    // Byte 0x01 is a control byte with no short escape -> backslash-u0001 = 6 payload bytes.
    std::ostringstream ctrl;
    ahfl::write_escaped_json_string(ctrl, std::string(1, static_cast<char>(0x01)));
    check(ctrl.str().size() - 2 == 6, "escape.control_is_6");
}

// ---------------------------------------------------------------------------
// Fixed-width scalar goldens (tight -> equality).
void test_scalar_goldens() {
    check(bound_of_single(unit_node()) == std::optional<std::uint64_t>(4), "scalar.unit_4");
    check(bound_of_single(bool_node()) == std::optional<std::uint64_t>(5), "scalar.bool_5");
    check(bound_of_single(int_node()) == std::optional<std::uint64_t>(20), "scalar.int_20");
    check(bound_of_single(float_node()) == std::optional<std::uint64_t>(24), "scalar.float_24");
    check(bound_of_single(timestamp_node()) == std::optional<std::uint64_t>(35), "scalar.ts_35");
    check(bound_of_single(uuid_node()) == std::optional<std::uint64_t>(44), "scalar.uuid_44");
    // String bounded max=10 -> 2 + 6*10 = 62.
    check(bound_of_single(string_node(10)) == std::optional<std::uint64_t>(62), "scalar.string_62");
    // String bounded max=0 -> 2 (empty quoted).
    check(bound_of_single(string_node(0)) == std::optional<std::uint64_t>(2),
          "scalar.string_empty_2");
}

// ---------------------------------------------------------------------------
// Unbounded matrix.
void test_unbounded_matrix() {
    auto is_unbounded = [](std::vector<CoreWireSchemaNode> nodes, std::uint32_t root) {
        auto binding = make_result_binding(std::move(nodes), root);
        if (!binding.has_value()) {
            return false; // admission failure is a different problem
        }
        auto r = max_canonical_json_size(*binding);
        return !r.has_value() && r.error() == MaxCanonicalSizeError::Unbounded;
    };
    check(is_unbounded({string_node(std::nullopt)}, 0), "unbounded.string_no_bound");
    check(is_unbounded({decimal_node()}, 0), "unbounded.decimal");
    check(is_unbounded({duration_node()}, 0), "unbounded.duration");
    check(is_unbounded({seq_node(CoreWireSequenceKind::List, 1, std::nullopt), int_node()}, 0),
          "unbounded.list_no_capacity");
    check(is_unbounded({seq_node(CoreWireSequenceKind::Set, 1, std::nullopt), int_node()}, 0),
          "unbounded.set_no_capacity");
    check(is_unbounded({map_node(1, 2, std::nullopt), string_node(4), int_node()}, 0),
          "unbounded.map_no_capacity");
    // Map with capacity but unbounded key String.
    check(is_unbounded({map_node(1, 2, std::optional<std::uint64_t>(3)), string_node(std::nullopt),
                        int_node()},
                       0),
          "unbounded.map_unbounded_key");
    // Map with capacity but unbounded value.
    check(is_unbounded({map_node(1, 2, std::optional<std::uint64_t>(3)), string_node(4),
                        decimal_node()},
                       0),
          "unbounded.map_unbounded_value");
    // Struct containing an unbounded child.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "S";
        st.fields.push_back({"a", CoreWireSchemaNodeId{1}});
        nodes.push_back({st});
        nodes.push_back(decimal_node());
        check(is_unbounded(std::move(nodes), 0), "unbounded.struct_child");
    }
    // Option of unbounded.
    check(is_unbounded({option_node(1), string_node(std::nullopt)}, 0), "unbounded.option_child");
    // Tuple containing an unbounded element (Decimal).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaTuple tup;
        tup.elements.push_back(CoreWireSchemaNodeId{1});
        nodes.push_back({tup});
        nodes.push_back(decimal_node());
        check(is_unbounded(std::move(nodes), 0), "unbounded.tuple_child");
    }
    // Enum whose variant payload slot is unbounded (String with no length_bounds).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v;
        v.wire_name = "V";
        v.payload_kind = CoreWirePayloadKind::Tuple;
        v.slots.push_back({"", CoreWireSchemaNodeId{1}});
        en.variants.push_back(v);
        nodes.push_back({en});
        nodes.push_back(string_node(std::nullopt));
        check(is_unbounded(std::move(nodes), 0), "unbounded.enum_child");
    }
    // Productive recursive cycle: Struct{next: Option<Struct>} -> nodes 0->1->0.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "Node";
        st.fields.push_back({"next", CoreWireSchemaNodeId{1}});
        nodes.push_back({st});          // 0: Struct
        nodes.push_back(option_node(0)); // 1: Option<Struct>
        check(is_unbounded(std::move(nodes), 0), "unbounded.productive_cycle");
    }
}

// ---------------------------------------------------------------------------
// Zero-capacity cut: a cut collection is finite even with an unbounded/cyclic
// child, and contributes only empty-collection framing.
void test_zero_capacity_cut() {
    // List capacity 0 with unbounded element -> 2 ([]).
    check(bound_of_single_pair({seq_node(CoreWireSequenceKind::List, 1,
                                         std::optional<std::uint64_t>(0)),
                                decimal_node()},
                               0) == std::optional<std::uint64_t>(2),
          "cut.list_cap0_unbounded_elem_2");
    // Map capacity 0 with unbounded key AND value -> 2 ({}).
    check(bound_of_single_pair({map_node(1, 2, std::optional<std::uint64_t>(0)),
                                string_node(std::nullopt), decimal_node()},
                               0) == std::optional<std::uint64_t>(2),
          "cut.map_cap0_unbounded_kv_2");
    // A cycle sitting behind a cap0 collection is not productive -> finite.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "N";
        st.fields.push_back({"kids", CoreWireSchemaNodeId{1}});
        nodes.push_back({st}); // 0: Struct
        // 1: List capacity 0 -> node 0 (the cycle is cut by the zero capacity).
        nodes.push_back(seq_node(CoreWireSequenceKind::List, 0, std::optional<std::uint64_t>(0)));
        auto binding = make_result_binding(std::move(nodes), 0);
        check(binding.has_value(), "cut.cycle_behind_cap0_admits");
        if (binding.has_value()) {
            auto r = max_canonical_json_size(*binding);
            // {"_type":"N","kids":[]} = 1 + 7 + 1 + 3 + 1 + 6 + 1 + 2 + 1 = 23.
            // The cap-0 List cuts the back-edge to node 0, so the schema is finite.
            check(r.has_value() && *r == 23, "cut.cycle_behind_cap0_finite");
        }
    }
}

// ---------------------------------------------------------------------------
// Overflow + priority (Unbounded beats SizeOverflow, order-independent).
void test_overflow_and_priority() {
    constexpr std::uint64_t kU64Max = std::numeric_limits<std::uint64_t>::max();
    auto err_of = [](std::vector<CoreWireSchemaNode> nodes, std::uint32_t root) {
        auto binding = make_result_binding(std::move(nodes), root);
        std::optional<MaxCanonicalSizeError> e;
        if (binding.has_value()) {
            auto r = max_canonical_json_size(*binding);
            if (!r.has_value()) {
                e = r.error();
            }
        }
        return e;
    };
    // List capacity UINT64_MAX of a nonzero element -> SizeOverflow.
    check(err_of({seq_node(CoreWireSequenceKind::List, 1,
                           std::optional<std::uint64_t>(kU64Max)),
                  int_node()},
                 0) == std::optional<MaxCanonicalSizeError>(MaxCanonicalSizeError::SizeOverflow),
          "overflow.list_capmax");
    // A struct with one finite (overflowing) field AND one unbounded field ->
    // Unbounded wins. Build the same violation set in two field orders.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "S";
        st.fields.push_back({"a", CoreWireSchemaNodeId{1}}); // overflowing list
        st.fields.push_back({"b", CoreWireSchemaNodeId{2}}); // unbounded decimal
        nodes.push_back({st});
        nodes.push_back(seq_node(CoreWireSequenceKind::List, 3,
                                 std::optional<std::uint64_t>(kU64Max)));
        nodes.push_back(decimal_node());
        nodes.push_back(int_node());
        check(err_of(std::move(nodes), 0) ==
                  std::optional<MaxCanonicalSizeError>(MaxCanonicalSizeError::Unbounded),
              "priority.unbounded_beats_overflow_order1");
    }
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "S";
        st.fields.push_back({"b", CoreWireSchemaNodeId{2}}); // unbounded decimal first
        st.fields.push_back({"a", CoreWireSchemaNodeId{1}}); // overflowing list second
        nodes.push_back({st});
        nodes.push_back(seq_node(CoreWireSequenceKind::List, 3,
                                 std::optional<std::uint64_t>(kU64Max)));
        nodes.push_back(decimal_node());
        nodes.push_back(int_node());
        check(err_of(std::move(nodes), 0) ==
                  std::optional<MaxCanonicalSizeError>(MaxCanonicalSizeError::Unbounded),
              "priority.unbounded_beats_overflow_order2");
    }
}

// ---------------------------------------------------------------------------
// Deep no-stack-overflow: a very deep acyclic Struct chain must produce a finite
// bound without crashing; a deep chain that loops back must be Unbounded. (A deep
// Struct chain is used rather than Option chains, since the verifier rejects
// Option-of-Option.)
void test_deep_no_stack_overflow() {
    constexpr std::uint32_t kDepth = 50000;
    auto struct_to = [](std::uint32_t child) {
        core::CoreWireSchemaStruct st;
        st.wire_name = "N";
        st.fields.push_back({"n", CoreWireSchemaNodeId{child}});
        return CoreWireSchemaNode{st};
    };
    // nodes[i] = Struct{n: nodes[i+1]} for i in [0,kDepth); nodes[kDepth] = Bool.
    {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.reserve(kDepth + 1);
        for (std::uint32_t i = 0; i < kDepth; ++i) {
            nodes.push_back(struct_to(i + 1));
        }
        nodes.push_back(bool_node());
        auto binding = make_result_binding(std::move(nodes), 0);
        check(binding.has_value(), "deep.acyclic_admits");
        if (binding.has_value()) {
            auto r = max_canonical_json_size(*binding);
            check(r.has_value(), "deep.acyclic_finite"); // finite, no crash
        }
    }
    // A deep Struct chain that loops back at the end -> productive cycle -> Unbounded.
    {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.reserve(kDepth);
        for (std::uint32_t i = 0; i < kDepth - 1; ++i) {
            nodes.push_back(struct_to(i + 1));
        }
        nodes.push_back(struct_to(0)); // last struct loops back to the root
        auto binding = make_result_binding(std::move(nodes), 0);
        check(binding.has_value(), "deep.cycle_admits");
        if (binding.has_value()) {
            auto r = max_canonical_json_size(*binding);
            check(!r.has_value() && r.error() == MaxCanonicalSizeError::Unbounded,
                  "deep.cycle_unbounded");
        }
    }
}

// ---------------------------------------------------------------------------
// Root-locality: an unbounded node reachable only from a DIFFERENT capability root
// must not pollute this binding's (all-bounded) root.
void test_root_locality() {
    // node 0: Bool (this Result root). node 1: Decimal (unbounded), reachable only
    // from a second capability's result.
    CoreWireSchemaTable table;
    table.format_version = 1;
    table.nodes.push_back(bool_node());    // 0
    table.nodes.push_back(decimal_node()); // 1
    CoreWireCapabilitySchema cap0;
    cap0.capability = CoreCapabilityId{0};
    cap0.source_symbol = 0;
    cap0.result = CoreWireSchemaNodeId{0};
    CoreWireCapabilitySchema cap1;
    cap1.capability = CoreCapabilityId{1};
    cap1.source_symbol = 0;
    cap1.result = CoreWireSchemaNodeId{1};
    table.capabilities.push_back(std::move(cap0));
    table.capabilities.push_back(std::move(cap1));

    auto verified = core::make_verified_wire_schema_table(std::move(table));
    check(verified.ok(), "root_locality.admits");
    if (verified.ok()) {
        CoreWireRootSelector selector;
        selector.capability = CoreCapabilityId{0};
        selector.expected_source_symbol = 0;
        selector.kind = CoreWireRootKind::Result;
        selector.param_index = 0;
        std::vector<CoreLowerDiagnostic> diags;
        auto binding =
            core::make_wire_binding_from_verified_table(*verified.table, selector, diags);
        check(binding.has_value(), "root_locality.binding");
        if (binding.has_value()) {
            auto r = max_canonical_json_size(*binding);
            check(r.has_value() && *r == 5, "root_locality.bool_root_finite_5");
        }
    }
}

// ---------------------------------------------------------------------------
// Composite goldens (tight -> equality), computed against E() by hand.
void test_composite_goldens() {
    // Struct{a: Bool}: {"_type":"S","a":true-ish}
    //   1 (`{`) + 7 (`"_type"`) + 1 (`:`) + 3 (`"S"`) + 1 (`,`) + 3 (`"a"`) + 1 (`:`)
    //   + 5 (bool) + 1 (`}`) = 23.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "S";
        st.fields.push_back({"a", CoreWireSchemaNodeId{1}});
        nodes.push_back({st});
        nodes.push_back(bool_node());
        check(bound_of_single_pair(std::move(nodes), 0) == std::optional<std::uint64_t>(23),
              "composite.struct_23");
    }
    // Tuple<Bool,Int>: [ + 5 + 4(int 20? -> 20) ... recompute: 2 frame + 5 + 20 + 1 comma = 28.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaTuple tup;
        tup.elements.push_back(CoreWireSchemaNodeId{1});
        tup.elements.push_back(CoreWireSchemaNodeId{2});
        nodes.push_back({tup});
        nodes.push_back(bool_node());
        nodes.push_back(int_node());
        check(bound_of_single_pair(std::move(nodes), 0) == std::optional<std::uint64_t>(28),
              "composite.tuple_28");
    }
    // Option<Bool>: max(4,5) = 5.
    check(bound_of_single_pair({option_node(1), bool_node()}, 0) ==
              std::optional<std::uint64_t>(5),
          "composite.option_some_5");
    // Enum max-over-variants: {"_enum":"E","_variant":"..."} plus payloads.
    //   base = 1 + 7(`"_enum"`) + 1 + 3(`"E"`) + 1 + 10(`"_variant"`) + 1 = 24.
    //   variant "Unit" (unit): 24 + 6(`"Unit"`) + 0 + 1(`}`) = 31.
    //   variant "Pair" (tuple<Bool,Int>): 24 + 6(`"Pair"`) + [ ,"_payload":[ = 1+11+1+1 ]
    //     + 5 + 20 + 1 comma + 1(`]`) + 1(`}`) ...
    //   max should be the tuple variant.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v_unit;
        v_unit.wire_name = "Unit";
        v_unit.payload_kind = CoreWirePayloadKind::Unit;
        CoreWireSchemaVariant v_pair;
        v_pair.wire_name = "Pair";
        v_pair.payload_kind = CoreWirePayloadKind::Tuple;
        v_pair.slots.push_back({"", CoreWireSchemaNodeId{1}});
        v_pair.slots.push_back({"", CoreWireSchemaNodeId{2}});
        en.variants.push_back(v_unit);
        en.variants.push_back(v_pair);
        nodes.push_back({en});
        nodes.push_back(bool_node());
        nodes.push_back(int_node());
        auto bound = bound_of_single_pair(std::move(nodes), 0);
        check(bound.has_value(), "composite.enum_has_bound");
        // MAX over variants: base 24 (see above). Unit variant = 24 + 6 + 0 + 1 = 31.
        // Pair variant = 24 + 6("Pair") + [,"_payload":[ = 1+11+1+1] + 5(Bool) + 20(Int)
        //   + 1 comma + 1(]) + 1(}) = 71. The Pair variant wins -> exact 71.
        check(bound == std::optional<std::uint64_t>(71), "composite.enum_max_variant_71");
    }
    // Enum with a ZERO-slot tuple and a ZERO-slot named payload -> payload term 0.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v_tup0;
        v_tup0.wire_name = "T";
        v_tup0.payload_kind = CoreWirePayloadKind::Tuple; // zero slots
        CoreWireSchemaVariant v_named0;
        v_named0.wire_name = "N";
        v_named0.payload_kind = CoreWirePayloadKind::Struct; // zero slots
        en.variants.push_back(v_tup0);
        en.variants.push_back(v_named0);
        nodes.push_back({en});
        // base 24; longest variant name is "T"/"N" both 3 -> 24 + 3 + 0 + 1 = 28.
        check(bound_of_single_pair(std::move(nodes), 0) == std::optional<std::uint64_t>(28),
              "composite.enum_zero_slot_payload_28");
    }
}

// ---------------------------------------------------------------------------
// Float boundary corpus (P1-3): the real value_to_json length of every extreme
// double is <= the fixed 24-byte Float bound.
void test_float_corpus() {
    const double corpus[] = {
        std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::min(),          // min normal
        -std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min(),   // min subnormal
        -std::numeric_limits<double>::denorm_min(),
        0.0,
        -0.0,
        1.0, // integral -> ".0"-suffixed
        -1.0,
    };
    bool all_le24 = true;
    for (const double d : corpus) {
        if (eval::value_to_json(eval::make_float(d)).size() > 24) {
            all_le24 = false;
        }
    }
    check(all_le24, "float_corpus.all_le24");
    // The bound itself is 24.
    check(bound_of_single(float_node()) == std::optional<std::uint64_t>(24),
          "float_corpus.bound_24");
}

// ---------------------------------------------------------------------------
// Value, prove it conforms via the real codec validate_value, then assert the real
// value_to_json length <= bound (never underestimate). Two Option witnesses per
// P0: Some(Bool) hits the inner bound (5), None hits 4.
void test_differential() {
    // Run one case: prove the value conforms, then compare actual bytes to bound.
    auto run = [](std::vector<CoreWireSchemaNode> nodes, std::uint32_t root,
                  const eval::Value &value, bool equality, std::string_view name) {
        auto binding = make_result_binding(std::move(nodes), root);
        if (!binding.has_value()) {
            check(false, std::string(name) + ".admits");
            return;
        }
        auto valid = ahfl::runtime::wire_codec::validate_value(value, *binding);
        check(valid.valid, std::string(name) + ".conforms");
        auto bound = max_canonical_json_size(*binding);
        check(bound.has_value(), std::string(name) + ".bounded");
        if (bound.has_value()) {
            const std::uint64_t actual = eval::value_to_json(value).size();
            if (equality) {
                check(actual == *bound, std::string(name) + ".equal");
            } else {
                check(actual <= *bound, std::string(name) + ".le");
            }
        }
    };

    // Scalars (tight).
    run({unit_node()}, 0, eval::make_unit(), true, "diff.unit");
    run({bool_node()}, 0, eval::make_bool(false), true, "diff.bool");
    run({int_node()}, 0, eval::make_int(-9223372036854775807LL - 1), true, "diff.int");
    run({float_node()}, 0, eval::make_float(-1.7976931348623157e+308), true, "diff.float");
    run({timestamp_node()}, 0, eval::make_timestamp(-9223372036854775807LL - 1), true, "diff.ts");
    {
        auto uuid = eval::make_uuid("0123456789abcdef0123456789abcdef");
        check(uuid.has_value(), "diff.uuid_built");
        if (uuid.has_value()) {
            run({uuid_node()}, 0, *uuid, true, "diff.uuid");
        }
    }
    // bounded String max=8, worst-case 8 control bytes -> actual 2+6*8=50 == bound.
    run({string_node(8)}, 0, eval::make_string(std::string(8, '\x01')), true, "diff.string_ctrl");

    // Option: Some(Bool) hits inner bound 5 (equality). None under Option<String
    // max=0> hits the max(4, S(0)=2)=4 bound with real bytes "null"=4 (equality).
    run({option_node(1), bool_node()}, 0, eval::make_option_some(eval::make_bool(false)), true,
        "diff.option_some");
    run({option_node(1), string_node(0)}, 0, eval::make_option_none(), true, "diff.option_none");

    // List<Bool> capacity 3, filled with 3 elements (tight arithmetic).
    {
        std::vector<eval::Value> items;
        items.push_back(eval::make_bool(false));
        items.push_back(eval::make_bool(false));
        items.push_back(eval::make_bool(false));
        run({seq_node(CoreWireSequenceKind::List, 1, std::optional<std::uint64_t>(3)), bool_node()},
            0, eval::make_list(std::move(items)), true, "diff.list_bool3");
    }
    // Set<Int> capacity 3, filled with 3 DISTINCT ints (<= due to distinctness slack).
    {
        std::vector<eval::Value> items;
        items.push_back(eval::make_int(1));
        items.push_back(eval::make_int(22));
        items.push_back(eval::make_int(333));
        run({seq_node(CoreWireSequenceKind::Set, 1, std::optional<std::uint64_t>(3)), int_node()},
            0, eval::make_set(std::move(items)), false, "diff.set_int3");
    }
    // Map<String(max=4), Int> capacity 2, filled with 2 distinct keys (<=).
    {
        std::vector<std::pair<eval::Value, eval::Value>> entries;
        entries.emplace_back(eval::make_string("ab"), eval::make_int(1));
        entries.emplace_back(eval::make_string("cd"), eval::make_int(2));
        run({map_node(1, 2, std::optional<std::uint64_t>(2)), string_node(4), int_node()}, 0,
            eval::make_map(std::move(entries)), false, "diff.map2");
    }
    // Struct{a: Bool} (tight).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = "S";
        st.fields.push_back({"a", CoreWireSchemaNodeId{1}});
        nodes.push_back({st});
        nodes.push_back(bool_node());
        std::unordered_map<std::string, eval::Value> fields;
        fields.emplace("a", eval::make_bool(false));
        run(std::move(nodes), 0, eval::make_struct("S", std::move(fields)), true, "diff.struct");
    }
    // Enum unit variant.
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v;
        v.wire_name = "Unit";
        v.payload_kind = CoreWirePayloadKind::Unit;
        en.variants.push_back(v);
        nodes.push_back({en});
        run(std::move(nodes), 0, eval::make_enum("E", "Unit"), true, "diff.enum_unit");
    }
    // Enum tuple variant (n>=1).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v;
        v.wire_name = "Pair";
        v.payload_kind = CoreWirePayloadKind::Tuple;
        v.slots.push_back({"", CoreWireSchemaNodeId{1}});
        v.slots.push_back({"", CoreWireSchemaNodeId{2}});
        en.variants.push_back(v);
        nodes.push_back({en});
        nodes.push_back(bool_node());
        nodes.push_back(int_node());
        std::vector<eval::Value> payload;
        payload.push_back(eval::make_bool(false));
        payload.push_back(eval::make_int(7));
        run(std::move(nodes), 0, eval::make_enum("E", "Pair", std::move(payload)), false,
            "diff.enum_tuple");
    }
    // P0-6: the MULTI-variant Enum{Unit, Pair<Bool,Int>} bound (71) is hit exactly
    // by a real value that selects the widest variant with worst-case scalars:
    // Pair(false, INT64_MIN) -> {"_enum":"E","_variant":"Pair","_payload":[false,
    // -9223372036854775808]} = 71 bytes == bound. This cross-checks the
    // max-over-variants formula against the real serializer (not a single-variant
    // schema).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v_unit;
        v_unit.wire_name = "Unit";
        v_unit.payload_kind = CoreWirePayloadKind::Unit;
        CoreWireSchemaVariant v_pair;
        v_pair.wire_name = "Pair";
        v_pair.payload_kind = CoreWirePayloadKind::Tuple;
        v_pair.slots.push_back({"", CoreWireSchemaNodeId{1}});
        v_pair.slots.push_back({"", CoreWireSchemaNodeId{2}});
        en.variants.push_back(v_unit);
        en.variants.push_back(v_pair);
        nodes.push_back({en});
        nodes.push_back(bool_node());
        nodes.push_back(int_node());
        std::vector<eval::Value> payload;
        payload.push_back(eval::make_bool(false));
        payload.push_back(eval::make_int(-9223372036854775807LL - 1)); // INT64_MIN
        run(std::move(nodes), 0, eval::make_enum("E", "Pair", std::move(payload)), true,
            "diff.enum_multi_variant_max");
    }
    // Enum named-payload variant (n>=1).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v;
        v.wire_name = "Data";
        v.payload_kind = CoreWirePayloadKind::Struct;
        v.slots.push_back({"code", CoreWireSchemaNodeId{1}});
        en.variants.push_back(v);
        nodes.push_back({en});
        nodes.push_back(int_node());
        std::unordered_map<std::string, eval::Value> named;
        named.emplace("code", eval::make_int(7));
        run(std::move(nodes), 0, eval::make_enum("E", "Data", std::move(named)), false,
            "diff.enum_named");
    }
    // Enum zero-slot tuple variant (payload omitted).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v;
        v.wire_name = "T";
        v.payload_kind = CoreWirePayloadKind::Tuple; // zero slots
        en.variants.push_back(v);
        nodes.push_back({en});
        run(std::move(nodes), 0, eval::make_enum("E", "T"), true, "diff.enum_tuple_zero");
    }
    // Enum zero-slot STRUCT (named) payload variant: value_to_json omits the
    // _named_payload wrapper, so payload term is 0 (P0-3, distinct from the
    // static zero-slot golden which mixed both variant kinds).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = "E";
        CoreWireSchemaVariant v;
        v.wire_name = "N";
        v.payload_kind = CoreWirePayloadKind::Struct; // zero slots
        en.variants.push_back(v);
        nodes.push_back({en});
        run(std::move(nodes), 0, eval::make_enum("E", "N"), true, "diff.enum_named_zero");
    }
    // Tuple<Bool,Int> (tight).
    {
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaTuple tup;
        tup.elements.push_back(CoreWireSchemaNodeId{1});
        tup.elements.push_back(CoreWireSchemaNodeId{2});
        nodes.push_back({tup});
        nodes.push_back(bool_node());
        nodes.push_back(int_node());
        std::vector<eval::Value> items;
        items.push_back(eval::make_bool(false));
        items.push_back(eval::make_int(7));
        run(std::move(nodes), 0, eval::make_list(std::move(items)), false, "diff.tuple");
    }
    // P0-1 escaped fixed-name lock: a Struct whose schema wire_name AND a field
    // wire_name contain a quote, backslash, and control byte. E() must use the
    // REAL escaper, so the bound EQUALS the real value_to_json length (this fails
    // if E() were approximated as 6*len+2, which over-counts short escapes).
    {
        const std::string sname = "A\"\\\x02";  // quote, backslash, 0x02 control
        const std::string fname = "f\"\\\x03";  // quote, backslash, 0x03 control
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaStruct st;
        st.wire_name = sname;
        st.fields.push_back({fname, CoreWireSchemaNodeId{1}});
        nodes.push_back({st});
        nodes.push_back(bool_node());
        std::unordered_map<std::string, eval::Value> fields;
        fields.emplace(fname, eval::make_bool(false));
        run(std::move(nodes), 0, eval::make_struct(sname, std::move(fields)), true,
            "diff.struct_escaped_names");
    }
    // P0-1 escaped fixed-name lock for Enum: enum/variant/named-slot names all
    // contain escapes; the tuple/named payload keys are fixed discriminators.
    {
        const std::string ename = "E\"\\\x04";
        const std::string vname = "V\t\n";       // short escapes (\t, \n = 2 bytes each)
        const std::string slot = "s\"\x05";      // quote + control
        std::vector<CoreWireSchemaNode> nodes;
        core::CoreWireSchemaEnum en;
        en.wire_name = ename;
        CoreWireSchemaVariant v;
        v.wire_name = vname;
        v.payload_kind = CoreWirePayloadKind::Struct;
        v.slots.push_back({slot, CoreWireSchemaNodeId{1}});
        en.variants.push_back(v);
        nodes.push_back({en});
        nodes.push_back(bool_node());
        std::unordered_map<std::string, eval::Value> named;
        named.emplace(slot, eval::make_bool(false));
        run(std::move(nodes), 0, eval::make_enum(ename, vname, std::move(named)), true,
            "diff.enum_escaped_names");
    }
}

} // namespace

int main() {
    test_escape_ssot_lock();
    test_scalar_goldens();
    test_unbounded_matrix();
    test_zero_capacity_cut();
    test_overflow_and_priority();
    test_deep_no_stack_overflow();
    test_root_locality();
    test_float_corpus();
    test_composite_goldens();
    test_differential();

    if (g_failures == 0) {
        std::cout << "core_wire_canonical_size: all checks passed (" << g_total << " assertions)\n";
        return 0;
    }
    std::cerr << "core_wire_canonical_size: " << g_failures << " failure(s)\n";
    return 1;
}
