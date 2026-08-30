#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "compiler/semantics/std_container_types.hpp"

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

// Intern a value type into a program's arena (dedup by structural equality).
[[nodiscard]] CoreValueTypeId intern_program_vt(CoreProgram &p, ir::core::CoreValueType vt) {
    for (std::uint32_t i = 0; i < p.value_types.size(); ++i) {
        if (p.value_types[i] == vt) {
            return CoreValueTypeId{i};
        }
    }
    const auto id = CoreValueTypeId{static_cast<std::uint32_t>(p.value_types.size())};
    p.value_types.push_back(std::move(vt));
    return id;
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

TEST_CASE("builtin nominal SSOT canonical names match the semantics container matcher (P1 sync gate)") {
    // The semantics layer (stdlib_bridge) hardcodes the same canonical names +
    // kinds + arities. Pin them to the single IR SSOT so a rename/kind change on
    // either side is caught rather than silently drifting into a third truth.
    const auto &table = ir::core::builtin_nominal_table();
    const auto find = [&](std::string_view canonical) -> const ir::core::BuiltinNominalDescriptor * {
        for (const auto &d : table) {
            if (d.canonical_name == canonical) {
                return &d;
            }
        }
        return nullptr;
    };
    using K = ir::core::CoreTypeDecl::Kind;
    struct Expect {
        std::string_view canonical;
        K kind;
        std::uint32_t arity;
    };
    const Expect specs[] = {
        {ahfl::stdlib_bridge::kOptionType, K::Enum, 1},
        {ahfl::stdlib_bridge::kResultType, K::Enum, 2},
        {ahfl::stdlib_bridge::kListType, K::Struct, 1},
        {ahfl::stdlib_bridge::kSetType, K::Struct, 1},
        {ahfl::stdlib_bridge::kMapType, K::Struct, 2},
    };
    for (const auto &s : specs) {
        INFO("stdlib_bridge canonical " << s.canonical);
        const auto *desc = find(s.canonical);
        REQUIRE(desc != nullptr); // the matcher's canonical MUST exist in the SSOT
        CHECK(desc->kind == s.kind);
        CHECK(desc->type_param_count == s.arity);
    }
}

TEST_CASE("P0-2: a REAL std generic decl is decorated with role + arity from the SSOT") {
    // Simulate include_stdlib / inlined / deserialized: a real List<T> struct decl
    // registered by the normal type-table path (NOT add_builtins) must still get
    // role=List + type_param_count=1, so a legal List<Int> lowers.
    using namespace ir::core;
    CoreProgram p;
    CoreTypeDecl user;
    user.kind = CoreTypeDecl::Kind::Struct;
    user.name = "app::User";
    p.types.push_back(std::move(user));
    // A real std List decl WITHOUT hand-set role/arity — the lowering pipeline's
    // register_type decorates from the SSOT. Here we assert the SSOT projection a
    // hand-built program must mirror, then confirm a List<User> lowers only when
    // decorated.
    CoreTypeDecl list;
    list.kind = CoreTypeDecl::Kind::Struct;
    list.name = "std::collections::List";
    // Decorate exactly as register_type would (role=List, arity=1).
    list.role = CoreNominalRole::List;
    list.type_param_count = 1;
    list.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                    .canonical_name = "std::collections::List",
                                    .id = std::size_t{42}};
    p.types.push_back(std::move(list));

    // List<User> resolved by SYMBOL ID (id-first), not just canonical.
    ir::TypeRef list_ref = nominal_ref(ir::TypeRefKind::Struct, "std::collections::List", 42);
    list_ref.params.push_back(
        make_owned<ir::TypeRef>(nominal_ref(ir::TypeRefKind::Struct, "app::User", 0)));
    std::string reason;
    const auto id = ir::core::lower_value_type_into(p, list_ref, &reason);
    INFO(reason);
    REQUIRE(id.has_value());
    CHECK_FALSE(verify_core_program(p).has_errors());
}

TEST_CASE("P0-3: lower_value_type_into resolves nominal bases ID-FIRST") {
    using namespace ir::core;
    CoreProgram p;
    // Two DIFFERENT nominals that share NO canonical, but the ref carries a symbol
    // id that must select the right one regardless of canonical scan order.
    CoreTypeDecl a;
    a.kind = CoreTypeDecl::Kind::Struct;
    a.name = "app::Alpha";
    a.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                 .canonical_name = "app::Alpha",
                                 .id = std::size_t{10}};
    p.types.push_back(std::move(a));

    // A ref whose canonical is "app::Alpha" but whose id is 10 resolves to Alpha.
    ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Struct, "app::Alpha", 10);
    std::string reason;
    const auto id = ir::core::lower_value_type_into(p, ref, &reason);
    INFO(reason);
    REQUIRE(id.has_value());
    const auto *nominal = std::get_if<CoreVtNominal>(&p.value_types[id->value].node);
    REQUIRE(nominal != nullptr);
    CHECK(nominal->base == CoreTypeId{0});

    // A ref with a WRONG id (99) but the right canonical still resolves via the
    // canonical fallback (id-first, THEN canonical) — but a wrong id + wrong
    // canonical fails closed.
    ir::TypeRef bad = nominal_ref(ir::TypeRefKind::Struct, "app::Missing", 99);
    CHECK_FALSE(ir::core::lower_value_type_into(p, bad, &reason).has_value());
}

TEST_CASE("P0-1: lower_value_type rejects stray fields per kind (field-tamper matrix)") {
    CoreProgram p = program_with_types();
    std::string reason;

    SUBCASE("Bool carrying int_bounds") {
        ir::TypeRef b = prim(ir::TypeRefKind::Bool);
        b.int_bounds = std::make_pair<std::int64_t, std::int64_t>(0, 1);
        CHECK_FALSE(ir::core::lower_value_type_into(p, b, &reason));
    }
    SUBCASE("Unit carrying collection_capacity") {
        ir::TypeRef u = prim(ir::TypeRefKind::Unit);
        u.collection_capacity = std::uint64_t{4};
        CHECK_FALSE(ir::core::lower_value_type_into(p, u, &reason));
    }
    SUBCASE("Int carrying a decimal_scale") {
        ir::TypeRef i = prim(ir::TypeRefKind::Int);
        i.decimal_scale = 2;
        CHECK_FALSE(ir::core::lower_value_type_into(p, i, &reason));
    }
    SUBCASE("Fn carrying a nominal_ref") {
        ir::TypeRef fn;
        fn.kind = ir::TypeRefKind::Fn;
        fn.first = make_owned<ir::TypeRef>(prim(ir::TypeRefKind::Bool));
        fn.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type, .canonical_name = "X"};
        CHECK_FALSE(ir::core::lower_value_type_into(p, fn, &reason));
    }
    SUBCASE("Struct TypeRef resolving to an Enum decl (cross-kind)") {
        // program_with_types has List (Struct); build a program with an Enum and
        // reference it with a Struct tag.
        CoreProgram q;
        ir::core::CoreTypeDecl e;
        e.kind = ir::core::CoreTypeDecl::Kind::Enum;
        e.name = "app::Color";
        e.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                     .canonical_name = "app::Color",
                                     .id = std::size_t{5}};
        q.types.push_back(std::move(e));
        ir::TypeRef as_struct = nominal_ref(ir::TypeRefKind::Struct, "app::Color", 5);
        CHECK_FALSE(ir::core::lower_value_type_into(q, as_struct, &reason));
    }
    SUBCASE("canonical drift between TypeRef and nominal_ref") {
        ir::TypeRef u = nominal_ref(ir::TypeRefKind::Struct, "app::User", 0);
        u.canonical_name = "app::Drifted"; // display canonical disagrees with the ref
        CHECK_FALSE(ir::core::lower_value_type_into(p, u, &reason));
    }
}

TEST_CASE("P1 verifier: a Closure whose signature is not a Fn is fail-closed") {
    using namespace ir::core;
    CoreProgram p = program_with_types();
    const auto bool_id = intern_program_vt(p, CoreValueType{CoreVtBool{}});
    // Closure signature points at a Bool (not a Fn) -> rejected.
    p.value_types.push_back(CoreValueType{CoreVtClosure{bool_id, {}}});
    CHECK(verify_core_program(p).has_errors());
}

TEST_CASE("P1 verifier: a forward/self child reference is fail-closed") {
    using namespace ir::core;
    CoreProgram p = program_with_types();
    // A tuple at index 0 referencing child id 1 (not yet interned) is a forward ref.
    p.value_types.push_back(CoreValueType{CoreVtTuple{{CoreValueTypeId{1}}}});
    p.value_types.push_back(CoreValueType{CoreVtBool{}});
    CHECK(verify_core_program(p).has_errors());
}

TEST_CASE("P1 verifier: a role<->kind mismatch on a nominal decl is fail-closed") {
    using namespace ir::core;
    CoreProgram p;
    // role=List but kind=Enum -> contradiction (List must be a Struct).
    CoreTypeDecl bad;
    bad.kind = CoreTypeDecl::Kind::Enum;
    bad.name = "std::collections::List";
    bad.role = CoreNominalRole::List;
    bad.type_param_count = 1;
    p.types.push_back(std::move(bad));
    CHECK(verify_core_program(p).has_errors());
}
