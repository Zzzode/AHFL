#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
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

TEST_CASE("build_core_type_environment fails closed on a malformed value type (member-template gate)") {
    // Prove the type-local gate reaches verify_value_types / member-template
    // checks, not only verify_types variance. A struct field whose member
    // template resolves to a bounded Int with min > max is a malformed value type
    // the value-type verifier must reject — surfaced through the gate as no
    // environment.
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
