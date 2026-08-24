#pragma once

// RFC 0017 (BMC Contract Semantics) slice 5: SMT-BMC engine.
//
// Turns a program's contract data predicates into proof-by-refutation SMT
// queries and discharges them through the solver seam (smt_solver.hpp):
//
//   * requires clauses  -> assumptions (asserted directly);
//   * invariant clauses -> goals AND assumptions (must hold, and may be relied
//                          on by later goals within the same contract);
//   * ensures clauses    -> goals (postconditions to prove);
//   * forbid clauses     -> goals whose *positive* form must be unreachable, so
//                          the goal proven is `(not predicate)`;
//   * divide/modulo      -> the divisor-non-zero obligation becomes its own goal.
//
// A goal G is proven by asserting the assumptions plus `(not G)` and checking
// unsat: unsat => G holds on all models (Safe); sat => a counterexample exists
// (Unsafe); unknown / unavailable => not proven (never silently Safe).
//
// "Bounded" here refers to the Int(lo,hi) range assumptions and the solver
// timeout; the data-predicate fragment is loop-free, so a single query per
// goal is complete for it (unbounded k is a no-op for pure data predicates and
// is handled by slice 6 for the temporal fragment).

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "verification/formal/smt_encode.hpp"
#include "verification/formal/smt_solver.hpp"

namespace ahfl::formal {

enum class SmtBmcStatus {
    Safe,        // every goal proven unconditionally (all queries unsat)
    BoundedSafe, // proven within the bound; the inductive step could not be
                 // strengthened to an unbounded proof (RFC 0017 Q6 fallback)
    Unsafe,      // at least one goal refuted (a query was sat)
    Unknown,     // a query returned unknown / timed out
    Unsupported, // no encodable data-predicate goals in the program
    SolverUnavailable,
};

// One discharged proof goal, with the solver verdict and enough context to
// attribute a failure back to its clause.
struct SmtBmcGoal {
    ir::ContractClauseKind kind{ir::ContractClauseKind::Ensures};
    std::string target_name;
    std::size_t clause_index{0};
    std::string description; // e.g. "ensures" or "requires divisor != 0"
    SmtSolverStatus verdict{SmtSolverStatus::SolverError};
    bool proven{false}; // verdict == Unsat
    // K-induction only: true when the goal held as a base case but its
    // inductive step could not be strengthened, so the proof is bound-limited.
    bool bounded_only{false};
    // Populated only when the goal was refuted (verdict == Sat): the concrete
    // symbol assignments (contract path -> value) that violate the goal, as a
    // materialized counterexample. Symbol names are the flat SMT names
    // (e.g. "input__x") the encoder produced.
    std::vector<std::pair<std::string, std::string>> counterexample;
    std::optional<SourceRange> source_range;
};

struct SmtBmcResult {
    SmtBmcStatus status{SmtBmcStatus::Unsupported};
    std::vector<SmtBmcGoal> goals;
    std::size_t proven_count{0};
    std::string error_message;
};

// Options controlling the SMT-BMC run.
struct SmtBmcOptions {
    SmtEncodeOptions encode; // e.g. emit_overflow_checks
    SmtSolverOptions solver; // solver kind / path / timeout
    // When true, attempt k-induction on each proven goal: a base case (the goal
    // itself) plus an inductive step. For the loop-free data-predicate fragment
    // there is no data-transition relation to induct over, so the step cannot be
    // strengthened and the goal is reported BoundedSafe rather than Safe (RFC
    // 0017 Q6). Off by default: a plain BMC pass reports unconditional Safe.
    bool use_k_induction{false};
};

// Run SMT-BMC over every contract in `program`. Pure w.r.t. the program; the
// only side effect is invoking the solver process per goal.
[[nodiscard]] SmtBmcResult run_smt_bmc(const ir::Program &program, const SmtBmcOptions &options);

[[nodiscard]] std::string_view smt_bmc_status_name(SmtBmcStatus status) noexcept;

// Render an SMT-BMC result as a text report block for `ahflc verify`. Lines are
// prefixed `smt_bmc_` to sit alongside the SMV `checker_*` lines. Refuted goals
// print their counterexample assignments. Deterministic.
void print_smt_bmc_report(const SmtBmcResult &result, std::ostream &out);

// Whether an SMT-BMC result should fail the verify command. Only a genuine
// refutation (Unsafe) fails; Unsupported / SolverUnavailable / Unknown /
// BoundedSafe / Safe never do (a missing solver is a skip, never a failure and
// never a false pass).
[[nodiscard]] bool smt_bmc_result_is_failure(const SmtBmcResult &result) noexcept;

} // namespace ahfl::formal
