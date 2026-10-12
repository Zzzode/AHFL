#pragma once

// RFC 0017 (BMC Contract Semantics) slice 5: SMT-BMC engine.
//
// Turns a program's contract data predicates into proof-by-refutation SMT
// queries and discharges them through the solver seam (smt_solver.hpp):
//
//   * requires clauses  -> assumptions (asserted directly);
//   * invariant clauses -> independent goals, never premises for other goals;
//   * ensures clauses    -> goals (postconditions to prove);
//   * forbid clauses     -> goals whose *positive* form must be unreachable, so
//                          the goal proven is `(not predicate)`;
//   * divide/modulo      -> the divisor-non-zero obligation becomes its own goal.
//
// A goal G is proven by asserting the assumptions plus `(not G)` and checking
// unsat: unsat => G holds on all models (Safe); sat => a counterexample exists
// (Unsafe); unknown / unavailable => not proven (never silently Safe).
//
// This proves contract implications only; no flow assignments or execution
// transition relation are encoded. Satisfiable preconditions are checked before
// refutation, preventing proofs from contradictory premises.
//
// Integer ranges are type-domain constraints, not execution-depth bounds.
// Timeout and unknown results remain failures. The optional two-query invariant
// check re-encodes abstract predicates with independent (havoc) values; it has
// no execution transition relation and is not an execution induction proof.
// Its bounded_only flag records this existing abstraction limitation.

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "verification/formal/smt_encode.hpp"
#include "verification/formal/smt_solver.hpp"
#include "verification/formal/subset.hpp"

namespace ahfl::formal {

enum class SmtBmcStatus {
    Safe,        // every contract implication proven under consistent requires
    BoundedSafe, // proven within the bound; the inductive step could not be
                 // strengthened to an unbounded proof (RFC 0017 Q6 fallback)
    InconsistentAssumptions, // requires and symbol bounds have no model
    Unsafe,      // at least one goal refuted (a query was sat)
    Unknown,     // a query returned unknown / timed out
    Unsupported, // no data goals, or selected clauses are not encodable
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
    bool proven{false}; // consistent premises and a completed Unsat proof
    // K-induction only: true when the goal held as a base case but its
    // inductive step was Sat (the invariant is not a tautology under havoc
    // transitions), so the proof is bound-limited. Tautological invariants
    // whose inductive step is Unsat are not flagged.
    bool bounded_only{false};
    // Populated only when the goal was refuted (verdict == Sat): the concrete
    // symbol assignments (contract path -> value) that violate the goal, as a
    // materialized counterexample. Symbol names are the flat SMT names
    // (e.g. "input__x") the encoder produced.
    std::vector<std::pair<std::string, std::string>> counterexample;
    // KR2.6: the free symbols referenced by this goal's contract, carrying the
    // AHFL source range of the field reference that introduced each (when
    // known). A counterexample assignment `counterexample[i].first` can be
    // joined by name against `symbols` to point the violating value back at its
    // field declaration in the source. Populated for every goal (not only
    // refuted ones) so emit/report paths can annotate uniformly.
    std::vector<SmtSymbol> symbols;
    std::optional<SourceRange> source_range;
};

struct SmtBmcAssumptionCheck {
    std::string target_name;
    SmtSolverStatus verdict{SmtSolverStatus::SolverError};
    bool consistent{false};
};

struct SmtBmcResult {
    SmtBmcStatus status{SmtBmcStatus::Unsupported};
    std::vector<SmtBmcGoal> goals;
    std::vector<SmtBmcAssumptionCheck> assumption_checks;
    std::vector<SubsetIneligibleClause> unsupported_clauses;
    std::size_t proven_count{0};
    std::string error_message;
};

// Options controlling the SMT-BMC run.
struct SmtBmcOptions {
    SmtEncodeOptions encode; // e.g. emit_overflow_checks
    SmtSolverOptions solver; // solver kind / path / timeout
    // When true, run the existing two-query abstract predicate check.
    // The base case is the standard refutation query. The inductive step
    // re-instantiates the predicate at step 0 (hypothesis) and step 1
    // (negation) with havoc transitions (transition relation = true). Unsat
    // means the invariant is a tautology (holds in all states) → Safe. Sat
    // means the invariant is not a tautology → BoundedSafe. Ensures/forbid/
    // divisor goals are one-shot properties complete for the loop-free
    // fragment and are not affected. Off by default.
    bool use_k_induction{false};
};

// Run SMT-BMC over every contract in `program`. Pure w.r.t. the program; the
// only side effect is invoking the solver process per goal.
[[nodiscard]] SmtBmcResult run_smt_bmc(const ir::Program &program, const SmtBmcOptions &options);

[[nodiscard]] std::string_view smt_bmc_status_name(SmtBmcStatus status) noexcept;

// Render a report for `ahflc check --verify-data-contracts`. Lines are
// prefixed `smt_bmc_` to sit alongside the SMV `checker_*` lines. Refuted goals
// print their counterexample assignments. Deterministic.
void print_smt_bmc_report(const SmtBmcResult &result, std::ostream &out);

// Explicitly requested proof goals fail closed: Safe/BoundedSafe succeed.
// No data goals is not applicable; unsupported clauses and unresolved proofs fail.
[[nodiscard]] bool smt_bmc_result_is_failure(const SmtBmcResult &result) noexcept;

} // namespace ahfl::formal
