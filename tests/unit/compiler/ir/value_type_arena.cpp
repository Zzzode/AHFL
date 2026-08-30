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
using ir::core::CoreVariance;
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
    list.variances = {CoreVariance::Covariant};
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
    list.variances = {ir::core::CoreVariance::Covariant};
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

// RFC 0026 P4 (coercion) F1: the builtin descriptor SSOT carries a per-parameter
// variance vector parallel to its arity (Option/List/Set covariant element,
// Result covariant both, Map invariant key + covariant value).
TEST_CASE("builtin nominal SSOT variance vector is parallel to arity") {
    using namespace ir::core;
    for (const auto &d : builtin_nominal_table()) {
        INFO(std::string(d.canonical_name));
        CHECK(d.variances.size() == d.type_param_count);
    }
    const auto by_name = [](std::string_view canonical) -> const BuiltinNominalDescriptor & {
        for (const auto &d : builtin_nominal_table()) {
            if (d.canonical_name == canonical) {
                return d;
            }
        }
        FAIL("descriptor not found");
        return builtin_nominal_table().front();
    };
    CHECK(by_name("std::option::Option").variances == std::vector<CoreVariance>{CoreVariance::Covariant});
    CHECK(by_name("std::result::Result").variances ==
          std::vector<CoreVariance>{CoreVariance::Covariant, CoreVariance::Covariant});
    CHECK(by_name("std::collections::Map").variances ==
          std::vector<CoreVariance>{CoreVariance::Invariant, CoreVariance::Covariant});
}

// RFC 0026 P4 (coercion) F1: a user generic nominal now carries a real arity +
// variance (add_struct/add_enum fill them from the AHFL-IR decl), which unlocks
// P4-A user-generic lowering: `Box<Int>` lowers to a CoreVtNominal over Box.
TEST_CASE("F1: a user generic nominal (Box<Int>) lowers with its real arity + variance") {
    using namespace ir::core;
    CoreProgram p;
    CoreTypeDecl box;
    box.kind = CoreTypeDecl::Kind::Struct;
    box.name = "app::Box";
    box.type_param_count = 1;                     // real user arity (was 0 before F1)
    box.variances = {CoreVariance::Covariant};    // T appears covariantly (a `value: T` field)
    box.symbol_ref =
        ir::SymbolRef{.kind = ir::SymbolRefKind::Type, .canonical_name = "app::Box", .id = std::size_t{7}};
    p.types.push_back(std::move(box));

    ir::TypeRef box_ref = nominal_ref(ir::TypeRefKind::Struct, "app::Box", 7);
    box_ref.params.push_back(make_owned<ir::TypeRef>([] {
        ir::TypeRef t;
        t.kind = ir::TypeRefKind::Int;
        return t;
    }()));
    std::string reason;
    const auto id = lower_value_type_into(p, box_ref, &reason);
    INFO(reason);
    REQUIRE(id.has_value());
    const auto &vt = p.value_types[id->value];
    const auto *nom = std::get_if<CoreVtNominal>(&vt.node);
    REQUIRE(nom != nullptr);
    CHECK(nom->base.value == 0u); // app::Box is type[0]
    REQUIRE(nom->args.size() == 1);
    CHECK(std::holds_alternative<CoreVtInt>(p.value_types[nom->args[0].value].node));
    // The CoreTypeDecl carries the real arity + variance (field template NOT
    // materialized here - that stays a P4-C concern).
    CHECK(p.types[0].type_param_count == 1);
    CHECK(p.types[0].variances == std::vector<CoreVariance>{CoreVariance::Covariant});
    CHECK(p.types[0].field_nominal_types.empty()); // field template not synthesized
    CHECK_FALSE(verify_core_program(p).has_errors());
}

TEST_CASE("F1: a user generic applied at the WRONG arity is fail-closed") {
    using namespace ir::core;
    CoreProgram p;
    CoreTypeDecl box;
    box.kind = CoreTypeDecl::Kind::Struct;
    box.name = "app::Box";
    box.type_param_count = 1;
    box.variances = {CoreVariance::Covariant};
    box.symbol_ref =
        ir::SymbolRef{.kind = ir::SymbolRefKind::Type, .canonical_name = "app::Box", .id = std::size_t{7}};
    p.types.push_back(std::move(box));

    // Box<Int, Int> - two args against arity 1.
    ir::TypeRef box_ref = nominal_ref(ir::TypeRefKind::Struct, "app::Box", 7);
    for (int i = 0; i < 2; ++i) {
        box_ref.params.push_back(make_owned<ir::TypeRef>([] {
            ir::TypeRef t;
            t.kind = ir::TypeRefKind::Int;
            return t;
        }()));
    }
    std::string reason;
    const auto id = lower_value_type_into(p, box_ref, &reason);
    CHECK_FALSE(id.has_value()); // arity mismatch fails closed
}

TEST_CASE("P0-3: lower_value_type_into resolves nominal bases STRICT id-first") {
    using namespace ir::core;

    SUBCASE("matching id resolves to that decl") {
        CoreProgram p;
        CoreTypeDecl a;
        a.kind = CoreTypeDecl::Kind::Struct;
        a.name = "app::Alpha";
        a.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                     .canonical_name = "app::Alpha",
                                     .id = std::size_t{10}};
        p.types.push_back(std::move(a));
        ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Struct, "app::Alpha", 10);
        std::string reason;
        const auto id = ir::core::lower_value_type_into(p, ref, &reason);
        INFO(reason);
        REQUIRE(id.has_value());
        const auto *nominal = std::get_if<CoreVtNominal>(&p.value_types[id->value].node);
        REQUIRE(nominal != nullptr);
        CHECK(nominal->base == CoreTypeId{0});
    }
    SUBCASE("WRONG id + right canonical against an id-bearing decl -> REJECT") {
        // The decl has id=10; a ref claiming id=99 (with the right canonical) must
        // NOT be silently downgraded to a spelling match.
        CoreProgram p;
        CoreTypeDecl a;
        a.kind = CoreTypeDecl::Kind::Struct;
        a.name = "app::Alpha";
        a.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                     .canonical_name = "app::Alpha",
                                     .id = std::size_t{10}};
        p.types.push_back(std::move(a));
        ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Struct, "app::Alpha", 99);
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(p, ref, &reason).has_value());
    }
    SUBCASE("present id + NAME-ONLY synthetic base (no id) + same canonical -> ACCEPT") {
        // The un-inlined std base carries a name-only ref (no id); a typed-side ref
        // WITH an id but the right canonical resolves to it (the legal fallback).
        CoreProgram p;
        CoreTypeDecl syn;
        syn.kind = CoreTypeDecl::Kind::Struct;
        syn.name = "app::Beta";
        syn.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                       .canonical_name = "app::Beta"}; // no id
        p.types.push_back(std::move(syn));
        ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Struct, "app::Beta", 7);
        std::string reason;
        CHECK(ir::core::lower_value_type_into(p, ref, &reason).has_value());
    }
    SUBCASE("matching id + canonical drift -> REJECT") {
        CoreProgram p;
        CoreTypeDecl a;
        a.kind = CoreTypeDecl::Kind::Struct;
        a.name = "app::Alpha";
        a.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                     .canonical_name = "app::Alpha",
                                     .id = std::size_t{10}};
        p.types.push_back(std::move(a));
        // The ref self-consistently names "app::Drifted" (TypeRef canonical ==
        // nominal_ref canonical) with id=10 — but id 10 resolves to a decl named
        // "app::Alpha". The strict resolver rejects the id/name drift.
        ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Struct, "app::Drifted", 10);
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(p, ref, &reason).has_value());
    }
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
    bad.variances = {ir::core::CoreVariance::Covariant};
    p.types.push_back(std::move(bad));
    CHECK(verify_core_program(p).has_errors());
}

TEST_CASE("P1-1: lower_nominal rejects a missing canonical and an unknown enum variant") {
    using namespace ir::core;
    CoreProgram p;
    // An enum Color { Red } to reference by variant.
    CoreTypeDecl color;
    color.kind = CoreTypeDecl::Kind::Enum;
    color.name = "app::Color";
    color.variants = {"Red"};
    color.variant_payloads.push_back(CoreTypeDecl::VariantPayload{});
    color.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                     .canonical_name = "app::Color",
                                     .id = std::size_t{3}};
    p.types.push_back(std::move(color));

    SUBCASE("missing TypeRef canonical (empty) is rejected even if nominal_ref resolves") {
        ir::TypeRef ref;
        ref.kind = ir::TypeRefKind::Enum;
        ref.canonical_name = ""; // empty display canonical
        ref.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                        .canonical_name = "app::Color",
                                        .id = std::size_t{3}};
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(p, ref, &reason).has_value());
    }
    SUBCASE("an unknown variant (Ghost) is NOT silently normalized to the parent enum") {
        ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Enum, "app::Color", 3);
        ref.variant_name = "Ghost"; // no such variant
        std::string reason;
        CHECK_FALSE(ir::core::lower_value_type_into(p, ref, &reason).has_value());
    }
    SUBCASE("a known variant (Red) normalizes to the parent enum and lowers") {
        ir::TypeRef ref = nominal_ref(ir::TypeRefKind::Enum, "app::Color", 3);
        ref.variant_name = "Red";
        std::string reason;
        const auto id = ir::core::lower_value_type_into(p, ref, &reason);
        INFO(reason);
        REQUIRE(id.has_value());
        // Identity is the parent enum; the variant name did not enter it.
        const auto *nominal = std::get_if<CoreVtNominal>(&p.value_types[id->value].node);
        REQUIRE(nominal != nullptr);
        CHECK(nominal->base == CoreTypeId{0});
    }
}

TEST_CASE("P1-2 verifier: role<->canonical identity is locked bidirectionally") {
    using namespace ir::core;

    SUBCASE("a user type faking a collection role (evil::Foo, role=List) is rejected") {
        CoreProgram p;
        CoreTypeDecl evil;
        evil.kind = CoreTypeDecl::Kind::Struct;
        evil.name = "evil::Foo";
        evil.role = CoreNominalRole::List; // canonical is NOT std::collections::List
        evil.type_param_count = 1;
        evil.variances = {ir::core::CoreVariance::Covariant};
        p.types.push_back(std::move(evil));
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("a builtin canonical with the wrong role (List name, Ordinary role) is rejected") {
        CoreProgram p;
        CoreTypeDecl list;
        list.kind = CoreTypeDecl::Kind::Struct;
        list.name = "std::collections::List";
        list.role = CoreNominalRole::Ordinary; // must be List
        list.type_param_count = 1;
        list.variances = {ir::core::CoreVariance::Covariant};
        p.types.push_back(std::move(list));
        CHECK(verify_core_program(p).has_errors());
    }
    SUBCASE("symbol_ref canonical drift from the decl name is rejected") {
        CoreProgram p;
        CoreTypeDecl t;
        t.kind = CoreTypeDecl::Kind::Struct;
        t.name = "app::User";
        t.symbol_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                     .canonical_name = "app::Drifted",
                                     .id = std::size_t{1}};
        p.types.push_back(std::move(t));
        CHECK(verify_core_program(p).has_errors());
    }
}

TEST_CASE("P4-C member template materializer substitutes recursively and hash-conses") {
    using namespace ir::core;
    CoreProgram p = program_with_types();

    CoreTypeDecl box;
    box.kind = CoreTypeDecl::Kind::Struct;
    box.name = "app::Box";
    box.fields = {"items", "map"};
    box.field_nominal_types = {CoreTypeId{}, CoreTypeId{}};
    box.field_has_default = {false, false};
    box.type_param_count = 1;
    box.variances = {CoreVariance::Covariant};
    CoreMemberTypeTemplateNode param;
    param.kind = CoreMemberTypeTemplateKind::Param;
    param.param_index = 0;
    box.member_type_templates.push_back(param); // #0 T
    CoreMemberTypeTemplateNode list;
    list.kind = CoreMemberTypeTemplateKind::Nominal;
    list.nominal = CoreTypeId{1}; // std List
    list.capacity = 4;
    list.children = {CoreMemberTypeTemplateNodeId{0}};
    box.member_type_templates.push_back(list); // #1 List<T>(4)
    CoreMemberTypeTemplateNode fn;
    fn.kind = CoreMemberTypeTemplateKind::Fn;
    fn.children = {CoreMemberTypeTemplateNodeId{0}};
    fn.fn_return = CoreMemberTypeTemplateNodeId{1};
    box.member_type_templates.push_back(fn); // #2 Fn(T)->List<T>(4)
    box.field_type_template_roots = {CoreMemberTypeTemplateNodeId{1},
                                     CoreMemberTypeTemplateNodeId{2}};
    p.types.push_back(std::move(box));

    std::string reason;
    const auto int_id = lower_value_type_into(p, prim(ir::TypeRefKind::Int), &reason);
    REQUIRE(int_id.has_value());
    const auto list_id = instantiate_member_template(
        p, CoreTypeId{2}, CoreMemberTypeTemplateNodeId{1}, {*int_id}, &reason);
    INFO(reason);
    REQUIRE(list_id.has_value());
    const auto *list_vt = std::get_if<CoreVtNominal>(&p.value_types[list_id->value].node);
    REQUIRE(list_vt != nullptr);
    CHECK(list_vt->base == CoreTypeId{1});
    CHECK(list_vt->args == std::vector<CoreValueTypeId>{*int_id});
    CHECK(list_vt->capacity == std::optional<std::uint64_t>{4});

    const auto fn_id = instantiate_member_template(
        p, CoreTypeId{2}, CoreMemberTypeTemplateNodeId{2}, {*int_id}, &reason);
    REQUIRE(fn_id.has_value());
    const auto *fn_vt = std::get_if<CoreVtFn>(&p.value_types[fn_id->value].node);
    REQUIRE(fn_vt != nullptr);
    CHECK(fn_vt->params == std::vector<CoreValueTypeId>{*int_id});
    CHECK(fn_vt->ret == *list_id);

    const auto size = p.value_types.size();
    reason.clear();
    CHECK(instantiate_member_template(
              p, CoreTypeId{2}, CoreMemberTypeTemplateNodeId{2}, {*int_id}, &reason) == fn_id);
    CHECK(p.value_types.size() == size);
    CHECK_FALSE(verify_core_program(p).has_errors());

    reason.clear();
    CHECK_FALSE(
        instantiate_member_template(p, CoreTypeId{2}, CoreMemberTypeTemplateNodeId{2}, {}, &reason)
            .has_value());
    CHECK(reason.find("expects 1") != std::string::npos);
}
