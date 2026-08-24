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
// of free symbols to declare, the precondition assumptions, the invariant
// terms (each tagged with its clause index so a goal never assumes itself),
// and the list of goals to prove.
struct ContractQueries {
    std::string target;
    std::vector<SmtSymbol> symbols;
    std::vector<std::string> requires_terms;
    struct Invariant {
        std::size_t clause_index;
        std::string term;
    };
    std::vector<Invariant> invariants;
    struct Goal {
        ir::ContractClauseKind kind;
        std::size_t clause_index;
        std::string description;
        std::string goal_term; // the predicate that must hold
        std::optional<SourceRange> source_range;
    };
    std::vector<Goal> goals;
};

void merge_symbols(std::vector<SmtSymbol> &into, const std::vector<SmtSymbol> &from) {
    for (const auto &symbol : from) {
        bool seen = false;
        for (const auto &existing : into) {
            if (existing.name == symbol.name) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            into.push_back(symbol);
        }
    }
}

// Build the refutation query for one goal: declare all contract symbols, assert
// the bound constraints, the preconditions, every invariant *except* the goal's
// own clause, then the negated goal. Unsat ⇒ the goal holds.
[[nodiscard]] std::string build_query(const ContractQueries &contract,
                                      const ContractQueries::Goal &goal) {
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
    for (const auto &invariant : contract.invariants) {
        if (invariant.clause_index == goal.clause_index) {
            continue; // never assume the very invariant we are proving
        }
        doc << "(assert " << invariant.term << ")\n";
    }
    doc << "(assert (not " << goal.goal_term << "))\n";
    doc << "(check-sat)\n";
    return doc.str();
}

[[nodiscard]] ContractQueries collect_contract(const ir::ContractDecl &contract,
                                               const SmtEncodeOptions &encode_options) {
    ContractQueries queries;
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
            continue; // out-of-subset clauses are abstracted (reported by slice 1)
        }
        merge_symbols(queries.symbols, encoded.symbols);

        const auto kind_name = std::string(ir::contract_clause_kind_name(clause.kind));

        switch (clause.kind) {
        case ir::ContractClauseKind::Requires:
            queries.requires_terms.push_back(*encoded.term);
            break;
        case ir::ContractClauseKind::Invariant:
            queries.invariants.push_back({i, *encoded.term});
            queries.goals.push_back({clause.kind, i, kind_name, *encoded.term, clause.source_range});
            break;
        case ir::ContractClauseKind::Ensures:
            queries.goals.push_back({clause.kind, i, kind_name, *encoded.term, clause.source_range});
            break;
        case ir::ContractClauseKind::Forbid:
            // The forbidden predicate must be unreachable, so the goal is its
            // negation.
            queries.goals.push_back(
                {clause.kind, i, kind_name, "(not " + *encoded.term + ")", clause.source_range});
            break;
        case ir::ContractClauseKind::Decreases:
            break; // unreachable (skipped above)
        }

        // Divide/modulo divisor-non-zero obligations become goals of their own,
        // attributed to the clause that produced them.
        for (const auto &obligation : encoded.obligations) {
            queries.goals.push_back({clause.kind, i, kind_name + " divisor != 0",
                                     obligation.predicate, clause.source_range});
        }
    }

    return queries;
}

} // namespace

SmtBmcResult run_smt_bmc(const ir::Program &program, const SmtBmcOptions &options) {
    SmtBmcResult result;

    std::vector<ContractQueries> contracts;
    std::size_t total_goals = 0;
    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr) {
            continue;
        }
        auto queries = collect_contract(*contract, options.encode);
        total_goals += queries.goals.size();
        contracts.push_back(std::move(queries));
    }

    if (total_goals == 0) {
        result.status = SmtBmcStatus::Unsupported;
        result.error_message = "no encodable contract data-predicate goals in program";
        return result;
    }

    bool any_unknown = false;
    bool any_unsafe = false;
    bool any_unavailable = false;
    bool any_bounded_only = false;

    for (const auto &contract : contracts) {
        for (const auto &goal : contract.goals) {
            const auto query = build_query(contract, goal);
            // Request a model so a refutation (Sat) comes with a concrete
            // counterexample assignment to materialize.
            SmtSolverOptions solver_options = options.solver;
            solver_options.request_model = true;
            const auto verdict = run_smt_solver(query, solver_options);

            SmtBmcGoal record;
            record.kind = goal.kind;
            record.target_name = contract.target;
            record.clause_index = goal.clause_index;
            record.description = goal.description;
            record.verdict = verdict.status;
            record.proven = verdict.status == SmtSolverStatus::Unsat;
            record.source_range = goal.source_range;

            if (record.proven) {
                ++result.proven_count;
                // K-induction: the base case (above) holds. The unbounded proof
                // needs an inductive step over a data-transition relation, but
                // the data-predicate fragment is loop-free (no such relation to
                // induct over), so the step cannot be strengthened. Per RFC 0017
                // Q6 we keep the bound-limited conclusion and flag it rather than
                // over-claim an unbounded proof.
                if (options.use_k_induction) {
                    record.bounded_only = true;
                    any_bounded_only = true;
                }
            } else if (verdict.status == SmtSolverStatus::Sat) {
                any_unsafe = true;
                record.counterexample = verdict.model; // concrete violating assignment
            } else if (verdict.status == SmtSolverStatus::SolverUnavailable) {
                any_unavailable = true;
                if (result.error_message.empty()) {
                    result.error_message = verdict.error_message;
                }
            } else {
                any_unknown = true;
            }

            result.goals.push_back(std::move(record));
        }
    }

    // Resolution order: a real refutation (Unsafe) is the strongest signal; a
    // missing solver must never read as Safe; then Unknown; then, when every
    // goal is proven, either unconditional Safe or (k-induction) BoundedSafe.
    if (any_unsafe) {
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
    return result.status == SmtBmcStatus::Unsafe;
}

void print_smt_bmc_report(const SmtBmcResult &result, std::ostream &out) {
    out << "smt_bmc_status: " << smt_bmc_status_name(result.status) << '\n';
    out << "smt_bmc_goals: " << result.goals.size() << '\n';
    out << "smt_bmc_proven: " << result.proven_count << '\n';

    if (result.status == SmtBmcStatus::Unsupported) {
        // No data-predicate goals — nothing more to say.
        return;
    }
    if (result.status == SmtBmcStatus::SolverUnavailable && !result.error_message.empty()) {
        out << "smt_bmc_skip_reason: " << result.error_message << '\n';
    }

    for (const auto &goal : result.goals) {
        if (goal.verdict == SmtSolverStatus::Sat) {
            out << "smt_bmc_refuted: contract " << goal.target_name << " " << goal.description
                << "[" << goal.clause_index << "]\n";
            for (const auto &[name, value] : goal.counterexample) {
                out << "  counterexample: " << name << " = " << value << '\n';
            }
        } else if (goal.bounded_only) {
            out << "smt_bmc_bounded: contract " << goal.target_name << " " << goal.description
                << "[" << goal.clause_index << "]\n";
        }
    }
}

} // namespace ahfl::formal
