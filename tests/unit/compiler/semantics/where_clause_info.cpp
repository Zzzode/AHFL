// ============================================================================
// WhereClauseInfo plumbing tests — P2d.S1
//
// Verifies that where_clause fields declared on StructDecl / EnumDecl /
// CapabilityDecl (the function-like nominal declaration in AHFL, standing in
// for FnTypeInfo per task spec) are faithfully propagated into the
// corresponding *TypeInfo records produced by typecheck_decls.
//
// Tests build AST manually because the grammar does not yet recognise the
// `where` keyword or generic type parameters; this task is plumbing-only.
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::ast;

// ---------------------------------------------------------------------------
// Small builder helpers
// ---------------------------------------------------------------------------

Owned<QualifiedName> make_qname(std::vector<std::string> segments) {
    auto qn = make_owned<QualifiedName>();
    qn->segments = std::move(segments);
    return qn;
}

Owned<TypeSyntax> make_named_type(std::string_view name) {
    auto ts = make_owned<TypeSyntax>();
    NamedType nt;
    nt.name = make_qname({std::string(name)});
    ts->node = std::move(nt);
    return ts;
}

// Build a where clause: "where T:Eq" — single bound, single trait.
Owned<WhereClauseSyntax> make_single_bound_where(std::string_view subject_name,
                                                  std::string_view trait_name) {
    auto clause = make_owned<WhereClauseSyntax>();

    auto constraint = make_owned<WhereConstraintSyntax>();
    constraint->subject = make_named_type(subject_name);
    constraint->is_predicate = false;
    constraint->bounds.push_back(make_named_type(trait_name));

    clause->constraints.push_back(std::move(constraint));
    return clause;
}

// ---------------------------------------------------------------------------
// Build a minimal program with a module declaration + the supplied nominal
// declarations. Runs resolver + typechecker. Returns TypeCheckResult so
// individual tests can inspect the TypeEnvironment.
// ---------------------------------------------------------------------------

struct ProgramFixture {
    Owned<Program> program;
    ResolveResult resolve;
    TypeCheckResult typecheck;

    explicit ProgramFixture(std::vector<Owned<Decl>> &&nominal_decls) {
        program = make_owned<Program>("where_clause_info_test.ahfl");

        // Module (required so the resolver registers a module context).
        auto module_name = make_qname({"wc_test"});
        auto module_decl = make_owned<ModuleDecl>(std::move(module_name));
        program->declarations.push_back(std::move(module_decl));

        // A dummy struct type used as a concrete CapabilityDecl return type so
        // build_capability_types() has something to resolve.
        auto return_struct = make_owned<StructDecl>("Unit");
        program->declarations.push_back(std::move(return_struct));

        for (auto &d : nominal_decls) {
            program->declarations.push_back(std::move(d));
        }

        Resolver resolver;
        resolve = resolver.resolve(*program);
        // Resolver errors (e.g. duplicate names) fail the test — these inputs
        // are always well-formed by construction.
        if (resolve.has_errors()) {
            for (const auto &entry : resolve.diagnostics.entries()) {
                MESSAGE("resolve diagnostic: " << entry.message);
            }
        }
        REQUIRE_FALSE(resolve.has_errors());

        TypeChecker checker;
        typecheck = checker.check(*program, resolve);
        // Type-checking errors (outside the scope of where-clause plumbing)
        // are also unexpected; let any surface as REQUIRE failures.
        REQUIRE_FALSE(typecheck.has_errors());
    }

    [[nodiscard]] SymbolId find_type_symbol(std::string_view name) const {
        const auto sym =
            resolve.symbol_table.find_local(SymbolNamespace::Types, name, "wc_test");
        REQUIRE(sym.has_value());
        return sym->get().id;
    }

    [[nodiscard]] SymbolId find_capability_symbol(std::string_view name) const {
        const auto sym = resolve.symbol_table.find_local(
            SymbolNamespace::Capabilities, name, "wc_test");
        REQUIRE(sym.has_value());
        return sym->get().id;
    }
};

} // namespace

// ============================================================================
// Acceptance test for CapabilityDecl (fn stand-in)
//
//   fn f<T>() where T:Eq  →  info.where_clause.syntax != nullptr
//
// Since the AHFL language does not currently have a `fn` declaration, the
// structurally equivalent nominal CapabilityDecl carries the generic where
// clause instead. This test is the primary acceptance signal.
// ============================================================================

TEST_CASE("CapabilityDecl (fn-like) where_clause propagates to CapabilityTypeInfo") {
    std::vector<Owned<Decl>> decls;

    auto cap = make_owned<CapabilityDecl>("f");
    cap->return_type = make_named_type("Unit");
    cap->where_clause = make_single_bound_where("T", "Eq");
    decls.push_back(std::move(cap));

    ProgramFixture fixture(std::move(decls));

    const auto sym = fixture.find_capability_symbol("f");
    const auto info = fixture.typecheck.environment.get_capability(sym);
    REQUIRE(info.has_value());

    // Acceptance signal: syntax pointer must be non-null (field populated).
    CHECK(info->get().where_clause.syntax != nullptr);

    // The lightweight bound list should also be populated (plumbing check).
    REQUIRE(info->get().where_clause.bounds.size() == 1);
    CHECK(info->get().where_clause.bounds[0].subject_name == "T");
    REQUIRE(info->get().where_clause.bounds[0].trait_names.size() == 1);
    CHECK(info->get().where_clause.bounds[0].trait_names[0] == "Eq");

    // Verify the same info is reachable through TypedProgram payload
    // (EnvironmentBuilder copies StructTypeInfo/EnumTypeInfo out of the
    // environment via TypedDeclPayload — CapabilityTypeInfo goes through the
    // same code path, so checking the env result suffices).
}

// ============================================================================
// StructDecl where_clause plumbing
// ============================================================================

TEST_CASE("StructDecl where_clause propagates to StructTypeInfo") {
    std::vector<Owned<Decl>> decls;

    auto s = make_owned<StructDecl>("SortedList");
    s->where_clause = make_single_bound_where("T", "Ord");
    decls.push_back(std::move(s));

    ProgramFixture fixture(std::move(decls));

    const auto sym = fixture.find_type_symbol("SortedList");
    const auto info = fixture.typecheck.environment.get_struct(sym);
    REQUIRE(info.has_value());

    CHECK(info->get().where_clause.syntax != nullptr);
    REQUIRE(info->get().where_clause.bounds.size() == 1);
    CHECK(info->get().where_clause.bounds[0].subject_name == "T");
    REQUIRE(info->get().where_clause.bounds[0].trait_names.size() == 1);
    CHECK(info->get().where_clause.bounds[0].trait_names[0] == "Ord");
}

// ============================================================================
// EnumDecl where_clause plumbing
// ============================================================================

TEST_CASE("EnumDecl where_clause propagates to EnumTypeInfo") {
    std::vector<Owned<Decl>> decls;

    // The Show trait: declared so where_clause type resolution succeeds.
    // The resolver now validates EnumDecl where_clause bounds (added for
    // recursive ADT support) so trait names must be registered symbols.
    auto show_trait = make_owned<TraitDecl>("Show");
    decls.push_back(std::move(show_trait));

    auto e = make_owned<EnumDecl>("Result");
    // Declare 'E' as a type parameter on the enum — the resolver inserts
    // type_param names into generic_type_params_ so they do not trigger
    // "unknown type" diagnostics during where_clause resolution.
    auto e_param = std::make_unique<TypeParamSyntax>();
    e_param->name = "E";
    e->type_params.push_back(std::move(e_param));
    e->where_clause = make_single_bound_where("E", "Show");
    decls.push_back(std::move(e));

    ProgramFixture fixture(std::move(decls));

    const auto sym = fixture.find_type_symbol("Result");
    const auto info = fixture.typecheck.environment.get_enum(sym);
    REQUIRE(info.has_value());

    CHECK(info->get().where_clause.syntax != nullptr);
    REQUIRE(info->get().where_clause.bounds.size() == 1);
    CHECK(info->get().where_clause.bounds[0].subject_name == "E");
    REQUIRE(info->get().where_clause.bounds[0].trait_names.size() == 1);
    CHECK(info->get().where_clause.bounds[0].trait_names[0] == "Show");
}

// ============================================================================
// Negative: declarations without where_clause keep the default-constructed
// (nullptr + empty) WhereClauseInfo.
// ============================================================================

TEST_CASE("Declarations without where_clause keep default WhereClauseInfo") {
    std::vector<Owned<Decl>> decls;

    auto s = make_owned<StructDecl>("Plain");
    decls.push_back(std::move(s));

    auto e = make_owned<EnumDecl>("Color");
    decls.push_back(std::move(e));

    auto cap = make_owned<CapabilityDecl>("noop");
    cap->return_type = make_named_type("Unit");
    decls.push_back(std::move(cap));

    ProgramFixture fixture(std::move(decls));

    {
        const auto s_info = fixture.typecheck.environment.get_struct(
            fixture.find_type_symbol("Plain"));
        REQUIRE(s_info.has_value());
        CHECK(s_info->get().where_clause.syntax == nullptr);
        CHECK(s_info->get().where_clause.bounds.empty());
    }
    {
        const auto e_info = fixture.typecheck.environment.get_enum(
            fixture.find_type_symbol("Color"));
        REQUIRE(e_info.has_value());
        CHECK(e_info->get().where_clause.syntax == nullptr);
        CHECK(e_info->get().where_clause.bounds.empty());
    }
    {
        const auto c_info = fixture.typecheck.environment.get_capability(
            fixture.find_capability_symbol("noop"));
        REQUIRE(c_info.has_value());
        CHECK(c_info->get().where_clause.syntax == nullptr);
        CHECK(c_info->get().where_clause.bounds.empty());
    }
}

// ============================================================================
// Multi-bound where clause:  where T:Eq+Hash, U:Clone
// ============================================================================

TEST_CASE("Multi-trait multi-bound where clause plumbing") {
    std::vector<Owned<Decl>> decls;

    auto s = make_owned<StructDecl>("HashMap");
    auto clause = make_owned<WhereClauseSyntax>();

    // K:Eq + Hash
    auto c1 = make_owned<WhereConstraintSyntax>();
    c1->subject = make_named_type("K");
    c1->is_predicate = false;
    c1->bounds.push_back(make_named_type("Eq"));
    c1->bounds.push_back(make_named_type("Hash"));
    clause->constraints.push_back(std::move(c1));

    // V:Clone
    auto c2 = make_owned<WhereConstraintSyntax>();
    c2->subject = make_named_type("V");
    c2->is_predicate = false;
    c2->bounds.push_back(make_named_type("Clone"));
    clause->constraints.push_back(std::move(c2));

    s->where_clause = std::move(clause);
    decls.push_back(std::move(s));

    ProgramFixture fixture(std::move(decls));

    const auto info = fixture.typecheck.environment.get_struct(
        fixture.find_type_symbol("HashMap"));
    REQUIRE(info.has_value());
    CHECK(info->get().where_clause.syntax != nullptr);
    REQUIRE(info->get().where_clause.bounds.size() == 2);

    CHECK(info->get().where_clause.bounds[0].subject_name == "K");
    REQUIRE(info->get().where_clause.bounds[0].trait_names.size() == 2);
    CHECK(info->get().where_clause.bounds[0].trait_names[0] == "Eq");
    CHECK(info->get().where_clause.bounds[0].trait_names[1] == "Hash");

    CHECK(info->get().where_clause.bounds[1].subject_name == "V");
    REQUIRE(info->get().where_clause.bounds[1].trait_names.size() == 1);
    CHECK(info->get().where_clause.bounds[1].trait_names[0] == "Clone");
}
