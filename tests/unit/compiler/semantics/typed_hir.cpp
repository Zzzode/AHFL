// ============================================================================
// Typed HIR + TypeContext unit tests.
//
// Covers two plan items:
//   * T1.2  Typed HIR output: every ExprSyntaxKind produces a TypedExpr entry
//          with the expected type/effect/purity/semantic metadata.
//   * T5.4  TypeContext lifecycle / interning:
//          - pointer identity within one context for equal types
//          - structural equality across independent contexts
//          - multi-threaded intern() of same-shape types is stable
//          - per-thread TypeContext + TypeChecker produces no cross-talk
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

struct TypedHIRFixture {
    ahfl::Frontend frontend;

    [[nodiscard]] ahfl::TypeCheckResult check(const std::string &source) const {
        const auto parse = frontend.parse_text("typed_hir_test.ahfl", source);
        if (parse.has_errors()) {
            std::ostringstream ss;
            parse.diagnostics.render(ss);
            std::fprintf(stderr, "=== PARSE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(parse.has_errors());
        REQUIRE(parse.program != nullptr);

        ahfl::Resolver resolver;
        const auto resolve = resolver.resolve(*parse.program);
        if (resolve.has_errors()) {
            std::ostringstream ss;
            resolve.diagnostics.render(ss);
            std::fprintf(stderr, "=== RESOLVE DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(resolve.has_errors());

        ahfl::TypeChecker checker;
        auto result = checker.check(*parse.program, resolve);
        if (result.has_errors()) {
            std::ostringstream ss;
            result.diagnostics.render(ss);
            std::fprintf(stderr, "=== TYPECHECK DIAGNOSTICS ===\n%s\n=== END ===\n", ss.str().c_str());
        }
        REQUIRE_FALSE(result.has_errors());
        return result;
    }
};

// Small prefix providing Request/Response/Capability shared by many cases.
const char *kSharedPrefix = R"AHFL(
struct Request { value: String; token: Optional<String> = none; }
struct Context { value: String = ""; }
struct Response { value: String; code: Int = 200; }

capability Echo(payload: String) -> Response;
predicate Ok(x: String) -> Bool;

const kMagic: Int = 42;
const kGreeting: String = "hello";
)AHFL";

ahfl::SourceRange range_of(const std::string &haystack, const std::string &needle) {
    const auto pos = haystack.find(needle);
    REQUIRE(pos != std::string::npos);
    return ahfl::SourceRange{
        .begin_offset = pos,
        .end_offset = pos + needle.size(),
    };
}

bool has_kind(const std::vector<ahfl::TypedExpr> &exprs, ahfl::ast::ExprSyntaxKind kind) {
    return std::any_of(
        exprs.begin(), exprs.end(), [kind](const ahfl::TypedExpr &e) { return e.kind == kind; });
}

const ahfl::TypedExpr *find_by_range(const std::vector<ahfl::TypedExpr> &exprs,
                                     ahfl::SourceRange range) {
    for (const auto &e : exprs) {
        if (e.range.begin_offset == range.begin_offset && e.range.end_offset == range.end_offset) {
            return &e;
        }
    }
    return nullptr;
}

} // namespace

// ============================================================================
// T1.2: Typed HIR per-ExprSyntaxKind coverage.
// ============================================================================

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 typed HIR covers all literal kinds") {
    const std::string source = std::string(kSharedPrefix) + R"AHFL(
agent LiteralAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}

contract for LiteralAgent {
    requires: Ok(input.value);
}

flow for LiteralAgent {
    state Init {
        // Bool / Int / Float / Decimal / String / None / Some literal.
        let t_bools: List<Bool> = [true, false];
        let t_ints: List<Int> = [1, 2, 3];
        let t_floats: List<Float> = [1.5, 2.5];
        let t_none: Optional<String> = none;
        let t_some: Optional<String> = some("world");
        // Collections.
        let t_list: List<Int> = [1, 2, 3];
        let t_set: Set<String> = set ["a", "b"];
        let t_map: Map<String, Int> = map ["x": 1, "y": 2];
        // Struct literal.
        let t_lit: Response = Response { value: "ok", code: 200 };
        // Path, call, member.
        let t_path = input.value;
        let t_call = Echo(input.value);
        // MemberAccess: postfix index-then-member triggers postfixExpr branch.
        let t_struct_list: List<Response> = [t_lit];
        let t_member = t_struct_list[0].value;
        // Unary / binary / group.
        let t_unary = -1;
        let t_binary = 1 + 2;
        let t_group = (1 + 2);
        // Index access on list.
        let t_index = t_ints[0];
        goto Done;
    }
    state Done {
        return Response { value: "done", code: 200 };
    }
}
)AHFL";

    const auto r = check(source);
    const auto &exprs = r.typed_program.expressions;
    REQUIRE_FALSE(exprs.empty());

    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::BoolLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::IntegerLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::FloatLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::StringLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::NoneLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Some));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::ListLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::SetLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::MapLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::StructLiteral));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Path));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Call));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Unary));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Binary));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::MemberAccess));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::Group));
    CHECK(has_kind(exprs, ahfl::ast::ExprSyntaxKind::IndexAccess));

    // Decimal + Duration are parsed differently.
    const std::string source2 = std::string(kSharedPrefix) + R"AHFL(
agent DecimalAgent {
    input: Request;
    context: Context;
    output: Response;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Echo];
    transition Init -> Done;
}
flow for DecimalAgent {
    state Init { goto Done; }
    state Done {
        let t_d = 3.14d;
        let t_dur = 15s;
        return Response { value: "done", code: 200 };
    }
}
)AHFL";
    const auto r2 = check(source2);
    CHECK(has_kind(r2.typed_program.expressions, ahfl::ast::ExprSyntaxKind::DecimalLiteral));
    CHECK(has_kind(r2.typed_program.expressions, ahfl::ast::ExprSyntaxKind::DurationLiteral));
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 typed HIR carries type/effect/purity for literals") {
    const std::string source = R"AHFL(
struct R { v: String; }
struct Ctx { v: String = ""; }
capability C(x: String) -> R;

agent A {
    input: R;
    context: Ctx;
    output: R;
    states: [S, Done];
    initial: S;
    final: [Done];
    capabilities: [C];
    transition S -> Done;
}
flow for A {
    state S { goto Done; }
    state Done {
        let t_lit = "hello";
        let t_num = 42;
        let t_call = C(input.v);
        return R { v: "bye" };
    }
}
)AHFL";
    const auto r = check(source);
    const auto &exprs = r.typed_program.expressions;

    // String literal.
    {
        const auto *e = find_by_range(exprs, range_of(source, "\"hello\""));
        REQUIRE(e != nullptr);
        REQUIRE(e->type != nullptr);
        CHECK(e->type->holds<ahfl::types::StringT>());
        CHECK(e->is_pure);
        CHECK(e->effect == ahfl::ExprEffect::Pure);
    }
    // Integer literal.
    {
        const auto *e = find_by_range(exprs, range_of(source, "42"));
        REQUIRE(e != nullptr);
        REQUIRE(e->type != nullptr);
        CHECK(e->type->holds<ahfl::types::IntT>());
        CHECK(e->is_pure);
    }
    // Capability call: NOT pure.
    {
        const auto *e = find_by_range(exprs, range_of(source, "C(input.v)"));
        REQUIRE(e != nullptr);
        CHECK_FALSE(e->is_pure);
        CHECK(e->effect == ahfl::ExprEffect::CapabilityCall);
        CHECK(e->call_target_kind == ahfl::TypedCallTargetKind::Capability);
    }
    // Struct literal: resolved_symbol points to struct decl.
    {
        const auto *e = find_by_range(exprs, range_of(source, "R { v: \"bye\" }"));
        REQUIRE(e != nullptr);
        REQUIRE(e->type != nullptr);
        CHECK(e->kind == ahfl::ast::ExprSyntaxKind::StructLiteral);
        REQUIRE(e->resolved_symbol.has_value());
        const auto struct_info = r.environment.find_struct("R");
        REQUIRE(struct_info.has_value());
        CHECK(*e->resolved_symbol == struct_info->get().symbol);
    }
    // Struct literal's children: one StructFieldValue named "v".
    {
        const auto *e = find_by_range(exprs, range_of(source, "R { v: \"bye\" }"));
        REQUIRE(e != nullptr);
        REQUIRE_FALSE(e->children.empty());
        bool found_field = false;
        for (const auto &c : e->children) {
            if (c.role == ahfl::TypedExprChildRole::StructFieldValue && c.name == "v") {
                found_field = true;
                break;
            }
        }
        CHECK(found_field);
    }
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 TypedDecl records all declaration kinds") {
    const std::string source = R"AHFL(
struct S { x: Int = 0; }
enum E { A, B }
type T = String;
const c: Int = 1;
capability Cap(in: String) -> S;
predicate Pred(in: Int) -> Bool;

agent X {
    input: S; context: S; output: S;
    states: [A, Done]; initial: A; final: [Done];
    capabilities: [Cap];
    transition A -> Done;
}
contract for X { requires: true; }
flow for X {
    state A { goto Done; }
    state Done { return S { x: input.x }; }
}
workflow W {
    input: S; output: S;
    node run_agent: X(input);
    return: run_agent;
}
)AHFL";
    const auto r = check(source);
    const auto &decls = r.typed_program.declarations;
    REQUIRE_FALSE(decls.empty());

    using K = ahfl::ast::NodeKind;
    std::unordered_set<K> seen;
    for (const auto &d : decls) seen.insert(d.kind);

    // Nominal / independent declarations always recorded.
    CHECK(seen.contains(K::StructDecl));
    CHECK(seen.contains(K::EnumDecl));
    CHECK(seen.contains(K::TypeAliasDecl));
    CHECK(seen.contains(K::ConstDecl));
    CHECK(seen.contains(K::CapabilityDecl));
    CHECK(seen.contains(K::PredicateDecl));
    CHECK(seen.contains(K::AgentDecl));
    CHECK(seen.contains(K::WorkflowDecl));

    // T1.5: ContractDecl and FlowDecl are now recorded as standalone
    // TypedDecl entries with associated_agent_symbol pointing to the
    // owning agent.
    CHECK(seen.contains(K::ContractDecl));
    CHECK(seen.contains(K::FlowDecl));

    // Locate the agent and contract/flow records by kind and verify the
    // associated agent symbol wiring.
    const auto agent_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::AgentDecl; });
    REQUIRE(agent_it != decls.end());
    const auto agent_symbol = agent_it->symbol;

    const auto contract_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::ContractDecl; });
    REQUIRE(contract_it != decls.end());
    REQUIRE(contract_it->associated_agent_symbol.has_value());
    CHECK(*contract_it->associated_agent_symbol == agent_symbol);

    const auto flow_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::FlowDecl; });
    REQUIRE(flow_it != decls.end());
    REQUIRE(flow_it->associated_agent_symbol.has_value());
    CHECK(*flow_it->associated_agent_symbol == agent_symbol);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "T1.5 TypedProgram records ContractDecl with agent association") {
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Resp { v: String; }
struct Ctx { v: String = ""; }
capability Nop(x: String) -> Resp;
predicate Safe(s: String) -> Bool;

agent Guard {
    input: Req; context: Ctx; output: Resp;
    states: [Init, Done]; initial: Init; final: [Done];
    capabilities: [Nop];
    transition Init -> Done;
}
contract for Guard {
    requires: Safe(input.v);
    ensures:  true;
}
flow for Guard {
    state Init { goto Done; }
    state Done { return Resp { v: input.v }; }
}
)AHFL";
    const auto r = check(source);
    const auto &decls = r.typed_program.declarations;

    using K = ahfl::ast::NodeKind;

    // Locate the Guard agent SymbolId.
    const auto agent_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::AgentDecl; });
    REQUIRE(agent_it != decls.end());
    const auto agent_symbol = agent_it->symbol;

    // Find ContractDecl entries. There must be exactly one for `Guard`.
    std::size_t contract_count = 0;
    for (const auto &d : decls) {
        if (d.kind != K::ContractDecl) continue;
        ++contract_count;
        REQUIRE(d.associated_agent_symbol.has_value());
        CHECK(*d.associated_agent_symbol == agent_symbol);
        // Range should be non-empty.
        CHECK(d.range.end_offset > d.range.begin_offset);
    }
    CHECK(contract_count == 1);
}

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "T1.5 TypedProgram records FlowDecl with agent association") {
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Resp { v: String; }
struct Ctx { v: String = ""; }
capability Work(x: String) -> Resp;

agent Runner {
    input: Req; context: Ctx; output: Resp;
    states: [Init, Run, Done]; initial: Init; final: [Done];
    capabilities: [Work];
    transition Init -> Run;
    transition Run -> Done;
}
contract for Runner { requires: true; }
flow for Runner {
    state Init { goto Run; }
    state Run  { let r = Work(input.v); goto Done; }
    state Done { return Resp { v: input.v }; }
}
)AHFL";
    const auto r = check(source);
    const auto &decls = r.typed_program.declarations;

    using K = ahfl::ast::NodeKind;

    // Locate the Runner agent SymbolId.
    const auto agent_it = std::find_if(
        decls.begin(), decls.end(), [](const auto &d) { return d.kind == K::AgentDecl; });
    REQUIRE(agent_it != decls.end());
    const auto agent_symbol = agent_it->symbol;

    // Find FlowDecl entries.
    std::size_t flow_count = 0;
    for (const auto &d : decls) {
        if (d.kind != K::FlowDecl) continue;
        ++flow_count;
        REQUIRE(d.associated_agent_symbol.has_value());
        CHECK(*d.associated_agent_symbol == agent_symbol);
        CHECK(d.range.end_offset > d.range.begin_offset);
    }
    CHECK(flow_count == 1);
}

TEST_CASE_FIXTURE(TypedHIRFixture, "T1.2 lowering produces equivalent IR via typed HIR entry") {
    const std::string source = R"AHFL(
struct R { v: String; }
struct Ctx { v: String = ""; }
capability C(x: String) -> R;

agent A {
    input: R; context: Ctx; output: R;
    states: [Init, Done]; initial: Init; final: [Done];
    capabilities: [C];
    transition Init -> Done;
}
flow for A {
    state Init { let r = C(input.v); goto Done; }
    state Done { return R { v: input.v }; }
}
)AHFL";
    const auto parse = frontend.parse_text("typed_hir_lower_equiv.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve.has_errors());
    ahfl::TypeChecker checker;
    const auto tc = checker.check(*parse.program, resolve);
    REQUIRE_FALSE(tc.has_errors());

    const auto via_ast = ahfl::lower_program_ir(*parse.program, resolve, tc);
    const auto via_typed = ahfl::lower_typed_program(tc.typed_program);

    // Declaration count must match.
    CHECK(via_ast.declarations.size() == via_typed.declarations.size());
    REQUIRE_FALSE(via_ast.declarations.empty());

    // Full structural equivalence via canonical JSON rendering.
    std::ostringstream ast_json, typed_json;
    ahfl::print_program_ir_json(via_ast, ast_json);
    ahfl::print_program_ir_json(via_typed, typed_json);
    const std::string ast_fp = ast_json.str();
    const std::string typed_fp = typed_json.str();

    REQUIRE(ast_fp.size() > 0);
    REQUIRE(typed_fp.size() > 0);
    CHECK(ast_fp == typed_fp);
}

// ============================================================================
// T5.4: TypeContext interning + isolation + concurrency.
// ============================================================================

TEST_CASE("T5.4 same-shape types get identical pointers in one context") {
    auto &tc = ahfl::TypeContext::global();

    CHECK(tc.make(ahfl::TypeKind::Int) == tc.make(ahfl::TypeKind::Int));
    CHECK(tc.string() == tc.string());
    CHECK(tc.decimal(2) == tc.decimal(2));
    CHECK(tc.bounded_string(0, 8) == tc.bounded_string(0, 8));

    CHECK(tc.struct_type("pkg::Foo", ahfl::SymbolId{7}) ==
          tc.struct_type("pkg::Foo", ahfl::SymbolId{7}));
    CHECK(tc.enum_type("pkg::Bar", ahfl::SymbolId{9}) ==
          tc.enum_type("pkg::Bar", ahfl::SymbolId{9}));

    const auto s = tc.string();
    CHECK(tc.optional(s) == tc.optional(s));
    CHECK(tc.list(s) == tc.list(s));
    CHECK(tc.set(s) == tc.set(s));
    CHECK(tc.map(s, tc.make(ahfl::TypeKind::Int)) ==
          tc.map(s, tc.make(ahfl::TypeKind::Int)));
}

TEST_CASE("T5.4 two independent contexts: different pointers, structural eq") {
    // Two separate TypeContext objects: same-shape types must compare equal
    // structurally but have distinct pointer identities.
    ahfl::TypeContext left;
    ahfl::TypeContext right;
    ahfl::TypeRelationContext ctx;

    const auto *ls = left.string();
    const auto *rs = right.string();
    REQUIRE(ls != nullptr);
    REQUIRE(rs != nullptr);
    CHECK(static_cast<const void *>(ls) != static_cast<const void *>(rs));
    CHECK(ctx.equivalent(*ls, *rs));

    const auto *lo = left.optional(left.make(ahfl::TypeKind::Int));
    const auto *ro = right.optional(right.make(ahfl::TypeKind::Int));
    CHECK(static_cast<const void *>(lo) != static_cast<const void *>(ro));
    CHECK(ctx.equivalent(*lo, *ro));

    // Same name + symbol value across contexts: equivalent.
    const auto *lst = left.struct_type("a::B", ahfl::SymbolId{3});
    const auto *rst = right.struct_type("a::B", ahfl::SymbolId{3});
    CHECK(ctx.equivalent(*lst, *rst));

    // Different names (no nominal symbols): NOT equivalent by structural name.
    const auto *l2 = left.struct_type("a::X");
    const auto *r2 = right.struct_type("a::Y");
    CHECK_FALSE(ctx.equivalent(*l2, *r2));

    // When both have a nominal SymbolId with the same numeric value AND
    // matching canonical name, equivalence holds (symbol-id dominates).
    // Cross-context numeric SymbolIds are not generally comparable, so
    // the practical rule is: same-name + same-symbol-value -> equivalent.
}

TEST_CASE("T5.4 multi-thread intern of same-shape is stable and crash-free") {
    constexpr int kThreads = 8;
    constexpr int kIters = 200;
    std::atomic<int> errors{0};

    auto worker = [&](int /*tid*/) {
        for (int i = 0; i < kIters; ++i) {
            auto &tc = ahfl::TypeContext::global();
            const auto *a = tc.string();
            const auto *b = tc.string();
            if (a != b || a == nullptr) { errors.fetch_add(1); return; }

            const auto *c = tc.map(tc.list(tc.optional(tc.make(ahfl::TypeKind::Int))),
                                   tc.decimal(4));
            const auto *d = tc.map(tc.list(tc.optional(tc.make(ahfl::TypeKind::Int))),
                                   tc.decimal(4));
            if (c != d || c == nullptr) { errors.fetch_add(1); return; }
        }
    };

    std::vector<std::future<void>> futures;
    futures.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        futures.push_back(std::async(std::launch::async, worker, t));
    }
    for (auto &f : futures) f.get();

    CHECK(errors.load() == 0);
}

TEST_CASE("T5.4 per-thread independent TypeContext + TypeChecker has no cross-talk") {
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Resp { v: String; }
struct Ctx { v: String = ""; }
capability Echo(x: String) -> Resp;
agent A {
    input: Req; context: Ctx; output: Resp;
    states: [Done]; initial: Done; final: [Done];
    capabilities: [Echo];
}
flow for A {
    state Done { return Resp { v: input.v }; }
}
)AHFL";

    constexpr int kThreads = 6;
    ahfl::Frontend frontend;

    const auto parse = frontend.parse_text("per_thread.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    ahfl::Resolver resolver;
    const auto resolve_shared = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve_shared.has_errors());

    auto worker = [&](int tid) -> std::string {
        ahfl::TypeContext local_types;
        ahfl::TypeChecker checker;
        auto tc = checker.check(*parse.program, resolve_shared, local_types);
        if (tc.has_errors()) return "ERROR";
        std::vector<std::string> names;
        for (const auto &[id, s] : tc.environment.structs()) (void)id, names.push_back(s.canonical_name);
        for (const auto &[id, c] : tc.environment.capabilities()) (void)id, names.push_back(c.canonical_name);
        std::sort(names.begin(), names.end());
        std::string fp;
        for (auto &n : names) fp += n + "|";
        // Sanity: local string type must not alias global string type.
        auto &global = ahfl::TypeContext::global();
        if (static_cast<const void *>(local_types.string()) ==
            static_cast<const void *>(global.string())) {
            fp += "__SAME_PTR__tid=" + std::to_string(tid);
        }
        return fp;
    };

    std::vector<std::future<std::string>> futures;
    futures.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        futures.push_back(std::async(std::launch::async, worker, t));
    }

    std::vector<std::string> results;
    results.reserve(kThreads);
    for (auto &f : futures) results.push_back(f.get());
    REQUIRE(results.size() == kThreads);

    for (int t = 1; t < kThreads; ++t) {
        CHECK(results[t] == results[0]);
    }
    for (int t = 0; t < kThreads; ++t) {
        INFO("worker " << t << " fingerprint: " << results[t]);
        CHECK(results[t].find("__SAME_PTR__") == std::string::npos);
        CHECK(results[t].find("ERROR") == std::string::npos);
    }
}

// ============================================================================
// T1.4: Declaration iteration walks TypedProgram.declarations
// (not the raw AST decl list) and preserves T1.2 equivalence.
// ============================================================================

TEST_CASE_FIXTURE(TypedHIRFixture,
                  "T1.4 decl iteration walks typed tree and covers all kinds") {
    // Source with multiple agents, multiple structs, a contract, a flow, an
    // enum, a type alias, a const, a capability, a predicate, and a workflow:
    // exercises the full TypedDecl kind set so we can assert coverage.
    const std::string source = R"AHFL(
struct Req { v: String; }
struct Ctx { v: String = ""; }
struct Resp { v: String; code: Int = 200; }

enum Color { Red, Green, Blue }
type Tag = String;

const kPreamble: String = "ok:";
const kLimit: Int = 100;

capability Echo(payload: String) -> Resp;
capability Validate(tag: String) -> Bool;

predicate Safe(s: String) -> Bool;
predicate Positive(n: Int) -> Bool;

agent Worker {
    input: Req; context: Ctx; output: Resp;
    states: [Init, Run, Done]; initial: Init; final: [Done];
    capabilities: [Echo];
    transition Init -> Run;
    transition Run -> Done;
}

agent Inspector {
    input: Req; context: Ctx; output: Resp;
    states: [Start, End]; initial: Start; final: [End];
    capabilities: [Validate];
    transition Start -> End;
}

contract for Worker {
    requires: Safe(input.v);
    ensures:  true;
}

contract for Inspector {
    requires: true;
}

flow for Worker {
    state Init { goto Run; }
    state Run  { let r = Echo(input.v); goto Done; }
    state Done { return Resp { v: input.v, code: 200 }; }
}

flow for Inspector {
    state Start { goto End; }
    state End   { return Resp { v: "checked", code: 200 }; }
}

workflow DoJob {
    input: Req; output: Resp;
    node w: Worker(input);
    node i: Inspector(input);
    return: w;
}
)AHFL";
    const auto parse = frontend.parse_text("t14_iter.ahfl", source);
    REQUIRE_FALSE(parse.has_errors());
    ahfl::Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve.has_errors());
    ahfl::TypeChecker checker;
    const auto tc = checker.check(*parse.program, resolve);
    REQUIRE_FALSE(tc.has_errors());

    const auto &decls = tc.typed_program.declarations;
    using K = ahfl::ast::NodeKind;
    std::unordered_set<K> seen;
    for (const auto &d : decls) seen.insert(d.kind);

    // Kind coverage: all nominal kinds + contract + flow.
    CHECK(seen.contains(K::StructDecl));
    CHECK(seen.contains(K::EnumDecl));
    CHECK(seen.contains(K::TypeAliasDecl));
    CHECK(seen.contains(K::ConstDecl));
    CHECK(seen.contains(K::CapabilityDecl));
    CHECK(seen.contains(K::PredicateDecl));
    CHECK(seen.contains(K::AgentDecl));
    CHECK(seen.contains(K::ContractDecl));
    CHECK(seen.contains(K::FlowDecl));
    CHECK(seen.contains(K::WorkflowDecl));

    // Count multiple structs / agents / contracts / flows / consts / caps / preds
    // to confirm the flat list accumulates them and not just one-per-kind.
    std::size_t struct_count = 0, agent_count = 0, contract_count = 0;
    std::size_t flow_count = 0, const_count = 0, cap_count = 0, pred_count = 0;
    for (const auto &d : decls) {
        if (d.kind == K::StructDecl) ++struct_count;
        if (d.kind == K::AgentDecl) ++agent_count;
        if (d.kind == K::ContractDecl) ++contract_count;
        if (d.kind == K::FlowDecl) ++flow_count;
        if (d.kind == K::ConstDecl) ++const_count;
        if (d.kind == K::CapabilityDecl) ++cap_count;
        if (d.kind == K::PredicateDecl) ++pred_count;
    }
    CHECK(struct_count == 3);
    CHECK(agent_count == 2);
    CHECK(contract_count == 2);
    CHECK(flow_count == 2);
    CHECK(const_count == 2);
    CHECK(cap_count == 2);
    CHECK(pred_count == 2);

    // Single-program mode: source_id on each TypedDecl must be empty.
    for (const auto &d : decls) CHECK_FALSE(d.source_id.has_value());

    // T1.2 equivalence: typed-tree lowering and AST-tree lowering must
    // produce byte-identical IR even when the declaration iteration path
    // changed to walk TypedProgram.declarations.
    const auto via_ast = ahfl::lower_program_ir(*parse.program, resolve, tc);
    const auto via_typed = ahfl::lower_typed_program(tc.typed_program);

    CHECK(via_ast.declarations.size() == via_typed.declarations.size());
    REQUIRE_FALSE(via_ast.declarations.empty());

    std::ostringstream ast_json, typed_json;
    ahfl::print_program_ir_json(via_ast, ast_json);
    ahfl::print_program_ir_json(via_typed, typed_json);
    const std::string ast_fp = ast_json.str();
    const std::string typed_fp = typed_json.str();

    REQUIRE_FALSE(ast_fp.empty());
    REQUIRE_FALSE(typed_fp.empty());
    CHECK(ast_fp == typed_fp);
}
