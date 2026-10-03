#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "common/project_input_support.hpp"
#include "compiler/syntax/frontend/project.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// RFC 0026 KR6.5 E4-B0-C2a: the shared type-table prelude + the AHFL->wire-schema
// migration projector + VerifiedWireSchemaBinding. These tests drive REAL source
// through the front end over the repo std sysroot (parse_project / resolve /
// typecheck / lower) so the migrated TypeRefs come from a genuinely verified AHFL
// program with real std Option/List, then:
//   * prove build_core_type_environment is byte-identical to the type table
//     lower_ahfl_to_core builds (the prelude refactor is inert);
//   * migrate capability return TypeRefs (scalar / refinement / struct / enum /
//     Option / List) into a VerifiedWireSchemaBinding and assert the shapes;
//   * prove the projector fails closed (no binding) where it must.
// C2a is INERT: no codec, no runtime behavior change (that is C2b).

namespace {

using namespace ahfl;
using namespace ahfl::ir::core;

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

// A program exercising every migration-relevant capability return shape, over the
// real std sysroot so std Option / List resolve to the production nominals.
constexpr std::string_view kSource = R"AHFL(module app::main;

import std::option;
import std::collections;

struct Point {
    x: Int;
    y: Int;
}

enum Color {
    Red,
    Green(Int),
}

struct Tree {
    children: std::collections::List<Tree>(4);
}

capability ScalarCap(flag: Bool) -> Bool;
capability RefineCap(n: Int) -> Int(0, 100);
capability StructCap(p: Point) -> Point;
capability EnumCap(c: Color) -> Color;
capability OptionCap(n: Int) -> std::option::Option<Int>;
capability ListCap(n: Int) -> std::collections::List<Int>(8);
capability TreeCap(n: Int) -> Tree;
capability MapCap(n: Int) -> std::collections::Map<String, Int>(4);
capability MapKeyCap(n: Int) -> std::collections::Map<Int, Int>(4);

agent Worker {
    input: Point;
    output: Point;
    states: [Done];
    initial: Done;
    final: [Done];
}
)AHFL";

// Lower the shared fixture source over the repo std sysroot to a verified AHFL IR.
[[nodiscard]] std::optional<ir::AhflIr> lower_fixture(std::string_view unique) {
    const auto root =
        std::filesystem::temp_directory_path() / ("ahfl_wire_migration_" + std::string(unique));
    std::filesystem::remove_all(root);
    const auto main_path = root / "app" / "main.ahfl";
    write_file(main_path, std::string(kSource));

    const Frontend frontend;
    const auto parse = parse_project(
        frontend,
        test_support::project_input_with_repo_std_for_test_file(main_path, root, __FILE__));
    if (parse.has_errors()) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(parse.graph);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(parse.graph, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    return lower_program_ir(parse.graph, resolve, typecheck);
}

[[nodiscard]] const ir::CapabilityDecl *find_capability(const ir::AhflIr &program,
                                                        std::string_view name) {
    for (const auto &decl : program.declarations) {
        if (const auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            if (cap->name == name || cap->symbol_ref.local_name == name) {
                return cap;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const CoreWireSchemaShape &shape_of(const VerifiedWireSchemaBinding &b,
                                                  CoreWireSchemaNodeId id) {
    REQUIRE(id.value < b.table().nodes.size());
    return b.table().nodes[id.value].shape;
}

// Build a verified type environment, asserting the type-local gate passed.
[[nodiscard]] VerifiedCoreTypeEnvironment require_env(const ir::AhflIr &program) {
    auto result = build_core_type_environment(program);
    REQUIRE(result.ok());
    REQUIRE(result.environment.has_value());
    return *result.environment;
}

} // namespace

TEST_CASE("build_core_type_environment matches lower_ahfl_to_core's type table byte-for-byte") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());

    const auto full = lower_ahfl_to_core(*program);
    REQUIRE(full.ok());
    const auto env = require_env(*program);

    // The shared prelude must produce EXACTLY the type table the full lowerer
    // builds (same source order, fixup, template finalize) — no body adds a
    // nominal declaration. The value-type arena the type-table pass interns is a
    // deterministic PREFIX of the full program's arena: full lowering appends
    // more value types while lowering capability / flow / workflow bodies, but it
    // must not reorder or alter the type-table-produced entries.
    CHECK(env.types() == full.program.types);
    REQUIRE(env.value_types().size() <= full.program.value_types.size());
    bool prefix = true;
    for (std::size_t i = 0; i < env.value_types().size(); ++i) {
        if (!(env.value_types()[i] == full.program.value_types[i])) {
            prefix = false;
            break;
        }
    }
    CHECK(prefix);
}

TEST_CASE("migration projector lowers scalar and refinement capability returns") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto env = require_env(*program);

    SUBCASE("Bool") {
        const auto *cap = find_capability(*program, "ScalarCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
        REQUIRE(result.ok());
        CHECK(std::holds_alternative<CoreWireSchemaBool>(
            shape_of(*result.binding, result.binding->root())));
    }

    SUBCASE("bounded Int refinement carries its bounds") {
        const auto *cap = find_capability(*program, "RefineCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
        REQUIRE(result.ok());
        const auto &shape = shape_of(*result.binding, result.binding->root());
        const auto &as_int = std::get<CoreWireSchemaInt>(shape);
        REQUIRE(as_int.bounds.has_value());
        CHECK(as_int.bounds->first == 0);
        CHECK(as_int.bounds->second == 100);
    }
}

TEST_CASE("migration projector lowers a struct return through the P4-C SSOT") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "StructCap");
    REQUIRE(cap != nullptr);
    const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, *program);
    REQUIRE(result.ok());
    const auto &shape = shape_of(*result.binding, result.binding->root());
    const auto &as_struct = std::get<CoreWireSchemaStruct>(shape);
    CHECK(as_struct.wire_name == "app::main::Point");
    REQUIRE(as_struct.fields.size() == 2);
    CHECK(as_struct.fields[0].wire_name == "x");
    CHECK(std::holds_alternative<CoreWireSchemaInt>(
        shape_of(*result.binding, as_struct.fields[0].type)));
    CHECK(as_struct.fields[1].wire_name == "y");
}

TEST_CASE("migration projector lowers an enum return with tuple + unit variants") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "EnumCap");
    REQUIRE(cap != nullptr);
    const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, *program);
    REQUIRE(result.ok());
    const auto &shape = shape_of(*result.binding, result.binding->root());
    const auto &as_enum = std::get<CoreWireSchemaEnum>(shape);
    CHECK(as_enum.wire_name == "app::main::Color");
    REQUIRE(as_enum.variants.size() == 2);
    CHECK(as_enum.variants[0].wire_name == "Red");
    CHECK(as_enum.variants[0].payload_kind == CoreWirePayloadKind::Unit);
    CHECK(as_enum.variants[1].wire_name == "Green");
    CHECK(as_enum.variants[1].payload_kind == CoreWirePayloadKind::Tuple);
    REQUIRE(as_enum.variants[1].slots.size() == 1);
    CHECK(std::holds_alternative<CoreWireSchemaInt>(
        shape_of(*result.binding, as_enum.variants[1].slots[0].type)));
}

TEST_CASE("migration projector lowers Option and bounded List returns") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto env = require_env(*program);

    SUBCASE("Option<Int>") {
        const auto *cap = find_capability(*program, "OptionCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
        REQUIRE(result.ok());
        const auto &opt = std::get<CoreWireSchemaOption>(
            shape_of(*result.binding, result.binding->root()));
        CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(*result.binding, opt.value)));
    }

    SUBCASE("List<Int>(8)") {
        const auto *cap = find_capability(*program, "ListCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
        REQUIRE(result.ok());
        const auto &list = std::get<CoreWireSchemaSequence>(
            shape_of(*result.binding, result.binding->root()));
        CHECK(list.kind == CoreWireSequenceKind::List);
        CHECK(list.capacity == std::optional<std::uint64_t>{8});
        CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(*result.binding, list.element)));
    }
}

TEST_CASE("migration is deterministic over an immutable environment") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "StructCap");
    REQUIRE(cap != nullptr);
    const auto env = require_env(*program);
    const auto a = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
    const auto b = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    // The environment is a const, copy-only immutable handle — migration cannot
    // mutate it (purity is structural), so it suffices to prove repeat migration
    // is byte-for-byte identical.
    CHECK(a.binding->table() == b.binding->table());
    CHECK(a.binding->root() == b.binding->root());
}

// Compile-time constraints that make the verified environment an unforgeable,
// immutable, copy-only handle. These are the "immutable probe" invariants: a
// consumer cannot default-construct one (only a verifying factory mints it),
// cannot mint one out of thin air, and only reads through const accessors.
static_assert(!std::is_default_constructible_v<VerifiedCoreTypeEnvironment>,
              "no public default ctor: an environment must come from a verifying factory");
static_assert(std::is_copy_constructible_v<VerifiedCoreTypeEnvironment> &&
                  std::is_copy_assignable_v<VerifiedCoreTypeEnvironment>,
              "the handle is copyable (cheap shared_ptr)");
// No dedicated move: the user-declared copy suppresses the implicit move ctor,
// so an rvalue binds to the COPY ctor. The runtime test below proves this leaves
// the source usable (non-destructive) rather than asserting it here, since
// is_move_constructible_v is trivially true via the copy ctor.
// The accessors hand back const references only — no mutator exists.
static_assert(std::is_same_v<decltype(std::declval<const VerifiedCoreTypeEnvironment &>().types()),
                             const std::vector<CoreTypeDecl> &>,
              "types() is a const accessor");
static_assert(
    std::is_same_v<decltype(std::declval<const VerifiedCoreTypeEnvironment &>().value_types()),
                   const std::vector<CoreValueType> &>,
    "value_types() is a const accessor");
// The same guarantees hold for the binding the codec consumes.
static_assert(!std::is_default_constructible_v<VerifiedWireSchemaBinding>,
              "no public default ctor: a binding must come from a verifying factory");
static_assert(std::is_copy_constructible_v<VerifiedWireSchemaBinding>,
              "the binding is copyable");

TEST_CASE("a verified environment survives copy AND rvalue-copy with identical projections") {
    // Runtime immutable-binding probe: because the environment declares a copy but
    // no move, `auto copy2 = std::move(env)` binds to the COPY ctor (non-
    // destructive) — the source stays fully usable. Prove all three handles
    // (source + two copies) still project the SAME capability return byte-for-byte,
    // so no handle was hollowed out.
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "StructCap");
    REQUIRE(cap != nullptr);

    auto env = require_env(*program);
    auto copy = env;                  // copy ctor
    auto copy2 = std::move(env);      // binds to COPY ctor (no move declared)

    const auto from_source = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
    const auto from_copy = migrate_type_ref_to_wire_binding(cap->return_type_ref, copy);
    const auto from_copy2 = migrate_type_ref_to_wire_binding(cap->return_type_ref, copy2);
    REQUIRE(from_source.ok()); // the "moved-from" source is NOT hollowed out
    REQUIRE(from_copy.ok());
    REQUIRE(from_copy2.ok());

    CHECK(from_source.binding->table() == from_copy.binding->table());
    CHECK(from_source.binding->table() == from_copy2.binding->table());
    CHECK(from_source.binding->root() == from_copy.binding->root());
    CHECK(from_source.binding->root() == from_copy2.binding->root());
    // The underlying type/value-type arenas are shared and structurally equal.
    CHECK(env.types() == copy.types());
    CHECK(env.value_types() == copy2.value_types());
}

TEST_CASE("migration projector fails closed on non-projectable TypeRefs") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto env = require_env(*program);

    SUBCASE("Unresolved type") {
        ir::TypeRef unresolved; // default kind == Unresolved
        const auto result = migrate_type_ref_to_wire_binding(unresolved, env);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }

    SUBCASE("Any type") {
        ir::TypeRef any;
        any.kind = ir::TypeRefKind::Any;
        const auto result = migrate_type_ref_to_wire_binding(any, env);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }

    SUBCASE("Never type") {
        ir::TypeRef never;
        never.kind = ir::TypeRefKind::Never;
        const auto result = migrate_type_ref_to_wire_binding(never, env);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }

    SUBCASE("a Struct TypeRef with an unresolved nominal (no verified decl)") {
        ir::TypeRef bogus;
        bogus.kind = ir::TypeRefKind::Struct;
        bogus.canonical_name = "app::main::DoesNotExist";
        // nominal_ref left with no id and a name that no decl claims.
        const auto result = migrate_type_ref_to_wire_binding(bogus, env);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }
}

TEST_CASE("the transported-table factory applies the public local verifier gate") {
    // Build a real binding, then feed a TAMPERED table (bad node ref) through the
    // transported-table factory: the local verifier must reject it.
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "StructCap");
    REQUIRE(cap != nullptr);
    const auto good = migrate_type_ref_to_wire_binding(cap->return_type_ref, *program);
    REQUIRE(good.ok());

    CoreWireSchemaTable tampered = good.binding->table();
    // Point a struct field at a node past the arena.
    for (auto &node : tampered.nodes) {
        if (auto *s = std::get_if<CoreWireSchemaStruct>(&node.shape); s != nullptr &&
                                                                       !s->fields.empty()) {
            s->fields[0].type = CoreWireSchemaNodeId{9999};
            break;
        }
    }
    std::vector<CoreLowerDiagnostic> diagnostics;
    const auto rejected = make_wire_binding_from_transported_table(
        std::move(tampered), good.binding->selector(), diagnostics);
    CHECK_FALSE(rejected.has_value());
    CHECK_FALSE(diagnostics.empty());
}

TEST_CASE("the transported-table factory rejects an Option with a null-encoding child (P0-9)") {
    // A hand-built transported table whose capability result is Option<Unit>. The
    // public local verifier's nullable-child gate must reject it, so no binding is
    // minted (the transport entry point shares the same single gate as the source
    // projector).
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaUnit{}});                        // 0
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}}); // 1
    CoreWireCapabilitySchema cap;
    cap.capability = CoreCapabilityId{0};
    cap.source_symbol = 55;
    cap.result = CoreWireSchemaNodeId{1};
    table.capabilities.push_back(cap);

    CoreWireRootSelector selector;
    selector.capability = CoreCapabilityId{0};
    selector.expected_source_symbol = 55;
    selector.kind = CoreWireRootKind::Result;
    selector.param_index = 0;

    std::vector<CoreLowerDiagnostic> diagnostics;
    const auto rejected =
        make_wire_binding_from_transported_table(std::move(table), selector, diagnostics);
    CHECK_FALSE(rejected.has_value());
    bool saw_unsupported = false;
    for (const auto &d : diagnostics) {
        if (d.code == wire_schema::kUnsupported) {
            saw_unsupported = true;
        }
    }
    CHECK(saw_unsupported);
}

TEST_CASE("the transported-table factory rejects reserved wire-name collisions (P0-11)") {
    const auto mint = [](CoreWireSchemaTable table) {
        CoreWireRootSelector selector;
        selector.capability = CoreCapabilityId{0};
        selector.expected_source_symbol = 77;
        selector.kind = CoreWireRootKind::Result;
        selector.param_index = 0;
        std::vector<CoreLowerDiagnostic> diagnostics;
        auto b = make_wire_binding_from_transported_table(std::move(table), selector, diagnostics);
        bool saw_unsupported = false;
        for (const auto &d : diagnostics) {
            if (d.code == wire_schema::kUnsupported) {
                saw_unsupported = true;
            }
        }
        return std::make_pair(b.has_value(), saw_unsupported);
    };

    SUBCASE("Struct field named '_type'") {
        CoreWireSchemaTable table;
        table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0
        CoreWireSchemaStruct st;
        st.wire_name = "app::Bad";
        st.fields.push_back(CoreWireSchemaField{"_type", CoreWireSchemaNodeId{0}});
        table.nodes.push_back(CoreWireSchemaNode{st}); // 1
        CoreWireCapabilitySchema cap;
        cap.capability = CoreCapabilityId{0};
        cap.source_symbol = 77;
        cap.result = CoreWireSchemaNodeId{1};
        table.capabilities.push_back(cap);
        const auto [has_binding, saw_unsupported] = mint(std::move(table));
        CHECK_FALSE(has_binding);
        CHECK(saw_unsupported);
    }

    SUBCASE("ordinary Enum named 'std::option::Option'") {
        CoreWireSchemaTable table;
        CoreWireSchemaEnum en;
        en.wire_name = "std::option::Option";
        en.variants.push_back(CoreWireSchemaVariant{"Red", CoreWirePayloadKind::Unit, {}});
        table.nodes.push_back(CoreWireSchemaNode{en}); // 0
        CoreWireCapabilitySchema cap;
        cap.capability = CoreCapabilityId{0};
        cap.source_symbol = 77;
        cap.result = CoreWireSchemaNodeId{0};
        table.capabilities.push_back(cap);
        const auto [has_binding, saw_unsupported] = mint(std::move(table));
        CHECK_FALSE(has_binding);
        CHECK(saw_unsupported);
    }
}

TEST_CASE("migration projector lowers String-keyed Map and rejects a non-String key") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto env = require_env(*program);

    SUBCASE("Map<String, Int> projects") {
        const auto *cap = find_capability(*program, "MapCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
        REQUIRE(result.ok());
        const auto &map = std::get<CoreWireSchemaMap>(
            shape_of(*result.binding, result.binding->root()));
        CHECK(std::holds_alternative<CoreWireSchemaString>(shape_of(*result.binding, map.key)));
        CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(*result.binding, map.value)));
    }

    SUBCASE("Map<Int, Int> fails closed (non-String key)") {
        const auto *cap = find_capability(*program, "MapKeyCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, env);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }
}

TEST_CASE("migration projector resolves a recursive nominal via a bounded sequence") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "TreeCap");
    REQUIRE(cap != nullptr);
    const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, *program);
    REQUIRE(result.ok());
    const auto root = result.binding->root();
    const auto &tree = std::get<CoreWireSchemaStruct>(shape_of(*result.binding, root));
    CHECK(tree.wire_name == "app::main::Tree");
    REQUIRE(tree.fields.size() == 1);
    const auto &list = std::get<CoreWireSchemaSequence>(shape_of(*result.binding, tree.fields[0].type));
    CHECK(list.capacity == std::optional<std::uint64_t>{4});
    // The recursive back-reference: List<Tree> element node is the Tree root.
    CHECK(list.element == root);
}

TEST_CASE("migration projector fails closed on a Fn TypeRef") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto env = require_env(*program);
    // A function value is not a wire value (value_json cannot represent it); the
    // C1 projector rejects CoreVtFn, so migration yields no binding.
    ir::TypeRef fn;
    fn.kind = ir::TypeRefKind::Fn;
    fn.first = std::make_unique<ir::TypeRef>();
    fn.first->kind = ir::TypeRefKind::Int; // return Int
    const auto result = migrate_type_ref_to_wire_binding(fn, env);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.binding.has_value());
    // NOTE: Closure has no constructible AHFL TypeRef surface here; its
    // fail-closed projection is covered by the C1 wire-schema probe
    // (core_wire_schema.cpp "value_json-unrepresentable types" -> closure).
}

TEST_CASE("migration projector rejects a Struct TypeRef carrying a WRONG resolved id") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto env = require_env(*program);
    // A Struct ref that names Point but carries a resolved SymbolId no Core type
    // holds must NOT silently downgrade to the canonical-name match (strict
    // id-first resolve_nominal_strict): a present-but-unmatched id fails closed.
    ir::TypeRef wrong;
    wrong.kind = ir::TypeRefKind::Struct;
    wrong.canonical_name = "app::main::Point";
    wrong.nominal_ref.kind = ir::SymbolRefKind::Type;
    wrong.nominal_ref.canonical_name = "app::main::Point";
    wrong.nominal_ref.id = 0x7fffffff; // an id no registered nominal carries
    const auto result = migrate_type_ref_to_wire_binding(wrong, env);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.binding.has_value());
}

TEST_CASE("the typed root selector prevents cross-capability and descendant confusion") {
    // Hand-build a two-capability wire table:
    //   nodes: [0] Int, [1] Bool, [2] String
    //   cap A (id 0, symbol 100): params [Int], result Bool
    //   cap B (id 1, symbol 200): params [String], result String
    // The selector factory must derive the root from {capability, kind,
    // param_index} + a source_symbol cross-check — never accept a raw node, a
    // cross-capability slot, or an out-of-range index.
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaBool{}});   // 1
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 2
    CoreWireCapabilitySchema cap_a;
    cap_a.capability = CoreCapabilityId{0};
    cap_a.source_symbol = 100;
    cap_a.params = {CoreWireSchemaNodeId{0}};
    cap_a.result = CoreWireSchemaNodeId{1};
    table.capabilities.push_back(cap_a);
    CoreWireCapabilitySchema cap_b;
    cap_b.capability = CoreCapabilityId{1};
    cap_b.source_symbol = 200;
    cap_b.params = {CoreWireSchemaNodeId{2}};
    cap_b.result = CoreWireSchemaNodeId{2};
    table.capabilities.push_back(cap_b);

    const auto mint = [&](CoreWireRootSelector selector) {
        std::vector<CoreLowerDiagnostic> diagnostics;
        auto b = make_wire_binding_from_transported_table(table, selector, diagnostics);
        return std::make_pair(std::move(b), std::move(diagnostics));
    };

    SUBCASE("cap A result with the correct source_symbol is accepted") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{0}, 100, CoreWireRootKind::Result, 0});
        REQUIRE(binding.has_value());
        CHECK(binding->root() == CoreWireSchemaNodeId{1}); // Bool
        CHECK(diagnostics.empty());
    }

    SUBCASE("cap A param 0 is accepted and derives the Int node") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{0}, 100, CoreWireRootKind::Param, 0});
        REQUIRE(binding.has_value());
        CHECK(binding->root() == CoreWireSchemaNodeId{0}); // Int
    }

    SUBCASE("a wrong source_symbol for the named capability is rejected") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{0}, 999, CoreWireRootKind::Result, 0});
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }

    SUBCASE("cap B's symbol cannot be used to select cap A (cross-capability)") {
        // Selecting capability id 0 while presenting cap B's symbol (200) fails:
        // the id names A, whose symbol is 100.
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{0}, 200, CoreWireRootKind::Result, 0});
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }

    SUBCASE("a Result selector with a nonzero param index is rejected") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{0}, 100, CoreWireRootKind::Result, 1});
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }

    SUBCASE("a Param selector with an out-of-range index is rejected") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{0}, 100, CoreWireRootKind::Param, 5});
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }

    SUBCASE("an out-of-range capability id is rejected") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{7}, 100, CoreWireRootKind::Result, 0});
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }
}

TEST_CASE("the root selector treats the capability id as identity, not a vector index") {
    // A wire table is the SELECTED-capability subset of a program, so the
    // projector preserves the original program-global CoreCapabilityId and the
    // local verifier only requires the entries to be strictly increasing + unique
    // — NOT zero-based or contiguous. This table legally holds the sparse ids
    // {3, 7}. `derive_root` must therefore look a capability up BY IDENTITY (exact
    // match), never subscript `capabilities[id]`:
    //   * a legal in-table id larger than the subset size (7, size 2) must be
    //     ACCEPTED, not rejected as "out of range"; and
    //   * an id absent from the table (0/1/5) must be REJECTED even when its
    //     expected_source_symbol matches some OTHER entry — otherwise selector
    //     id=1 would silently bind to whatever entry sits at index 1, breaking the
    //     "selector + derived root pinned together" invariant.
    //   nodes: [0] Int, [1] Bool, [2] String
    //   cap 3 (symbol 300): params [Int],    result Bool
    //   cap 7 (symbol 700): params [String], result String
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaBool{}});   // 1
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 2
    CoreWireCapabilitySchema cap3;
    cap3.capability = CoreCapabilityId{3};
    cap3.source_symbol = 300;
    cap3.params = {CoreWireSchemaNodeId{0}};
    cap3.result = CoreWireSchemaNodeId{1};
    table.capabilities.push_back(cap3);
    CoreWireCapabilitySchema cap7;
    cap7.capability = CoreCapabilityId{7};
    cap7.source_symbol = 700;
    cap7.params = {CoreWireSchemaNodeId{2}};
    cap7.result = CoreWireSchemaNodeId{2};
    table.capabilities.push_back(cap7);

    // Sanity: this sparse-id table is a LEGAL table (passes the local verifier).
    REQUIRE(verify_core_wire_schema_table_local(table).empty());

    const auto mint = [&](CoreWireRootSelector selector) {
        std::vector<CoreLowerDiagnostic> diagnostics;
        auto b = make_wire_binding_from_transported_table(table, selector, diagnostics);
        return std::make_pair(std::move(b), std::move(diagnostics));
    };

    SUBCASE("id 3 result is accepted and derives the Bool node") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{3}, 300, CoreWireRootKind::Result, 0});
        REQUIRE(binding.has_value());
        CHECK(binding->root() == CoreWireSchemaNodeId{1}); // Bool
        CHECK(diagnostics.empty());
    }

    SUBCASE("id 7 param 0 is accepted even though 7 exceeds the subset size") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{7}, 700, CoreWireRootKind::Param, 0});
        REQUIRE(binding.has_value()); // the OLD id-as-index code rejected this
        CHECK(binding->root() == CoreWireSchemaNodeId{2}); // String
    }

    SUBCASE("the adversarial case: absent id 1 + another entry's symbol is rejected") {
        // The OLD code did `capabilities[1]` == cap7 and, since 700 matches cap7's
        // symbol, MINTED a binding whose payload claimed cap 1 but whose root came
        // from cap 7. Identity lookup rejects it: id 1 is not in {3, 7}.
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{1}, 700, CoreWireRootKind::Param, 0});
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }

    SUBCASE("other absent ids (0, 5) are rejected") {
        for (std::uint32_t absent : {0u, 5u}) {
            auto [binding, diagnostics] =
                mint({CoreCapabilityId{absent}, 300, CoreWireRootKind::Result, 0});
            CHECK_FALSE(binding.has_value());
            CHECK_FALSE(diagnostics.empty());
        }
    }

    SUBCASE("a present id with the wrong source_symbol is still rejected") {
        auto [binding, diagnostics] =
            mint({CoreCapabilityId{7}, 300, CoreWireRootKind::Result, 0}); // 300 is cap3's
        CHECK_FALSE(binding.has_value());
        CHECK_FALSE(diagnostics.empty());
    }

    SUBCASE("slot kind / index constraints still hold on a sparse id") {
        // Result selector with a nonzero param index.
        {
            auto [binding, diagnostics] =
                mint({CoreCapabilityId{3}, 300, CoreWireRootKind::Result, 2});
            CHECK_FALSE(binding.has_value());
        }
        // Param selector past the (single) param slot.
        {
            auto [binding, diagnostics] =
                mint({CoreCapabilityId{7}, 700, CoreWireRootKind::Param, 4});
            CHECK_FALSE(binding.has_value());
        }
    }
}

TEST_CASE("build_core_type_environment fails closed on a malformed generic decl (type-local gate)") {
    // A user generic struct declaring type_param_count=1 but leaving its variance
    // vector empty is accepted by TypeEnv construction, yet the value-type /
    // type-table verifier rejects it (variances must be parallel to arity). The
    // type-local gate must surface that so seed.ok() is honest — NOT merely the
    // diagnostics TypeEnv happened to raise.
    ir::AhflIr program;
    ir::StructDecl bad;
    bad.name = "BadGeneric";
    bad.symbol_ref.kind = ir::SymbolRefKind::Type;
    bad.symbol_ref.canonical_name = "app::main::BadGeneric";
    bad.symbol_ref.id = 4242;
    bad.type_param_count = 1;
    // type_param_variances deliberately left EMPTY (size 0 != arity 1).
    program.declarations.emplace_back(std::move(bad));

    const auto result = build_core_type_environment(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.environment.has_value()); // no environment minted on gate failure
    // The single-arg migration overload builds the environment internally; it
    // must fail closed (no binding) on the same malformed program.
    ir::TypeRef bad_ref;
    bad_ref.kind = ir::TypeRefKind::Struct;
    bad_ref.canonical_name = "app::main::BadGeneric";
    const auto migrated = migrate_type_ref_to_wire_binding(bad_ref, program);
    CHECK_FALSE(migrated.ok());
    CHECK_FALSE(migrated.binding.has_value());
}

TEST_CASE("build_core_type_environment fails closed when the type-table lowerer rejects a "
          "refinement") {
    // HONEST claim: a struct field typed `BoundedInt` with min > max is rejected
    // by the type-table LOWERER (populate_core_type_table -> ValueTypeArena::lower,
    // core_lower.cpp), BEFORE the value-type verifier ever runs. This test proves
    // only that build_core_type_environment fails closed (no environment) on that
    // lowering error — it does NOT exercise verify_value_types. The direct
    // verifier negative below covers the verify_value_types gate itself.
    ir::AhflIr program;
    ir::StructDecl holder;
    holder.name = "Holder";
    holder.symbol_ref.kind = ir::SymbolRefKind::Type;
    holder.symbol_ref.canonical_name = "app::main::Holder";
    holder.symbol_ref.id = 4243;
    ir::FieldDecl field;
    field.name = "n";
    field.type_ref.kind = ir::TypeRefKind::BoundedInt;
    field.type_ref.int_bounds = std::pair<std::int64_t, std::int64_t>{100, 0}; // min > max
    holder.fields.push_back(std::move(field));
    program.declarations.emplace_back(std::move(holder));

    const auto result = build_core_type_environment(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.environment.has_value());
}

TEST_CASE("verify_core_program's verify_value_types rejects a malformed value-type arena") {
    // This is the value-type gate build_core_type_environment relies on, tested
    // DIRECTLY on the verifier. A production lowering never mints an Int refinement
    // with min > max (the lowerer screens it first), but a transported or
    // deserialized arena can carry one, so the structural verifier MUST still fail
    // closed. Hand-build the smallest arena that isolates verify_value_type_node's
    // refinement check and feed it straight to verify_core_program — no lowerer in
    // the path, so a pass here is genuine verifier behavior.
    CoreProgram program;
    CoreValueType bad;
    bad.node = CoreVtInt{std::pair<std::int64_t, std::int64_t>{100, 0}}; // min > max
    program.value_types.push_back(bad);

    const auto verified = verify_core_program(program);
    CHECK_FALSE(verified.ok());
    bool saw_refinement_error = false;
    for (const auto &d : verified.diagnostics) {
        if (d.code == verify::kValueTypeRefinementInvalid) {
            saw_refinement_error = true;
        }
    }
    CHECK(saw_refinement_error);
}

TEST_CASE("migration resolves an un-inlined std builtin via the descriptor-backed synthetic base") {
    // A program that does NOT declare or import std::option: add_builtins stamps a
    // NAME-ONLY synthetic Option base (its symbol_ref carries no id). A TypeRef
    // that names std::option::Option with a present-but-unmatched id must resolve
    // through the descriptor-backed synthetic-builtin exception in
    // resolve_nominal_strict (NOT fail closed, NOT hit a real decl). This proves
    // the id-first tightening kept exactly that one name-only exception.
    ir::AhflIr program;
    ir::StructDecl anchor; // a trivial real decl so the program is non-empty
    anchor.name = "Anchor";
    anchor.symbol_ref.kind = ir::SymbolRefKind::Type;
    anchor.symbol_ref.canonical_name = "app::main::Anchor";
    anchor.symbol_ref.id = 5000;
    program.declarations.emplace_back(std::move(anchor));

    const auto env = require_env(program);
    // Option<Int> with a present id no registered nominal carries: the synthetic
    // Option base (name-only, no id) is the only legal match.
    ir::TypeRef option;
    option.kind = ir::TypeRefKind::Enum;
    option.canonical_name = "std::option::Option";
    option.nominal_ref.kind = ir::SymbolRefKind::Type;
    option.nominal_ref.canonical_name = "std::option::Option";
    option.nominal_ref.id = 0x6fffffff; // unmatched id -> descriptor-backed name-only base
    option.params.push_back(std::make_unique<ir::TypeRef>());
    option.params[0]->kind = ir::TypeRefKind::Int;

    const auto result = migrate_type_ref_to_wire_binding(option, env);
    REQUIRE(result.ok());
    const auto &opt = std::get<CoreWireSchemaOption>(
        shape_of(*result.binding, result.binding->root()));
    CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(*result.binding, opt.value)));
}

namespace {

// A legal, self-contained two-capability wire table with SPARSE program-global
// ids {3, 7} and SymbolId 0 on the first entry. Reused by the B2-A-pre
// verified-table authority tests. Not a runtime artifact — hand-built and asserted
// legal via the public local verifier.
//   nodes: [0] Int, [1] Bool, [2] String
//   cap 3 (symbol 0):   params [Int],    result Bool
//   cap 7 (symbol 700): params [String], result String
[[nodiscard]] ahfl::ir::core::CoreWireSchemaTable make_sparse_authority_table() {
    using namespace ahfl::ir::core;
    CoreWireSchemaTable table;
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}});    // 0
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaBool{}});   // 1
    table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaString{}}); // 2
    CoreWireCapabilitySchema cap3;
    cap3.capability = CoreCapabilityId{3};
    cap3.source_symbol = 0; // SymbolId{0} is a legal source symbol
    cap3.params = {CoreWireSchemaNodeId{0}};
    cap3.result = CoreWireSchemaNodeId{1};
    table.capabilities.push_back(cap3);
    CoreWireCapabilitySchema cap7;
    cap7.capability = CoreCapabilityId{7};
    cap7.source_symbol = 700;
    cap7.params = {CoreWireSchemaNodeId{2}};
    cap7.result = CoreWireSchemaNodeId{2};
    table.capabilities.push_back(cap7);
    return table;
}

} // namespace

// ---- B2-A-pre: shared verified-table authority -----------------------------

// Copy-only / no-move contract for BOTH the authority and the binding. We do NOT
// assert !is_move_constructible: a user-declared copy ctor makes an rvalue bind to
// the copy ctor, so is_move_constructible is legitimately TRUE. We pin the intended
// contract: copy-constructible + copy-assignable, and not default-constructible
// (the only way in is a verifying factory). The runtime no-hollow-state behavior is
// exercised by the move-construction tests below.
static_assert(std::is_copy_constructible_v<ahfl::ir::core::VerifiedWireSchemaTable>);
static_assert(std::is_copy_assignable_v<ahfl::ir::core::VerifiedWireSchemaTable>);
static_assert(std::is_copy_constructible_v<ahfl::ir::core::VerifiedWireSchemaBinding>);
static_assert(std::is_copy_assignable_v<ahfl::ir::core::VerifiedWireSchemaBinding>);
static_assert(!std::is_default_constructible_v<ahfl::ir::core::VerifiedWireSchemaTable>);
static_assert(!std::is_default_constructible_v<ahfl::ir::core::VerifiedWireSchemaBinding>);

TEST_CASE("make_verified_wire_schema_table admits a legal table and rejects a tampered one") {
    SUBCASE("a legal sparse-id table is admitted with an empty diagnostic bag") {
        auto admitted = make_verified_wire_schema_table(make_sparse_authority_table());
        CHECK(admitted.ok());
        CHECK(admitted.table.has_value());
        CHECK(admitted.diagnostics.empty());
    }

    SUBCASE("a table with a dangling node reference is rejected, no authority minted") {
        auto table = make_sparse_authority_table();
        table.capabilities[0].result = CoreWireSchemaNodeId{9999}; // past the arena
        auto admitted = make_verified_wire_schema_table(std::move(table));
        CHECK_FALSE(admitted.ok());
        CHECK_FALSE(admitted.table.has_value());
        CHECK_FALSE(admitted.diagnostics.empty()); // exact local-verifier bag
    }

    SUBCASE("an Option with a null-encoding child is rejected (shares the P0-9 gate)") {
        CoreWireSchemaTable table;
        table.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaUnit{}});
        table.nodes.push_back(
            CoreWireSchemaNode{CoreWireSchemaOption{CoreWireSchemaNodeId{0}}});
        CoreWireCapabilitySchema cap;
        cap.capability = CoreCapabilityId{0};
        cap.source_symbol = 1;
        cap.result = CoreWireSchemaNodeId{1};
        table.capabilities.push_back(cap);
        auto admitted = make_verified_wire_schema_table(std::move(table));
        CHECK_FALSE(admitted.ok());
        CHECK_FALSE(admitted.table.has_value());
        CHECK_FALSE(admitted.diagnostics.empty());
    }
}

TEST_CASE("one verified authority mints many bindings sharing a single table backing") {
    auto admitted = make_verified_wire_schema_table(make_sparse_authority_table());
    REQUIRE(admitted.ok());
    const auto &authority = *admitted.table;

    std::vector<CoreLowerDiagnostic> diag_a;
    auto result_binding = make_wire_binding_from_verified_table(
        authority, {CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 0}, diag_a);
    std::vector<CoreLowerDiagnostic> diag_b;
    auto param_binding = make_wire_binding_from_verified_table(
        authority, {CoreCapabilityId{3}, 0, CoreWireRootKind::Param, 0}, diag_b);

    REQUIRE(result_binding.has_value());
    REQUIRE(param_binding.has_value());
    CHECK(diag_a.empty());
    CHECK(diag_b.empty());
    // Selector/root correctness: cap 3 result is Bool node 1, param 0 is Int node 0.
    CHECK(result_binding->root() == CoreWireSchemaNodeId{1});
    CHECK(param_binding->root() == CoreWireSchemaNodeId{0});

    // Pointer-identical backing: both bindings' table() alias the SAME allocation,
    // proving no per-mint copy. (Uses only the existing binding accessor.)
    CHECK(&result_binding->table() == &param_binding->table());

    // A Result selector from cap 7 (SymbolId 700) shares the same backing too.
    std::vector<CoreLowerDiagnostic> diag_c;
    auto cap7_binding = make_wire_binding_from_verified_table(
        authority, {CoreCapabilityId{7}, 700, CoreWireRootKind::Result, 0}, diag_c);
    REQUIRE(cap7_binding.has_value());
    CHECK(cap7_binding->root() == CoreWireSchemaNodeId{2}); // String
    CHECK(&cap7_binding->table() == &result_binding->table());
}

TEST_CASE("a minted binding outlives the authority and its source table") {
    std::optional<VerifiedWireSchemaBinding> binding;
    {
        auto admitted = make_verified_wire_schema_table(make_sparse_authority_table());
        REQUIRE(admitted.ok());
        std::vector<CoreLowerDiagnostic> diagnostics;
        binding = make_wire_binding_from_verified_table(
            *admitted.table, {CoreCapabilityId{7}, 700, CoreWireRootKind::Param, 0}, diagnostics);
        REQUIRE(binding.has_value());
        // `admitted` (and the only VerifiedWireSchemaTable handle) is destroyed here.
    }
    // The shared_ptr backing keeps the arena alive: the binding is still usable.
    CHECK(binding->root() == CoreWireSchemaNodeId{2}); // String
    CHECK(std::holds_alternative<CoreWireSchemaString>(
        binding->table().nodes[binding->root().value].shape));
}

TEST_CASE("copying the authority or a binding shares the backing; the source stays usable") {
    auto admitted = make_verified_wire_schema_table(make_sparse_authority_table());
    REQUIRE(admitted.ok());

    // Copy the authority; mint from the COPY, then mint from the ORIGINAL. Both
    // mints must succeed (source still usable) and share ONE backing.
    VerifiedWireSchemaTable authority_copy = *admitted.table; // copy ctor

    // Move-CONSTRUCT a second authority. The type is copy-only (no move declared),
    // so `std::move` binds to the copy ctor — the source is therefore NOT hollowed:
    // it copies the shared_ptr and both remain fully usable. We keep a separate
    // source we are willing to move FROM, then mint from BOTH it and the move-copy.
    VerifiedWireSchemaTable move_source = *admitted.table;
    VerifiedWireSchemaTable authority_move_copy = std::move(move_source); // binds copy ctor

    std::vector<CoreLowerDiagnostic> d1;
    auto from_copy = make_wire_binding_from_verified_table(
        authority_copy, {CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 0}, d1);
    std::vector<CoreLowerDiagnostic> d2;
    auto from_original = make_wire_binding_from_verified_table(
        *admitted.table, {CoreCapabilityId{3}, 0, CoreWireRootKind::Param, 0}, d2);
    std::vector<CoreLowerDiagnostic> d3;
    auto from_move_copy = make_wire_binding_from_verified_table(
        authority_move_copy, {CoreCapabilityId{7}, 700, CoreWireRootKind::Result, 0}, d3);
    // The move SOURCE is still usable (no hollow state) and shares the same backing.
    std::vector<CoreLowerDiagnostic> d4;
    auto from_move_source = make_wire_binding_from_verified_table(
        move_source, {CoreCapabilityId{7}, 700, CoreWireRootKind::Param, 0}, d4);

    REQUIRE(from_copy.has_value());
    REQUIRE(from_original.has_value());
    REQUIRE(from_move_copy.has_value());
    REQUIRE(from_move_source.has_value());
    CHECK(&from_copy->table() == &from_original->table());
    CHECK(&from_copy->table() == &from_move_copy->table());
    CHECK(&from_copy->table() == &from_move_source->table());

    // Copy a binding; the copy aliases the same backing and the source is unchanged.
    VerifiedWireSchemaBinding binding_copy = *from_copy; // copy ctor
    CHECK(&binding_copy.table() == &from_copy->table());
    CHECK(binding_copy.root() == from_copy->root());

    // Move-CONSTRUCT a binding from a source we then still read (copy-only => the
    // source is not hollowed).
    VerifiedWireSchemaBinding binding_move_source = *from_original;
    VerifiedWireSchemaBinding binding_move_copy = std::move(binding_move_source); // copy ctor
    CHECK(&binding_move_copy.table() == &from_original->table());
    CHECK(binding_move_copy.root() == CoreWireSchemaNodeId{0});
    CHECK(binding_move_source.root() == CoreWireSchemaNodeId{0}); // source still usable
    CHECK(&binding_move_source.table() == &from_original->table());
    CHECK(from_original->root() == CoreWireSchemaNodeId{0}); // original untouched
}

TEST_CASE("the verified-table mint enforces the selector SSOT and clears pre-seeded diagnostics") {
    auto admitted = make_verified_wire_schema_table(make_sparse_authority_table());
    REQUIRE(admitted.ok());
    const auto &authority = *admitted.table;

    const auto mint = [&](CoreWireRootSelector selector) {
        std::vector<CoreLowerDiagnostic> diagnostics;
        // Pre-seed the bag: the mint MUST clear it on entry (success AND failure).
        diagnostics.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                                                  "pre.seeded", "stale", std::nullopt, {}});
        auto b = make_wire_binding_from_verified_table(authority, selector, diagnostics);
        return std::make_pair(std::move(b), std::move(diagnostics));
    };
    // A failure bag must be nonempty AND must NOT contain the stale pre-seeded
    // entry (nonempty alone would pass even if clear() were broken).
    const auto is_fresh_failure = [](const std::vector<CoreLowerDiagnostic> &diagnostics) {
        if (diagnostics.empty()) {
            return false;
        }
        for (const auto &d : diagnostics) {
            if (d.code == "pre.seeded") {
                return false;
            }
        }
        return true;
    };

    SUBCASE("success clears the pre-seeded diagnostic") {
        auto [binding, diagnostics] = mint({CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 0});
        REQUIRE(binding.has_value());
        CHECK(diagnostics.empty()); // stale entry gone; never nullopt with empty bag
    }
    SUBCASE("wrong source_symbol is rejected") {
        auto [binding, diagnostics] = mint({CoreCapabilityId{3}, 999, CoreWireRootKind::Result, 0});
        CHECK_FALSE(binding.has_value());
        CHECK(is_fresh_failure(diagnostics));
    }
    SUBCASE("an absent capability id is rejected") {
        auto [binding, diagnostics] = mint({CoreCapabilityId{1}, 700, CoreWireRootKind::Param, 0});
        CHECK_FALSE(binding.has_value());
        CHECK(is_fresh_failure(diagnostics));
    }
    SUBCASE("a Result selector with a nonzero param index is rejected") {
        auto [binding, diagnostics] = mint({CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 1});
        CHECK_FALSE(binding.has_value());
        CHECK(is_fresh_failure(diagnostics));
    }
    SUBCASE("a Param selector with an out-of-range index is rejected") {
        auto [binding, diagnostics] = mint({CoreCapabilityId{3}, 0, CoreWireRootKind::Param, 9});
        CHECK_FALSE(binding.has_value());
        CHECK(is_fresh_failure(diagnostics));
    }
    SUBCASE("an invalid selector kind is rejected") {
        CoreWireRootSelector selector{CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 0};
        selector.kind = static_cast<CoreWireRootKind>(0xff);
        auto [binding, diagnostics] = mint(selector);
        CHECK_FALSE(binding.has_value());
        CHECK(is_fresh_failure(diagnostics));
    }
}

TEST_CASE("the legacy transported-table wrapper still verifies-then-mints unchanged") {
    // The old one-shot path delegates through the same admission + mint, so a legal
    // table still mints and a tampered table is still rejected with a nonempty bag.
    std::vector<CoreLowerDiagnostic> ok_diag;
    auto ok_binding = make_wire_binding_from_transported_table(
        make_sparse_authority_table(), {CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 0},
        ok_diag);
    REQUIRE(ok_binding.has_value());
    CHECK(ok_binding->root() == CoreWireSchemaNodeId{1});
    CHECK(ok_diag.empty());

    auto tampered = make_sparse_authority_table();
    tampered.capabilities[1].params[0] = CoreWireSchemaNodeId{9999};
    std::vector<CoreLowerDiagnostic> bad_diag;
    auto bad_binding = make_wire_binding_from_transported_table(
        std::move(tampered), {CoreCapabilityId{3}, 0, CoreWireRootKind::Result, 0}, bad_diag);
    CHECK_FALSE(bad_binding.has_value());
    CHECK_FALSE(bad_diag.empty());
}
