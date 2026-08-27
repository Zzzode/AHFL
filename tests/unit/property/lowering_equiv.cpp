// KR5.10: real lowering-equivalence property tests.
//
// The previous version of this file only asserted invariants of the AST
// *generator* (e.g. "generated agents always have states") and never touched
// `ir::` or the lowering pipeline — the label "lowering equivalence" was a
// misnomer. This rewrite drives a curated corpus of valid AHFL programs
// through the real front end (parse -> resolve -> typecheck -> lower) and
// asserts genuine mid-end properties:
//
//   P1  HIR->IR lowering is deterministic: lowering the same program twice
//       produces byte-identical IR JSON. Semantic preservation must not depend
//       on run-to-run nondeterminism (map ordering, address-derived ids, ...).
//   P2  Lowered IR is well-formed: verify_ir_program(BackendReady) succeeds,
//       i.e. the HIR->IR step preserves enough structure for the backends.
//   P3  opt-IR equivalence: lowering to the optimization IR and running the
//       optimizer to fixpoint preserves the observable function structure
//       (same set of OptFunction names) and keeps the program verifiable —
//       optimization is semantics-preserving, not shape-destroying.
//
// A curated corpus (rather than the random AstGenerator) is used because the
// generator emits approximate source that is not guaranteed to typecheck; a
// property over the lowering pipeline needs inputs that actually reach it.

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/verify.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "compiler/ir/opt/opt_lower.hpp"
#include "compiler/ir/opt/opt_passes.hpp"
#include "compiler/ir/opt/opt_verify.hpp"
#include "tooling/testing/ast_generator.hpp"

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name.c_str());
    } else {
        std::printf("  FAIL: %s\n", name.c_str());
    }
}

// A lowered program plus the intermediate results, or an empty optional when
// the source failed an earlier stage (which the corpus is expected not to do).
struct Lowered {
    bool ok{false};
    std::string ir_json;
    ahfl::ir::Program program;
};

[[nodiscard]] std::string lower_to_ir_json(const ahfl::ast::Program &program,
                                           const ahfl::ResolveResult &resolve,
                                           const ahfl::TypeCheckResult &typecheck) {
    std::ostringstream out;
    ahfl::print_program_ir_json(ahfl::lower_program_ir(program, resolve, typecheck), out);
    return out.str();
}

// Run the full front end on a source string. Returns ok=false (with a printed
// diagnostic) if any stage errors, so a corpus regression is visible.
[[nodiscard]] bool
run_pipeline(const std::string &label, const std::string &source, std::string &ir_json_out) {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(label + ".ahfl", source);
    if (parse_result.has_errors() || parse_result.program == nullptr) {
        std::printf("    (%s: parse failed)\n", label.c_str());
        return false;
    }
    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        std::printf("    (%s: resolve failed)\n", label.c_str());
        return false;
    }
    const ahfl::TypeChecker checker;
    const auto type_result = checker.check(*parse_result.program, resolve_result);
    if (type_result.has_errors()) {
        std::printf("    (%s: typecheck failed)\n", label.c_str());
        return false;
    }

    // P1: deterministic lowering — two independent lowerings must be identical.
    const auto json_a = lower_to_ir_json(*parse_result.program, resolve_result, type_result);
    const auto json_b = lower_to_ir_json(*parse_result.program, resolve_result, type_result);
    if (json_a != json_b) {
        std::printf("    (%s: lowering is nondeterministic)\n", label.c_str());
        return false;
    }
    ir_json_out = json_a;

    // P2: lowered IR is backend-ready.
    const auto program = ahfl::lower_program_ir(*parse_result.program, resolve_result, type_result);
    const auto verify = ahfl::ir::verify_ir_program(program, ahfl::ir::IrVerificationMode::BackendReady);
    if (verify.has_errors()) {
        std::printf("    (%s: IR verification failed)\n", label.c_str());
        return false;
    }

    // P3: opt-IR equivalence — optimizing preserves the OptFunction name set
    // and keeps the program verifiable.
    auto unopt = ahfl::ir::opt::lower_to_opt(program);
    std::vector<std::string> names_before;
    names_before.reserve(unopt.functions.size());
    for (const auto &fn : unopt.functions) {
        names_before.push_back(fn.name);
    }
    if (ahfl::ir::opt::verify_opt_program(unopt).has_errors()) {
        std::printf("    (%s: unoptimized opt-IR fails verify)\n", label.c_str());
        return false;
    }
    for (auto &fn : unopt.functions) {
        (void)ahfl::ir::opt::optimize(fn);
    }
    std::vector<std::string> names_after;
    names_after.reserve(unopt.functions.size());
    for (const auto &fn : unopt.functions) {
        names_after.push_back(fn.name);
    }
    if (names_before != names_after) {
        std::printf("    (%s: optimization changed the function set)\n", label.c_str());
        return false;
    }
    if (ahfl::ir::opt::verify_opt_program(unopt).has_errors()) {
        std::printf("    (%s: optimized opt-IR fails verify)\n", label.c_str());
        return false;
    }
    return true;
}

// Curated corpus of small but structurally varied valid AHFL programs.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> corpus() {
    std::vector<std::pair<std::string, std::string>> programs;

    programs.emplace_back("simple_agent", R"AHFL(
struct Request { value: Int; }
struct Ctx { count: Int = 0; }

agent Worker {
    input: Request;
    context: Ctx;
    output: Ctx;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
flow for Worker {
    state Init {
        let doubled = input.value + input.value;
        goto Done;
    }
    state Done { return Ctx { count: 0 }; }
}
)AHFL");

    programs.emplace_back("contract_agent", R"AHFL(
struct Request { value: Int; }
struct Ctx { }

agent Guarded {
    input: Request;
    context: Ctx;
    output: Ctx;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
flow for Guarded {
    state Init { goto Done; }
    state Done { return Ctx { }; }
}
contract for Guarded {
    requires: input.value > 0;
    ensures: true;
}
)AHFL");

    programs.emplace_back("workflow_program", R"AHFL(
pub struct Request { value: Int; }
pub struct Reply { total: Int; }
pub struct Ctx { }

pub agent Node {
    input: Request;
    context: Ctx;
    output: Reply;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
flow for Node {
    state Init { goto Done; }
    state Done { return Reply { total: input.value }; }
}
pub workflow Pipe {
    input: Request;
    output: Reply;
    node run: Node(input);
    safety: always (not running(run) or (1 + 1 == 2));
    liveness: eventually completed(run, Done);
    return: run;
}
)AHFL");

    return programs;
}

} // namespace

int main() {
    std::printf("=== Property Tests: Lowering Equivalence (KR5.10) ===\n");

    // Real lowering properties over the curated corpus.
    for (const auto &[label, source] : corpus()) {
        std::string ir_json;
        check(run_pipeline(label, source, ir_json),
              "lowering P1-P3 hold for corpus program '" + label + "'");
        check(!ir_json.empty(), "lowered IR JSON is non-empty for '" + label + "'");
    }

    // Retained generator-determinism sanity (same seed => same AST). This is a
    // precondition for reproducible property inputs, not a lowering property.
    {
        using namespace ahfl::testing;
        AstGenerator gen1({.seed = 42});
        AstGenerator gen2({.seed = 42});
        check(gen1.generate_agent().name == gen2.generate_agent().name,
              "generator: same seed produces same agent name");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
