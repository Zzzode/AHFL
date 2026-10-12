#include "verification/formal/smt_bmc.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "verification/formal/smt_encode.hpp"

#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace ahfl::formal {

namespace {

// Everything gathered from one contract before goals are discharged: the union
// of free symbols to declare, the preconditions, and independent proof goals.
struct ContractQueries {
    const ir::ContractDecl *first_contract{nullptr};
    std::string target;
    std::vector<SmtSymbol> symbols;
    std::vector<std::string> requires_terms;
    struct Goal {
        ir::ContractClauseKind kind;
        std::size_t clause_index;
        std::string description;
        std::string goal_term; // the predicate that must hold
        ir::ExprRef expr;      // KR7.2: the source expression, for step-indexed re-encoding
        std::optional<SourceRange> source_range;
    };
    std::vector<Goal> goals;
};

void merge_symbols(std::vector<SmtSymbol> &into, const std::vector<SmtSymbol> &from) {
    for (const auto &symbol : from) {
        bool seen = false;
        for (auto &existing : into) {
            if (existing.name == symbol.name) {
                seen = true;
                // Backfill a source range if a later clause referencing the
                // same symbol carries one the first encounter lacked (KR2.6).
                if (!existing.source_range.has_value() && symbol.source_range.has_value()) {
                    existing.source_range = symbol.source_range;
                }
                break;
            }
        }
        if (!seen) {
            into.push_back(symbol);
        }
    }
}

// Build the refutation query for one goal: declare all contract symbols, assert
// the bound constraints and preconditions, then the negated goal.
// Other goals cannot serve as assumptions. Unsat implies the contract formula.
[[nodiscard]] std::string build_query(const ContractQueries &contract,
                                      const ContractQueries::Goal *goal = nullptr) {
    std::ostringstream doc;
    doc << "(set-logic QF_NIA)\n";
    for (const auto &symbol : contract.symbols) {
        doc << "(declare-const " << symbol.name << " " << smt_sort_keyword(symbol.sort) << ")\n";
        if (symbol.int_bounds.has_value()) {
            doc << "(assert (and (>= " << symbol.name << " " << symbol.int_bounds->first
                << ") (<= " << symbol.name << " " << symbol.int_bounds->second << ")))\n";
        }
    }
    for (const auto &assumption : contract.requires_terms) {
        doc << "(assert " << assumption << ")\n";
    }
    if (goal != nullptr) {
        doc << "(assert (not " << goal->goal_term << "))\n";
    }
    doc << "(check-sat)\n";
    return doc.str();
}

// KR7.2: Build the inductive-step query for an invariant goal. The predicate is
// instantiated at step 0 (induction hypothesis: I holds) and step 1 (negation:
// I fails). With havoc transitions (transition relation = true), there is no
// constraint between step 0 and step 1, so the step is Unsat only when I is a
// tautology. A Sat result honestly downgrades the goal to BoundedSafe.
[[nodiscard]] std::optional<std::string> build_inductive_step_query(const ContractQueries::Goal &goal,
                                                     const SmtEncodeOptions &base_options) {
    // Re-encode at step 0 (hypothesis) and step 1 (negation).
    SmtEncodeOptions step0_options = base_options;
    step0_options.step = 0;
    SmtEncodeOptions step1_options = base_options;
    step1_options.step = 1;

    const auto encoded_step0 = encode_predicate(goal.expr, step0_options);
    const auto encoded_step1 = encode_predicate(goal.expr, step1_options);

    // Encoding failure must remain unproven, rather than fabricate a solver verdict.
    if (!encoded_step0.ok() || !encoded_step1.ok()) {
        return std::nullopt;
    }

    std::ostringstream doc;
    doc << "(set-logic QF_NIA)\n";

    // Declare symbols at step 0.
    for (const auto &symbol : encoded_step0.symbols) {
        doc << "(declare-const " << symbol.name << " " << smt_sort_keyword(symbol.sort) << ")\n";
        if (symbol.int_bounds.has_value()) {
            doc << "(assert (and (>= " << symbol.name << " " << symbol.int_bounds->first
                << ") (<= " << symbol.name << " " << symbol.int_bounds->second << ")))\n";
        }
    }
    // Declare symbols at step 1.
    for (const auto &symbol : encoded_step1.symbols) {
        doc << "(declare-const " << symbol.name << " " << smt_sort_keyword(symbol.sort) << ")\n";
        if (symbol.int_bounds.has_value()) {
            doc << "(assert (and (>= " << symbol.name << " " << symbol.int_bounds->first
                << ") (<= " << symbol.name << " " << symbol.int_bounds->second << ")))\n";
        }
    }

    // Induction hypothesis: I holds at step 0.
    doc << "(assert " << *encoded_step0.term << ")\n";
    // Negation of the goal: I fails at step 1.
    doc << "(assert (not " << *encoded_step1.term << "))\n";
    // Havoc transition: no constraint between step 0 and step 1.

    doc << "(check-sat)\n";
    return doc.str();
}

[[nodiscard]] ContractQueries collect_contract(const ir::ContractDecl &contract,
                                               const SmtEncodeOptions &encode_options,
                                               std::size_t clause_begin) {
    ContractQueries queries;
    queries.first_contract = &contract;
    queries.target = std::string(ir::symbol_canonical_name(contract.target_ref));

    const auto &clauses = contract.clauses;
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        const auto &clause = clauses[i];
        const auto *expr = std::get_if<ir::ExprRef>(&clause.value);
        if (expr == nullptr || clause.kind == ir::ContractClauseKind::Decreases) {
            continue;
        }

        const auto encoded = encode_predicate(*expr, encode_options);
        if (!encoded.ok()) {
            continue; // eligibility was checked before collecting proof queries
        }
        merge_symbols(queries.symbols, encoded.symbols);

        const auto kind_name = std::string(ir::contract_clause_kind_name(clause.kind));

        switch (clause.kind) {
        case ir::ContractClauseKind::Requires:
            queries.requires_terms.push_back(*encoded.term);
            break;
        case ir::ContractClauseKind::Invariant:
            queries.goals.push_back({clause.kind, clause_begin + i, kind_name, *encoded.term, *expr, clause.source_range});
            break;
        case ir::ContractClauseKind::Ensures:
            queries.goals.push_back({clause.kind, clause_begin + i, kind_name, *encoded.term, *expr, clause.source_range});
            break;
        case ir::ContractClauseKind::Forbid:
            // The forbidden predicate must be unreachable, so the goal is its
            // negation.
            queries.goals.push_back(
                {clause.kind, clause_begin + i, kind_name, "(not " + *encoded.term + ")", *expr, clause.source_range});
            break;
        case ir::ContractClauseKind::Decreases:
            break; // unreachable (skipped above)
        }

        // Divide/modulo divisor-non-zero obligations become goals of their own,
        // attributed to the clause that produced them.
        for (const auto &obligation : encoded.obligations) {
            queries.goals.push_back({clause.kind, clause_begin + i, kind_name + " divisor != 0",
                                     obligation.predicate, *expr, clause.source_range});
        }
    }

    return queries;
}

} // namespace

SmtBmcResult run_smt_bmc(const ir::Program &program, const SmtBmcOptions &options) {
    SmtBmcResult result;

    result.unsupported_clauses = analyze_contract_subset_eligibility(program, options.encode).ineligible;
    if (!result.unsupported_clauses.empty()) {
        result.status = SmtBmcStatus::Unsupported;
        result.error_message = "selected contract predicates or assumptions are not SMT-encodable";
        return result;
    }

    ir::ContractClauseIndex clause_index;
    clause_index.rebuild(program);
    std::vector<ContractQueries> contracts;
    std::size_t total_goals = 0;
    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr) {
            continue;
        }
        auto queries = collect_contract(*contract, options.encode, clause_index.clause_begin(*contract));
        total_goals += queries.goals.size();
        bool merged = false;
        for (auto &existing : contracts) {
            if (!clause_index.same_agent(*existing.first_contract, *contract)) continue;
            merge_symbols(existing.symbols, queries.symbols);
            for (auto &term : queries.requires_terms) {
                existing.requires_terms.push_back(std::move(term));
            }
            for (auto &goal : queries.goals) {
                existing.goals.push_back(std::move(goal));
            }
            merged = true;
            break;
        }
        if (!merged) contracts.push_back(std::move(queries));
    }

    if (total_goals == 0) {
        result.status = SmtBmcStatus::Unsupported;
        result.error_message = "no encodable contract data-predicate goals in program";
        return result;
    }

    bool any_inconsistent = false;
    bool any_unknown = false;
    bool any_unsafe = false;
    bool any_unavailable = false;
    bool any_bounded_only = false;

    for (const auto &contract : contracts) {
        if (contract.goals.empty()) {
            continue;
        }
        const auto assumptions = run_smt_solver(build_query(contract), options.solver);
        const bool consistent = assumptions.status == SmtSolverStatus::Sat;
        result.assumption_checks.push_back({contract.target, assumptions.status, consistent});
        if (!consistent) {
            any_inconsistent |= assumptions.status == SmtSolverStatus::Unsat;
            any_unavailable |= assumptions.status == SmtSolverStatus::SolverUnavailable;
            any_unknown |= assumptions.status != SmtSolverStatus::Unsat &&
                           assumptions.status != SmtSolverStatus::SolverUnavailable;
            if (result.error_message.empty()) {
                result.error_message = assumptions.status == SmtSolverStatus::Unsat
                    ? "contract assumptions are inconsistent for " + contract.target
                    : assumptions.error_message;
            }
        }
        for (const auto &goal : contract.goals) {
            const auto query = build_query(contract, &goal);
            // Request a model so a refutation (Sat) comes with a concrete
            // counterexample assignment to materialize.
            SmtSolverOptions solver_options = options.solver;
            solver_options.request_model = true;
            const auto verdict = consistent ? run_smt_solver(query, solver_options) : assumptions;

            SmtBmcGoal record;
            record.kind = goal.kind;
            record.target_name = contract.target;
            record.clause_index = goal.clause_index;
            record.description = goal.description;
            record.verdict = verdict.status;
            record.proven = consistent && verdict.status == SmtSolverStatus::Unsat;
            record.source_range = goal.source_range;
            // KR2.6: carry the contract's symbols (with per-field source ranges)
            // so a counterexample assignment can be joined by name to its field
            // declaration in the source.
            record.symbols = contract.symbols;

            if (record.proven) {
                ++result.proven_count;
                // KR7.2: real two-query k-induction for invariant goals. The
                // base case (above) holds. The inductive step re-instantiates
                // the predicate at step 0 (hypothesis) and step 1 (negation)
                // with havoc transitions (transition relation = true). Unsat
                // means the invariant is a tautology (holds in all states, so
                // the step is vacuously valid) → Safe. Sat means the invariant
                // is not a tautology → BoundedSafe (the base case is sound but
                // the step cannot be discharged without a real transition
                // relation). Ensures/forbid/divisor goals are one-shot
                // properties complete for the loop-free fragment, so they need
                // no inductive step and are not flagged.
                if (options.use_k_induction && goal.kind == ir::ContractClauseKind::Invariant) {
                    const auto step_query = build_inductive_step_query(goal, options.encode);
                    const auto step_verdict = step_query.has_value()
                        ? run_smt_solver(*step_query, options.solver)
                        : SmtSolverResult{};
                    if (step_verdict.status == SmtSolverStatus::Sat) {
                        record.bounded_only = true;
                        any_bounded_only = true;
                    } else if (step_verdict.status != SmtSolverStatus::Unsat) {
                        record.proven = false;
                        --result.proven_count;
                        record.verdict = step_verdict.status;
                        any_unavailable |= step_verdict.status == SmtSolverStatus::SolverUnavailable;
                        any_unknown |= step_verdict.status != SmtSolverStatus::SolverUnavailable;
                        result.error_message = step_query.has_value() ? step_verdict.error_message
                            : "cannot encode invariant induction query";
                    }
                }
            } else if (consistent && verdict.status == SmtSolverStatus::Sat) {
                any_unsafe = true;
                record.counterexample = verdict.model; // concrete violating assignment
            } else if (verdict.status == SmtSolverStatus::SolverUnavailable) {
                any_unavailable = true;
                if (result.error_message.empty()) {
                    result.error_message = verdict.error_message;
                }
            } else if (consistent) {
                any_unknown = true;
                if (result.error_message.empty()) {
                    result.error_message = verdict.error_message;
                }
            }

            result.goals.push_back(std::move(record));
        }
    }

    // Inconsistent premises cannot establish a proof. Refutations and missing
    // or inconclusive solvers also prevent a successful proof result.
    if (any_inconsistent) {
        result.status = SmtBmcStatus::InconsistentAssumptions;
    } else if (any_unsafe) {
        result.status = SmtBmcStatus::Unsafe;
    } else if (any_unavailable) {
        result.status = SmtBmcStatus::SolverUnavailable;
    } else if (any_unknown) {
        result.status = SmtBmcStatus::Unknown;
    } else if (any_bounded_only) {
        result.status = SmtBmcStatus::BoundedSafe;
    } else {
        result.status = SmtBmcStatus::Safe;
    }

    return result;
}

std::string_view smt_bmc_status_name(SmtBmcStatus status) noexcept {
    switch (status) {
    case SmtBmcStatus::Safe:
        return "safe";
    case SmtBmcStatus::BoundedSafe:
        return "bounded_safe";
    case SmtBmcStatus::InconsistentAssumptions:
        return "inconsistent_assumptions";
    case SmtBmcStatus::Unsafe:
        return "unsafe";
    case SmtBmcStatus::Unknown:
        return "unknown";
    case SmtBmcStatus::Unsupported:
        return "unsupported";
    case SmtBmcStatus::SolverUnavailable:
        return "solver_unavailable";
    }
    return "unsupported";
}

bool smt_bmc_result_is_failure(const SmtBmcResult &result) noexcept {
    return result.status != SmtBmcStatus::Safe && result.status != SmtBmcStatus::BoundedSafe &&
           !(result.status == SmtBmcStatus::Unsupported && result.unsupported_clauses.empty());
}

void print_smt_bmc_report(const SmtBmcResult &result, std::ostream &out) {
    out << "smt_bmc_status: " << smt_bmc_status_name(result.status) << '\n';
    out << "smt_bmc_goals: " << result.goals.size() << '\n';
    out << "smt_bmc_proven: " << result.proven_count << '\n';

    out << "smt_bmc_scope: contract_implication\n";
    out << "smt_bmc_assumption_checks: " << result.assumption_checks.size() << '\n';
    std::size_t consistent_count = 0;
    for (const auto &assumptions : result.assumption_checks) {
        consistent_count += assumptions.consistent ? 1 : 0;
    }
    out << "smt_bmc_assumptions_consistent: " << consistent_count << '\n';
    for (const auto &assumptions : result.assumption_checks) {
        out << "smt_bmc_assumptions: contract " << assumptions.target_name << " "
            << (assumptions.consistent ? "consistent" : "not_established") << " ("
            << smt_solver_status_name(assumptions.verdict) << ")\n";
    }
    out << "smt_bmc_unsupported_clauses: " << result.unsupported_clauses.size() << '\n';
    for (const auto &clause : result.unsupported_clauses) {
        out << "smt_bmc_unsupported: contract " << clause.target_name << " "
            << ir::contract_clause_kind_name(clause.kind) << "[" << clause.clause_index
            << "] " << describe_rejection(clause.rejection) << '\n';
    }
    if (!result.error_message.empty()) {
        out << "smt_bmc_error: " << result.error_message << '\n';
    }

    for (const auto &goal : result.goals) {
        if (goal.verdict == SmtSolverStatus::Sat) {
            out << "smt_bmc_refuted: contract " << goal.target_name << " " << goal.description
                << "[" << goal.clause_index << "]\n";
            for (const auto &[name, value] : goal.counterexample) {
                out << "  formula_counterexample: " << name << " = " << value;
                // KR2.6: annotate with the field's source offsets when the
                // encoder recorded a range for this symbol.
                for (const auto &symbol : goal.symbols) {
                    if (symbol.name == name && symbol.source_range.has_value() &&
                        !symbol.source_range->empty()) {
                        out << " @[" << symbol.source_range->begin_offset << ","
                            << symbol.source_range->end_offset << ")";
                        break;
                    }
                }
                out << '\n';
            }
        } else if (goal.bounded_only) {
            out << "smt_bmc_bounded: contract " << goal.target_name << " " << goal.description
                << "[" << goal.clause_index << "]\n";
        }
    }
}

} // namespace ahfl::formal
