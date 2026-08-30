#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// RFC 0026 P4-A1: the logical value-type arena + hash-cons interner +
// lower_value_type shape gate + verify_value_types + the builtin nominal
// descriptor SSOT. These tests exercise the arena in isolation (a hand-built
// CoreProgram whose `types` table stands in for the lowerer's TypeEnv) so the
// interner canonicalisation, determinism, fail-closed shape gate, and standalone
// verifier are pinned independently of the dispatch consumer (which lands in A2).

namespace {

using namespace ahfl;
using ir::core::CoreNominalRole;
using ir::core::CoreProgram;
using ir::core::CoreTypeDecl;
using ir::core::CoreValueTypeId;

// A struct/enum TypeRef with a resolved nominal_ref (id-first bridge), the shape
// lower_value_type consumes for a nominal.
[[nodiscard]] ir::TypeRef nominal_ref(ir::TypeRefKind kind, const std::string &canonical,
                                      std::size_t id) {
    ir::TypeRef t;
    t.kind = kind;
    t.canonical_name = canonical;
    t.display_name = canonical;
    t.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                  .canonical_name = canonical,
                                  .local_name = canonical,
                                  .module_name = "",
                                  .id = id};
    return t;
}

[[nodiscard]] ir::TypeRef prim(ir::TypeRefKind kind) {
    ir::TypeRef t;
    t.kind = kind;
    return t;
}

// A program whose type table has: [0] user struct "app::User" (0 params),
// [1] std List (role List, 1 param). Enough to drive nominal lowering.
[[nodiscard]] CoreProgram program_with_types() {
    CoreProgram p;
    CoreTypeDecl user;
    user.kind = CoreTypeDecl::Kind::Struct;
    user.name = "app::User";
    p.types.push_back(std::move(user));
    CoreTypeDecl list;
    list.kind = CoreTypeDecl::Kind::Struct;
    list.name = "std::collections::List";
    list.role = CoreNominalRole::List;
    list.type_param_count = 1;
    p.types.push_back(std::move(list));
    return p;
}

} // namespace

TEST_CASE("value-type interner is a canonical hash-cons (same structural type -> same id)") {
    CoreProgram p = program_with_types();
    std::string reason;
    // Two independent Int lowerings must yield the SAME id.
    const auto a = ir::core::lower_value_type_into(p, prim(ir::TypeRefKind::Int), &reason);
    const auto b = ir::core::lower_value_type_into(p, prim(ir::TypeRefKind::Int), &reason);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
    CHECK(p.value_types.size() == 1); // deduped, not appended twice

    // A structurally different type gets a distinct id + a new arena entry.
    const auto c = ir::core::lower_value_type_into(p, prim(ir::TypeRefKind::Bool), &reason);
    REQUIRE(c.has_value());
    CHECK_FALSE(*c == *a);
    CHECK(p.value_types.size() == 2);
}

TEST_CASE("value-type lowering is deterministic across use-sites (nested type -> same id)") {
    CoreProgram p = program_with_types();
    std::string reason;
    // List<User> built twice from two independently-constructed TypeRefs.
    const auto make_list_of_user = [] {
        ir::TypeRef list = nominal_ref(ir::TypeRefKind::Struct, "std::collections::List", 1);
        list.collection_capacity = std::uint64_t{8};
        list.params.push_back(
            make_owned<ir::TypeRef>(nominal_ref(ir::TypeRefKind::Struct, "app::User", 0)));
        return list;
    };
    const auto first = ir::core::lower_value_type_into(p, make_list_of_user(), &reason);
    const std::size_t after_first = p.value_types.size();
    const auto second = ir::core::lower_value_type_into(p, make_list_of_user(), &reason);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    // Second lowering adds NOTHING (User + List<User> already interned).
    CHECK(p.value_types.size() == after_first);
    // The interned program verifies clean.
    CHECK_FALSE(ir::core::verify_core_program(p).has_errors());

    // Cross-program determinism (Codex ruling a): two fresh programs lowering the
    // SAME input sequence produce identical arena ORDER, so same-owner-arena
    // CoreValueTypeId equality is stable across independent lowerings.
    const auto lower_sequence = [](CoreProgram &prog) {
        std::string r;
        (void)ir::core::lower_value_type_into(prog, prim(ir::TypeRefKind::Int), &r);
        ir::TypeRef list = nominal_ref(ir::TypeRefKind::Struct, "std::collections::List", 1);
        list.params.push_back(
            make_owned<ir::TypeRef>(nominal_ref(ir::TypeRefKind::Struct, "app::User", 0)));
        (void)ir::core::lower_value_type_into(prog, list, &r);
    };
    CoreProgram p1 = program_with_types();
    CoreProgram p2 = program_with_types();
    lower_sequence(p1);
    lower_sequence(p2);
    REQUIRE(p1.value_types.size() == p2.value_types.size());
    CHECK(p1.value_types == p2.value_types); // identical order + content
}

TEST_CASE("lower_value_type fails closed on non-materializable / malformed inputs") {
    CoreProgram p = program_with_types();

    SUBCASE("Unresolved") {
        std::string reason;
        CHECK_FALSE(
            ir::core::lower_value_type_into(p, prim(ir::TypeRefKind::Unresolved), &reason));
        CHECK_FALSE(reason.empty());
    }
    SUBCASE("Any") {
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(p, prim(ir::TypeRefKind::Any), &reason));
    }
    SUBCASE("Never (rejected in this dispatch consumer)") {
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(p, prim(ir::TypeRefKind::Never), &reason));
    }
    SUBCASE("nominal missing its resolved identity") {
        std::string reason;
        ir::TypeRef bad;
        bad.kind = ir::TypeRefKind::Struct;
        bad.canonical_name = "app::User"; // no nominal_ref
        CHECK_FALSE(ir::core::lower_value_type_into(p, bad, &reason));
    }
    SUBCASE("nominal that does not resolve to a Core type") {
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(
            p, nominal_ref(ir::TypeRefKind::Struct, "app::Missing", 99), &reason));
    }
    SUBCASE("wrong generic arity") {
        std::string reason;
        // List expects 1 arg; give 0.
        CHECK_FALSE(ir::core::lower_value_type_into(
            p, nominal_ref(ir::TypeRefKind::Struct, "std::collections::List", 1), &reason));
    }
    SUBCASE("capacity on a non-collection nominal") {
        std::string reason;
        ir::TypeRef user = nominal_ref(ir::TypeRefKind::Struct, "app::User", 0);
        user.collection_capacity = std::uint64_t{4}; // User is Ordinary -> rejected
        CHECK_FALSE(ir::core::lower_value_type_into(p, user, &reason));
    }
    SUBCASE("Fn missing its return type") {
        std::string reason;
        ir::TypeRef fn;
        fn.kind = ir::TypeRefKind::Fn; // first (return) is null
        CHECK_FALSE(ir::core::lower_value_type_into(p, fn, &reason));
    }
    SUBCASE("primitive carrying a stray nominal identity") {
        std::string reason;
        ir::TypeRef b = prim(ir::TypeRefKind::Bool);
        b.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type, .canonical_name = "X"};
        CHECK_FALSE(ir::core::lower_value_type_into(p, b, &reason));
    }
}

TEST_CASE("lower_value_type accepts a well-formed Fn and bounded refinements") {
    CoreProgram p = program_with_types();
    std::string reason;
    // Fn(Int) -> Bool: params=[Int], return in `first`.
    ir::TypeRef fn;
    fn.kind = ir::TypeRefKind::Fn;
    fn.params.push_back(make_owned<ir::TypeRef>(prim(ir::TypeRefKind::Int)));
    fn.first = make_owned<ir::TypeRef>(prim(ir::TypeRefKind::Bool));
    const auto id = ir::core::lower_value_type_into(p, fn, &reason);
    INFO(reason);
    REQUIRE(id.has_value());
    CHECK_FALSE(ir::core::verify_core_program(p).has_errors());

    // BoundedInt with a valid range.
    ir::TypeRef bi;
    bi.kind = ir::TypeRefKind::BoundedInt;
    bi.int_bounds = std::make_pair<std::int64_t, std::int64_t>(0, 10);
    CHECK(ir::core::lower_value_type_into(p, bi, &reason).has_value());
}

TEST_CASE("verify_value_types rejects a hand-built corrupt arena") {
    using namespace ir::core;

    SUBCASE("out-of-range child id") {
        CoreProgram p = program_with_types();
        CoreVtTuple tup;
        tup.elements.push_back(CoreValueTypeId{7}); // no such entry
        p.value_types.push_back(CoreValueType{tup});
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("duplicate structural entries (interning bypassed)") {
        CoreProgram p = program_with_types();
        p.value_types.push_back(CoreValueType{CoreVtBool{}});
        p.value_types.push_back(CoreValueType{CoreVtBool{}}); // structural dup
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("nominal arity mismatch") {
        CoreProgram p = program_with_types();
        // List (base id 1) expects 1 arg; give 0.
        p.value_types.push_back(CoreValueType{CoreVtNominal{CoreTypeId{1}, {}, std::nullopt}});
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("capacity on a non-collection nominal") {
        CoreProgram p = program_with_types();
        // User (base id 0, Ordinary) with a capacity.
        p.value_types.push_back(
            CoreValueType{CoreVtNominal{CoreTypeId{0}, {}, std::uint64_t{4}}});
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("nominal base id out of range") {
        CoreProgram p = program_with_types();
        p.value_types.push_back(
            CoreValueType{CoreVtNominal{CoreTypeId{99}, {}, std::nullopt}});
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("reversed Int refinement bound") {
        CoreProgram p = program_with_types();
        p.value_types.push_back(CoreValueType{CoreVtInt{std::make_pair<std::int64_t, std::int64_t>(
            10, 0)}});
        CHECK(verify_core_program(p).has_errors());
    }
}

// Textual scan of `pub struct <Name><...>` generic arity in a sysroot file, to
// guard the builtin nominal descriptor SSOT against sysroot drift (invariant 3).
namespace {
[[nodiscard]] std::optional<std::uint32_t> read_struct_arity(const std::string &path,
                                                             const std::string &name) {
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return std::nullopt;
    }
    std::string text;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    std::fclose(f);
    const std::string needle = "struct " + name;
    const auto pos = text.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const auto lt = text.find('<', pos);
    const auto brace = text.find('{', pos);
    if (lt == std::string::npos || (brace != std::string::npos && lt > brace)) {
        return 0; // no generic params
    }
    const auto gt = text.find('>', lt);
    if (gt == std::string::npos) {
        return std::nullopt;
    }
    const std::string params = text.substr(lt + 1, gt - lt - 1);
    std::uint32_t count = params.empty() ? 0 : 1;
    for (char c : params) {
        if (c == ',') {
            ++count;
        }
    }
    return count;
}
} // namespace

TEST_CASE("builtin nominal SSOT collection arities match the sysroot (drift guard)") {
    const auto &table = ir::core::builtin_nominal_table();
    REQUIRE_FALSE(table.empty());
    // The collection structs live in std/collections.ahfl.
    struct Expect {
        std::string canonical;
        std::string struct_name;
    };
    const Expect collections[] = {
        {"std::collections::List", "List"},
        {"std::collections::Set", "Set"},
        {"std::collections::Map", "Map"},
    };
    for (const auto &e : collections) {
        const auto *desc = [&]() -> const ir::core::BuiltinNominalDescriptor * {
            for (const auto &d : table) {
                if (std::string(d.canonical_name) == e.canonical) {
                    return &d;
                }
            }
            return nullptr;
        }();
        INFO("collection " << e.canonical);
        REQUIRE(desc != nullptr);
        const auto arity = read_struct_arity("std/collections.ahfl", e.struct_name);
        REQUIRE(arity.has_value());
        CHECK(desc->type_param_count == *arity);
    }
}
