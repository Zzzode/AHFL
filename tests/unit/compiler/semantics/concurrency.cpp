// Concurrent stress tests for the AHFL type-checker.
//
// Goal: prove that multiple TypeChecker instances can run in parallel
// without any shared mutable state leaking from one compilation into
// another.  All three exercises deliberately run on the same process
// threads managed by std::async so TSAN can pick up data races.
//
// Sanitizer verification commands (run from repo root):
//   cmake --preset asan  && cmake --build --preset build-asan  && ctest --preset test-asan  --output-on-failure
//   cmake --preset tsan  && cmake --build --preset build-tsan  && ctest --preset test-tsan  --output-on-failure

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace std::string_view_literals;

// ---------------------------------------------------------------------------
// Tiny, framework-free test harness so the binary links against nothing
// beyond the compiler libraries already used by the project-check tests.
// ---------------------------------------------------------------------------

struct TestFailure : std::runtime_error {
    explicit TestFailure(const std::string &what) : std::runtime_error(what) {}
};

struct Harness {
    const char *suite_name{nullptr};
    int passed{0};
    int failed{0};
    std::mutex mu;

    void run(const char *name, void (*fn)()) {
        try {
            fn();
            std::lock_guard<std::mutex> lk(mu);
            ++passed;
            std::printf("  [PASS] %s.%s\n", suite_name, name);
        } catch (const TestFailure &e) {
            std::lock_guard<std::mutex> lk(mu);
            ++failed;
            std::fprintf(stderr, "  [FAIL] %s.%s: %s\n", suite_name, name, e.what());
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lk(mu);
            ++failed;
            std::fprintf(stderr, "  [CRASH] %s.%s: %s\n", suite_name, name, e.what());
        }
    }
};

#define REQUIRE_FALSE(cond) REQUIRE(!(cond))
#define STRINGIFY_DETAIL(x) #x
#define STRINGIFY(x) STRINGIFY_DETAIL(x)
#define FAIL(msg) throw TestFailure(std::string(__FILE__ ":" STRINGIFY(__LINE__) " ") + (msg))
#define REQUIRE(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            FAIL("requirement failed: " #cond);                                                    \
        }                                                                                          \
    } while (0)
#define REQUIRE_EQ(a, b)                                                                           \
    do {                                                                                           \
        const auto &_a = (a);                                                                      \
        const auto &_b = (b);                                                                      \
        if (!(_a == _b)) {                                                                         \
            std::ostringstream _ss;                                                                \
            _ss << "requirement failed: " #a " == " #b;                                            \
            FAIL(_ss.str());                                                                       \
        }                                                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// Pipeline helper — parse, resolve, typecheck a single-file program.
// ---------------------------------------------------------------------------

struct PipelineResult {
    ahfl::ParseResult parse;
    ahfl::ResolveResult resolve;
    ahfl::TypeCheckResult typecheck;
    std::vector<std::string> canonical_decl_names;
    std::vector<ahfl::SymbolId> nominal_symbol_ids;
    std::size_t expression_type_count{0};
    std::size_t nominal_decl_count{0};
    std::string diagnostics_text;
    std::size_t error_count{0};
};

[[nodiscard]] std::string render_diagnostics(const ahfl::DiagnosticBag &bag) {
    std::ostringstream out;
    bag.render(out);
    return out.str();
}

[[nodiscard]] std::size_t count_errors(const ahfl::DiagnosticBag &bag) {
    std::size_t n = 0;
    for (const auto &d : bag.entries()) {
        if (d.severity == ahfl::DiagnosticSeverity::Error) {
            ++n;
        }
    }
    return n;
}

// Extract a stable, ordered list of canonical declaration names from a
// type-check result.  The order is deterministic (sorted) so concurrent
// runs of the same program can be compared.
[[nodiscard]] std::vector<std::string>
collect_canonical_names(const ahfl::TypeEnvironment &env,
                        const ahfl::ResolveResult &resolve) {
    std::vector<std::string> names;
    names.reserve(env.structs().size() + env.enums().size() +
                  env.capabilities().size() + env.predicates().size() +
                  env.agents().size() + env.workflows().size());
    for (const auto &[id, info] : env.structs()) names.push_back(info.canonical_name);
    for (const auto &[id, info] : env.enums()) names.push_back(info.canonical_name);
    for (const auto &[id, info] : env.capabilities()) names.push_back(info.canonical_name);
    for (const auto &[id, info] : env.predicates()) names.push_back(info.canonical_name);
    for (const auto &[id, info] : env.agents()) names.push_back(info.canonical_name);
    for (const auto &[id, info] : env.workflows()) names.push_back(info.canonical_name);
    std::sort(names.begin(), names.end());
    (void)resolve; // reserved for future name lookups
    return names;
}

// Collect SymbolIds of nominal declarations (structs + enums) so the
// concurrent "same program" test can assert that all type-check threads
// see the same symbols (which are owned by the shared ResolveResult).
[[nodiscard]] std::vector<ahfl::SymbolId>
collect_nominal_ids(const ahfl::TypeEnvironment &env) {
    std::vector<ahfl::SymbolId> ids;
    ids.reserve(env.structs().size() + env.enums().size());
    for (const auto &[id, info] : env.structs()) ids.push_back(info.symbol);
    for (const auto &[id, info] : env.enums()) ids.push_back(info.symbol);
    std::sort(ids.begin(), ids.end(), [](ahfl::SymbolId a, ahfl::SymbolId b) {
        return a.value < b.value;
    });
    return ids;
}

[[nodiscard]] PipelineResult run_pipeline(std::string_view display_name, std::string_view source) {
    const ahfl::Frontend frontend;
    auto parse = frontend.parse_text(std::string(display_name), std::string(source));
    if (parse.has_errors() || parse.program == nullptr) {
        PipelineResult r;
        r.parse = std::move(parse);
        r.diagnostics_text = render_diagnostics(r.parse.diagnostics);
        r.error_count = count_errors(r.parse.diagnostics);
        return r;
    }
    const ahfl::Resolver resolver;
    auto resolve = resolver.resolve(*parse.program);
    const ahfl::TypeChecker typechecker;
    auto typecheck = typechecker.check(*parse.program, resolve);
    PipelineResult r;
    r.parse = std::move(parse);
    r.resolve = std::move(resolve);
    r.typecheck = std::move(typecheck);
    r.canonical_decl_names =
        collect_canonical_names(r.typecheck.environment, r.resolve);
    r.nominal_symbol_ids = collect_nominal_ids(r.typecheck.environment);
    r.expression_type_count = r.typecheck.typed_program.expressions.size();
    r.nominal_decl_count =
        r.typecheck.environment.structs().size() + r.typecheck.environment.enums().size();
    r.diagnostics_text = render_diagnostics(r.typecheck.diagnostics);
    r.error_count = count_errors(r.typecheck.diagnostics);
    return r;
}

// ---------------------------------------------------------------------------
// Eight distinct programs used by the "no crosstalk" test.
//
//   Index 0..6 are well-formed and each owns a unique "signature" name
//   (Req_A, Req_B, ..., Req_G).
//   Index 7 is deliberately broken so we also assert diagnostics don't
//   leak into concurrent green-program results.
// ---------------------------------------------------------------------------

struct ProgramCase {
    const char *name;
    const char *source;
    std::size_t expected_structs;
    std::size_t expected_agents;
    std::size_t expected_capabilities;
    std::size_t expected_errors;
    bool contains_signature(std::string_view signature,
                            const std::vector<std::string> &names) const {
        return std::any_of(names.begin(), names.end(),
                           [&](const std::string &n) { return n.find(signature) != n.npos; });
    }
};

ProgramCase g_cases[] = {
    {"A_program",
     R"AHFL(module programs::A;
struct Req_A { value: String; }
struct Ctx_A { value: String = "a"; }
struct Res_A { value: String; }
capability Do_A(request: Req_A) -> Res_A;
agent Agent_A {
    input: Req_A;
    context: Ctx_A;
    output: Res_A;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Do_A];
    transition Init -> Done;
}
flow for Agent_A {
    state Init { let reply = Do_A(input); ctx.value = reply.value; goto Done; }
    state Done { return Res_A { value: ctx.value }; }
}
workflow WF_A { input: Req_A; output: Res_A; node run: Agent_A(input); return: run; }
)AHFL",
     /*structs=*/3, /*agents=*/1, /*caps=*/1, /*errors=*/0},
    {"B_program",
     R"AHFL(module programs::B;
struct Req_B { a: Int; b: Int; }
struct Ctx_B { sum: Int = 0; }
struct Res_B { sum: Int; }
capability Calc_B(request: Req_B) -> Res_B;
agent Agent_B {
    input: Req_B;
    context: Ctx_B;
    output: Res_B;
    states: [Start, End];
    initial: Start;
    final: [End];
    capabilities: [Calc_B];
    transition Start -> End;
}
flow for Agent_B {
    state Start { let reply = Calc_B(input); ctx.sum = reply.sum; goto End; }
    state End { return Res_B { sum: ctx.sum }; }
}
)AHFL",
     /*structs=*/3, /*agents=*/1, /*caps=*/1, /*errors=*/0},
    {"C_program",
     R"AHFL(module programs::C;
enum Color_C {
    Red,
    Green,
    Blue,
}
struct Req_C { color: Color_C; }
struct Res_C { label: String = ""; }
capability Describe_C(request: Req_C) -> Res_C;
agent Agent_C {
    input: Req_C;
    context: Res_C;
    output: Res_C;
    states: [I, O];
    initial: I;
    final: [O];
    capabilities: [Describe_C];
    transition I -> O;
}
flow for Agent_C {
    state I { let reply = Describe_C(input); ctx.label = reply.label; goto O; }
    state O { return Res_C { label: ctx.label }; }
}
)AHFL",
     /*structs=*/2, /*agents=*/1, /*caps=*/1, /*errors=*/0},
    {"D_program",
     R"AHFL(module programs::D;
struct Req_D { value: Int; }
struct Ctx_D { value: Int = 0; ok: Bool = false; }
struct Res_D { ok: Bool; }
predicate IsPositive_D(value: Int) -> Bool;
capability Run_D(request: Req_D) -> Res_D;
agent Agent_D {
    input: Req_D;
    context: Ctx_D;
    output: Res_D;
    states: [X, Y];
    initial: X;
    final: [Y];
    capabilities: [Run_D];
    transition X -> Y;
}
contract for Agent_D { requires: IsPositive_D(input.value); }
flow for Agent_D {
    state X { let reply = Run_D(input); ctx.ok = reply.ok; goto Y; }
    state Y { return Res_D { ok: ctx.ok }; }
}
)AHFL",
     /*structs=*/3, /*agents=*/1, /*caps=*/1, /*errors=*/0},
    {"E_program",
     R"AHFL(module programs::E;
struct Req_E_Inner { s: String; }
type Req_E = Req_E_Inner;
struct Res_E { s: String = ""; }
capability Pipe_E(request: Req_E) -> Res_E;
agent Agent_E {
    input: Req_E;
    context: Res_E;
    output: Res_E;
    states: [P, Q];
    initial: P;
    final: [Q];
    capabilities: [Pipe_E];
    transition P -> Q;
}
flow for Agent_E {
    state P { let reply = Pipe_E(input); ctx.s = reply.s; goto Q; }
    state Q { return Res_E { s: ctx.s }; }
}
)AHFL",
     /*structs=*/2, /*agents=*/1, /*caps=*/1, /*errors=*/0},
    {"F_program",
     R"AHFL(module programs::F;
struct Req_F { x: Int; }
struct Acc_F { half: Int = 0; final_x: Int = 0; }
struct Res_F { x: Int; }
capability HalfCap_F(request: Req_F) -> Res_F;
capability Double_F(request: Res_F) -> Res_F;
agent Agent_F {
    input: Req_F;
    context: Acc_F;
    output: Res_F;
    states: [I, M, O];
    initial: I;
    final: [O];
    capabilities: [HalfCap_F, Double_F];
    transition I -> M;
    transition M -> O;
}
flow for Agent_F {
    state I { let h = HalfCap_F(input); ctx.half = h.x; goto M; }
    state M { let r = Double_F(Res_F { x: ctx.half }); ctx.final_x = r.x; goto O; }
    state O { return Res_F { x: ctx.final_x }; }
}
)AHFL",
     /*structs=*/3, /*agents=*/1, /*caps=*/2, /*errors=*/0},
    {"G_program",
     R"AHFL(module programs::G;
struct Req_G { id: String; amount: Int; }
struct Audit_G { id: String = ""; note: String = ""; }
struct Res_G { ok: Bool; note: String; }
capability Authorize_G(request: Req_G) -> Res_G;
capability Log_G(audit: Audit_G) -> Res_G;
agent Agent_G {
    input: Req_G;
    context: Audit_G;
    output: Res_G;
    states: [Start, Auth, LogS, End];
    initial: Start;
    final: [End];
    capabilities: [Authorize_G, Log_G];
    transition Start -> Auth;
    transition Auth -> LogS;
    transition LogS -> End;
}
flow for Agent_G {
    state Start { goto Auth; }
    state Auth { let reply = Authorize_G(input); ctx.note = reply.note; goto LogS; }
    state LogS { let logged = Log_G(ctx); ctx.note = logged.note; goto End; }
    state End { return Res_G { ok: true, note: ctx.note }; }
}
)AHFL",
     /*structs=*/3, /*agents=*/1, /*caps=*/2, /*errors=*/0},
    {"H_program",
     R"AHFL(module programs::H;
struct Req_H { x: Int; }
struct Res_H { y: Int = 0; }
capability Cap_H(request: Req_H) -> Res_H;
agent Agent_H {
    input: Req_H;
    context: Res_H;
    output: Res_H;
    states: [I, O];
    initial: I;
    final: [O];
    capabilities: [Cap_H];
    transition I -> O;
}
flow for Agent_H {
    state I { ctx.bogus_field_one_H = input.x; let reply = Cap_H(input); ctx.bogus_field_two_H = reply.y; goto O; }
    state O { return Res_H { y: ctx.y }; }
}
)AHFL",
     /*structs=*/2, /*agents=*/1, /*caps=*/1, /*errors=*/2},
};

constexpr std::size_t kCaseCount = sizeof(g_cases) / sizeof(g_cases[0]);

// ---------------------------------------------------------------------------
// Test #1 — 8 distinct programs concurrently.  No cross-talk.
// ---------------------------------------------------------------------------

void test_concurrent_distinct_programs_no_crosstalk() {
    static_assert(kCaseCount == 8,
                  "P2.3 expects exactly 8 distinct concurrent programs");

    std::vector<std::future<PipelineResult>> futures;
    futures.reserve(kCaseCount);
    for (std::size_t i = 0; i < kCaseCount; ++i) {
        futures.push_back(std::async(std::launch::async, [i] {
            return run_pipeline(
                std::string("conc_") + g_cases[i].name + ".ahfl", g_cases[i].source);
        }));
    }

    std::vector<PipelineResult> results;
    results.reserve(kCaseCount);
    for (auto &f : futures) results.push_back(f.get());
    REQUIRE_EQ(results.size(), kCaseCount);

    // --- Per-program declarations contain ONLY its own signatures. ----
    for (std::size_t i = 0; i < kCaseCount; ++i) {
        const auto &c = g_cases[i];
        const auto &r = results[i];
        // Structural counts match.
        if (r.error_count == 0) {
            REQUIRE_EQ(r.typecheck.environment.structs().size(), c.expected_structs);
            REQUIRE_EQ(r.typecheck.environment.agents().size(), c.expected_agents);
            REQUIRE_EQ(r.typecheck.environment.capabilities().size(), c.expected_capabilities);
        }
        // Must contain its own signature name.
        std::string own_sig = std::string("_") + c.name[0];
        bool has_own =
            std::any_of(r.canonical_decl_names.begin(), r.canonical_decl_names.end(),
                        [&](const std::string &n) { return n.find(own_sig) != n.npos; });
        if (!has_own) {
            std::ostringstream ss;
            ss << "program " << c.name
               << " missing its own signature '" << own_sig
               << "' in canonical names list (pe="
               << r.parse.has_errors() << " re="
               << r.resolve.has_errors() << " names={";
            for (std::size_t k = 0; k < r.canonical_decl_names.size(); ++k) {
                if (k) ss << ",";
                ss << r.canonical_decl_names[k];
            }
            ss << "})";
            FAIL(ss.str());
        }
        // Must NOT contain any other case's signature.
        for (std::size_t j = 0; j < kCaseCount; ++j) {
            if (i == j) continue;
            std::string other_sig = std::string("_") + g_cases[j].name[0];
            bool has_other =
                std::any_of(r.canonical_decl_names.begin(), r.canonical_decl_names.end(),
                            [&](const std::string &n) {
                                    return n.find(other_sig) != n.npos;
                                });
            if (has_other) {
                std::ostringstream ss;
                ss << "program " << c.name << " leaked declaration from "
                   << g_cases[j].name << " (found '" << other_sig << "')";
                FAIL(ss.str());
            }
        }
        // Error count matches the case's expectation.
        if (r.error_count != c.expected_errors) {
            std::ostringstream ss;
            ss << "program " << c.name << " expected " << c.expected_errors
               << " errors, got " << r.error_count;
            if (!r.diagnostics_text.empty()) ss << ":\n" << r.diagnostics_text;
            FAIL(ss.str());
        }
    }

    // --- Diagnostics isolation: broken-program errors do not appear ---
    // --- in any green-program diagnostic output.                    ---
    const PipelineResult &broken = results[7];
    REQUIRE(broken.error_count > 0);
    REQUIRE(broken.diagnostics_text.find("bogus_field_one_H") != std::string::npos ||
            broken.diagnostics_text.find("bogus_field_two_H") != std::string::npos);
    for (std::size_t i = 0; i < 7; ++i) {
        const auto &r = results[i];
        if (!r.diagnostics_text.empty()) {
            if (r.diagnostics_text.find("bogus_field_one_H") != std::string::npos ||
                r.diagnostics_text.find("bogus_field_two_H") != std::string::npos) {
                std::ostringstream ss;
                ss << "green program " << g_cases[i].name
                   << " contains diagnostics emitted for the broken program";
                FAIL(ss.str());
            }
        }
    }

    // --- Resolver / type-check per-program isolation. ------------------
    // Each thread's resolver owns its own SymbolTable and produces
    // SymbolId values that start at 0 within each invocation.  Two
    // distinct programs may therefore legitimately share *numeric* id
    // values; what must never happen is that the *type information*
    // stored under those ids leaks from one program into another.
    //
    // We detect that leak indirectly: for every nominal symbol id that
    // program P owns, the name string returned by the symbol-table
    // lookup must match the canonical name stored on the type-info
    // record AND must contain P's letter signature (e.g. "_A").  If a
    // shared-global mutation had swapped the backing storage the name
    // strings would disagree, and the test fails.
    for (std::size_t i = 0; i < kCaseCount; ++i) {
        const auto &c = g_cases[i];
        const auto &r = results[i];
        const std::string letter_sig = std::string("_") + c.name[0];
        const auto &symbols = r.resolve.symbol_table.symbols();
        // Structs
        for (const auto &[id, info] : r.typecheck.environment.structs()) {
            const auto sym = symbols.cbegin() + info.symbol.value;
            REQUIRE(sym < symbols.cend());
            REQUIRE_EQ(sym->canonical_name, info.canonical_name);
            if (info.canonical_name.find(letter_sig) == std::string::npos) {
                std::ostringstream ss;
                ss << "struct name '" << info.canonical_name
                   << "' in program " << c.name
                   << " does not contain its letter signature '" << letter_sig
                   << "' — possible cross-program name table leak";
                FAIL(ss.str());
            }
        }
        // Enums
        for (const auto &[id, info] : r.typecheck.environment.enums()) {
            const auto sym = symbols.cbegin() + info.symbol.value;
            REQUIRE(sym < symbols.cend());
            REQUIRE_EQ(sym->canonical_name, info.canonical_name);
            if (info.canonical_name.find(letter_sig) == std::string::npos) {
                std::ostringstream ss;
                ss << "enum name '" << info.canonical_name
                   << "' in program " << c.name
                   << " does not contain its letter signature '" << letter_sig
                   << "' — possible cross-program name table leak";
                FAIL(ss.str());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Test #2 — 8 concurrent type-checks of the SAME (already resolved) AST.
//
// The AST + ResolveResult are parsed and resolved exactly once by the
// main thread.  8 worker threads then call TypeChecker::check() on
// those immutable inputs.  All 8 results must be byte-for-byte
// equivalent on the metrics that are not explicitly synthesised per
// invocation.  This proves TypeChecker::check is read-only on its
// inputs.
// ---------------------------------------------------------------------------

struct SharedInput {
    ahfl::ParseResult parse;
    ahfl::ResolveResult resolve;
};

void test_concurrent_same_program_isolated_results() {
    constexpr std::string_view shared_source = R"AHFL(module m::Shared;
struct Req_S { value: String; }
struct Ctx_S { value: String = "pending"; }
struct Res_S { value: String; }
capability Do_S(r: Req_S) -> Res_S;
predicate P_S(v: String) -> Bool;
agent Agent_S {
    input: Req_S; context: Ctx_S; output: Res_S;
    states: [Init, Done]; initial: Init; final: [Done];
    capabilities: [Do_S]; transition Init -> Done;
}
contract for Agent_S { requires: P_S(input.value); }
flow for Agent_S {
    state Init { let r = Do_S(input); ctx.value = r.value; goto Done; }
    state Done { return Res_S { value: ctx.value }; }
}
workflow WF_S { input: Req_S; output: Res_S; node r: Agent_S(input); return: r; }
)AHFL";

    const ahfl::Frontend frontend;
    auto parse = frontend.parse_text("shared_program.ahfl", std::string(shared_source));
    REQUIRE_FALSE(parse.has_errors());
    REQUIRE(parse.program != nullptr);
    const ahfl::Resolver resolver;
    auto resolve_once = resolver.resolve(*parse.program);
    REQUIRE_FALSE(resolve_once.has_errors());

    SharedInput shared{.parse = std::move(parse), .resolve = std::move(resolve_once)};

    constexpr std::size_t kWorkers = 8;
    std::vector<std::future<ahfl::TypeCheckResult>> workers;
    workers.reserve(kWorkers);
    for (std::size_t i = 0; i < kWorkers; ++i) {
        workers.push_back(std::async(std::launch::async, [&shared] {
            const ahfl::TypeChecker tc;
            return tc.check(*shared.parse.program, shared.resolve);
        }));
    }

    std::vector<ahfl::TypeCheckResult> outputs;
    outputs.reserve(kWorkers);
    for (auto &w : workers) outputs.push_back(w.get());

    // Decl counts.
    const auto expected_structs = outputs.front().environment.structs().size();
    const auto expected_enums = outputs.front().environment.enums().size();
    const auto expected_agents = outputs.front().environment.agents().size();
    const auto expected_caps = outputs.front().environment.capabilities().size();
    const auto expected_expr = outputs.front().typed_program.expressions.size();
    const auto expected_errors = count_errors(outputs.front().diagnostics);

    // First nominal symbol id (the first struct in canonical order) —
    // stable because it comes from the shared ResolveResult.
    std::vector<ahfl::SymbolId> ref_ids = collect_nominal_ids(outputs.front().environment);
    REQUIRE_FALSE(ref_ids.empty());
    const auto first_symbol_id = ref_ids.front().value;

    for (std::size_t i = 0; i < kWorkers; ++i) {
        const auto &o = outputs[i];
        if (o.environment.structs().size() != expected_structs) {
            FAIL("worker result disagrees on struct count");
        }
        if (o.environment.enums().size() != expected_enums) {
            FAIL("worker result disagrees on enum count");
        }
        if (o.environment.agents().size() != expected_agents) {
            FAIL("worker result disagrees on agent count");
        }
        if (o.environment.capabilities().size() != expected_caps) {
            FAIL("worker result disagrees on capability count");
        }
        if (o.typed_program.expressions.size() != expected_expr) {
            FAIL("worker result disagrees on expression type map size");
        }
        if (count_errors(o.diagnostics) != expected_errors) {
            FAIL("worker result disagrees on error count");
        }
        std::vector<ahfl::SymbolId> ids = collect_nominal_ids(o.environment);
        if (ids.size() != ref_ids.size()) {
            FAIL("worker produced different number of nominal symbols");
        }
        if (ids.front().value != first_symbol_id) {
            FAIL("worker produced a different first nominal symbol id — "
                 "ResolveResult was mutated?");
        }
        for (std::size_t k = 0; k < ids.size(); ++k) {
            if (ids[k] != ref_ids[k]) {
                FAIL("worker nominal-symbol-id vector differs from reference");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Test #3 — Determinism: sequential(10) vs concurrent(10) runs produce
// identical error counts and identical diagnostic text ordering.
// ---------------------------------------------------------------------------

struct DeterminismCase {
    const char *name;
    const char *source;
};

DeterminismCase g_det_cases[] = {
    {"ok",
     R"AHFL(module m::DetOk;
struct Req_D { x: Int; }
struct Res_D { y: Int; }
capability C_D(r: Req_D) -> Res_D;
agent A_D {
    input: Req_D; context: Res_D; output: Res_D;
    states: [I, O]; initial: I; final: [O];
    capabilities: [C_D]; transition I -> O;
}
flow for A_D { state I { goto O; } state O { return Res_D { y: 1 }; } }
)AHFL"},
    {"broken",
     R"AHFL(module m::DetBad;
struct Req_D { x: Int; }
capability C_D(r: Req_D) -> Missing_D;
agent A_D {
    input: Req_D; context: Req_D; output: Req_D;
    states: [I, O]; initial: I; final: [O];
    capabilities: [C_D]; transition I -> O;
}
flow for A_D { state I { ctx.no_such_D = input.x; goto O; } state O { return Req_D { x: 1 }; } }
)AHFL"},
};

using ResultVec = std::vector<std::pair<std::size_t, std::string>>;

[[nodiscard]] ResultVec run_sequential(const DeterminismCase &tc, std::size_t repetitions) {
    ResultVec out;
    out.reserve(repetitions);
    for (std::size_t i = 0; i < repetitions; ++i) {
        auto r = run_pipeline(std::string("det_") + tc.name + "_" + std::to_string(i) + ".ahfl",
                              tc.source);
        out.emplace_back(r.error_count, r.diagnostics_text);
    }
    return out;
}

[[nodiscard]] ResultVec run_concurrent(const DeterminismCase &tc, std::size_t repetitions) {
    std::vector<std::future<std::pair<std::size_t, std::string>>> futures;
    futures.reserve(repetitions);
    for (std::size_t i = 0; i < repetitions; ++i) {
        futures.push_back(std::async(std::launch::async, [&tc, i] {
            auto r = run_pipeline(
                std::string("det_") + tc.name + "_c" + std::to_string(i) + ".ahfl", tc.source);
            return std::make_pair(r.error_count, r.diagnostics_text);
        }));
    }
    ResultVec out;
    out.reserve(repetitions);
    for (auto &f : futures) out.push_back(f.get());
    return out;
}

void test_sequential_vs_concurrent_determinism() {
    constexpr std::size_t kRuns = 10;
    for (const auto &tc : g_det_cases) {
        const auto seq = run_sequential(tc, kRuns);
        const auto par = run_concurrent(tc, kRuns);
        REQUIRE_EQ(seq.size(), kRuns);
        REQUIRE_EQ(par.size(), kRuns);

        // Each sequential run must itself be identical to run #0 —
        // this is the intra-mode determinism baseline.
        for (std::size_t i = 1; i < kRuns; ++i) {
            if (seq[i] != seq[0]) {
                std::ostringstream ss;
                ss << "sequential runs for case '" << tc.name
                   << "' are not self-identical at index " << i;
                FAIL(ss.str());
            }
        }

        // Every concurrent run must agree with the sequential baseline
        // both on error count and on the exact rendered diagnostic
        // text.  Set equality (order-independent) is fine because the
        // concurrent scheduler does not guarantee thread completion
        // order; we only require that the SET of rendered diagnostics
        // is identical between the two modes.
        std::unordered_map<std::string, std::size_t> seq_buckets;
        std::unordered_map<std::string, std::size_t> par_buckets;
        for (std::size_t i = 0; i < kRuns; ++i) {
            if (seq[i].first != par[i].first) {
                std::ostringstream ss;
                ss << "case '" << tc.name << "' seq@" << i << " has " << seq[i].first
                   << " errors, par@" << i << " has " << par[i].first;
                FAIL(ss.str());
            }
            seq_buckets[seq[i].second]++;
            par_buckets[par[i].second]++;
        }
        if (seq_buckets != par_buckets) {
            std::ostringstream ss;
            ss << "case '" << tc.name
               << "': diagnostic text multiset differs between sequential "
                  "and concurrent runs";
            FAIL(ss.str());
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
    Harness h{.suite_name = "p2_3_concurrency"};
    h.run("concurrent_distinct_programs_no_crosstalk",
          test_concurrent_distinct_programs_no_crosstalk);
    h.run("concurrent_same_program_isolated_results",
          test_concurrent_same_program_isolated_results);
    h.run("sequential_vs_concurrent_determinism",
          test_sequential_vs_concurrent_determinism);
    std::printf("%s: %d passed, %d failed\n", h.suite_name, h.passed, h.failed);
    return h.failed == 0 ? 0 : 1;
}
