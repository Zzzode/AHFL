#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
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

// True iff some ERROR diagnostic's message contains `needle` — used to prove a
// negative hits the INTENDED gate (all wire gates share the kInvalid code, so
// the message is what distinguishes local-order vs reprojection vs shape gates).
[[nodiscard]] bool has_message(const std::vector<CoreLowerDiagnostic> &diagnostics,
                               std::string_view needle) {
    for (const auto &d : diagnostics) {
        if (d.severity == CoreDiagnosticSeverity::Error &&
            d.message.find(needle) != std::string::npos) {
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

// The P0 regression fixture, shared in spirit with core_layout: a generic user
// struct with TWO generic fields, each instantiating a distinct bounded List<T>.
// Projecting/laying-out the second field re-reads the owner nominal AFTER the
// first field's member instantiation appended to the scratch value-type arena —
// exactly the use-after-free the by-value snapshot fixes. Also carries a Result
// return and a recursive nominal so §7.1 recursion/Result gaps are covered.
//
//   struct Box<T> { a: List<T>(4), b: List<T>(8) }
//   struct Tree { children: List<Tree>(4) }   // recursive via bounded List
//   enum Pair<A,B> is modeled with Result<A,B> (builtin) for the Result case.
//   cap GenCap(Box<Int>) -> Result<Int, String>
//   cap TreeCap(Tree)    -> Bool
struct GenericProgram {
    CoreProgram program;
    CoreCapabilityId gen_cap{};
    CoreCapabilityId tree_cap{};
    CoreValueTypeId box_int{};
};

enum : std::uint32_t {
    kGenListTy = 0,   // std::collections::List
    kGenBoxTy = 1,    // Box<T>
    kGenTreeTy = 2,   // Tree
    kGenResultTy = 3, // std::result::Result
};

[[nodiscard]] GenericProgram make_generic_program() {
    GenericProgram g;
    CoreProgram &p = g.program;

    // [0] List<T>
    p.types.push_back(collection("std::collections::List", CoreNominalRole::List, 1,
                                 {CoreVariance::Covariant}));

    // [1] struct Box<T> { a: List<T>(4), b: List<T>(8) }
    CoreTypeDecl box;
    box.kind = CoreTypeDecl::Kind::Struct;
    box.name = "Box";
    box.fields = {"a", "b"};
    box.field_nominal_types = {CoreTypeId{kGenListTy}, CoreTypeId{kGenListTy}};
    box.field_has_default = {false, false};
    box.type_param_count = 1;
    box.variances = {CoreVariance::Covariant};
    // templates: [0] Param(0)=T, [1] List<T>(4), [2] List<T>(8).
    box.member_type_templates = {param(0)};
    CoreMemberTypeTemplateNode list4;
    list4.kind = CoreMemberTypeTemplateKind::Nominal;
    list4.nominal = CoreTypeId{kGenListTy};
    list4.children = {CoreMemberTypeTemplateNodeId{0}};
    list4.capacity = std::uint64_t{4};
    box.member_type_templates.push_back(list4);
    CoreMemberTypeTemplateNode list8 = list4;
    list8.capacity = std::uint64_t{8};
    box.member_type_templates.push_back(list8);
    box.field_type_template_roots = {CoreMemberTypeTemplateNodeId{1},
                                     CoreMemberTypeTemplateNodeId{2}};
    p.types.push_back(std::move(box));

    // [2] struct Tree { children: List<Tree>(4) }
    CoreTypeDecl tree;
    tree.kind = CoreTypeDecl::Kind::Struct;
    tree.name = "Tree";
    tree.fields = {"children"};
    tree.field_nominal_types = {CoreTypeId{kGenListTy}};
    tree.field_has_default = {false};
    // templates: [0] Nominal Tree (self), [1] List<Tree>(4).
    CoreMemberTypeTemplateNode self;
    self.kind = CoreMemberTypeTemplateKind::Nominal;
    self.nominal = CoreTypeId{kGenTreeTy};
    tree.member_type_templates = {self};
    CoreMemberTypeTemplateNode tree_list;
    tree_list.kind = CoreMemberTypeTemplateKind::Nominal;
    tree_list.nominal = CoreTypeId{kGenListTy};
    tree_list.children = {CoreMemberTypeTemplateNodeId{0}};
    tree_list.capacity = std::uint64_t{4};
    tree.member_type_templates.push_back(tree_list);
    tree.field_type_template_roots = {CoreMemberTypeTemplateNodeId{1}};
    p.types.push_back(std::move(tree));

    // [3] std::result::Result<A,B>
    CoreTypeDecl result;
    result.kind = CoreTypeDecl::Kind::Enum;
    result.name = "std::result::Result";
    result.role = CoreNominalRole::Result;
    result.type_param_count = 2;
    result.variances = {CoreVariance::Covariant, CoreVariance::Covariant};
    result.variants = {"Ok", "Err"};
    result.member_type_templates = {param(0), param(1)};
    CoreTypeDecl::VariantPayload ok;
    ok.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    ok.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
    CoreTypeDecl::VariantPayload err;
    err.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    err.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{1}};
    result.variant_payloads = {std::move(ok), std::move(err)};
    p.types.push_back(std::move(result));

    // value-type arena (topological).
    p.value_types = {
        CoreValueType{CoreVtInt{}},                                             // 0
        CoreValueType{CoreVtString{}},                                          // 1
        CoreValueType{CoreVtBool{}},                                            // 2
        CoreValueType{CoreVtNominal{CoreTypeId{kGenBoxTy},
                                    {CoreValueTypeId{0}}, std::nullopt}},        // 3 Box<Int>
        CoreValueType{CoreVtNominal{CoreTypeId{kGenTreeTy}, {}, std::nullopt}},  // 4 Tree
        CoreValueType{CoreVtNominal{CoreTypeId{kGenResultTy},
                                    {CoreValueTypeId{0}, CoreValueTypeId{1}},
                                    std::nullopt}},                             // 5 Result<Int,String>
    };
    g.box_int = CoreValueTypeId{3};

    const auto add_cap = [&](std::string name, std::vector<CoreValueTypeId> params,
                             CoreValueTypeId ret, std::size_t symbol_id) {
        const CoreCapabilityId id{static_cast<std::uint32_t>(p.capabilities.size())};
        CoreCapabilityDecl c;
        c.symbol_ref.kind = ir::SymbolRefKind::Capability;
        c.symbol_ref.canonical_name = "app::" + name;
        c.symbol_ref.id = symbol_id;
        c.name = std::move(name);
        c.param_types = std::move(params);
        c.return_type = ret;
        p.capabilities.push_back(std::move(c));
        return id;
    };
    g.gen_cap = add_cap("GenCap", {CoreValueTypeId{3}}, CoreValueTypeId{5}, 20);
    g.tree_cap = add_cap("TreeCap", {CoreValueTypeId{4}}, CoreValueTypeId{2}, 21);
    return g;
}

// A fixture exercising every remaining directly-projected scalar/refinement
// shape plus an enum with a Struct-form payload variant.
//   enum Shape { Dot, Line{len: BoundedInt(0,100), label: BoundedString(1,8)} }
//   cap ScalarsCap(Unit, Float, Decimal(3), Duration, Timestamp, Uuid,
//                  BoundedInt(-5,5), BoundedString(2,4)) -> Shape
struct ScalarProgram {
    CoreProgram program;
    CoreCapabilityId cap{};
};

[[nodiscard]] ScalarProgram make_scalar_program() {
    ScalarProgram s;
    CoreProgram &p = s.program;

    // value-type arena (topological). Bounded refinements carry their bounds.
    p.value_types = {
        CoreValueType{CoreVtUnit{}},                                                 // 0
        CoreValueType{CoreVtFloat{}},                                                // 1
        CoreValueType{CoreVtDecimal{3}},                                             // 2
        CoreValueType{CoreVtDuration{}},                                             // 3
        CoreValueType{CoreVtTimestamp{}},                                            // 4
        CoreValueType{CoreVtUuid{}},                                                 // 5
        CoreValueType{CoreVtInt{std::pair<std::int64_t, std::int64_t>{-5, 5}}},      // 6
        CoreValueType{CoreVtString{std::pair<std::int64_t, std::int64_t>{2, 4}}},    // 7
        CoreValueType{CoreVtInt{std::pair<std::int64_t, std::int64_t>{0, 100}}},     // 8 Line.len
        CoreValueType{CoreVtString{std::pair<std::int64_t, std::int64_t>{1, 8}}},    // 9 Line.label
    };

    // enum Shape { Dot (unit), Line{len, label} (struct payload) }.
    CoreTypeDecl shape;
    shape.kind = CoreTypeDecl::Kind::Enum;
    shape.name = "Shape";
    shape.variants = {"Dot", "Line"};
    shape.member_type_templates = {concrete(CoreValueTypeId{8}), concrete(CoreValueTypeId{9})};
    CoreTypeDecl::VariantPayload dot;
    CoreTypeDecl::VariantPayload line;
    line.kind = CoreTypeDecl::VariantPayload::Kind::Struct;
    line.field_names = {"len", "label"};
    line.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0},
                                     CoreMemberTypeTemplateNodeId{1}};
    shape.variant_payloads = {std::move(dot), std::move(line)};
    p.types.push_back(std::move(shape));

    CoreCapabilityDecl c;
    c.symbol_ref.kind = ir::SymbolRefKind::Capability;
    c.symbol_ref.canonical_name = "app::ScalarsCap";
    c.symbol_ref.id = 30;
    c.name = "ScalarsCap";
    c.param_types = {CoreValueTypeId{0}, CoreValueTypeId{1}, CoreValueTypeId{2},
                     CoreValueTypeId{3}, CoreValueTypeId{4}, CoreValueTypeId{5},
                     CoreValueTypeId{6}, CoreValueTypeId{7}};
    c.return_type = CoreValueTypeId{static_cast<std::uint32_t>(p.value_types.size())};
    // Shape nominal value type (the enum result).
    p.value_types.push_back(CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::nullopt}});
    s.cap = CoreCapabilityId{0};
    p.capabilities.push_back(std::move(c));
    return s;
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

// --- P0 regression + §7.1 generic / recursion / Result coverage ------------

TEST_CASE("the generic fixture CoreProgram verifies clean") {
    const GenericProgram g = make_generic_program();
    const auto verification = verify_core_program(g.program);
    for (const auto &d : verification.diagnostics) {
        INFO("unexpected verifier diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(verification.ok());
}

TEST_CASE("wire projection of a two-generic-field struct is UAF-safe (P0 regression)") {
    // Box<Int> { a: List<Int>(4), b: List<Int>(8) }. Projecting field `b` re-reads
    // the owner nominal after field `a`'s member instantiation reallocated the
    // scratch value-type arena. Under ASan this is the heap-use-after-free the
    // by-value snapshot fixes; under any sanitizer-free build it must still emit
    // the correct two-field struct with distinct capacities.
    const GenericProgram g = make_generic_program();
    const auto result = project_core_wire_schema(g.program, {g.gen_cap});
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &cap = table.capabilities[0];
    REQUIRE(cap.params.size() == 1);

    const auto &box = std::get<CoreWireSchemaStruct>(shape_of(table, cap.params[0]));
    CHECK(box.wire_name == "Box");
    REQUIRE(box.fields.size() == 2);
    const auto &list_a = std::get<CoreWireSchemaSequence>(shape_of(table, box.fields[0].type));
    const auto &list_b = std::get<CoreWireSchemaSequence>(shape_of(table, box.fields[1].type));
    CHECK(list_a.kind == CoreWireSequenceKind::List);
    CHECK(list_a.capacity == std::optional<std::uint64_t>{4});
    CHECK(list_b.capacity == std::optional<std::uint64_t>{8});
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, list_a.element)));
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, list_b.element)));

    // Result<Int, String> return: an enum with two Tuple-payload variants.
    const auto &res = std::get<CoreWireSchemaEnum>(shape_of(table, cap.result));
    CHECK(res.wire_name == "std::result::Result");
    REQUIRE(res.variants.size() == 2);
    CHECK(res.variants[0].wire_name == "Ok");
    CHECK(res.variants[0].payload_kind == CoreWirePayloadKind::Tuple);
    REQUIRE(res.variants[0].slots.size() == 1);
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, res.variants[0].slots[0].type)));
    CHECK(res.variants[1].wire_name == "Err");
    REQUIRE(res.variants[1].slots.size() == 1);
    CHECK(
        std::holds_alternative<CoreWireSchemaString>(shape_of(table, res.variants[1].slots[0].type)));

    CHECK(project_core_wire_schema(g.program, {g.gen_cap}).table == result.table);
}

TEST_CASE("wire projection resolves a recursive nominal via a bounded sequence") {
    // Tree { children: List<Tree>(4) } — the List element schema node must refer
    // back to the reserved Tree struct node (reserve-before-descend), and the
    // table must verify (a cycle is legal when every ref is valid + reachable).
    const GenericProgram g = make_generic_program();
    const auto result = project_core_wire_schema(g.program, {g.tree_cap});
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &cap = table.capabilities[0];
    REQUIRE(cap.params.size() == 1);

    const auto tree_id = cap.params[0];
    const auto &tree = std::get<CoreWireSchemaStruct>(shape_of(table, tree_id));
    CHECK(tree.wire_name == "Tree");
    REQUIRE(tree.fields.size() == 1);
    const auto &list = std::get<CoreWireSchemaSequence>(shape_of(table, tree.fields[0].type));
    CHECK(list.capacity == std::optional<std::uint64_t>{4});
    CHECK(list.element == tree_id); // recursive back-reference
    CHECK(verify_core_wire_schema_table(g.program, {g.tree_cap}, table).empty());
}

TEST_CASE("wire node order is canonical: ascending program-global value type id") {
    // Design §2.2: nodes are ordered by program-global CoreValueTypeId, NOT by
    // capability params/result discovery order. ScalarCap params=[Int(vt0),
    // String(vt2)], result=Bool(vt1): the node arena must therefore be
    // [Int, Bool, String] (source ids 0,1,2), regardless of the param order that
    // discovers String before Bool.
    const CoreProgram program = make_wire_program();
    const auto result = project_core_wire_schema(program, caps({kScalarCap}));
    REQUIRE(result.ok());
    const auto &table = *result.table;
    REQUIRE(table.nodes.size() == 3);
    CHECK(std::holds_alternative<CoreWireSchemaInt>(table.nodes[0].shape));
    CHECK(std::holds_alternative<CoreWireSchemaBool>(table.nodes[1].shape));
    CHECK(std::holds_alternative<CoreWireSchemaString>(table.nodes[2].shape));
    // The capability edges must have been remapped to point at the reordered ids.
    const auto &cap = table.capabilities[0];
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(table, cap.params[0])));
    CHECK(std::holds_alternative<CoreWireSchemaString>(shape_of(table, cap.params[1])));
    CHECK(std::holds_alternative<CoreWireSchemaBool>(shape_of(table, cap.result)));
}

TEST_CASE("repeated projection of the same selection is byte-identical") {
    // Determinism guard: the same canonical selection projects an identical table
    // every time. (Discovery-order independence — that node order follows source
    // value-type id, not param/result DFS — is proven separately by the canonical
    // node-order case above; this case only asserts repeated projection is stable.)
    const CoreProgram program = make_wire_program();
    const auto a = project_core_wire_schema(program, caps({kCollCap, kScalarCap}));
    const auto b = project_core_wire_schema(program, caps({kCollCap, kScalarCap}));
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(*a.table == *b.table);
    const auto ea = encode_core_wire_schema_table(*a.table);
    const auto eb = encode_core_wire_schema_table(*b.table);
    REQUIRE(ea.ok());
    REQUIRE(eb.ok());
    CHECK(*ea.bytes == *eb.bytes);
}

// --- local verifier hardening (P1-1 orphan gate + hand-built negatives) ----

TEST_CASE("table verifier rejects an orphan node even with an empty capability set") {
    // P1-1: reachability must be seeded unconditionally. A table with a node but
    // no capability roots has an unreachable (orphan) node and must be rejected —
    // both by the standalone verifier and by the encoder (no partial publish).
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaUnit{}});
    const CoreProgram program = make_wire_program();
    const auto diagnostics = verify_core_wire_schema_table(program, {}, table);
    CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));

    const auto encoded = encode_core_wire_schema_table(table);
    CHECK_FALSE(encoded.ok());
}

TEST_CASE("an empty table (no nodes, no capabilities) is legal") {
    CoreWireSchemaTable table;
    const CoreProgram program = make_wire_program();
    CHECK(verify_core_wire_schema_table(program, {}, table).empty());
    const auto encoded = encode_core_wire_schema_table(table);
    CHECK(encoded.ok());
}

TEST_CASE("table verifier rejects a hand-built table with a bad node reference") {
    // A struct field pointing past the node arena must fail the local pass.
    CoreWireSchemaTable table;
    CoreWireSchemaStruct s;
    s.wire_name = "Bad";
    s.fields.push_back(CoreWireSchemaField{"x", CoreWireSchemaNodeId{7}});
    table.nodes.push_back(CoreWireSchemaNode{std::move(s)});
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.result = CoreWireSchemaNodeId{0};
    table.capabilities.push_back(std::move(cap));
    const CoreProgram program = make_wire_program();
    const auto diagnostics = verify_core_wire_schema_table(program, caps({0}), table);
    CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
}

// --- §7.1 coverage closure: direct scalars/refinements + enum struct payload --

TEST_CASE("the scalar fixture CoreProgram verifies clean") {
    const ScalarProgram s = make_scalar_program();
    const auto verification = verify_core_program(s.program);
    for (const auto &d : verification.diagnostics) {
        INFO("unexpected verifier diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    CHECK(verification.ok());
}

TEST_CASE("wire projection lowers every scalar and refinement shape verbatim") {
    const ScalarProgram s = make_scalar_program();
    const auto result = project_core_wire_schema(s.program, {s.cap});
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &cap = table.capabilities[0];
    REQUIRE(cap.params.size() == 8);

    CHECK(std::holds_alternative<CoreWireSchemaUnit>(shape_of(table, cap.params[0])));
    CHECK(std::holds_alternative<CoreWireSchemaFloat>(shape_of(table, cap.params[1])));
    const auto &decimal = std::get<CoreWireSchemaDecimal>(shape_of(table, cap.params[2]));
    CHECK(decimal.scale == 3);
    CHECK(std::holds_alternative<CoreWireSchemaDuration>(shape_of(table, cap.params[3])));
    CHECK(std::holds_alternative<CoreWireSchemaTimestamp>(shape_of(table, cap.params[4])));
    CHECK(std::holds_alternative<CoreWireSchemaUuid>(shape_of(table, cap.params[5])));

    const auto &bounded_int = std::get<CoreWireSchemaInt>(shape_of(table, cap.params[6]));
    REQUIRE(bounded_int.bounds.has_value());
    CHECK(bounded_int.bounds->first == -5);
    CHECK(bounded_int.bounds->second == 5);
    const auto &bounded_str = std::get<CoreWireSchemaString>(shape_of(table, cap.params[7]));
    REQUIRE(bounded_str.length_bounds.has_value());
    CHECK(bounded_str.length_bounds->first == 2);
    CHECK(bounded_str.length_bounds->second == 4);
}

TEST_CASE("wire projection lowers an enum variant with a Struct payload") {
    const ScalarProgram s = make_scalar_program();
    const auto result = project_core_wire_schema(s.program, {s.cap});
    REQUIRE(result.ok());
    const auto &table = *result.table;
    const auto &shape = std::get<CoreWireSchemaEnum>(shape_of(table, table.capabilities[0].result));
    CHECK(shape.wire_name == "Shape");
    REQUIRE(shape.variants.size() == 2);
    CHECK(shape.variants[0].wire_name == "Dot");
    CHECK(shape.variants[0].payload_kind == CoreWirePayloadKind::Unit);
    CHECK(shape.variants[0].slots.empty());

    const auto &line = shape.variants[1];
    CHECK(line.wire_name == "Line");
    CHECK(line.payload_kind == CoreWirePayloadKind::Struct);
    REQUIRE(line.slots.size() == 2);
    CHECK(line.slots[0].wire_name == "len"); // struct payload carries field names
    const auto &len = std::get<CoreWireSchemaInt>(shape_of(table, line.slots[0].type));
    REQUIRE(len.bounds.has_value());
    CHECK(len.bounds->second == 100);
    CHECK(line.slots[1].wire_name == "label");
    CHECK(std::holds_alternative<CoreWireSchemaString>(shape_of(table, line.slots[1].type)));

    CHECK(verify_core_wire_schema_table(s.program, {s.cap}, table).empty());
}

// --- §7.1 coverage closure: table verifier fail-closed negatives -----------

TEST_CASE("public verifier rejects a duplicated capability entry at the local gate") {
    // Two capability roots with the same id violate strictly-increasing/unique.
    // This is a LOCAL gate: it fires before reprojection is even attempted.
    const CoreProgram program = make_wire_program();
    const auto projected = project_core_wire_schema(program, caps({kScalarCap}));
    REQUIRE(projected.ok());
    CoreWireSchemaTable table = *projected.table;
    table.capabilities.push_back(table.capabilities[0]); // duplicate root id
    const auto diagnostics = verify_core_wire_schema_table(program, caps({kScalarCap}), table);
    CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
    CHECK(has_message(diagnostics, "capability roots are not strictly ordered")); // local gate
}

TEST_CASE("public verifier rejects a wrong source_symbol at the reprojection gate") {
    // A tampered source_symbol is locally well-formed (the local pass does not
    // know Core symbols), so it must be caught by the deterministic reprojection
    // equality against the verified Core signatures.
    const CoreProgram program = make_wire_program();
    const auto selection = caps({kScalarCap});
    const auto projected = project_core_wire_schema(program, selection);
    REQUIRE(projected.ok());
    // The untampered table passes BOTH gates (local + reprojection).
    REQUIRE(verify_core_wire_schema_table(program, selection, *projected.table).empty());
    CoreWireSchemaTable table = *projected.table;
    // Mutate ONLY source_symbol; the node graph and roots are untouched. The
    // local pass does not inspect source_symbol, so the rejection below can only
    // come from the deterministic reprojection gate.
    table.capabilities[0].source_symbol += 1;
    const auto diagnostics = verify_core_wire_schema_table(program, selection, table);
    CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
    CHECK(has_message(diagnostics, "deterministic Core reprojection")); // reprojection gate
}

TEST_CASE("projection fails closed with no table on a wrong-arity nominal (unverified Core)") {
    // A nominal value type whose arg count disagrees with its declaration makes
    // verify_core_program fail, so projection must refuse with kInvalidCore and
    // publish no table.
    CoreProgram program = make_wire_program();
    // kVtOptionInt (#5) is Option<Int>; drop its arg so arity (0) != decl (1).
    std::get<CoreVtNominal>(program.value_types[kVtOptionInt].node).args.clear();
    const auto result = project_core_wire_schema(program, caps({kOptListCap}));
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.table.has_value());
    CHECK(has_code(result.diagnostics, std::string(wire_schema::kInvalidCore)));
}

TEST_CASE("public verifier and encoder reject an unknown wire enum underlying value") {
    const CoreProgram program = make_wire_program();

    SUBCASE("unknown sequence kind") {
        const auto projected = project_core_wire_schema(program, caps({kOptListCap}));
        REQUIRE(projected.ok());
        CoreWireSchemaTable table = *projected.table;
        for (auto &node : table.nodes) {
            if (auto *seq = std::get_if<CoreWireSchemaSequence>(&node.shape)) {
                seq->kind = static_cast<CoreWireSequenceKind>(7); // out of range
            }
        }
        CHECK(has_code(verify_core_wire_schema_table(program, caps({kOptListCap}), table),
                       std::string(wire_schema::kInvalid)));
        CHECK(has_message(verify_core_wire_schema_table(program, caps({kOptListCap}), table),
                          "sequence kind is invalid")); // local shape gate
        CHECK_FALSE(encode_core_wire_schema_table(table).ok());
    }

    SUBCASE("unknown enum payload kind") {
        const auto projected = project_core_wire_schema(program, caps({kStructCap}));
        REQUIRE(projected.ok());
        CoreWireSchemaTable table = *projected.table;
        for (auto &node : table.nodes) {
            if (auto *en = std::get_if<CoreWireSchemaEnum>(&node.shape)) {
                en->variants[0].payload_kind = static_cast<CoreWirePayloadKind>(9);
            }
        }
        CHECK(has_code(verify_core_wire_schema_table(program, caps({kStructCap}), table),
                       std::string(wire_schema::kInvalid)));
        CHECK(has_message(verify_core_wire_schema_table(program, caps({kStructCap}), table),
                          "payload kind is invalid")); // local shape gate
        CHECK_FALSE(encode_core_wire_schema_table(table).ok());
    }
}

// RFC 0026 C2b P0-9: an Option whose DIRECT child itself encodes as JSON null
// (Unit, or another Option) makes None and Some(child-null) indistinguishable on
// the wire. The single owning gate lives in the local verifier's Option branch,
// so it covers BOTH the source projector (which runs verify_local before
// publishing and resets its table on failure) and any transported/hand-built
// table (which is admitted only through the same public local verifier).
namespace {

// Wrap a finished node vector + result node in a one-capability table so the
// public local verifier can be exercised on a hand-built (transported) table.
[[nodiscard]] CoreWireSchemaTable
wrap_result_table(std::vector<CoreWireSchemaNode> nodes, CoreWireSchemaNodeId result) {
    CoreWireSchemaTable table;
    table.nodes = std::move(nodes);
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.source_symbol = 7;
    cap.result = result;
    table.capabilities.push_back(cap);
    return table;
}

} // namespace

TEST_CASE("local verifier rejects an Option with a null-encoding direct child (P0-9)") {
    SUBCASE("Option<Unit> is rejected as UNSUPPORTED") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaUnit{}});                       // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
        CHECK(has_message(diagnostics, "encodes as JSON null"));
    }

    SUBCASE("Option<Option<Int>> is rejected as UNSUPPORTED") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});                        // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1 inner
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{1}}}); // 2 outer
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{2});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
        CHECK(has_message(diagnostics, "encodes as JSON null"));
    }

    SUBCASE("a self-referential Option cycle is rejected") {
        // node 0 = Option whose child is itself: a legal cyclic graph, but the
        // child shape is Option, so the nullable-child gate rejects it.
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 0
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
    }
}

TEST_CASE("local verifier still accepts Option of a non-null-encoding child (P0-9)") {
    SUBCASE("Option<Int> stays legal") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});                        // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        CHECK(verify_core_wire_schema_table_local(table).empty());
    }

    SUBCASE("recursive Struct with an Option<Struct> field stays legal") {
        // Node { next: Option<Node> } — the Option child is a Struct, which never
        // encodes as null, so this recursive schema must NOT be false-rejected.
        std::vector<CoreWireSchemaNode> nodes;
        CoreWireSchemaStruct node_struct;
        node_struct.wire_name = "app::Node";
        node_struct.fields.push_back(CoreWireSchemaField{"next", CoreWireSchemaNodeId{1}});
        nodes.push_back(CoreWireSchemaNode{node_struct});                                   // 0 Node
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1 Option<Node>
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
        CHECK(verify_core_wire_schema_table_local(table).empty());
    }
}

TEST_CASE("source projector accepts a recursive Struct with an Option<Struct> field (P0-9)") {
    // struct Node { next: Option<Node> } — the Option child is a Struct (never
    // null-encoding), so this recursive schema must project. It also exercises the
    // builder's reserve-placeholder-then-backfill path (Node reserved while its
    // Option<Node> field projects), guarding against a future regression that
    // moves the nullable-child gate into build_nominal and mistakes a reserved
    // default-Unit node for a real Unit child.
    CoreProgram program;

    // [0] std::option::Option<T> — full metadata so verify_core_program passes.
    CoreTypeDecl option;
    option.kind = CoreTypeDecl::Kind::Enum;
    option.name = "std::option::Option";
    option.role = CoreNominalRole::Option;
    option.symbol_ref.kind = ir::SymbolRefKind::Type;
    option.symbol_ref.canonical_name = "std::option::Option";
    option.symbol_ref.id = 1;
    option.type_param_count = 1;
    option.variances = {CoreVariance::Covariant};
    option.variants = {"Some", "None"};
    option.member_type_templates = {param(0)};
    CoreTypeDecl::VariantPayload some;
    some.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
    some.slot_type_template_roots = {CoreMemberTypeTemplateNodeId{0}};
    option.variant_payloads = {std::move(some), CoreTypeDecl::VariantPayload{}};
    program.types.push_back(std::move(option));
    constexpr std::uint32_t kNodeOptionTy = 0;

    // [1] struct Node { next: Option<Node> }.
    CoreTypeDecl node;
    node.kind = CoreTypeDecl::Kind::Struct;
    node.name = "app::Node";
    node.symbol_ref.kind = ir::SymbolRefKind::Type;
    node.symbol_ref.canonical_name = "app::Node";
    node.symbol_ref.id = 2;
    node.fields = {"next"};
    node.field_nominal_types = {CoreTypeId{kNodeOptionTy}};
    node.field_has_default = {false};
    constexpr std::uint32_t kNodeTy = 1;
    // templates: [0] Nominal Node (self), [1] Option<Node>.
    CoreMemberTypeTemplateNode self;
    self.kind = CoreMemberTypeTemplateKind::Nominal;
    self.nominal = CoreTypeId{kNodeTy};
    node.member_type_templates = {self};
    CoreMemberTypeTemplateNode option_node;
    option_node.kind = CoreMemberTypeTemplateKind::Nominal;
    option_node.nominal = CoreTypeId{kNodeOptionTy};
    option_node.children = {CoreMemberTypeTemplateNodeId{0}};
    node.member_type_templates.push_back(option_node);
    node.field_type_template_roots = {CoreMemberTypeTemplateNodeId{1}};
    program.types.push_back(std::move(node));

    // value-type arena: just the Node nominal root.
    program.value_types = {
        CoreValueType{CoreVtNominal{CoreTypeId{kNodeTy}, {}, std::nullopt}}, // 0 Node
    };

    CoreCapabilityDecl cap;
    cap.symbol_ref.kind = ir::SymbolRefKind::Capability;
    cap.symbol_ref.canonical_name = "app::NodeCap";
    cap.symbol_ref.id = 100;
    cap.name = "NodeCap";
    cap.return_type = CoreValueTypeId{0};
    program.capabilities.push_back(std::move(cap));

    REQUIRE(verify_core_program(program).ok());

    const auto result = project_core_wire_schema(program, caps({0}));
    REQUIRE(result.ok());
    REQUIRE(result.table.has_value());
    CHECK(verify_core_wire_schema_table_local(*result.table).empty());

    // root is a Struct whose `next` field points at an Option whose child points
    // back at the same Struct node (recursive cycle round-trips).
    const auto &table = *result.table;
    const auto &cap_schema = table.capabilities.at(0);
    const auto &root_shape = shape_of(table, cap_schema.result);
    const auto *node_struct = std::get_if<CoreWireSchemaStruct>(&root_shape);
    REQUIRE(node_struct != nullptr);
    REQUIRE(node_struct->fields.size() == 1);
    const auto &next_shape = shape_of(table, node_struct->fields[0].type);
    const auto *next_option = std::get_if<CoreWireSchemaOption>(&next_shape);
    REQUIRE(next_option != nullptr);
    // The Option child closes the schema graph by back-referencing the Node root.
    CHECK(next_option->value == cap_schema.result);
}

TEST_CASE("source projector fails closed on Option<Unit> without publishing a table (P0-9)") {
    // Reuse the fully-valid std Option fixture and append a Unit value type + an
    // Option<Unit> value type + a capability returning it, so the program passes
    // verify_core_program and the projector reaches the nullable-child gate (a
    // half-built hand fixture would fail earlier with INVALID_CORE and prove
    // nothing).
    CoreProgram program = make_wire_program();
    REQUIRE(verify_core_program(program).ok());

    const auto unit_vt = CoreValueTypeId{static_cast<std::uint32_t>(program.value_types.size())};
    program.value_types.push_back(CoreValueType{CoreVtUnit{}});
    const auto option_unit_vt =
        CoreValueTypeId{static_cast<std::uint32_t>(program.value_types.size())};
    program.value_types.push_back(
        CoreValueType{CoreVtNominal{CoreTypeId{kOptionTy}, {unit_vt}, std::nullopt}});

    const auto cap_index = static_cast<std::uint32_t>(program.capabilities.size());
    CoreCapabilityDecl cap;
    cap.symbol_ref.kind = ir::SymbolRefKind::Capability;
    cap.symbol_ref.canonical_name = "app::MaybeUnitCap";
    cap.symbol_ref.id = 100;
    cap.name = "MaybeUnitCap";
    cap.return_type = option_unit_vt;
    program.capabilities.push_back(std::move(cap));

    REQUIRE(verify_core_program(program).ok()); // the extended program is still valid Core

    const auto result = project_core_wire_schema(program, caps({cap_index}));
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.table.has_value()); // no partial table published
    CHECK(has_code(result.diagnostics, std::string(wire_schema::kUnsupported)));
    CHECK_FALSE(has_code(result.diagnostics, std::string(wire_schema::kInvalidCore)));
}

// RFC 0026 C2b P0-11: the local verifier rejects two writer-impossible reserved
// collisions that a transported / hand-built table could otherwise forge. Both
// are legal-but-unsupported (core.wire.UNSUPPORTED), and both are gated only in
// LocalSchemaVerifier so the source projector, the local verify, and the
// transported binding path share one check.
TEST_CASE("local verifier rejects reserved wire-name collisions (P0-11)") {
    SUBCASE("Struct field named '_type' is UNSUPPORTED") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0
        CoreWireSchemaStruct st;
        st.wire_name = "app::Bad";
        st.fields.push_back(CoreWireSchemaField{"_type", CoreWireSchemaNodeId{0}});
        nodes.push_back(CoreWireSchemaNode{st}); // 1
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
        CHECK(has_message(diagnostics, "reserved wire name '_type'"));
    }

    SUBCASE("ordinary Enum named 'std::option::Option' is UNSUPPORTED") {
        std::vector<CoreWireSchemaNode> nodes;
        CoreWireSchemaEnum en;
        en.wire_name = "std::option::Option";
        en.variants.push_back(CoreWireSchemaVariant{"Red", CoreWirePayloadKind::Unit, {}});
        nodes.push_back(CoreWireSchemaNode{en}); // 0
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
        CHECK(has_message(diagnostics, "reserved wire name 'std::option::Option'"));
    }

    // invalid-before-unsupported: a table that is BOTH structurally invalid AND
    // reserved must report the structural (kInvalid) error, never kUnsupported.
    // The malformation is chosen to pass the reachability `mark` phase (all ids in
    // range) and fail only inside validate_node, so it genuinely proves the
    // reserved gate runs AFTER the structural checks within the Struct/Enum branch.
    SUBCASE("Struct with duplicate '_type' fields is kInvalid, not UNSUPPORTED") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0 (reachable)
        CoreWireSchemaStruct st;
        st.wire_name = "app::Bad";
        // Two fields both named "_type", both referencing the valid Int child:
        // names_unique fails inside validate_node before the reserved-name gate.
        st.fields.push_back(CoreWireSchemaField{"_type", CoreWireSchemaNodeId{0}});
        st.fields.push_back(CoreWireSchemaField{"_type", CoreWireSchemaNodeId{0}});
        nodes.push_back(CoreWireSchemaNode{st}); // 1
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
        CHECK_FALSE(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
    }

    SUBCASE("reserved Enum with a unit variant carrying a slot is kInvalid, not UNSUPPORTED") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0 (reachable)
        CoreWireSchemaEnum en;
        en.wire_name = "std::option::Option";
        CoreWireSchemaVariant bad_unit;
        bad_unit.wire_name = "X";
        bad_unit.payload_kind = CoreWirePayloadKind::Unit;
        // A unit variant must have no slots; this one references the valid Int so
        // every id is reachable, but the payload structural check fails inside
        // validate_node before the reserved-name gate.
        bad_unit.slots.push_back(CoreWireSchemaField{"", CoreWireSchemaNodeId{0}});
        en.variants.push_back(bad_unit);
        nodes.push_back(CoreWireSchemaNode{en}); // 1
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        const auto diagnostics = verify_core_wire_schema_table_local(table);
        CHECK(has_code(diagnostics, std::string(wire_schema::kInvalid)));
        CHECK_FALSE(has_code(diagnostics, std::string(wire_schema::kUnsupported)));
    }

    SUBCASE("a genuine CoreWireSchemaOption stays legal") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});                        // 0
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        CHECK(verify_core_wire_schema_table_local(table).empty());
    }

    SUBCASE("an ordinary Struct with non-reserved field names stays legal") {
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0
        CoreWireSchemaStruct st;
        st.wire_name = "app::Point";
        st.fields.push_back(CoreWireSchemaField{"n", CoreWireSchemaNodeId{0}});
        nodes.push_back(CoreWireSchemaNode{st}); // 1 (root)
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
        CHECK(verify_core_wire_schema_table_local(table).empty());
    }

    SUBCASE("an ordinary Enum with a non-reserved name stays legal") {
        std::vector<CoreWireSchemaNode> nodes;
        CoreWireSchemaEnum en;
        en.wire_name = "app::Color";
        en.variants.push_back(CoreWireSchemaVariant{"Red", CoreWirePayloadKind::Unit, {}});
        nodes.push_back(CoreWireSchemaNode{en}); // 0 (root)
        const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
        CHECK(verify_core_wire_schema_table_local(table).empty());
    }

    SUBCASE("source projection of an ordinary Struct + Enum still succeeds") {
        // kStructCap is Pair (struct) -> Color (enum): a real projection through
        // build_struct/build_enum. The reserved-name gate must not touch these
        // non-reserved names, so the projector publishes a table.
        const CoreProgram program = make_wire_program();
        const auto result = project_core_wire_schema(program, caps({kStructCap}));
        CHECK(result.ok());
        CHECK(result.table.has_value());
    }
}

// --- E4-B0-C1 (E4-B1 stage) decoder: canonical payload admission authority -----

namespace {

// Encode a projected selection to bytes (REQUIREs a clean projection + encode).
[[nodiscard]] std::vector<std::uint8_t>
encode_selection(const CoreProgram &program, const std::vector<CoreCapabilityId> &selection) {
    const auto projected = project_core_wire_schema(program, selection);
    REQUIRE(projected.ok());
    const auto encoded = encode_core_wire_schema_table(*projected.table);
    REQUIRE(encoded.ok());
    return *encoded.bytes;
}

// A distinctive marker used to prove the decoder never echoes payload/name bytes.
constexpr std::string_view kSecretName = "SECRET_WIRE_NAME_MARKER_ZZZ";

[[nodiscard]] bool any_diag_contains(const std::vector<CoreLowerDiagnostic> &diagnostics,
                                     std::string_view needle) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&](const CoreLowerDiagnostic &d) {
                           return d.message.find(needle) != std::string::npos;
                       });
}

// A Decimal(scale) node is the cleanest signed-LEB carrier: `scale` is the only
// s64 field on any shape, so an encode->decode->equal round-trip exercises s64 at
// exactly the requested boundary.
[[nodiscard]] std::vector<std::uint8_t> encode_decimal_scale(std::int64_t scale) {
    std::vector<CoreWireSchemaNode> nodes;
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaDecimal{scale}});
    const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
    const auto encoded = encode_core_wire_schema_table(table);
    REQUIRE(encoded.ok());
    return *encoded.bytes;
}

} // namespace

TEST_CASE("wire decoder round-trips a rich table and its bytes re-encode identically") {
    const CoreProgram program = make_wire_program();
    const auto selection = caps({kStructCap, kOptListCap, kCollCap, kScalarCap});
    const auto projected = project_core_wire_schema(program, selection);
    REQUIRE(projected.ok());
    const auto encoded = encode_core_wire_schema_table(*projected.table);
    REQUIRE(encoded.ok());

    const auto decoded = decode_core_wire_schema_table(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.table.has_value());
    CHECK(!decoded.has_errors());
    // Structural round-trip: decoded table equals the projected one.
    CHECK(*decoded.table == *projected.table);
    // Canonical round-trip: re-encoding the decoded table reproduces the bytes.
    const auto reencoded = encode_core_wire_schema_table(*decoded.table);
    REQUIRE(reencoded.ok());
    CHECK(*reencoded.bytes == *encoded.bytes);
}

TEST_CASE("wire decoder round-trips the generic (UAF/Result) and recursive tables") {
    {
        const GenericProgram g = make_generic_program();
        const auto bytes = encode_selection(g.program, {g.gen_cap});
        const auto decoded = decode_core_wire_schema_table(bytes);
        REQUIRE(decoded.ok());
        const auto reencoded = encode_core_wire_schema_table(*decoded.table);
        REQUIRE(reencoded.ok());
        CHECK(*reencoded.bytes == bytes);
    }
    {
        const GenericProgram g = make_generic_program();
        const auto bytes = encode_selection(g.program, {g.tree_cap});
        const auto decoded = decode_core_wire_schema_table(bytes);
        REQUIRE(decoded.ok());
        // The recursive Tree back-reference survives decode.
        const auto reencoded = encode_core_wire_schema_table(*decoded.table);
        REQUIRE(reencoded.ok());
        CHECK(*reencoded.bytes == bytes);
    }
}

TEST_CASE("wire decoder round-trips a hand-built table exercising every node shape") {
    // One node per shape, ALL root-reachable from the final Struct, so decode_node
    // covers every tag and every optional/bounds/capacity/ids field, and the local
    // verifier (no-orphan) accepts the graph. Covers: Unit(tag0), Bool, Int(bounds
    // + unbounded), Float, String(len bounds), Decimal(scale), Duration, Timestamp,
    // Uuid, Option, Sequence List + Set(capacity), Map(capacity), Struct, Enum
    // (Unit + Tuple + Struct payload kinds), Tuple.
    std::vector<CoreWireSchemaNode> nodes;
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaUnit{}});          // 0 Unit
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaBool{}});          // 1 Bool
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{{{-5, 5}}}});  // 2 Int bounded
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});           // 3 Int unbounded
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaFloat{}});         // 4 Float
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{{{0, 16}}}}); // 5 String len bounds
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaDecimal{2}});      // 6 Decimal scale
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaDuration{}});      // 7 Duration
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaTimestamp{}});     // 8 Timestamp
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaUuid{}});          // 9 Uuid
    // 10 Option<Int(3)>
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{3}}});
    CoreWireSchemaSequence list;
    list.kind = CoreWireSequenceKind::List;
    list.element = CoreWireSchemaNodeId{1};
    nodes.push_back(CoreWireSchemaNode{list}); // 11 List<Bool> (no capacity)
    CoreWireSchemaSequence set;
    set.kind = CoreWireSequenceKind::Set;
    set.element = CoreWireSchemaNodeId{3};
    set.capacity = std::optional<std::uint64_t>{4};
    nodes.push_back(CoreWireSchemaNode{set}); // 12 Set<Int>(4)
    CoreWireSchemaMap map;
    map.key = CoreWireSchemaNodeId{5};
    map.value = CoreWireSchemaNodeId{3};
    map.capacity = std::optional<std::uint64_t>{8};
    nodes.push_back(CoreWireSchemaNode{map}); // 13 Map<String,Int>(8)
    CoreWireSchemaTuple tup;
    tup.elements = {CoreWireSchemaNodeId{1}, CoreWireSchemaNodeId{4}};
    nodes.push_back(CoreWireSchemaNode{tup}); // 14 (Bool,Float)
    CoreWireSchemaEnum en;
    en.wire_name = "app::Shape";
    en.variants.push_back(CoreWireSchemaVariant{"None", CoreWirePayloadKind::Unit, {}});
    en.variants.push_back(CoreWireSchemaVariant{
        "Pos", CoreWirePayloadKind::Tuple,
        {CoreWireSchemaField{"", CoreWireSchemaNodeId{2}},
         CoreWireSchemaField{"", CoreWireSchemaNodeId{4}}}});
    en.variants.push_back(CoreWireSchemaVariant{
        "Named", CoreWirePayloadKind::Struct,
        {CoreWireSchemaField{"a", CoreWireSchemaNodeId{10}},
         CoreWireSchemaField{"b", CoreWireSchemaNodeId{11}}}});
    nodes.push_back(CoreWireSchemaNode{en}); // 15 enum (Unit + Tuple + Struct payloads)
    CoreWireSchemaStruct st;
    st.wire_name = "app::Bag";
    st.fields.push_back(CoreWireSchemaField{"u", CoreWireSchemaNodeId{0}});
    st.fields.push_back(CoreWireSchemaField{"decimal", CoreWireSchemaNodeId{6}});
    st.fields.push_back(CoreWireSchemaField{"dur", CoreWireSchemaNodeId{7}});
    st.fields.push_back(CoreWireSchemaField{"ts", CoreWireSchemaNodeId{8}});
    st.fields.push_back(CoreWireSchemaField{"id", CoreWireSchemaNodeId{9}});
    st.fields.push_back(CoreWireSchemaField{"set", CoreWireSchemaNodeId{12}});
    st.fields.push_back(CoreWireSchemaField{"map", CoreWireSchemaNodeId{13}});
    st.fields.push_back(CoreWireSchemaField{"tup", CoreWireSchemaNodeId{14}});
    st.fields.push_back(CoreWireSchemaField{"shape", CoreWireSchemaNodeId{15}});
    nodes.push_back(CoreWireSchemaNode{st}); // 16 root

    const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{16});
    REQUIRE(verify_core_wire_schema_table_local(table).empty());
    const auto encoded = encode_core_wire_schema_table(table);
    REQUIRE(encoded.ok());
    const auto decoded = decode_core_wire_schema_table(*encoded.bytes);
    REQUIRE(decoded.ok());
    CHECK(*decoded.table == table);
}

TEST_CASE("wire decoder accepts an empty (no-node, no-cap) table round-trip") {
    CoreWireSchemaTable table; // format_version 1, zero nodes, zero caps
    const auto encoded = encode_core_wire_schema_table(table);
    REQUIRE(encoded.ok());
    const auto decoded = decode_core_wire_schema_table(*encoded.bytes);
    REQUIRE(decoded.ok());
    CHECK(*decoded.table == table);
    CHECK(decoded.table->nodes.empty());
    CHECK(decoded.table->capabilities.empty());
}

TEST_CASE("wire decoder rejects header / framing corruption") {
    const CoreProgram program = make_wire_program();
    const auto good = encode_selection(program, caps({kScalarCap}));

    SUBCASE("bad magic") {
        auto bytes = good;
        bytes[0] = 'X';
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(decoded.has_errors());
    }
    SUBCASE("empty input") {
        const std::vector<std::uint8_t> empty;
        const auto decoded = decode_core_wire_schema_table(empty);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
    }
    SUBCASE("magic-only truncation") {
        const std::vector<std::uint8_t> magic{'A', 'H', 'F', 'L', 'W', 'S'};
        const auto decoded = decode_core_wire_schema_table(magic);
        CHECK(!decoded.ok());
    }
    SUBCASE("unsupported format version") {
        auto bytes = good;
        bytes[6] = 2; // version LEB byte immediately after magic
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
    }
    SUBCASE("trailing bytes after a valid payload") {
        auto bytes = good;
        bytes.push_back(0x00);
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
    }
    SUBCASE("every proper prefix cut is rejected (exhaustive truncation)") {
        for (std::size_t cut = 0; cut < good.size(); ++cut) {
            const std::vector<std::uint8_t> prefix(good.begin(), good.begin() + cut);
            const auto decoded = decode_core_wire_schema_table(prefix);
            INFO("prefix length " << cut);
            CHECK(!decoded.ok());
            CHECK(!decoded.table.has_value());
        }
    }
}

TEST_CASE("wire decoder rejects an unknown node kind tag") {
    // A single-node table whose node tag byte is an unknown shape id.
    // Hand-assemble: magic + version(1) + node_count(1) + tag(0xFF).
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 0xFF};
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!decoded.table.has_value());
    CHECK(any_diag_contains(decoded.diagnostics, "unknown node kind"));
}

TEST_CASE("wire decoder rejects non-binary discriminant tags at each discriminant gate") {
    SUBCASE("Int optional_bounds tag != 0/1") {
        // magic + v1 + node_count(1) + Int(2) + bounds tag(0x02, illegal).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 2, 0x02};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "non-canonical optional tag"));
    }
    SUBCASE("Sequence optional_u64 capacity tag != 0/1") {
        // Sequence(10) + kind List(0) + element(node 0) + capacity tag(0x03, illegal).
        // node 0 is the sequence itself (self element keeps it in-range for the tag
        // check, which runs before reachability).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01,
                                        10,  0x00, 0x00, 0x03};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "non-canonical optional tag"));
    }
    SUBCASE("Sequence kind != List/Set") {
        // Sequence(10) + kind(0x02, illegal).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 10, 0x02};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "unknown sequence kind"));
    }
    SUBCASE("Enum variant payload_kind != Unit/Tuple/Struct") {
        // Enum(13) + name("E") + variant_count(1) + variant name("V") +
        // payload_kind(0x03, illegal).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 13,
                                        1,   'E', 0x01, 1,   'V', 0x03};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "unknown enum payload kind"));
    }
}

TEST_CASE("wire decoder rejects a non-canonical LEB integer via the re-encode gate") {
    // format_version encoded non-shortest: 0x81 0x00 == 1 with a redundant group.
    // magic + [0x81,0x00] (version=1 overlong) + node_count(0) + cap_count(0).
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x81, 0x00, 0x00, 0x00};
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!decoded.table.has_value());
    CHECK(any_diag_contains(decoded.diagnostics, "not canonical"));
}

TEST_CASE("wire decoder rejects an out-of-range final u64 LEB group") {
    // node_count with a 10th group at shift 63 carrying payload 0x02: only 0/1 fit
    // there, so this overflows the u64 domain and reports out-of-range (distinct
    // from the >64-bit overlong gate, which needs an 11th group). Bytes: magic +
    // version(1) + 9x continuation-zero (0x80) + terminating group 0x02.
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01};
    for (int i = 0; i < 9; ++i) {
        bytes.push_back(0x80);
    }
    bytes.push_back(0x02);
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!decoded.table.has_value());
    CHECK(any_diag_contains(decoded.diagnostics, "out-of-range"));
}

TEST_CASE("wire decoder rejects a count that exceeds the remaining bytes") {
    // node_count = 0xFF (255) but no node bodies follow.
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0xFF, 0x01};
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!decoded.table.has_value());
    CHECK(any_diag_contains(decoded.diagnostics, "remaining bytes"));
}

TEST_CASE("wire decoder rejects a string length that exceeds the remaining bytes") {
    // magic + version(1) + node_count(1) + tag Struct(12) + name_len(0xFF) + 'x'.
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 12, 0xFF, 0x01, 'x'};
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!decoded.table.has_value());
    CHECK(any_diag_contains(decoded.diagnostics, "string longer than remaining bytes"));
}

TEST_CASE("wire decoder emits fixed diagnostics that never echo payload names or bytes") {
    // Build a table whose Struct carries the secret marker as its wire_name, encode
    // it, then corrupt a trailing byte so decode fails. The diagnostics must not
    // contain the secret name.
    std::vector<CoreWireSchemaNode> nodes;
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0
    CoreWireSchemaStruct st;
    st.wire_name = std::string(kSecretName);
    st.fields.push_back(CoreWireSchemaField{"n", CoreWireSchemaNodeId{0}});
    nodes.push_back(CoreWireSchemaNode{st}); // 1 root
    const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{1});
    const auto encoded = encode_core_wire_schema_table(table);
    REQUIRE(encoded.ok());
    auto bytes = *encoded.bytes;
    bytes.push_back(0x00); // trailing byte -> decode fails
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!any_diag_contains(decoded.diagnostics, kSecretName));
}

TEST_CASE("wire decoder ok()/has_errors()/table are a consistent tri-state") {
    const CoreProgram program = make_wire_program();
    const auto good = encode_selection(program, caps({kScalarCap}));
    {
        const auto decoded = decode_core_wire_schema_table(good);
        CHECK(decoded.ok());
        CHECK(decoded.table.has_value());
        CHECK(!decoded.has_errors());
    }
    {
        auto bad = good;
        bad[0] = 'Z';
        const auto decoded = decode_core_wire_schema_table(bad);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(decoded.has_errors());
    }
}

// A Decimal(scale) node is the cleanest signed-LEB carrier: `scale` is the only
// s64 field on any shape, so an encode->decode->equal round-trip exercises s64 at
// exactly the requested boundary.
TEST_CASE("wire decoder round-trips signed LEB canonical boundaries via Decimal scale") {
    for (const std::int64_t scale : {std::int64_t{0}, std::int64_t{63}, std::int64_t{64},
                                     std::int64_t{127}, std::int64_t{128}, std::int64_t{-1},
                                     std::int64_t{-64}, std::int64_t{-65},
                                     std::numeric_limits<std::int64_t>::max(),
                                     std::numeric_limits<std::int64_t>::min()}) {
        const auto bytes = encode_decimal_scale(scale);
        const auto decoded = decode_core_wire_schema_table(bytes);
        INFO("scale " << scale);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.table->nodes.size() == 1);
        CHECK(std::get<CoreWireSchemaDecimal>(decoded.table->nodes[0].shape).scale == scale);
        const auto reencoded = encode_core_wire_schema_table(*decoded.table);
        REQUIRE(reencoded.ok());
        CHECK(*reencoded.bytes == bytes);
    }
}

TEST_CASE("wire decoder rejects malformed signed LEB encodings") {
    // Decimal scale == +0 is canonically one byte 0x00. A non-shortest +0 as
    // 0x80 0x00 (a redundant continuation of a positive) must fail the canonical
    // re-encode gate. Bytes: magic + v1 + node_count(1) + tag Decimal(5) + scale
    // 0x80 0x00 + cap_count(0).
    // A Decimal node needs a capability whose result points at it, or verify_local
    // rejects it as an orphan BEFORE the canonical re-encode gate is reached. The
    // suffix `cap_count(1) cap_id(0) source_symbol(7) params(0) result(node 0)`.
    SUBCASE("redundant positive sign group") {
        std::vector<std::uint8_t> bytes{'A',  'H',  'F',  'L',  'W',  'S',  0x01, 0x01,
                                        5,    0x80, 0x00, 0x01, 0x00, 0x07, 0x00, 0x00};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "not canonical"));
    }
    SUBCASE("redundant negative sign group") {
        // scale == -1 is canonically 0x7f. A non-shortest -1 as 0xff 0x7f (a
        // redundant sign-extension of a negative) must fail the canonical gate.
        std::vector<std::uint8_t> bytes{'A',  'H',  'F',  'L',  'W',  'S',  0x01, 0x01,
                                        5,    0xff, 0x7f, 0x01, 0x00, 0x07, 0x00, 0x00};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "not canonical"));
    }
    SUBCASE("out-of-range signed final group (shift == 63)") {
        // s64 is a distinct reader from u64. A Decimal scale whose 10th group at
        // shift 63 carries payload 0x01 (only a pure sign group 0x00/0x7f fits
        // there) overflows int64 and reports out-of-range. Bytes: magic + v1 +
        // node_count(1) + Decimal(5) + 9x continuation-zero (0x80) + 0x01.
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 5};
        for (int i = 0; i < 9; ++i) {
            bytes.push_back(0x80);
        }
        bytes.push_back(0x01);
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "out-of-range signed"));
    }
    SUBCASE("overlong signed (>64-bit) run (shift >= 64)") {
        // A Decimal scale with 10 continuation groups drives shift from 63 to 70
        // on the 10th read; the attempted 11th group is rejected before consumption
        // by the shift >= 64 overlong gate (distinct from the shift == 63
        // out-of-range gate). Bytes: magic + v1 + node_count(1) + Decimal(5) + 10x
        // continuation-zero (0x80) + 0x00.
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 5};
        for (int i = 0; i < 10; ++i) {
            bytes.push_back(0x80);
        }
        bytes.push_back(0x00);
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "overlong signed"));
    }
}

TEST_CASE("wire decoder separates u32-domain overflow from u64 overflow") {
    SUBCASE("a valid u64 that exceeds the 32-bit domain (node_count) is rejected") {
        // node_count encoded as 0x1_0000_0000 (2^32): a legal 5-byte u64 that is
        // out of the u32 domain used for counts/ids. LEB of 2^32 = 80 80 80 80 10.
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01,
                                        0x80, 0x80, 0x80, 0x80, 0x10};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "32-bit domain"));
    }
    SUBCASE("a legal 10-byte UINT64_MAX source_symbol round-trips exactly") {
        // source_symbol is the only u64 field; UINT64_MAX is the maximal legal
        // 10-byte LEB (ff ff ff ff ff ff ff ff ff 01). It must decode to the exact
        // value and re-encode byte-identically.
        std::vector<CoreWireSchemaNode> nodes;
        nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});
        auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
        table.capabilities[0].source_symbol = std::numeric_limits<std::uint64_t>::max();
        const auto encoded = encode_core_wire_schema_table(table);
        REQUIRE(encoded.ok());
        const auto decoded = decode_core_wire_schema_table(*encoded.bytes);
        REQUIRE(decoded.ok());
        CHECK(decoded.table->capabilities[0].source_symbol ==
              std::numeric_limits<std::uint64_t>::max());
        const auto reencoded = encode_core_wire_schema_table(*decoded.table);
        REQUIRE(reencoded.ok());
        CHECK(*reencoded.bytes == *encoded.bytes);
    }
    SUBCASE("an overlong (>64-bit) LEB is rejected as overflow, not a u32 error") {
        // source_symbol as a >64-bit run: 10 continuation groups drive shift from
        // 63 to 70 on the 10th read; the attempted 11th group is rejected before
        // consumption by the shift >= 64 overlong gate (distinct from out-of-range).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x00, 0x01, 0x00};
        for (int i = 0; i < 10; ++i) {
            bytes.push_back(0x80); // continuation, payload 0
        }
        bytes.push_back(0x01); // attempted 11th group -> rejected before consumption
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "overlong"));
    }
}

TEST_CASE("wire decoder count at the reserved 32-bit sentinel is rejected before reserve") {
    // node_count == kInvalid (UINT32_MAX) = LEB ff ff ff ff 0f. Must be rejected by
    // bounded_count's sentinel gate BEFORE any reserve, independent of remaining.
    std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01,
                                    0xff, 0xff, 0xff, 0xff, 0x0f};
    const auto decoded = decode_core_wire_schema_table(bytes);
    CHECK(!decoded.ok());
    CHECK(!decoded.table.has_value());
    CHECK(any_diag_contains(decoded.diagnostics, "reserved 32-bit sentinel"));
}

TEST_CASE("wire decoder cap-id: kInvalid / order / duplicate reject, large-sparse accept") {
    // Helper: assemble a two-cap, single-Int-node table whose two capability ids
    // are the given values, both roots pointing at node 0.
    auto assemble = [](std::uint32_t cap0, std::uint32_t cap1) {
        std::vector<std::uint8_t> b{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 2 /*Int*/};
        auto leb = [&](std::uint64_t v) {
            do {
                std::uint8_t c = v & 0x7fU;
                v >>= 7U;
                if (v != 0) {
                    c |= 0x80U;
                }
                b.push_back(c);
            } while (v != 0);
        };
        // Int tag 2 has an optional bounds byte (absent = 0).
        b.push_back(0x00); // Int bounds absent
        leb(2);            // cap_count
        // cap0: id, source_symbol(7), params(0), result(node 0)
        leb(cap0);
        leb(7);
        leb(0);
        leb(0);
        // cap1
        leb(cap1);
        leb(9);
        leb(0);
        leb(0);
        return b;
    };
    SUBCASE("strictly increasing large sparse non-invalid ids are accepted") {
        const auto bytes = assemble(5, 4000000000U); // both < UINT32_MAX, increasing
        const auto decoded = decode_core_wire_schema_table(bytes);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.table->capabilities.size() == 2);
        CHECK(decoded.table->capabilities[1].capability.value == 4000000000U);
    }
    SUBCASE("a kInvalid cap id is rejected") {
        const auto bytes = assemble(5, CoreCapabilityId::kInvalid);
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(any_diag_contains(decoded.diagnostics, "strictly ordered and unique"));
    }
    SUBCASE("non-increasing (out of order) cap ids are rejected") {
        const auto bytes = assemble(9, 5);
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(any_diag_contains(decoded.diagnostics, "strictly ordered and unique"));
    }
    SUBCASE("duplicate cap ids are rejected") {
        const auto bytes = assemble(5, 5);
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(any_diag_contains(decoded.diagnostics, "strictly ordered and unique"));
    }
}

TEST_CASE("wire decoder local-verifier negatives entered via raw bytes") {
    SUBCASE("orphan node (unreachable) is rejected") {
        // Two Int nodes, root = node 0; node 1 is unreachable.
        // magic + v1 + node_count(2) + Int(2)+bounds-absent + Int(2)+bounds-absent
        // + cap_count(1) + cap_id(0) + source_symbol(7) + params(0) + result(0).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x02,
                                        2,   0x00, 2,   0x00, 0x01, 0x00, 0x07, 0x00, 0x00};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(any_diag_contains(decoded.diagnostics, "orphan node"));
    }
    SUBCASE("a NodeId reference out of the table range is rejected") {
        // Single Option node whose child is node 9 (only node 0 exists).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01,
                                        9,   9,   0x01, 0x00, 0x07, 0x00, 0x00};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics, "node reference is out of range"));
    }
    SUBCASE("a Struct field named _type (reserved) is rejected without echoing names") {
        // Struct{ _type: Int }, root, with the struct wire_name set to the secret
        // marker. This exercises the parse-succeeds -> local-verifier-fails path and
        // asserts the reserved-name diagnostic does NOT echo the (attacker-supplied)
        // struct name. magic + v1 + node_count(2) + Int(2)+absent + Struct(12) +
        // name(kSecretName) + field_count(1) + field name("_type") + field type(0) +
        // cap_count(1) cap_id0 sym7 params0 result(node 1).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x02, 2, 0x00, 12};
        auto push_str = [&](std::string_view s) {
            bytes.push_back(static_cast<std::uint8_t>(s.size()));
            bytes.insert(bytes.end(), s.begin(), s.end());
        };
        push_str(kSecretName);       // struct wire_name = secret marker
        bytes.push_back(0x01);       // field_count
        push_str("_type");           // reserved field name
        bytes.push_back(0x00);       // field type -> node 0
        bytes.push_back(0x01);       // cap_count
        bytes.push_back(0x00);       // cap_id 0
        bytes.push_back(0x07);       // source_symbol
        bytes.push_back(0x00);       // params 0
        bytes.push_back(0x01);       // result -> node 1 (struct)
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(any_diag_contains(decoded.diagnostics, "reserved wire name '_type'"));
        CHECK(!any_diag_contains(decoded.diagnostics, kSecretName));
    }
    SUBCASE("nullable Option (Option<Unit>) is rejected") {
        // Option -> Unit: Unit encodes as null, so None and Some(Unit) collide.
        // magic + v1 + node_count(2) + Unit(0) + Option(9)+child(0) + cap_count(1)
        // cap_id0 sym7 params0 result(node 1).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x02,
                                        0,   9,   0,   0x01, 0x00, 0x07, 0x00, 0x01};
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(any_diag_contains(decoded.diagnostics, "encodes as JSON null"));
    }
    SUBCASE("an ordinary Enum wire_name std::option::Option (reserved) is rejected") {
        // A tag-13 Enum whose wire_name collides with the value_json Option
        // special-case name. Bytes: magic + v1 + node_count(1) + Enum(13) +
        // name("std::option::Option") + variant_count(1) + variant name("None") +
        // payload_kind Unit(0) + slots(0) + cap_count(1) cap_id0 sym7 params0
        // result(node 0).
        std::vector<std::uint8_t> bytes{'A', 'H', 'F', 'L', 'W', 'S', 0x01, 0x01, 13};
        auto push_str = [&](std::string_view s) {
            bytes.push_back(static_cast<std::uint8_t>(s.size()));
            bytes.insert(bytes.end(), s.begin(), s.end());
        };
        push_str("std::option::Option");
        bytes.push_back(0x01); // variant_count
        push_str("None");
        bytes.push_back(0x00); // payload_kind Unit
        bytes.push_back(0x00); // slots count
        bytes.push_back(0x01); // cap_count
        bytes.push_back(0x00); // cap_id 0
        bytes.push_back(0x07); // source_symbol
        bytes.push_back(0x00); // params 0
        bytes.push_back(0x00); // result -> node 0
        const auto decoded = decode_core_wire_schema_table(bytes);
        CHECK(!decoded.ok());
        CHECK(!decoded.table.has_value());
        CHECK(any_diag_contains(decoded.diagnostics,
                                "ordinary enum uses the reserved wire name 'std::option::Option'"));
    }
}

TEST_CASE("wire decoder handles a very deep acyclic reachable chain without recursion") {
    // The reachability verifier walks an untrusted graph; a recursive DFS would
    // overflow the C++ stack on a legal but very deep acyclic chain, so it is an
    // iterative worklist. Build a minimal single-edge chain node[i] = Tuple{ node[i+1] },
    // node[N] = Int (an Option-of-Option chain would trip the nullable gate). Tuple
    // avoids the per-node string/field/set allocation a Struct chain incurs while
    // exercising the same for_each_child/mark path. Depth proves the non-recursive
    // path but stays fast.
    constexpr std::uint32_t kDepth = 20000;
    std::vector<CoreWireSchemaNode> nodes;
    nodes.reserve(kDepth + 1);
    for (std::uint32_t i = 0; i < kDepth; ++i) {
        CoreWireSchemaTuple tup;
        tup.elements.push_back(CoreWireSchemaNodeId{i + 1});
        nodes.push_back(CoreWireSchemaNode{tup});
    }
    nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // node[kDepth]
    const auto table = wrap_result_table(std::move(nodes), CoreWireSchemaNodeId{0});
    REQUIRE(verify_core_wire_schema_table_local(table).empty());
    const auto encoded = encode_core_wire_schema_table(table);
    REQUIRE(encoded.ok());
    const auto decoded = decode_core_wire_schema_table(*encoded.bytes);
    REQUIRE(decoded.ok());
    CHECK(decoded.table->nodes.size() == kDepth + 1);
    const auto reencoded = encode_core_wire_schema_table(*decoded.table);
    REQUIRE(reencoded.ok());
    CHECK(*reencoded.bytes == *encoded.bytes);
}
