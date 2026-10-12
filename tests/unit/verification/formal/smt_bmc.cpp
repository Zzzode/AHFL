// RFC 0017 (BMC Contract Semantics) slice 5 unit tests: SMT-BMC engine.
//
// The engine invokes the solver per goal, so the always-on assertions cover
// the solver-independent behaviour (no goals -> Unsupported; a bogus solver
// path -> SolverUnavailable, never Safe). The proof semantics (ensures proven,
// off-by-one refuted, divisor obligation) run only when AHFL_Z3_PATH points at
// a real Z3, matching the guarded ctest.

#include "verification/formal/smt_bmc.hpp"

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/decl.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/types.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl::formal;
namespace ir = ahfl::ir;

int test_count = 0;
int pass_count = 0;
std::string fake_solver_path;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

ir::TypeRef int_type() {
    ir::TypeRef t;
    t.kind = ir::TypeRefKind::Int;
    t.display_name = "Int";
    return t;
}

ir::ExprRef int_lit(ir::Program &p, const std::string &spelling) {
    return p.expr_arena.make(ir::IntegerLiteralExpr{.spelling = spelling});
}

ir::ExprRef int_path(ir::Program &p, const std::string &root, std::vector<std::string> members,
                     ir::SourceRangeOpt source_range = std::nullopt) {
    ir::Path path;
    path.root_kind = ir::PathRootKind::Identifier;
    path.root_name = root;
    path.members = std::move(members);
    return p.expr_arena.make(ir::PathExpr{.path = std::move(path)}, source_range, int_type());
}

ir::ExprRef bin(ir::Program &p, ir::ExprBinaryOp op, ir::ExprRef lhs, ir::ExprRef rhs) {
    return p.expr_arena.make(ir::BinaryExpr{.op = op, .lhs = lhs, .rhs = rhs});
}

// RFC 0025: a bounded List<Int>(capacity) type ref, used as the resolved type
// of a quantified collection operand so the SMT-BMC encoder can unroll it.
ir::TypeRef bounded_list_int_type(std::uint64_t capacity) {
    ir::TypeRef list;
    list.kind = ir::TypeRefKind::Struct;
    list.canonical_name = "std::collections::List";
    list.display_name = "std::collections::List<Int>(" + std::to_string(capacity) + ")";
    list.collection_capacity = capacity;
    auto element = ahfl::Owned<ir::TypeRef>(new ir::TypeRef{int_type()});
    list.params.push_back(std::move(element));
    return list;
}

// A path expression whose resolved type is a bounded List<Int>(capacity).
ir::ExprRef bounded_list_path(ir::Program &p, const std::string &root,
                              std::vector<std::string> members, std::uint64_t capacity) {
    ir::Path path;
    path.root_kind = ir::PathRootKind::Identifier;
    path.root_name = root;
    path.members = std::move(members);
    return p.expr_arena.make(ir::PathExpr{.path = std::move(path)}, std::nullopt,
                             bounded_list_int_type(capacity));
}

// RFC 0024: forall x in <collection>: <body>.
ir::ExprRef forall_expr(ir::Program &p, const std::string &binder, ir::ExprRef collection,
                        ir::ExprRef body) {
    return p.expr_arena.make(ir::QuantifierExpr{
        .kind = ir::QuantifierExpr::Kind::ForAll,
        .binder = binder,
        .value_binder = {},
        .collection = collection,
        .body = body,
    });
}

void add_contract(ir::Program &p, const std::string &target,
                  std::vector<std::pair<ir::ContractClauseKind, ir::ExprRef>> clauses) {
    ir::ContractDecl contract;
    contract.target_ref = ir::SymbolRef{
        .kind = ir::SymbolRefKind::Agent,
        .canonical_name = target,
        .local_name = target,
        .module_name = {},
    };
    for (auto &[kind, expr] : clauses) {
        contract.clauses.push_back(ir::ContractClause{
            .kind = kind,
            .value = std::variant<ir::ExprRef, ir::TemporalExprPtr>{expr},
            .source_range = ahfl::SourceRange{1, 2},
        });
    }
    p.declarations.push_back(std::move(contract));
}

bool z3_available() {
    const char *z3 = std::getenv("AHFL_Z3_PATH");
    return z3 != nullptr && *z3 != '\0';
}

// ---------------------------------------------------------------------------
// Solver-independent behaviour
// ---------------------------------------------------------------------------

void test_no_contracts_unsupported() {
    ir::Program p;
    auto r = run_smt_bmc(p, {});
    check(r.status == SmtBmcStatus::Unsupported && r.goals.empty(),
          "program without contracts is Unsupported");
}

void test_unavailable_solver_never_safe() {
    ir::Program p;
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "1"))}});
    SmtBmcOptions options;
    options.solver.solver_path = "/nonexistent/path/to/z3";
    auto r = run_smt_bmc(p, options);
    check(r.status == SmtBmcStatus::SolverUnavailable,
          "a missing solver yields SolverUnavailable, never Safe");
    check(r.goals.size() == 1 && !r.goals.front().proven, "goal recorded but not proven");
}

void test_goal_metadata() {
    ir::Program p;
    add_contract(p, "pkg::A",
                 {{ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::Greater, int_path(p, "output", {"n"}),
                       int_lit(p, "0"))}});
    SmtBmcOptions options;
    options.solver.solver_path = "/nonexistent/z3"; // don't actually solve
    auto r = run_smt_bmc(p, options);
    check(r.goals.size() == 1, "one goal for one ensures clause");
    if (r.goals.size() == 1) {
        const auto &g = r.goals.front();
        check(g.kind == ir::ContractClauseKind::Ensures, "goal kind is ensures");
        check(g.target_name == "pkg::A", "goal carries target name");
        check(g.description == "ensures", "goal description");
        check(g.source_range.has_value(), "goal carries source range");
    }
}

void test_divisor_obligation_becomes_goal() {
    ir::Program p;
    // ensures: (10 / input.d) == (10 / input.d) — a divide obligation is added.
    auto div = bin(p, ir::ExprBinaryOp::Divide, int_lit(p, "10"), int_path(p, "input", {"d"}));
    auto div2 = bin(p, ir::ExprBinaryOp::Divide, int_lit(p, "10"), int_path(p, "input", {"d"}));
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Ensures, bin(p, ir::ExprBinaryOp::Equal, div, div2)}});
    SmtBmcOptions options;
    options.solver.solver_path = "/nonexistent/z3";
    auto r = run_smt_bmc(p, options);
    // One ensures goal + two divisor obligations (one per divide node).
    check(r.goals.size() == 3, "ensures + two divisor obligations become three goals");
    bool has_divisor_goal = false;
    for (const auto &g : r.goals) {
        if (g.description.find("divisor != 0") != std::string::npos) {
            has_divisor_goal = true;
        }
    }
    check(has_divisor_goal, "a divisor-non-zero goal is present");
}

void test_unsupported_clauses_fail_closed() {
    for (const auto kind : {ir::ContractClauseKind::Requires, ir::ContractClauseKind::Ensures}) {
        ir::Program p;
        add_contract(p, "Agent", {{kind, p.expr_arena.make(ir::CallExpr{
            .callee = "host_predicate", .arguments = {}, .callee_ref = {}})},
            {ir::ContractClauseKind::Ensures,
             bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "1"))}});
        auto r = run_smt_bmc(p, {});
        check(r.status == SmtBmcStatus::Unsupported && r.unsupported_clauses.size() == 1,
              "unsupported goal or precondition is recorded, never skipped into Safe");
        check(smt_bmc_result_is_failure(r) && r.proven_count == 0,
              "unsupported selected contract fails closed");
    }
    check(!smt_bmc_result_is_failure(run_smt_bmc(ir::Program{}, {})),
          "no data goals is not applicable");
}

void test_cumulative_goal_ordinals() {
    ir::Program p;
    add_contract(p, "Agent", {{ir::ContractClauseKind::Requires,
        bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "1"))}});
    add_contract(p, "Agent", {{ir::ContractClauseKind::Ensures,
        bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "2"), int_lit(p, "2"))}});
    for (auto &decl : p.declarations) {
        std::get<ir::ContractDecl>(decl).target_ref.id = 7;
    }
    SmtBmcOptions options;
    options.solver.solver_path = "/nonexistent/z3";
    const auto r = run_smt_bmc(p, options);
    check(r.goals.size() == 1 && r.goals.front().clause_index == 1,
          "goal index is cumulative across Agent contract blocks");
}

void test_unresolved_results_fail_closed() {
    for (const auto status : {SmtBmcStatus::Unknown, SmtBmcStatus::SolverUnavailable,
                              SmtBmcStatus::InconsistentAssumptions}) {
        SmtBmcResult r;
        r.status = status;
        check(smt_bmc_result_is_failure(r), "unresolved or inconsistent proof cannot succeed");
    }
}

// The native test executable implements a deterministic solver child mode.
// This tests the real process seam without an external solver or shell fixture.
void test_solver_unknown_and_errors() {
    for (const std::string scenario : {"unknown_assumption", "unknown_goal", "error_goal",
                                       "unknown_step", "error_step", "error_verdict", "timeout_step"}) {
        ir::Program p;
        add_contract(p, "Agent", {
            {ir::ContractClauseKind::Invariant,
             bin(p, ir::ExprBinaryOp::Equal, int_path(p, scenario, {"x"}),
                 int_path(p, scenario, {"x"}))}});
        SmtBmcOptions options;
        options.solver.solver_path = fake_solver_path;
        options.use_k_induction = true;
        if (scenario == "timeout_step") options.solver.timeout = std::chrono::seconds{1};
        const auto r = run_smt_bmc(p, options);
        check(r.status == SmtBmcStatus::Unknown && r.proven_count == 0,
              "solver " + scenario + " is unproven, never Safe or BoundedSafe");
        check(smt_bmc_result_is_failure(r), "solver " + scenario + " fails closed");
    }
}

int fake_solver() {
    const std::string query{std::istreambuf_iterator<char>{std::cin}, {}};
    const bool goal_query = query.find("(assert (not ") != std::string::npos;
    const bool induction_query = query.find("__1") != std::string::npos;
    if (induction_query && query.find("timeout_step") != std::string::npos) {
        std::this_thread::sleep_for(std::chrono::seconds{3});
        std::cout << "unsat\n";
    } else if (query.find("unknown_assumption") != std::string::npos ||
        (goal_query && query.find("unknown_goal") != std::string::npos) ||
        (induction_query && query.find("unknown_step") != std::string::npos)) {
        std::cout << "unknown\n";
    } else if (goal_query && query.find("error_verdict") != std::string::npos) {
        std::cout << "(error \"rejected input\")\nunsat\n";
    } else if ((goal_query && query.find("error_goal") != std::string::npos) ||
               (induction_query && query.find("error_step") != std::string::npos)) {
        std::cout << "(error \"injected solver failure\")\n";
        return 2;
    } else {
        std::cout << (goal_query ? "unsat\n" : "sat\n");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Real-solver proof semantics (only with AHFL_Z3_PATH)
// ---------------------------------------------------------------------------

void test_real_ensures_proven() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // requires: input.x > 0 ; ensures: input.x >= 1  (provable over Int)
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Requires,
                   bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))},
                  {ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::GreaterEqual, int_path(p, "input", {"x"}),
                       int_lit(p, "1"))}});
    auto r = run_smt_bmc(p, {});
    check(r.assumption_checks.size() == 1 && r.assumption_checks.front().consistent,
          "requires satisfiability is established before proving ensures");
    check(r.status == SmtBmcStatus::Safe, "provable ensures under a precondition is Safe");
}

void test_real_inconsistent_preconditions() {
    if (!z3_available()) return;
    ir::Program p;
    add_contract(p, "Agent", {
        {ir::ContractClauseKind::Requires,
         bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))},
        {ir::ContractClauseKind::Requires,
         bin(p, ir::ExprBinaryOp::LessEqual, int_path(p, "input", {"x"}), int_lit(p, "0"))},
        {ir::ContractClauseKind::Ensures,
         bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "2"))}});
    const auto r = run_smt_bmc(p, {});
    check(r.status == SmtBmcStatus::InconsistentAssumptions && r.proven_count == 0,
          "contradictory requires cannot prove an arbitrary goal by vacuity");
    check(r.assumption_checks.size() == 1 && !r.assumption_checks.front().consistent,
          "inconsistent premises are separately recorded");
}

void test_real_cross_block_contract_scope() {
    if (!z3_available()) return;
    for (const bool with_ids : {false, true}) {
        for (const bool contradictory : {false, true}) {
            ir::Program p;
            // Embedding callers may omit IDs; the shared identity boundary
            // still resolves both source references to this numeric Agent slot.
            ir::AgentDecl agent;
            agent.name = "Agent";
            p.declarations.push_back(std::move(agent));
            add_contract(p, "Agent", {{ir::ContractClauseKind::Requires,
                bin(p, ir::ExprBinaryOp::Equal, int_path(p, "input", {"x"}), int_lit(p, "0"))}});
            add_contract(p, "Agent", {{ir::ContractClauseKind::Ensures,
                bin(p, ir::ExprBinaryOp::Equal, int_path(p, "input", {"x"}), int_lit(p, "0"))}});
            if (contradictory) {
                add_contract(p, "Agent", {{ir::ContractClauseKind::Requires,
                    bin(p, ir::ExprBinaryOp::Equal, int_path(p, "input", {"x"}), int_lit(p, "1"))}});
            }
            if (with_ids) {
                for (auto &decl : p.declarations) {
                    if (auto *contract = std::get_if<ir::ContractDecl>(&decl)) {
                        contract->target_ref.id = 7;
                    }
                }
            }
            const auto r = run_smt_bmc(p, {});
            check(r.assumption_checks.size() == 1,
                  "all contract blocks of one Agent share one premise consistency check");
            check(r.status == (contradictory ? SmtBmcStatus::InconsistentAssumptions : SmtBmcStatus::Safe),
                  contradictory ? "cross-block contradictory requires fail without vacuous proof"
                                : "a goal uses requires from another contract of the same Agent");
            check(r.proven_count == (contradictory ? 0U : 1U),
                  "cross-block proof count reflects consistent Agent premises");
        }
    }
}

void test_unresolved_contract_scopes_do_not_alias() {
    ir::Program p;
    // No Agent declaration and no target IDs: identical display names alone
    // must not establish canonical scope identity.
    for (int i = 0; i < 2; ++i) {
        add_contract(p, "Unresolved", {{ir::ContractClauseKind::Ensures,
            bin(p, ir::ExprBinaryOp::Equal, int_lit(p, "1"), int_lit(p, "1"))}});
    }
    SmtBmcOptions options;
    options.solver.solver_path = "/nonexistent/z3";
    const auto r = run_smt_bmc(p, options);
    check(r.assumption_checks.size() == 2,
          "unresolved contract references cannot alias by display name");
}

void test_real_invariants_cannot_assume_each_other() {
    if (!z3_available()) return;
    ir::Program p;
    // Identical invariants used to prove each other circularly.
    add_contract(p, "Agent", {
        {ir::ContractClauseKind::Invariant,
         bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))},
        {ir::ContractClauseKind::Invariant,
         bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))},
        {ir::ContractClauseKind::Ensures,
         bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))}});
    SmtBmcOptions options;
    options.use_k_induction = true;
    const auto r = run_smt_bmc(p, options);
    check(r.status == SmtBmcStatus::Unsafe && r.proven_count == 0,
          "independent invariants and ensures cannot use unproved invariants as premises");
}

void test_real_ensures_refuted() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // ensures: input.x >= 1 with NO precondition — refutable (x could be 0).
    // The `input.x` reference carries a source range so we can assert KR2.6:
    // the refuted goal's symbols map the counterexample variable back to source.
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::GreaterEqual,
                       int_path(p, "input", {"x"}, ahfl::SourceRange{40, 47}),
                       int_lit(p, "1"))}});
    auto r = run_smt_bmc(p, {});
    check(r.status == SmtBmcStatus::Unsafe, "unconditioned ensures is refuted (Unsafe)");
    // The refuted goal must carry a concrete counterexample assignment.
    bool has_cex = false;
    bool cex_var_mapped = false;
    for (const auto &g : r.goals) {
        if (g.verdict == SmtSolverStatus::Sat && !g.counterexample.empty()) {
            has_cex = true;
            // KR2.6: each counterexample variable joins by name to a goal symbol
            // carrying the source range of its field reference.
            for (const auto &[name, value] : g.counterexample) {
                (void)value;
                for (const auto &sym : g.symbols) {
                    if (sym.name == name && sym.name == "input__x" &&
                        sym.source_range.has_value() &&
                        sym.source_range->begin_offset == 40 &&
                        sym.source_range->end_offset == 47) {
                        cex_var_mapped = true;
                    }
                }
            }
        }
    }
    check(has_cex, "refuted goal carries a materialized counterexample");
    check(cex_var_mapped,
          "refuted counterexample variable maps back to its field source range (KR2.6)");
}

// RFC 0024/0025: a bounded-collection quantifier discharged end-to-end by real
// Z3 — the encoder unrolls `forall x in coll: P` over the List capacity, the
// BMC engine threads it as a goal, and Z3 returns a verdict.
void test_real_forall_proven() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // requires: forall x in input.xs: x > 0   (xs : List<Int>(3))
    // ensures:  forall x in input.xs: x >= 1  — provable under the precondition
    //           (each element's x>0 implies x>=1 over Int, per unrolled index).
    auto req = forall_expr(
        p, "x", bounded_list_path(p, "input", {"xs"}, 3),
        bin(p, ir::ExprBinaryOp::Greater, int_path(p, "x", {}), int_lit(p, "0")));
    auto ens = forall_expr(
        p, "x", bounded_list_path(p, "input", {"xs"}, 3),
        bin(p, ir::ExprBinaryOp::GreaterEqual, int_path(p, "x", {}), int_lit(p, "1")));
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Requires, req},
                  {ir::ContractClauseKind::Ensures, ens}});
    auto r = run_smt_bmc(p, {});
    check(r.status == SmtBmcStatus::Safe,
          "bounded forall ensures is proven Safe by real Z3 under a matching precondition");
}

void test_real_forall_refuted() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // ensures: forall x in input.xs: x >= 1  with NO precondition — refutable
    // (some unrolled element can be 0), so Unsafe with a counterexample.
    auto ens = forall_expr(
        p, "x", bounded_list_path(p, "input", {"xs"}, 2),
        bin(p, ir::ExprBinaryOp::GreaterEqual, int_path(p, "x", {}), int_lit(p, "1")));
    add_contract(p, "Agent", {{ir::ContractClauseKind::Ensures, ens}});
    auto r = run_smt_bmc(p, {});
    check(r.status == SmtBmcStatus::Unsafe,
          "unconditioned bounded forall ensures is refuted (Unsafe) by real Z3");
    bool has_cex = false;
    for (const auto &g : r.goals) {
        if (g.verdict == SmtSolverStatus::Sat && !g.counterexample.empty()) {
            has_cex = true;
        }
    }
    check(has_cex, "refuted quantified goal carries a materialized counterexample");
}

void test_real_k_induction_bounded_safe() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // A provable invariant, but under k-induction with havoc transitions the
    // inductive step is Sat (the invariant is not a tautology), so the
    // conclusion is BoundedSafe, not Safe.
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Requires,
                   bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))},
                  {ir::ContractClauseKind::Invariant,
                   bin(p, ir::ExprBinaryOp::GreaterEqual, int_path(p, "input", {"x"}),
                       int_lit(p, "1"))}});
    SmtBmcOptions options;
    options.use_k_induction = true;
    auto r = run_smt_bmc(p, options);
    check(r.status == SmtBmcStatus::BoundedSafe,
          "k-induction with havoc transitions falls back to BoundedSafe for non-tautological "
          "invariants");
    bool all_bounded = !r.goals.empty();
    for (const auto &g : r.goals) {
        if (g.proven && !g.bounded_only) {
            all_bounded = false;
        }
    }
    check(all_bounded, "proven invariant goals are flagged bounded_only under k-induction");
}

void test_real_k_induction_tautology_safe() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // A tautological invariant (x == x) holds in all states, so the inductive
    // step is Unsat even with havoc transitions → Safe, not BoundedSafe.
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Invariant,
                   bin(p, ir::ExprBinaryOp::Equal, int_path(p, "input", {"x"}),
                       int_path(p, "input", {"x"}))}});
    SmtBmcOptions options;
    options.use_k_induction = true;
    auto r = run_smt_bmc(p, options);
    check(r.status == SmtBmcStatus::Safe,
          "k-induction with havoc transitions proves tautological invariants as Safe");
    for (const auto &g : r.goals) {
        if (g.proven) {
            check(!g.bounded_only, "tautological invariant is not flagged bounded_only");
        }
    }
}

void test_real_k_induction_ensures_not_bounded() {
    if (!z3_available()) {
        return;
    }
    ir::Program p;
    // Ensures goals are one-shot properties complete for the loop-free
    // fragment, so they are NOT flagged bounded_only under k-induction.
    add_contract(p, "Agent",
                 {{ir::ContractClauseKind::Requires,
                   bin(p, ir::ExprBinaryOp::Greater, int_path(p, "input", {"x"}), int_lit(p, "0"))},
                  {ir::ContractClauseKind::Ensures,
                   bin(p, ir::ExprBinaryOp::GreaterEqual, int_path(p, "input", {"x"}),
                       int_lit(p, "1"))}});
    SmtBmcOptions options;
    options.use_k_induction = true;
    auto r = run_smt_bmc(p, options);
    check(r.status == SmtBmcStatus::Safe,
          "ensures goals are not downgraded to BoundedSafe under k-induction");
    for (const auto &g : r.goals) {
        if (g.proven) {
            check(!g.bounded_only, "ensures goal is not flagged bounded_only");
        }
    }
}

void test_k_induction_status_name() {
    check(smt_bmc_status_name(SmtBmcStatus::BoundedSafe) == "bounded_safe", "bounded_safe name");
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string{argv[1]} == "-in") return fake_solver();
    fake_solver_path = std::filesystem::absolute(argv[0]).string();
    test_no_contracts_unsupported();
    test_unavailable_solver_never_safe();
    test_goal_metadata();
    test_divisor_obligation_becomes_goal();

    test_unsupported_clauses_fail_closed();
    test_cumulative_goal_ordinals();
    test_unresolved_results_fail_closed();
    test_solver_unknown_and_errors();
    test_real_inconsistent_preconditions();
    test_unresolved_contract_scopes_do_not_alias();
    test_real_cross_block_contract_scope();
    test_real_invariants_cannot_assume_each_other();
    test_real_ensures_proven();
    test_real_ensures_refuted();
    test_real_forall_proven();
    test_real_forall_refuted();
    test_real_k_induction_bounded_safe();
    test_real_k_induction_tautology_safe();
    test_real_k_induction_ensures_not_bounded();
    test_k_induction_status_name();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
