#pragma once

// RFC 0017 (BMC Contract Semantics) slice 1: verifiable-subset eligibility.
//
// Walks a program's contract data predicates (the ExprRef branch of
// ir::ContractClause::value behind requires/ensures/invariant/forbid) and
// classifies each as SMT-encodable or not, using the slice-2 encoder
// (smt_encode.hpp). Clauses that leave the verifiable subset are reported so
// the caller can emit formal.NOT_IN_VERIFIED_SUBSET rather than silently
// abstracting them (the current SMV-backend behaviour, smv_formula.cpp).
//
// This is a pure analysis: it inspects the program and returns a value; it
// mutates nothing and invokes no solver.

#include <optional>
#include <string>
#include <vector>

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "verification/formal/smt_encode.hpp"

namespace ahfl::formal {

// One contract data-predicate clause that could not be encoded into the
// verifiable subset. Carries enough structured, index-derived context for a
// SourceRange'd diagnostic.
struct SubsetIneligibleClause {
    ir::ContractClauseKind kind{ir::ContractClauseKind::Requires};
    std::string target_name;               // Contract target (agent) canonical name
    std::size_t clause_index{0};            // Position within the contract's clause list
    SmtEncodeRejection rejection{SmtEncodeRejection::UnsupportedNode};
    std::optional<SourceRange> source_range; // Clause range, for diagnostics
};

// Result of the eligibility walk over a whole program.
struct SubsetEligibilityReport {
    std::size_t data_predicate_clauses{0};       // # ExprRef clauses examined
    std::size_t eligible_clauses{0};             // # that encode into the subset
    std::vector<SubsetIneligibleClause> ineligible; // the rest, with reasons

    [[nodiscard]] bool all_eligible() const noexcept {
        return ineligible.empty();
    }
};

// Analyze every ContractDecl's data-predicate clauses. Temporal clauses (the
// TemporalExprPtr branch) are out of scope for the SMT data path and are not
// counted here. Pure.
[[nodiscard]] SubsetEligibilityReport
analyze_contract_subset_eligibility(const ir::Program &program,
                                    const SmtEncodeOptions &options = {});

} // namespace ahfl::formal
