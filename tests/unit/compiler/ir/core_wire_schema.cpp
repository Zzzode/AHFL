#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// RFC 0026 KR6.5 E4-B0-C1: deterministic logical wire schemas for selected Core
// capability imports. These tests build a KNOWN-GOOD, verifier-clean CoreProgram
// carrying only a type table + a topologically-ordered value-type arena + a set
// of capability imports (no agents/flows needed — `verify_core_program` accepts a
// capabilities-only program). They then:
//   * project the wire closure of a canonical selection and assert the shapes,
//   * prove projection is deterministic + reprojection-verifiable,
//   * prove the fail-closed rejections (Fn/Closure/Never/non-String Map key,
//     non-canonical / out-of-range selection, unverified Core),
//   * prove the local table verifier and the canonical encoder.
// The projector is a SIDE artifact: it never mutates the CoreProgram and is
// independent of the P4-D physical layout pass.

namespace {

using namespace ahfl;
using namespace ahfl::ir::core;

[[nodiscard]] bool has_code(const std::vector<CoreLowerDiagnostic> &diagnostics,
                            std::string_view code) {
    for (const auto &d : diagnostics) {
        if (d.severity == CoreDiagnosticSeverity::Error && d.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const CoreWireSchemaShape &shape_of(const CoreWireSchemaTable &table,
                                                  CoreWireSchemaNodeId id) {
    REQUIRE(id.value < table.nodes.size());
    return table.nodes[id.value].shape;
}

// A member-template node that resolves directly to an already-interned value
// type (used for the concrete fields of the user struct/enum fixtures).
[[nodiscard]] CoreMemberTypeTemplateNode concrete(CoreValueTypeId value) {
    CoreMemberTypeTemplateNode node;
    node.kind = CoreMemberTypeTemplateKind::Concrete;
    node.concrete = value;
    return node;
}

[[nodiscard]] CoreMemberTypeTemplateNode param(std::uint32_t index) {
    CoreMemberTypeTemplateNode node;
    node.kind = CoreMemberTypeTemplateKind::Param;
    node.param_index = index;
    return node;
}

// A builtin collection shell (List/Set/Map): a Struct nominal that carries the
// role + arity + variance the verifier cross-checks against the descriptor SSOT,
// and NO member templates (a builtin collection must not carry them).
[[nodiscard]] CoreTypeDecl collection(std::string canonical, CoreNominalRole role,
                                      std::uint32_t arity, std::vector<CoreVariance> variances) {
    CoreTypeDecl decl;
    decl.kind = CoreTypeDecl::Kind::Struct;
    decl.name = std::move(canonical);
    decl.role = role;
    decl.type_param_count = arity;
    decl.variances = std::move(variances);
    return decl;
}

// Type-table indices in the fixture program.
enum : std::uint32_t {
    kPairTy = 0,
    kColorTy = 1,
    kOptionTy = 2,
    kListTy = 3,
    kSetTy = 4,
    kMapTy = 5,
};

// Value-type arena indices (topological: every child id precedes its parent).
enum : std::uint32_t {
    kVtInt = 0,
    kVtBool = 1,
    kVtString = 2,
    kVtPair = 3,        // Pair
    kVtColor = 4,       // Color
    kVtOptionInt = 5,   // Option<Int>
    kVtListInt = 6,     // List<Int> capacity 8
    kVtSetBool = 7,     // Set<Bool>
    kVtMapStrInt = 8,   // Map<String, Int>
    kVtTupleIB = 9,     // (Int, Bool)
    kVtFn = 10,         // fn() -> Int
    kVtClosure = 11,    // closure of the Fn
    kVtNever = 12,      // Never
    kVtTupleNever = 13, // (Never)
    kVtMapIntInt = 14,  // Map<Int, Int>  (non-String key)
};

// Capability ids (declaration order) + their canonical SymbolIds.
enum : std::uint32_t {
    kStructCap = 0,  // Pair -> Color
    kOptListCap = 1, // Option<Int> -> List<Int>
    kCollCap = 2,    // (Set<Bool>, Map<String,Int>) -> (Int, Bool)
    kScalarCap = 3,  // (Int, String) -> Bool
    kFnCap = 4,      // fn -> Bool               (wire: UNSUPPORTED)
    kClosureCap = 5, // closure -> Bool          (wire: UNSUPPORTED)
    kNeverCap = 6,   // (Never) -> Bool          (wire: UNSUPPORTED)
    kMapKeyCap = 7,  // Map<Int,Int> -> Bool     (wire: UNSUPPORTED_MAP_KEY)
};

[[nodiscard]] CoreProgram make_wire_program() {
    CoreProgram p;

    // --- type table ---------------------------------------------------------
    // [0] struct Pair { a: Int, b: Bool }
    CoreTypeDecl pair;
    pair.kind = CoreTypeDecl::Kind::Struct;
    pair.name = "Pair";
    pair.fields = {"a", "b"};
    pair.field_nominal_types = {CoreTypeId{}, CoreTypeId{}}; // both primitive
    pair.field_has_default = {false, false};
    pair.member_type_templates = {concrete(CoreValueTypeId{kVtInt}),
                                  concrete(CoreValueTypeId{kVtBool})};
    pair.field_type_template_roots = {CoreMemberTypeTemplateNodeId{0},
                                      CoreMemberTypeTemplateNodeId{1}};
    p.types.push_back(std::move(pair));

    // [1] enum Color { Red, Green(Int) }
    CoreTypeDecl color;
    color.kind = CoreTypeDecl::Kind::Enum;
    color.name = "Color";
    color.variants = {"Red", "Green"};
    color.member_type_templates = {concrete(CoreValueTypeId{kVtInt})};
    CoreTypeDecl::VariantPayload red;
    CoreTypeDecl::VariantPayload green;
    green.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    green.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
    color.variant_payloads = {std::move(red), std::move(green)};
    p.types.push_back(std::move(color));

    // [2] std::option::Option<T>
    CoreTypeDecl option;
    option.kind = CoreTypeDecl::Kind::Enum;
    option.name = "std::option::Option";
    option.role = CoreNominalRole::Option;
    option.type_param_count = 1;
    option.variances = {CoreVariance::Covariant};
    option.variants = {"Some", "None"};
    option.member_type_templates = {param(0)};
    CoreTypeDecl::VariantPayload some;
    some.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    some.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
    option.variant_payloads = {std::move(some), CoreTypeDecl::VariantPayload{}};
    p.types.push_back(std::move(option));

    // [3..5] std collections.
    p.types.push_back(collection("std::collections::List", CoreNominalRole::List, 1,
                                 {CoreVariance::Covariant}));
    p.types.push_back(collection("std::collections::Set", CoreNominalRole::Set, 1,
                                 {CoreVariance::Covariant}));
    p.types.push_back(collection("std::collections::Map", CoreNominalRole::Map, 2,
                                 {CoreVariance::Invariant, CoreVariance::Covariant}));

    // --- value-type arena (topological) ------------------------------------
    p.value_types = {
        CoreValueType{CoreVtInt{}},                                            // 0
        CoreValueType{CoreVtBool{}},                                           // 1
        CoreValueType{CoreVtString{}},                                         // 2
        CoreValueType{CoreVtNominal{CoreTypeId{kPairTy}, {}, std::nullopt}},   // 3
        CoreValueType{CoreVtNominal{CoreTypeId{kColorTy}, {}, std::nullopt}},  // 4
        CoreValueType{
            CoreVtNominal{CoreTypeId{kOptionTy}, {CoreValueTypeId{kVtInt}}, std::nullopt}}, // 5
        CoreValueType{
            CoreVtNominal{CoreTypeId{kListTy}, {CoreValueTypeId{kVtInt}}, std::uint64_t{8}}}, // 6
        CoreValueType{
            CoreVtNominal{CoreTypeId{kSetTy}, {CoreValueTypeId{kVtBool}}, std::nullopt}}, // 7
        CoreValueType{CoreVtNominal{CoreTypeId{kMapTy},
                                    {CoreValueTypeId{kVtString}, CoreValueTypeId{kVtInt}},
                                    std::nullopt}},                                       // 8
        CoreValueType{CoreVtTuple{{CoreValueTypeId{kVtInt}, CoreValueTypeId{kVtBool}}}},  // 9
        CoreValueType{CoreVtFn{{}, CoreValueTypeId{kVtInt}}},                             // 10
        CoreValueType{CoreVtClosure{CoreValueTypeId{kVtFn}, {}}},                         // 11
        CoreValueType{CoreVtNever{}},                                                     // 12
        CoreValueType{CoreVtTuple{{CoreValueTypeId{kVtNever}}}},                          // 13
        CoreValueType{CoreVtNominal{CoreTypeId{kMapTy},
                                    {CoreValueTypeId{kVtInt}, CoreValueTypeId{kVtInt}},
                                    std::nullopt}}, // 14
    };

    // --- capability imports -------------------------------------------------
    const auto add_cap = [&](std::string name, std::vector<CoreValueTypeId> params,
                             CoreValueTypeId ret, std::size_t symbol_id) {
        CoreCapabilityDecl c;
        c.symbol_ref.kind = ir::SymbolRefKind::Capability;
        c.symbol_ref.canonical_name = "app::" + name;
        c.symbol_ref.id = symbol_id;
        c.name = std::move(name);
        c.param_types = std::move(params);
        c.return_type = ret;
        p.capabilities.push_back(std::move(c));
    };
    add_cap("StructCap", {CoreValueTypeId{kVtPair}}, CoreValueTypeId{kVtColor}, 10);
    add_cap("OptListCap", {CoreValueTypeId{kVtOptionInt}}, CoreValueTypeId{kVtListInt}, 11);
    add_cap("CollCap", {CoreValueTypeId{kVtSetBool}, CoreValueTypeId{kVtMapStrInt}},
            CoreValueTypeId{kVtTupleIB}, 12);
    add_cap("ScalarCap", {CoreValueTypeId{kVtInt}, CoreValueTypeId{kVtString}},
            CoreValueTypeId{kVtBool}, 13);
    add_cap("FnCap", {CoreValueTypeId{kVtFn}}, CoreValueTypeId{kVtBool}, 14);
    add_cap("ClosureCap", {CoreValueTypeId{kVtClosure}}, CoreValueTypeId{kVtBool}, 15);
    add_cap("NeverCap", {CoreValueTypeId{kVtTupleNever}}, CoreValueTypeId{kVtBool}, 16);
    add_cap("MapKeyCap", {CoreValueTypeId{kVtMapIntInt}}, CoreValueTypeId{kVtBool}, 17);

    return p;
}

[[nodiscard]] std::vector<CoreCapabilityId> caps(std::vector<std::uint32_t> ids) {
    std::vector<CoreCapabilityId> out;
    out.reserve(ids.size());
    for (const auto id : ids) {
        out.push_back(CoreCapabilityId{id});
    }
    return out;
}

} // namespace

TEST_CASE("the fixture CoreProgram verifies clean (capabilities-only program)") {
    const CoreProgram program = make_wire_program();
    const auto verification = verify_core_program(program);
    for (const auto &d : verification.diagnostics) {
        INFO("unexpected verifier diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(verification.ok());
}

TEST_CASE("wire projection lowers scalar capability signatures") {
    const CoreProgram program = make_wire_program();
    const auto result = project_core_wire_schema(program, caps({kScalarCap}));
    REQUIRE(result.ok());
    const auto &table = *result.table;
    CHECK(table.format_version == 1);
    REQUIRE(table.capabilities.size() == 1);
    const auto &cap = table.capabilities[0];
    CHECK(cap.capability.value == kScalarCap);
    CHECK(cap.source_symbol == 13);
    REQUIRE(cap.params.size() == 2);
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, cap.params[0])));
    CHECK(std::holds_alternative<CoreWireSchemaString>(shape_of(table, cap.params[1])));
    CHECK(std::holds_alternative<CoreWireSchemaBool>(shape_of(table, cap.result)));
}

TEST_CASE("wire projection lowers a struct parameter and an enum result") {
    const CoreProgram program = make_wire_program();
    const auto result = project_core_wire_schema(program, caps({kStructCap}));
    REQUIRE(result.ok());
    const auto &table = *result.table;
    REQUIRE(table.capabilities.size() == 1);
    const auto &cap = table.capabilities[0];
    REQUIRE(cap.params.size() == 1);

    const auto &pair = std::get<CoreWireSchemaStruct>(shape_of(table, cap.params[0]));
    CHECK(pair.wire_name == "Pair");
    REQUIRE(pair.fields.size() == 2);
    CHECK(pair.fields[0].wire_name == "a");
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, pair.fields[0].type)));
    CHECK(pair.fields[1].wire_name == "b");
    CHECK(std::holds_alternative<CoreWireSchemaBool>(shape_of(table, pair.fields[1].type)));

    const auto &color = std::get<CoreWireSchemaEnum>(shape_of(table, cap.result));
    CHECK(color.wire_name == "Color");
    REQUIRE(color.variants.size() == 2);
    CHECK(color.variants[0].wire_name == "Red");
    CHECK(color.variants[0].payload_kind == CoreWirePayloadKind::Unit);
    CHECK(color.variants[0].slots.empty());
    CHECK(color.variants[1].wire_name == "Green");
    CHECK(color.variants[1].payload_kind == CoreWirePayloadKind::Tuple);
    REQUIRE(color.variants[1].slots.size() == 1);
    CHECK(color.variants[1].slots[0].wire_name.empty());
    CHECK(std::holds_alternative<CoreWireSchemaInt>(
        shape_of(table, color.variants[1].slots[0].type)));
}

TEST_CASE("wire projection lowers Option and bounded sequence generics") {
    const CoreProgram program = make_wire_program();
    const auto result = project_core_wire_schema(program, caps({kOptListCap}));
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &cap = table.capabilities[0];
    REQUIRE(cap.params.size() == 1);

    const auto &option = std::get<CoreWireSchemaOption>(shape_of(table, cap.params[0]));
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, option.value)));

    const auto &list = std::get<CoreWireSchemaSequence>(shape_of(table, cap.result));
    CHECK(list.kind == CoreWireSequenceKind::List);
    CHECK(list.capacity == std::optional<std::uint64_t>{8});
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, list.element)));
}

TEST_CASE("wire projection lowers Set, String-keyed Map, and Tuple") {
    const CoreProgram program = make_wire_program();
    const auto result = project_core_wire_schema(program, caps({kCollCap}));
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &cap = table.capabilities[0];
    REQUIRE(cap.params.size() == 2);

    const auto &set = std::get<CoreWireSchemaSequence>(shape_of(table, cap.params[0]));
    CHECK(set.kind == CoreWireSequenceKind::Set);
    CHECK(std::holds_alternative<CoreWireSchemaBool>(shape_of(table, set.element)));

    const auto &map = std::get<CoreWireSchemaMap>(shape_of(table, cap.params[1]));
    CHECK(std::holds_alternative<CoreWireSchemaString>(shape_of(table, map.key)));
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, map.value)));

    const auto &tuple = std::get<CoreWireSchemaTuple>(shape_of(table, cap.result));
    REQUIRE(tuple.elements.size() == 2);
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, tuple.elements[0])));
    CHECK(std::holds_alternative<CoreWireSchemaBool>(shape_of(table, tuple.elements[1])));
}

TEST_CASE("wire projection over a multi-capability selection dedups shared nodes") {
    const CoreProgram program = make_wire_program();
    const auto result =
        project_core_wire_schema(program, caps({kStructCap, kOptListCap, kCollCap, kScalarCap}));
    REQUIRE(result.ok());
    const auto &table = *result.table;
    CHECK(table.capabilities.size() == 4);
    // Int/Bool/String are shared across many capabilities but interned once each.
    std::size_t ints = 0;
    std::size_t bools = 0;
    std::size_t strings = 0;
    for (const auto &node : table.nodes) {
        ints += std::holds_alternative<CoreWireSchemaInt>(node.shape);
        bools += std::holds_alternative<CoreWireSchemaBool>(node.shape);
        strings += std::holds_alternative<CoreWireSchemaString>(node.shape);
    }
    CHECK(ints == 1);
    CHECK(bools == 1);
    CHECK(strings == 1);
}

TEST_CASE("wire projection is deterministic and reprojection-verifiable") {
    const CoreProgram program = make_wire_program();
    const auto selection = caps({kStructCap, kOptListCap, kCollCap, kScalarCap});
    const auto first = project_core_wire_schema(program, selection);
    const auto second = project_core_wire_schema(program, selection);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(*first.table == *second.table);
    // A faithfully-projected table verifies clean against its Core source.
    CHECK(verify_core_wire_schema_table(program, selection, *first.table).empty());
}

TEST_CASE("table verifier rejects a tampered reprojection and a bad format version") {
    const CoreProgram program = make_wire_program();
    const auto selection = caps({kScalarCap});
    const auto projected = project_core_wire_schema(program, selection);
    REQUIRE(projected.ok());

    SUBCASE("reprojection mismatch") {
        CoreWireSchemaTable tampered = *projected.table;
        // Flip the Bool result node to a Unit shape: still a locally-valid graph,
        // but no longer the deterministic Core reprojection.
        for (auto &node : tampered.nodes) {
            if (std::holds_alternative<CoreWireSchemaBool>(node.shape)) {
                node.shape = CoreWireSchemaUnit{};
            }
        }
        const auto diagnostics = verify_core_wire_schema_table(program, selection, tampered);
        CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
    }

    SUBCASE("unsupported format version fails the local pass") {
        CoreWireSchemaTable tampered = *projected.table;
        tampered.format_version = 2;
        const auto diagnostics = verify_core_wire_schema_table(program, selection, tampered);
        CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
    }
}

TEST_CASE("wire projection fails closed on value_json-unrepresentable types") {
    const CoreProgram program = make_wire_program();

    SUBCASE("function value") {
        const auto result = project_core_wire_schema(program, caps({kFnCap}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kUnsupported)));
    }

    SUBCASE("closure value") {
        const auto result = project_core_wire_schema(program, caps({kClosureCap}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kUnsupported)));
    }

    SUBCASE("Never nested in a materialized aggregate") {
        const auto result = project_core_wire_schema(program, caps({kNeverCap}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kUnsupported)));
    }

    SUBCASE("Map with a non-String key") {
        const auto result = project_core_wire_schema(program, caps({kMapKeyCap}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kUnsupportedMapKey)));
    }
}

TEST_CASE("wire projection requires a canonical capability selection") {
    const CoreProgram program = make_wire_program();

    SUBCASE("non-ascending / duplicate ids") {
        const auto result = project_core_wire_schema(program, caps({kScalarCap, kStructCap}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kInvalidSelection)));
    }

    SUBCASE("duplicate id") {
        const auto result = project_core_wire_schema(program, caps({kStructCap, kStructCap}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kInvalidSelection)));
    }

    SUBCASE("out-of-range id") {
        const auto result = project_core_wire_schema(program, caps({999}));
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.diagnostics, std::string(wire_schema::kInvalidSelection)));
    }
}

TEST_CASE("wire projection refuses an unverified CoreProgram") {
    CoreProgram program = make_wire_program();
    // Duplicate a capability SymbolId so `verify_core_program` fails closed.
    CoreCapabilityDecl duplicate = program.capabilities[kScalarCap];
    duplicate.name = "ScalarCapAgain";
    program.capabilities.push_back(std::move(duplicate));
    const auto result = project_core_wire_schema(program, caps({kScalarCap}));
    CHECK_FALSE(result.ok());
    CHECK(has_code(result.diagnostics, std::string(wire_schema::kInvalidCore)));
}

TEST_CASE("wire encoder emits a deterministic, magic-prefixed payload") {
    const CoreProgram program = make_wire_program();
    const auto selection = caps({kStructCap, kOptListCap, kCollCap, kScalarCap});
    const auto projected = project_core_wire_schema(program, selection);
    REQUIRE(projected.ok());

    const auto first = encode_core_wire_schema_table(*projected.table);
    REQUIRE(first.ok());
    const auto &bytes = *first.bytes;
    REQUIRE(bytes.size() >= 6);
    const std::vector<std::uint8_t> magic{'A', 'H', 'F', 'L', 'W', 'S'};
    CHECK(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 6) == magic);

    const auto second = encode_core_wire_schema_table(*projected.table);
    REQUIRE(second.ok());
    CHECK(*first.bytes == *second.bytes);
}
