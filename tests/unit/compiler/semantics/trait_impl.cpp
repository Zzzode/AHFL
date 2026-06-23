#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// P3c trait / impl typecheck tests (RFC corelib-rfc.zh.md §3.2.2 + §6 P3;
// corelib-type-system.zh.md §1.3 trait EBNF, §1.4 impl EBNF, §2 trait
// resolution + coherence + orphan rule).
//
// P3a-b (grammar / AST TraitDecl + ImplDecl, trait/impl signature checking)
// and the first P3c dispatch slices are landed. These tests exercise the
// parse -> resolve -> typecheck pipeline plus, where the RFC requires
// module-level reasoning, a hand-built multi-module SourceGraph (the same
// construction the effects.cpp session-equivalence test uses). They assert:
//
//   - a `trait` declaration + a matching `impl Trait for Type` block
//     typecheck clean (signature match passes, no orphan, no duplicate);
//   - a trait with multiple methods requires the impl to provide all of them;
//   - a missing trait method impl reports TRAIT_METHOD_NOT_FOUND;
//   - a trait impl written in a module that defines neither the type nor the
//     trait reports ORPHAN_IMPL (the strict orphan rule, RFC §2.2);
//   - an inherent impl (no `TraitRef for`) is accepted without coherence
//     enforcement;
//   - a program with no trait/impl constructs at all still typechecks clean
//     (P3 backward-compat guard, RFC §6 "P3 纯新增, 不破 956").
//
// Grammar note (AHFL.g4): the `param` rule is `IDENT ':' type_` — the type
// annotation is mandatory, so every method parameter (including the `self`
// receiver) is written `self: Type`. The `type_` production does not yet
// accept `Name<args>` at the impl-header position, so a generic trait's
// *application* (`impl Trait<T> for Type`) is out of scope for P3a-b.
//
// Method dispatch is intentionally metadata-based for now: the resolver does
// not register impl method names as standalone Function symbols, so `e.method`
// resolves during expression typecheck against the ImplTypeInfo table.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string module_preamble() {
    return std::string{R"AHFL(
module trait_impl;
)AHFL"};
}

// Run the full single-program pipeline and return the typecheck result. On
// unexpected parse or resolve failures the diagnostic is surfaced via MESSAGE
// so a failing test reports an actionable cause rather than a silent boolean.
[[nodiscard]] ahfl::TypeCheckResult typecheck_source(std::string_view filename,
                                                     const std::string &source) {
    const ahfl::Frontend frontend;
    const auto parse_result = frontend.parse_text(std::string(filename), source);
    if (parse_result.has_errors()) {
        MESSAGE("parse errors detected");
        for (const auto &entry : parse_result.diagnostics.entries()) {
            MESSAGE("  parse: " << entry.message);
        }
    }
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        for (const auto &entry : resolve_result.diagnostics.entries()) {
            MESSAGE("  resolve: " << entry.message);
        }
    }
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    auto result = type_checker.check(*parse_result.program, resolve_result);
    if (result.has_errors() && !result.diagnostics.entries().empty()) {
        const auto &d = result.diagnostics.entries().front();
        MESSAGE("typecheck diagnostic: " << d.message);
    }
    return result;
}

[[nodiscard]] bool has_diagnostic_code(const ahfl::TypeCheckResult &result,
                                       std::string_view code_substring) {
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.has_value() && entry.code->find(code_substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Multi-module SourceGraph harness.
//
// The strict orphan rule (RFC §2.2) compares the impl's module against the
// trait's and the type's defining modules. The typechecker's
// `module_name_of(source_id)` resolves the impl's module only when a
// SourceGraph is supplied (single-program mode has no per-source module, so
// the orphan check is a no-op there). We build a SourceGraph by hand, mirroring
// the construction in tests/unit/compiler/semantics/effects.cpp's
// session-equivalence test, giving each SourceUnit a distinct `module_name`.
//
// Each unit's text opens with `module <name>;` so the resolver populates
// `Symbol::module_name` consistently with `SourceUnit::module_name` (a mismatch
// would be flagged by the resolver's module-boundary guard). This keeps the
// orphan comparison deterministic.
//
// Cross-module references use qualified path names (`module::Name`): the
// resolver's `canonical_names` index maps `<module>::<local>` to its symbol,
// so a foreign type/trait is reached by its fully qualified spelling without
// an `import` declaration.
// ---------------------------------------------------------------------------

struct ModuleSource {
    std::string display_name;
    std::string module_name;
    std::string text;
};

[[nodiscard]] ahfl::TypeCheckResult typecheck_modules(const std::vector<ModuleSource> &units,
                                                      bool expect_resolve_clean) {
    const ahfl::Frontend frontend;
    ahfl::SourceGraph graph;
    graph.sources.reserve(units.size());

    for (std::size_t i = 0; i < units.size(); ++i) {
        auto parse = frontend.parse_text(units[i].display_name, units[i].text);
        if (parse.has_errors()) {
            MESSAGE("parse errors in " << units[i].display_name);
            for (const auto &entry : parse.diagnostics.entries()) {
                MESSAGE("  parse: " << entry.message);
            }
        }
        REQUIRE_FALSE(parse.has_errors());
        REQUIRE(parse.program != nullptr);

        ahfl::SourceUnit unit{
            .id = ahfl::SourceId{i + 1},
            .path = units[i].display_name,
            .module_name = units[i].module_name,
            .module_range = ahfl::SourceRange{},
            .source = std::move(parse.source),
            .program = std::move(parse.program),
            .imports = {},
        };
        graph.module_to_source.emplace(units[i].module_name, ahfl::SourceId{i + 1});
        graph.sources.push_back(std::move(unit));
    }
    if (!units.empty()) {
        graph.entry_sources.push_back(graph.sources.front().id);
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(graph);
    if (expect_resolve_clean) {
        if (resolve_result.has_errors()) {
            for (const auto &entry : resolve_result.diagnostics.entries()) {
                MESSAGE("  resolve: " << entry.message);
            }
        }
        REQUIRE_FALSE(resolve_result.has_errors());
    }

    const ahfl::TypeChecker type_checker;
    auto result = type_checker.check(graph, resolve_result);
    if (result.has_errors() && !result.diagnostics.entries().empty()) {
        const auto &d = result.diagnostics.entries().front();
        MESSAGE("typecheck diagnostic: " << d.message);
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// TC1: RFC §6 P3 acceptance baseline — a `trait` declaration with a method
// signature plus a matching `impl Trait for Type` block typechecks cleanly.
// This is the surface the trait-method-call dispatch path hooks into: the
// trait's method set is recorded and the impl's signatures match it.
// ---------------------------------------------------------------------------
TEST_CASE("trait declaration and matching impl block typecheck cleanly") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

trait Show {
    fn show(self: Counter) -> Int;
}

impl Show for Counter {
    fn show(self: Counter) -> Int {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("trait_show.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC2: A trait may declare multiple methods; the impl must provide all of
// them with matching signatures.
// ---------------------------------------------------------------------------
TEST_CASE("trait with multiple methods typechecks when impl provides all") {
    const std::string source = module_preamble() + R"AHFL(
struct Pair {
    a: Int;
    b: Int;
}

trait PairOps {
    fn first(self: Pair) -> Int;
    fn second(self: Pair) -> Int;
}

impl PairOps for Pair {
    fn first(self: Pair) -> Int {
        return self.a;
    }
    fn second(self: Pair) -> Int {
        return self.b;
    }
}
)AHFL";

    const auto result = typecheck_source("trait_multi_method.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC3: trait method resolution surface — when a trait method is declared with
// a `self: Type` receiver and the impl provides a body, the program parses,
// resolves and typechecks clean. This is the "trait Show { fn show(self) -> T
// }; impl Show for Type { fn show(self) -> T { ... } }" acceptance shape from
// RFC §6 P3. Direct `x.show()` dispatch is covered by the later P3c tests
// below.
// ---------------------------------------------------------------------------
TEST_CASE("trait method with self receiver typechecks through impl") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

trait Show {
    fn show(self: Counter) -> Int;
}

impl Show for Counter {
    fn show(self: Counter) -> Int {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("trait_self_receiver.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC4: A trait with type parameters (RFC §1.3 `TraitDecl ::= "trait" Ident
// TypeParams? ...`) parses, resolves and typechecks at the declaration level.
// The grammar's `type_` production does not yet accept `Name<args>` at the
// impl-header position, so the generic-trait *application* surface
// (`impl Trait<T> for Type`) is out of scope for P3a-b; this test exercises the
// declaration side — a trait declaring type params — alongside a non-generic
// impl so the type-param scoping set the resolver builds for fn/generics (P2)
// extends cleanly to trait declarations without breaking the existing surface.
// ---------------------------------------------------------------------------
TEST_CASE("generic trait declaration typechecks alongside non-generic impl") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

trait Container<T> {
    fn count(self: Counter) -> Int;
}

impl Container for Counter {
    fn count(self: Counter) -> Int {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("trait_generic.ahfl", source);
    // The generic trait declaration parses and resolves; the non-generic impl
    // names the trait unqualified. P3b resolves the trait symbol and matches
    // the impl's `count` signature against the trait's.
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC5: A trait impl that omits a method declared by the trait reports
// TRAIT_METHOD_NOT_FOUND (RFC §2.1 signature coverage).
// ---------------------------------------------------------------------------
TEST_CASE("missing trait method impl reports TRAIT_METHOD_NOT_FOUND") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

trait PairOps {
    fn first(self: Counter) -> Int;
    fn second(self: Counter) -> Int;
}

impl PairOps for Counter {
    fn first(self: Counter) -> Int {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("trait_missing_method.ahfl", source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "TRAIT_METHOD_NOT_FOUND"));
}

// ---------------------------------------------------------------------------
// TC6: An impl may provide a method not declared by the trait; this is
// reported as the trait-method-set mismatch diagnostic (RFC §2.1: every impl
// method must name a real trait method).
// ---------------------------------------------------------------------------
TEST_CASE("impl method not in trait reports trait method-set mismatch") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

trait Show {
    fn show(self: Counter) -> Int;
}

impl Show for Counter {
    fn show(self: Counter) -> Int {
        return self.value;
    }
    fn extra(self: Counter) -> Int {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("trait_extra_method.ahfl", source);
    CHECK(result.has_errors());
    // The impl provides `extra` which is not a trait method.
    CHECK(has_diagnostic_code(result, "TRAIT_METHOD_NOT_FOUND"));
}

// ---------------------------------------------------------------------------
// TC7: An inherent impl (no `TraitRef for`) is accepted without coherence /
// signature enforcement. RFC §1.4 distinguishes inherent vs trait impl purely
// by the presence of `TraitRef "for"`.
// ---------------------------------------------------------------------------
TEST_CASE("inherent impl block typechecks without coherence enforcement") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

impl Counter {
    fn increment(self: Counter) -> Int {
        return self.value + 1;
    }
}
)AHFL";

    const auto result = typecheck_source("inherent_impl.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC8: Orphan rule (RFC §2.2 strict variant). An `impl Trait for Type`
// written in a module that defines *neither* the type nor the trait reports
// ORPHAN_IMPL. Requires a multi-module SourceGraph so the typechecker can
// resolve per-source modules.
//
//   - module `shapes` defines struct `Circle`;
//   - module `fmtlib` defines trait `Describe`;
//   - module `handlers` writes `impl fmtlib::Describe for shapes::Circle` — an
//     orphan: it defines neither `Circle` nor `Describe`.
// ---------------------------------------------------------------------------
TEST_CASE("orphan impl reports ORPHAN_IMPL across modules") {
    const std::vector<ModuleSource> units = {
        ModuleSource{
            .display_name = "shapes.ahfl",
            .module_name = "shapes",
            .text = R"AHFL(
module shapes;

struct Circle {
    radius: Int;
}
)AHFL",
        },
        ModuleSource{
            .display_name = "fmtlib.ahfl",
            .module_name = "fmtlib",
            .text = R"AHFL(
module fmtlib;

trait Describe {
    fn describe(self: shapes::Circle) -> Int;
}
)AHFL",
        },
        ModuleSource{
            // The orphan: defines neither Circle nor Describe, yet writes the
            // impl. Both symbols are reached via qualified paths so resolution
            // succeeds and the coherence check fires. RFC §2.2 rejects this
            // with E::orphan_impl.
            .display_name = "handlers.ahfl",
            .module_name = "handlers",
            .text = R"AHFL(
module handlers;

impl fmtlib::Describe for shapes::Circle {
    fn describe(self: shapes::Circle) -> Int {
        return self.radius;
    }
}
)AHFL",
        },
    };

    const auto result = typecheck_modules(units, /*expect_resolve_clean=*/true);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "ORPHAN_IMPL"));
}

// ---------------------------------------------------------------------------
// TC9: Orphan rule passes when the impl lives in the module that defines the
// trait. Mirror of TC8 but the impl is co-located with `Describe` in
// `fmtlib` — coherence is satisfied (no ORPHAN_IMPL). The trait is referenced
// unqualified (same module); the foreign type is qualified.
// ---------------------------------------------------------------------------
TEST_CASE("impl in trait-defining module satisfies orphan rule") {
    const std::vector<ModuleSource> units = {
        ModuleSource{
            .display_name = "shapes.ahfl",
            .module_name = "shapes",
            .text = R"AHFL(
module shapes;

struct Circle {
    radius: Int;
}
)AHFL",
        },
        ModuleSource{
            // Co-located with the trait: coherence passes.
            .display_name = "fmtlib.ahfl",
            .module_name = "fmtlib",
            .text = R"AHFL(
module fmtlib;

trait Describe {
    fn describe(self: shapes::Circle) -> Int;
}

impl Describe for shapes::Circle {
    fn describe(self: shapes::Circle) -> Int {
        return self.radius;
    }
}
)AHFL",
        },
    };

    const auto result = typecheck_modules(units, /*expect_resolve_clean=*/true);
    CHECK_FALSE(has_diagnostic_code(result, "ORPHAN_IMPL"));
}

// ---------------------------------------------------------------------------
// TC10: Orphan rule passes when the impl lives in the module that defines the
// target type (the other allowed location per RFC §2.2).
// ---------------------------------------------------------------------------
TEST_CASE("impl in type-defining module satisfies orphan rule") {
    const std::vector<ModuleSource> units = {
        ModuleSource{
            // Co-located with the type: coherence passes.
            .display_name = "shapes.ahfl",
            .module_name = "shapes",
            .text = R"AHFL(
module shapes;

struct Circle {
    radius: Int;
}

impl fmtlib::Describe for Circle {
    fn describe(self: Circle) -> Int {
        return self.radius;
    }
}
)AHFL",
        },
        ModuleSource{
            .display_name = "fmtlib.ahfl",
            .module_name = "fmtlib",
            .text = R"AHFL(
module fmtlib;

trait Describe {
    fn describe(self: shapes::Circle) -> Int;
}
)AHFL",
        },
    };

    const auto result = typecheck_modules(units, /*expect_resolve_clean=*/true);
    CHECK_FALSE(has_diagnostic_code(result, "ORPHAN_IMPL"));
}

// ---------------------------------------------------------------------------
// TC11 (P3c): Inherent method dispatch. A value receiver call `c.add(1)` should
// resolve to the unique inherent impl method for the receiver's nominal type,
// check the receiver against the method's explicit `self` parameter, and return
// the method's declared return type.
// ---------------------------------------------------------------------------
TEST_CASE("inherent method call dispatches through impl signature") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

impl Counter {
    fn add(self: Counter, delta: Int) -> Int effect Pure decreases 0 {
        return self.value + delta;
    }
}

fn caller(c: Counter) -> Int effect Pure decreases 0 {
    return c.add(1);
}
)AHFL";

    const auto result = typecheck_source("inherent_method_dispatch.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

// ---------------------------------------------------------------------------
// TC12 (P3c): Trait method dispatch. With no inherent method present,
// `c.show()` should resolve to the unique trait impl method for Counter.
// ---------------------------------------------------------------------------
TEST_CASE("trait method call dispatches through impl signature") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

trait Show {
    fn show(self: Counter) -> Int;
}

impl Show for Counter {
    fn show(self: Counter) -> Int effect Pure decreases 0 {
        return self.value;
    }
}

fn caller(c: Counter) -> Int effect Pure decreases 0 {
    return c.show();
}
)AHFL";

    const auto result = typecheck_source("trait_method_dispatch.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
}

TEST_CASE("impl method body with wrong return type produces error") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

impl Counter {
    fn bad(self: Counter) -> String effect Pure decreases 0 {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("impl_method_bad_return.ahfl", source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "TYPE_MISMATCH"));
}

TEST_CASE("impl method body effect underdeclared is diagnosed") {
    const std::string source = module_preamble() + R"AHFL(
fn nondet_source() -> Int effect Nondet {
    return 1;
}

struct Counter {
    value: Int;
}

impl Counter {
    fn bad_effect(self: Counter) -> Int effect Pure decreases 0 {
        return nondet_source();
    }
}
)AHFL";

    const auto result = typecheck_source("impl_method_underdeclared.ahfl", source);
    CHECK(result.has_errors());
    CHECK(has_diagnostic_code(result, "EFFECT_UNDERDECLARED"));
}

TEST_CASE("impl method body block index is recorded") {
    const std::string source = module_preamble() + R"AHFL(
struct Counter {
    value: Int;
}

impl Counter {
    fn value(self: Counter) -> Int effect Pure decreases 0 {
        return self.value;
    }
}
)AHFL";

    const auto result = typecheck_source("impl_method_body_index.ahfl", source);
    REQUIRE_FALSE(result.has_errors());
    const auto &impls = result.environment.impls();
    REQUIRE(impls.size() == 1);
    const auto &impl = impls.begin()->second;
    REQUIRE(impl.methods.size() == 1);
    CHECK(impl.methods.front().body_block_index != UINT32_MAX);
    CHECK(impl.methods.front().body_block_index < result.typed_program.blocks.size());
}

// ---------------------------------------------------------------------------
// TC13: Backward compatibility — a program with no P3 trait/impl constructs at
// all still typechecks clean. Guards the RFC §6 "P3 纯新增, 不破 956"
// constraint.
// ---------------------------------------------------------------------------
TEST_CASE("legacy program without trait/impl still typechecks") {
    const std::string source = module_preamble() + R"AHFL(
struct Request {
    value: String;
}

struct Context {
    value: String = "seed";
}

struct Response {
    value: String;
}

agent LegacyAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for LegacyAgent {
    state Done {
        return Response { value: input.value };
    }
}
)AHFL";

    const auto result = typecheck_source("trait_backward_compat.ahfl", source);
    CHECK_FALSE(result.has_errors());
}
