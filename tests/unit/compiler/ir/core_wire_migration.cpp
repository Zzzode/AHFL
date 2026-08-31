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

capability ScalarCap(flag: Bool) -> Bool;
capability RefineCap(n: Int) -> Int(0, 100);
capability StructCap(p: Point) -> Point;
capability EnumCap(c: Color) -> Color;
capability OptionCap(n: Int) -> std::option::Option<Int>;
capability ListCap(n: Int) -> std::collections::List<Int>(8);

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

} // namespace

TEST_CASE("build_core_type_environment matches lower_ahfl_to_core's type table byte-for-byte") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());

    const auto full = lower_ahfl_to_core(*program);
    REQUIRE(full.ok());
    const auto seed = build_core_type_environment(*program);
    REQUIRE(seed.ok());

    // The shared prelude must produce EXACTLY the type table the full lowerer
    // builds (same source order, fixup, template finalize) — no body adds a
    // nominal declaration. The value-type arena the type-table pass interns is a
    // deterministic PREFIX of the full program's arena: full lowering appends
    // more value types while lowering capability / flow / workflow bodies, but it
    // must not reorder or alter the type-table-produced entries.
    CHECK(seed.types == full.program.types);
    REQUIRE(seed.value_types.size() <= full.program.value_types.size());
    bool prefix = true;
    for (std::size_t i = 0; i < seed.value_types.size(); ++i) {
        if (!(seed.value_types[i] == full.program.value_types[i])) {
            prefix = false;
            break;
        }
    }
    CHECK(prefix);
}

TEST_CASE("migration projector lowers scalar and refinement capability returns") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto seed = build_core_type_environment(*program);
    REQUIRE(seed.ok());

    SUBCASE("Bool") {
        const auto *cap = find_capability(*program, "ScalarCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, seed);
        REQUIRE(result.ok());
        CHECK(std::holds_alternative<CoreWireSchemaBool>(
            shape_of(*result.binding, result.binding->root())));
    }

    SUBCASE("bounded Int refinement carries its bounds") {
        const auto *cap = find_capability(*program, "RefineCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, seed);
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
    const auto seed = build_core_type_environment(*program);
    REQUIRE(seed.ok());

    SUBCASE("Option<Int>") {
        const auto *cap = find_capability(*program, "OptionCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, seed);
        REQUIRE(result.ok());
        const auto &opt = std::get<CoreWireSchemaOption>(
            shape_of(*result.binding, result.binding->root()));
        CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(*result.binding, opt.value)));
    }

    SUBCASE("List<Int>(8)") {
        const auto *cap = find_capability(*program, "ListCap");
        REQUIRE(cap != nullptr);
        const auto result = migrate_type_ref_to_wire_binding(cap->return_type_ref, seed);
        REQUIRE(result.ok());
        const auto &list = std::get<CoreWireSchemaSequence>(
            shape_of(*result.binding, result.binding->root()));
        CHECK(list.kind == CoreWireSequenceKind::List);
        CHECK(list.capacity == std::optional<std::uint64_t>{8});
        CHECK(std::holds_alternative<CoreWireSchemaInt>(shape_of(*result.binding, list.element)));
    }
}

TEST_CASE("migration is pure and deterministic") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto *cap = find_capability(*program, "StructCap");
    REQUIRE(cap != nullptr);
    const auto seed = build_core_type_environment(*program);
    const auto before = seed;
    const auto a = migrate_type_ref_to_wire_binding(cap->return_type_ref, seed);
    const auto b = migrate_type_ref_to_wire_binding(cap->return_type_ref, seed);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    // Seed untouched (pure) and repeat migration structurally identical.
    CHECK(seed.types == before.types);
    CHECK(seed.value_types == before.value_types);
    CHECK(a.binding->table() == b.binding->table());
    CHECK(a.binding->root() == b.binding->root());
}

TEST_CASE("migration projector fails closed on non-projectable TypeRefs") {
    const auto program = lower_fixture(__func__);
    REQUIRE(program.has_value());
    const auto seed = build_core_type_environment(*program);
    REQUIRE(seed.ok());

    SUBCASE("Unresolved type") {
        ir::TypeRef unresolved; // default kind == Unresolved
        const auto result = migrate_type_ref_to_wire_binding(unresolved, seed);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }

    SUBCASE("Any type") {
        ir::TypeRef any;
        any.kind = ir::TypeRefKind::Any;
        const auto result = migrate_type_ref_to_wire_binding(any, seed);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }

    SUBCASE("Never type") {
        ir::TypeRef never;
        never.kind = ir::TypeRefKind::Never;
        const auto result = migrate_type_ref_to_wire_binding(never, seed);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.binding.has_value());
    }

    SUBCASE("a Struct TypeRef with an unresolved nominal (no verified decl)") {
        ir::TypeRef bogus;
        bogus.kind = ir::TypeRefKind::Struct;
        bogus.canonical_name = "app::main::DoesNotExist";
        // nominal_ref left with no id and a name that no decl claims.
        const auto result = migrate_type_ref_to_wire_binding(bogus, seed);
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
        std::move(tampered), good.binding->root(), diagnostics);
    CHECK_FALSE(rejected.has_value());
    CHECK_FALSE(diagnostics.empty());
}
